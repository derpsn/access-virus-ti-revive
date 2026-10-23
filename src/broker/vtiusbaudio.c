// vtiusbaudio.c - Virus TI USB audio streaming on WinUSB, the way VirusUSB.sys does it (findings E075).
//
// With src/inf/vti_winusb.inf binding WinUSB to the audio function (MI_00 = interfaces 0..2), the plugin process
// streams the isochronous endpoints itself instead of going through Windows' usbaudio.sys / WASAPI:
//   - interface 2 alt 1, EP 0x82 IN: 6 ch x 16 bit, one packet per 1 ms frame (44 or 45 frames at 44.1 kHz,
//     the count decided by the synth's clock),
//   - interface 1 alt 1, EP 0x01 OUT (adaptive): 2 ch x 16 bit.
// Like the driver's PGFramePatternObserver (FUN_f1027520) every received IN packet's frame count is recorded and
// the OUT stream sends the same counts ("frame pattern" mirroring), so the synth stays the clock master and the
// plugin's frame counter is exactly the number of frames the synth produced. WASAPI instead delivers frames by
// the PC's clock (exactly 44100/s), which drifts against the synth (23 ppm in the VM, E074) until the synth
// rejects the plugin's timestamps ("Sync Information Event", global 0x54).
// Sample rate: SET_CUR SAMPLING_FREQ (class request to the endpoint) on both endpoints, as the driver does; 44.1 or
// 48 kHz (rate following, design-rate.md step 4): the broker sets the wanted rate (vua_want_rate) and this thread
// switches between two reads in the driver's order (ratesw.h) - it owns the handles, so no SET_CUR hits a running
// stream.
#include "vtiusbaudio.h"
#include "pktstat.h"
#include "ratesw.h"
#include <setupapi.h>
#include <winusb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Device interface GUID of MI_00 (vti_winusb.inf, MI00_AddReg)
static const GUID GUID_MI00 = {0x9B879656, 0x91A6, 0x4D84, {0x9C, 0x75, 0x1F, 0xAC, 0x19, 0xEA, 0x62, 0x5B}};

#define EP_OUT 0x01
#define EP_IN 0x82
#define IN_CH 6
#define OUT_CH 2
#define IN_PKT 592      // wMaxPacketSize EP 0x82
#define OUT_PKT 200     // wMaxPacketSize EP 0x01
#define NP 10           // packets (ms) per IN read = per plugin delivery (as WASAPI's 10 ms period; 4 ms was too short, E077)
#define NR 8            // IN reads in flight (80 ms)
#define NW 64           // OUT packet slots
#define OUT_LEAD 12     // OUT packets queued before the first IN packet arrives (ms)
#define FN_MARGIN 8     // a read is scheduled at least this many USB frames (ms) ahead of the current frame
#define GAP_MAX 5000    // largest gap (ms) filled with silence after a stall

typedef struct {
    HANDLE file;
    WINUSB_INTERFACE_HANDLE h0, h1, h2; // interface 0 (control), 1 (OUT stream), 2 (IN stream)
    WINUSB_ISOCH_BUFFER_HANDLE bin, bout;
    uint8_t *inbuf, *outbuf;
    USBD_ISO_PACKET_DESCRIPTOR desc[NR][NP];
    OVERLAPPED rov[NR], wov[NW];
    int rbusy[NR], wbusy[NW]; // a read / write is queued (only those are waited for)
    unsigned wnext;      // next OUT slot
    int out_running;     // OUT stream started (ContinueStream valid)
    uint32_t rate;       // the rate set with SET_CUR (44100 or 48000)
    unsigned pattern;    // nominal pattern position (44/45 at 44.1 kHz, 48 at 48 kHz)
    ULONG next_fn;       // USB frame number of the next IN read
    ULONG gap[NR];       // USB frames skipped before read i (it was rescheduled after a stall)
} dev;

static vua_log_fn g_log;
#define LOG(...) do { if (g_log) g_log(__VA_ARGS__); } while (0)

static int open_dev(dev *d)
{
    HDEVINFO di = SetupDiGetClassDevsW(&GUID_MI00, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (di == INVALID_HANDLE_VALUE)
        return -1;
    SP_DEVICE_INTERFACE_DATA id = {.cbSize = sizeof id};
    int ok = -1;
    if (SetupDiEnumDeviceInterfaces(di, NULL, &GUID_MI00, 0, &id)) {
        DWORD need = 0;
        SetupDiGetDeviceInterfaceDetailW(di, &id, NULL, 0, &need, NULL);
        // the size query failed (need too small for the struct) or no memory: no path, the caller falls back to WASAPI
        SP_DEVICE_INTERFACE_DETAIL_DATA_W *dd = need >= sizeof *dd ? malloc(need) : NULL;
        if (dd)
            dd->cbSize = sizeof *dd;
        if (dd && SetupDiGetDeviceInterfaceDetailW(di, &id, dd, need, NULL, NULL)) {
            d->file = CreateFileW(dd->DevicePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, NULL);
            if (d->file == INVALID_HANDLE_VALUE)
                LOG("usbaudio: open MI_00 failed (error %lu; in use by another process?)", GetLastError());
            else if (!WinUsb_Initialize(d->file, &d->h0))
                LOG("usbaudio: WinUsb_Initialize failed (error %lu)", GetLastError());
            else
                ok = 0;
        }
        free(dd);
    }
    SetupDiDestroyDeviceInfoList(di);
    return ok;
}

// UAC1 SET_CUR SAMPLING_FREQ_CONTROL on an isochronous endpoint (bmRequestType 0x22 = class, endpoint)
static int set_rate(WINUSB_INTERFACE_HANDLE h, uint8_t ep, uint32_t rate)
{
    WINUSB_SETUP_PACKET sp = {0x22, 0x01, 0x0100, ep, 3};
    UCHAR data[3] = {(UCHAR)rate, (UCHAR)(rate >> 8), (UCHAR)(rate >> 16)};
    ULONG done = 0;
    if (!WinUsb_ControlTransfer(h, sp, data, 3, &done, NULL)) {
        LOG("usbaudio: SET_CUR %u Hz on EP 0x%02x failed (error %lu)", rate, ep, GetLastError());
        return -1;
    }
    return 0;
}

// Abort both pipes, wait for every queued read and write, unregister the buffers: nothing of the stream is left on
// the wire (the first step of a rate switch, and of the close).
static void stream_stop(dev *d)
{
    if (d->h2)
        WinUsb_AbortPipe(d->h2, EP_IN);
    if (d->h1)
        WinUsb_AbortPipe(d->h1, EP_OUT);
    DWORD n;
    for (int i = 0; i < NR; i++)
        if (d->rbusy[i]) {
            GetOverlappedResult(d->file, &d->rov[i], &n, TRUE);
            d->rbusy[i] = 0;
        }
    for (int i = 0; i < NW; i++)
        if (d->wbusy[i]) {
            GetOverlappedResult(d->file, &d->wov[i], &n, TRUE);
            d->wbusy[i] = 0;
        }
    d->out_running = 0;
    d->wnext = 0;
    if (d->bin)
        WinUsb_UnregisterIsochBuffer(d->bin);
    if (d->bout)
        WinUsb_UnregisterIsochBuffer(d->bout);
    d->bin = d->bout = NULL;
}

static void close_dev(dev *d)
{
    stream_stop(d);
    for (int i = 0; i < NR; i++)
        if (d->rov[i].hEvent)
            CloseHandle(d->rov[i].hEvent);
    for (int i = 0; i < NW; i++)
        if (d->wov[i].hEvent)
            CloseHandle(d->wov[i].hEvent);
    if (d->h2) {
        WinUsb_SetCurrentAlternateSetting(d->h2, 0);
        WinUsb_Free(d->h2);
    }
    if (d->h1) {
        WinUsb_SetCurrentAlternateSetting(d->h1, 0);
        WinUsb_Free(d->h1);
    }
    if (d->h0)
        WinUsb_Free(d->h0);
    if (d->file && d->file != INVALID_HANDLE_VALUE)
        CloseHandle(d->file);
    VirtualFree(d->inbuf, 0, MEM_RELEASE);
    VirtualFree(d->outbuf, 0, MEM_RELEASE);
}

static vua_out_fn g_out;
static void *g_out_user;

void vua_set_out(vua_out_fn fn, void *user)
{
    g_out_user = user;
    g_out = fn;
}

static volatile LONG g_rate_lost; // seconds of the other rate's packets seen (E241); read and cleared by vua_rate_lost()

LONG vua_rate_lost(void) { return InterlockedExchange(&g_rate_lost, 0); }

static volatile LONG g_want = VUA_RATE, g_rate = VUA_RATE, g_switches; // vua_want_rate / vua_rate / vua_switches
static void (*g_before_start)(uint32_t rate);

void vua_want_rate(uint32_t rate) { InterlockedExchange(&g_want, (LONG)vti_rate_valid(rate)); }
uint32_t vua_rate(void) { return (uint32_t)g_rate; }
LONG vua_switches(void) { return g_switches; }
void vua_set_rate_fn(void (*before_start)(uint32_t rate)) { g_before_start = before_start; }

// Queue one OUT packet of `frames` frames: audio from the vua_set_out source, or silence (`silent`, or no source).
static int send_out(dev *d, unsigned frames, int silent)
{
    unsigned s = d->wnext;
    if (d->wbusy[s]) { // reap the oldest slot (it was queued NW packets = NW ms ago, long done)
        DWORD n;
        if (!GetOverlappedResult(d->file, &d->wov[s], &n, TRUE))
            d->out_running = 0; // stream broke (e.g. underrun): restart with ContinueStream FALSE
        d->wbusy[s] = 0;
    }
    ULONG len = frames * OUT_CH * 2;
    vua_out_fn out = g_out;
    if (out && !silent)
        out((int16_t *)(d->outbuf + s * OUT_PKT), frames, g_out_user);
    else
        memset(d->outbuf + s * OUT_PKT, 0, len);
    ResetEvent(d->wov[s].hEvent);
    if (!WinUsb_WriteIsochPipeAsap(d->bout, s * OUT_PKT, len, d->out_running ? TRUE : FALSE, &d->wov[s]) &&
        GetLastError() != ERROR_IO_PENDING) {
        LOG("usbaudio: OUT write failed (error %lu)", GetLastError());
        d->out_running = 0;
        return -1;
    }
    d->out_running = 1;
    d->wbusy[s] = 1;
    d->wnext = (s + 1) % NW;
    return 0;
}

// Reads are scheduled on explicit USB frame numbers (1 frame = 1 ms = 1 packet at full speed), so the stream
// position is always known: if the thread stalled and the next frame is already too close or past, the read is
// moved to now + FN_MARGIN and the skipped frames are recorded in gap[i]; they are filled with silence of the
// nominal length when read i completes, so the plugin's frame counter stays equal to the synth's (E078).
static int submit_read(dev *d, int i)
{
    ULONG now = 0;
    LARGE_INTEGER ts;
    if (!WinUsb_GetCurrentFrameNumber(d->h2, &now, &ts)) {
        LOG("usbaudio: GetCurrentFrameNumber failed (error %lu)", GetLastError());
        return -1;
    }
    d->gap[i] = 0;
    if ((LONG)(d->next_fn - now) < FN_MARGIN) {
        ULONG start = now + FN_MARGIN;
        d->gap[i] = start - d->next_fn;
        d->next_fn = start;
    }
    ULONG fn = d->next_fn;
    ResetEvent(d->rov[i].hEvent);
    if (!WinUsb_ReadIsochPipe(d->bin, i * NP * IN_PKT, NP * IN_PKT, &fn, NP, d->desc[i], &d->rov[i]) &&
        GetLastError() != ERROR_IO_PENDING) {
        LOG("usbaudio: IN read failed (error %lu)", GetLastError());
        return -1;
    }
    d->rbusy[i] = 1;
    d->next_fn += NP;
    return 0;
}

// Deliver `packets` USB frames of silence with the nominal pattern (the driver's repair).
static uint32_t fill_silence(dev *d, int16_t *pcm, ULONG packets, vua_in_fn in, void *user)
{
    uint32_t total = 0;
    while (packets) {
        ULONG k = packets < NP ? packets : NP;
        uint32_t frames = 0;
        for (ULONG j = 0; j < k; j++)
            frames += vti_pkt_nominal(d->rate, d->pattern++);
        memset(pcm, 0, frames * IN_CH * 2);
        in(pcm, frames, user);
        total += frames;
        packets -= k;
    }
    return total;
}

// Register the buffers, queue the OUT lead-in and NR reads at d->rate (the start, and the last step of a switch).
static int stream_start(dev *d)
{
    d->pattern = 0;
    if (!WinUsb_RegisterIsochBuffer(d->h2, EP_IN, d->inbuf, NR * NP * IN_PKT, &d->bin) ||
        !WinUsb_RegisterIsochBuffer(d->h1, EP_OUT, d->outbuf, NW * OUT_PKT, &d->bout)) {
        LOG("usbaudio: RegisterIsochBuffer failed (error %lu)", GetLastError());
        return -1;
    }
    // OUT lead-in with the nominal pattern (44.1 kHz: 44 x 9, 45, the driver's InitFramePattern; 48 kHz: 48), then
    // every IN packet is mirrored
    for (int i = 0; i < OUT_LEAD; i++)
        send_out(d, vti_pkt_nominal(d->rate, d->pattern++), 0);
    ULONG now = 0;
    LARGE_INTEGER ts;
    WinUsb_GetCurrentFrameNumber(d->h2, &now, &ts);
    d->next_fn = now + FN_MARGIN;
    for (int i = 0; i < NR; i++)
        if (submit_read(d, i))
            return -1;
    LOG("usbaudio: streaming %.1f kHz on WinUSB (IN EP 0x82 %d ch, OUT EP 0x01 %d ch, frame pattern mirrored)",
        d->rate / 1000.0, IN_CH, OUT_CH);
    return 0;
}

// the operations of a switch (ratesw.h), on the stream thread's device
static void op_stop(void *u) { stream_stop((dev *)u); }
static int op_alt(void *u, int iface, int alt)
{
    dev *d = (dev *)u;
    if (!WinUsb_SetCurrentAlternateSetting(iface == 1 ? d->h1 : d->h2, (UCHAR)alt)) {
        LOG("usbaudio: alternate setting %d of interface %d failed (error %lu)", alt, iface, GetLastError());
        return -1;
    }
    return 0;
}
static void op_sleep(void *u, int ms)
{
    (void)u;
    Sleep((DWORD)ms);
}
static int op_set_cur(void *u, uint8_t ep, uint32_t rate)
{
    dev *d = (dev *)u;
    return set_rate(ep == EP_IN ? d->h2 : d->h1, ep, rate);
}
static int op_start(void *u, uint32_t rate)
{
    dev *d = (dev *)u;
    d->rate = rate;
    InterlockedExchange(&g_rate, (LONG)rate);
    if (g_before_start)
        g_before_start(rate);
    return stream_start(d);
}

int vua_run(volatile LONG *running, vua_in_fn in, void *user, vua_log_fn log)
{
    g_log = log;
    const uint32_t want0 = (uint32_t)g_want;
    dev d = {0};
    vti_rate_ops ops = {&d, op_stop, op_alt, op_sleep, op_set_cur, op_start};
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL); // the driver completes at DPC level
    if (open_dev(&d))
        return -1; // no WinUSB audio function: caller falls back to WASAPI
    int rc = 1;
    if (!WinUsb_GetAssociatedInterface(d.h0, 0, &d.h1) || !WinUsb_GetAssociatedInterface(d.h0, 1, &d.h2)) {
        LOG("usbaudio: GetAssociatedInterface failed (error %lu)", GetLastError());
        goto out;
    }
    if (!WinUsb_SetCurrentAlternateSetting(d.h1, 1) || !WinUsb_SetCurrentAlternateSetting(d.h2, 1)) {
        LOG("usbaudio: SetCurrentAlternateSetting failed (error %lu)", GetLastError());
        goto out;
    }
    d.rate = want0;
    if (vti_rate_set_cur(&ops, d.rate)) { // EP 0x82, 0x01, 0x82, 0x01 like the driver (E323)
        if (d.rate == VUA_RATE)
            goto out;
        LOG("usbaudio: the Virus refused %.1f kHz at the start - 44.1 kHz instead", d.rate / 1000.0);
        d.rate = VUA_RATE; // once, like a refused switch (ratesw.h)
        if (vti_rate_set_cur(&ops, d.rate))
            goto out;
    }
    InterlockedCompareExchange(&g_want, (LONG)d.rate, (LONG)want0);
    InterlockedExchange(&g_rate, (LONG)d.rate);
    if (g_before_start)
        g_before_start(d.rate);
    d.inbuf = VirtualAlloc(NULL, NR * NP * IN_PKT, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    d.outbuf = VirtualAlloc(NULL, NW * OUT_PKT, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    for (int i = 0; i < NR; i++)
        d.rov[i].hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    for (int i = 0; i < NW; i++)
        d.wov[i].hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!d.inbuf || !d.outbuf || stream_start(&d))
        goto out;
    static int16_t pcm[NP * (IN_PKT / 2)];
    uint64_t total = 0, errs = 0, gaps = 0, next_log = (uint64_t)d.rate * 60;
    vti_pktstat ps = {0}; // packet sizes as sent, before the repair (E175)
    vti_pktstat_set_rate(&ps, d.rate);
    int cur = 0, rate_tries = 0;
    rc = 0;
    while (*running) {
        uint32_t want = (uint32_t)g_want;
        if (want != d.rate) { // between two reads: the whole stream stops, switches and starts again
            uint32_t from = d.rate;
            LOG("usbaudio: switching %.1f -> %.1f kHz", from / 1000.0, want / 1000.0);
            int r = vti_rate_switch(&ops, from, want);
            // asked once: a refusal is not retried; a newer wish that came in meanwhile stays
            InterlockedCompareExchange(&g_want, (LONG)d.rate, (LONG)want);
            InterlockedIncrement(&g_switches);
            if (r) {
                LOG("usbaudio: rate switch to %.1f kHz %s", want / 1000.0,
                    r == -1 ? "refused by the Virus - back at the old rate" : "failed - the stream stops");
                if (r == -2) {
                    rc = 1;
                    break;
                }
            }
            memset(&ps, 0, sizeof ps);
            vti_pktstat_set_rate(&ps, d.rate);
            cur = 0;
            rate_tries = 0;
            continue;
        }
        DWORD n = 0;
        if (WaitForSingleObject(d.rov[cur].hEvent, 200) != WAIT_OBJECT_0)
            continue;
        BOOL ok = GetOverlappedResult(d.file, &d.rov[cur], &n, FALSE);
        d.rbusy[cur] = 0;
        uint32_t frames = 0;
        if (d.gap[cur]) { // frames skipped before this read (stall): they passed on the wire, count them
            ULONG g = d.gap[cur] < GAP_MAX ? d.gap[cur] : GAP_MAX;
            gaps += d.gap[cur];
            if (gaps <= 2000)
                LOG("usbaudio: stream stalled, %lu ms filled with silence", d.gap[cur]);
            total += fill_silence(&d, pcm, g, in, user);
        }
        if (ok) {
            // Packet offsets: relative to this read's part of the registered buffer (WinUSB docs). Logged once.
            uint8_t *base = d.inbuf + cur * NP * IN_PKT;
            static int shown;
            if (cur == 1 && !shown++)
                LOG("usbaudio: read 1 packet offsets %lu %lu %lu (read size %d)", d.desc[1][0].Offset,
                    d.desc[1][1].Offset, d.desc[1][NP - 1].Offset, NP * IN_PKT);
            if (d.desc[cur][0].Offset >= (ULONG)(cur * NP * IN_PKT) && cur)
                base = d.inbuf; // offsets turned out to be absolute
            for (int k = 0; k < NP; k++) {
                USBD_ISO_PACKET_DESCRIPTOR *p = &d.desc[cur][k];
                uint32_t f = p->Status == 0 ? p->Length / (IN_CH * 2) : 0;
                unsigned nominal = vti_pkt_nominal(d.rate, d.pattern++);
                int ev = vti_pktstat_add(&ps, f);
                if (ev > 0)
                    LOG("usbaudio: packets out of the %.1f kHz range since %llu ms after the start (last: %u frames, "
                        "%lu bytes, status 0x%lx) - replaced with silence", d.rate / 1000.0,
                        (unsigned long long)ps.bad_since, f, p->Length, (unsigned long)p->Status);
                else if (ev < 0) {
                    LOG("usbaudio: packets back in range at %llu ms after the start", (unsigned long long)ps.packets);
                    rate_tries = 0;
                }
                // the Virus left the wanted rate (E241: after a wake, a replug): once per second of the other rate's
                // packets ask for it again, three times; the broker's device thread adds the panel's global
                // (vua_rate_lost)
                if (vti_pktstat_rate_lost(&ps) && ps.bad_run % VTI_PKT_BAD_RUN == 0) {
                    const char *other = d.rate == 48000 ? "44.1" : "48";
                    if (rate_tries < 3) {
                        rate_tries++;
                        // both endpoints every time: a failed OUT request must not skip the IN one
                        int r_out = set_rate(d.h1, EP_OUT, d.rate), r_in = set_rate(d.h2, EP_IN, d.rate);
                        int r = r_out || r_in;
                        LOG("usbaudio: the Virus sends %s kHz packets - asking for %.1f kHz again (%d/3): SET_CUR %s",
                            other, d.rate / 1000.0, rate_tries, r ? "failed" : "accepted");
                    } else if (rate_tries++ == 3)
                        LOG("usbaudio: the Virus stays at %s kHz - its sound stays silent; switch it off and on", other);
                    InterlockedIncrement(&g_rate_lost);
                }
                int silent;
                unsigned of = vti_pkt_out(d.rate, f, nominal, &silent); // the other rate: the same size, silent (E241)
                if (!vti_pkt_in_range(d.rate, f)) {
                    // Lost packet, or still the old rate right after SET_CUR (~0.3 s): substitute the nominal pattern
                    // value with silence, like the driver's PGFramePatternObserver "repair".
                    if (p->Status)
                        errs++;
                    f = nominal;
                    memset(pcm + frames * IN_CH, 0, f * IN_CH * 2);
                } else {
                    memcpy(pcm + frames * IN_CH, base + p->Offset, f * IN_CH * 2);
                }
                frames += f;
                send_out(&d, of, silent); // mirror the synth's packet towards it
            }
            total += frames;
            in(pcm, frames, user);
        } else {
            // Read lost: its NP frames passed on the wire, keep the clock running with silence of nominal length.
            errs++;
            if (errs <= 20)
                LOG("usbaudio: IN read %d failed (error %lu), repaired with silence", cur, GetLastError());
            for (unsigned k = 0; k < NP; k++)
                send_out(&d, vti_pkt_nominal(d.rate, d.pattern + k), 0); // the same counts fill_silence uses
            total += fill_silence(&d, pcm, NP, in, user);
        }
        if (submit_read(&d, cur)) {
            rc = 1;
            break;
        }
        cur = (cur + 1) % NR;
        if (total >= next_log) {
            const uint64_t *z = ps.size;
            LOG("usbaudio: %llu frames, packets 43/44/45: %llu/%llu/%llu, errors %llu, stalled %llu ms; out of range "
                "(silence): empty %llu, 1-42 %llu, 46 %llu, 47-49 %llu, more %llu",
                (unsigned long long)total, (unsigned long long)z[VTI_PKT_43], (unsigned long long)z[VTI_PKT_44],
                (unsigned long long)z[VTI_PKT_45], (unsigned long long)errs, (unsigned long long)gaps,
                (unsigned long long)z[VTI_PKT_EMPTY], (unsigned long long)z[VTI_PKT_SHORT],
                (unsigned long long)z[VTI_PKT_46], (unsigned long long)z[VTI_PKT_48K], (unsigned long long)z[VTI_PKT_LONG]);
            next_log += (uint64_t)d.rate * 60;
        }
    }
out:
    close_dev(&d);
    LOG("usbaudio: stopped (rc %d)", rc);
    return rc;
}
