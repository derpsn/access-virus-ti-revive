// vtibroker.c - per-user background process that owns the Virus TI over WinUSB and serves clients
// (wrapper VST3 instances, later Control Center and MIDI ports) over \\.\pipe\vtibroker (vtiproto.h).
//
// Why a broker (findings/design.md, E042): WinUSB allows one owner per interface, and the device stops
// accepting OUT data when nobody drains EP 0x85/0x86. The broker keeps the device open and drained
// permanently, sends the handshake once per device arrival, reconnects after unplug/errors, and
// fans received data out to every connected client.
// Audio (protocol 2): the broker also owns the USB audio function (MI_00) while an audio client is registered
// and streams it on WinUSB (vtiusbaudio.c) into the shared input ring of vtiaudioshm.h, signalling every audio
// client's event per block, like VirusUSB.sys filled each client's mapped AudioSM.
//
// usage: vtibroker.exe [-v]      (-v: log every message; log file %TEMP%\vtibroker.log)
#include "vticore.h"
#include "vtiproto.h"
#include "brokerinfo.h"
#include "flashinfo.h"
#include "ctrlguard.h"
#include "owner.h"
#include "ratelever.h"
#include "vtipipe_io.h"
#include "vtiaudioshm.h"
#include "vtiusbaudio.h"
#include "ratesw.h"
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CLIENTS 16
#define IDLE_EXIT_MS 30000 // exit when no client has been connected for this long (started on demand, vticore_pipe.c)

static HANDLE g_log = INVALID_HANDLE_VALUE;
static int g_verbose;
static CRITICAL_SECTION g_cs_log, g_cs_dev, g_cs_clients;
static vti *g_dev;
static volatile LONG g_dev_failed; // set by the RX path on fatal device errors

#define QUEUE_BYTES (256 * 1024)

// Messages to a client go through a byte queue drained by the client's own sender thread, so a slow or
// stuck client can never block the device readers (they must keep EP 0x85/0x86 drained, E042).
typedef struct {
    HANDLE pipe;
    CRITICAL_SECTION q; // protects queue
    HANDLE q_event;     // set when data is queued or the client dies
    uint8_t *queue;
    uint32_t q_head, q_len;
    volatile LONG alive;
    HANDLE sender;
    int notes; // has sent note-ons: silence the Virus when it goes away (hanging-note protection)
    int observer;            // protocol 4: read-only observer (Nerd mode)
    volatile LONG64 dropped; // observer: messages dropped because its queue was full
    char name[48];           // from HELLO (protocol 4)
} client;
static client *g_clients[MAX_CLIENTS];

// Plug-in instances of different programs (two DAWs) must not share the Virus: each sends its own sync ping and resets
// the Virus' only stamp clock (E148). The owner rule of owner.h, the same in the POSIX broker: the first
// "TI Revive Virus Control" owns it, the others are told "another program has it" (state 2, no RX, no writes) until it
// leaves. The ASIO driver and the Control Center share it.
#define PLUGIN_NAME_TEXT VTI_PLUGIN_NAME
static client *g_plugin_owner; // g_cs_clients
static int is_plugin(const client *c) { return vti_is_plugin(c->name); }
static int plugin_waits(const client *c) { return vti_client_waits(is_plugin(c), c == g_plugin_owner); } // g_cs_clients held

// ------------------------------------------------------------------------------------------------- audio
static vti_audio_shm *g_shm;
static CRITICAL_SECTION g_cs_audio;     // g_audio list (short holds; also taken by the audio callback)
static CRITICAL_SECTION g_cs_audio_ctl; // stream start/stop (never held by the audio callback)
static struct {
    client *c;
    HANDLE ev;
} g_audio[MAX_CLIENTS];
static HANDLE g_audio_thread;
static volatile LONG g_audio_running, g_audio_blocks;

static void obs_log(const char *line, int len); // observers get every log line (below)

static void blog(const char *fmt, ...)
{
    char buf[1024];
    SYSTEMTIME st;
    GetLocalTime(&st);
    int n = snprintf(buf, sizeof buf, "%02d:%02d:%02d.%03d ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_list ap;
    va_start(ap, fmt);
    n += vsnprintf(buf + n, sizeof buf - n - 2, fmt, ap);
    va_end(ap);
    if (n > (int)sizeof buf - 2)
        n = sizeof buf - 2;
    while (n > 0 && buf[n - 1] == '\n')
        n--;
    buf[n++] = '\n';
    DWORD w;
    EnterCriticalSection(&g_cs_log);
    WriteFile(g_log, buf, n, &w, NULL);
    fwrite(buf, 1, n, stdout);
    LeaveCriticalSection(&g_cs_log);
    obs_log(buf, n - 1);
}

// Queue one message (header + a + b). On overflow the client is dropped instead of blocking the caller.
// Called by vua_run's streaming thread for every completed IN read (~441 frames per 10 ms).
static void audio_in(const int16_t *pcm, uint32_t frames, void *user)
{
    (void)user;
    int64_t w = g_shm->in_frames;
    for (uint32_t i = 0; i < frames; i++)
        memcpy(&g_shm->in[((w + i) & (VTI_AUDIO_RING - 1)) * VTI_AUDIO_IN_CH], pcm + i * VTI_AUDIO_IN_CH,
               VTI_AUDIO_IN_CH * sizeof(int16_t));
    LARGE_INTEGER q;
    QueryPerformanceCounter(&q);
    g_shm->in_qpc = q.QuadPart;
    InterlockedExchange64((volatile LONG64 *)&g_shm->in_frames, w + frames); // publishes the data written above
    InterlockedIncrement(&g_audio_blocks);
    EnterCriticalSection(&g_cs_audio);
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (g_audio[i].ev)
            SetEvent(g_audio[i].ev);
    LeaveCriticalSection(&g_cs_audio);
}

// OUT packet source (vua_set_out): frames the writing client (ASIO driver) appended to the output ring. Playback
// starts once OUT_PREFILL frames are buffered (so one late delivery does not underrun); if the client got ahead by
// more than OUT_MAX (e.g. after a stall), the oldest frames are dropped to bound the latency. Silence otherwise.
#define OUT_PREFILL 512
#define OUT_MAX 2048
static int g_out_playing;
static uint64_t g_out_underruns, g_out_dropped, g_out_frames;

static void audio_out(int16_t *dst, uint32_t frames, void *user)
{
    (void)user;
    int64_t w = *(volatile int64_t *)&g_shm->out_write, r = g_shm->out_read;
    int64_t avail = w - r;
    if (avail < 0 || avail > VTI_AUDIO_RING) { // writer restarted or far ahead: resynchronise
        r = w;
        avail = 0;
        g_out_playing = 0;
    }
    if (!g_out_playing && avail >= OUT_PREFILL)
        g_out_playing = 1;
    if (g_out_playing && avail > OUT_MAX) {
        g_out_dropped += (uint64_t)(avail - OUT_PREFILL);
        r = w - OUT_PREFILL;
        avail = OUT_PREFILL;
    }
    uint32_t n = 0;
    if (g_out_playing) {
        n = avail < frames ? (uint32_t)avail : frames;
        for (uint32_t i = 0; i < n; i++)
            memcpy(dst + i * VTI_AUDIO_OUT_CH, &g_shm->out[((r + i) & (VTI_AUDIO_RING - 1)) * VTI_AUDIO_OUT_CH],
                   VTI_AUDIO_OUT_CH * sizeof(int16_t));
        if (n < frames) { // ran dry: refill before playing again
            g_out_underruns++;
            g_out_playing = 0;
        }
    }
    memset(dst + n * VTI_AUDIO_OUT_CH, 0, (frames - n) * VTI_AUDIO_OUT_CH * sizeof(int16_t));
    g_shm->out_read = r + n;
    g_out_frames += n;
}

static vti_rate_guard g_rate_guard; // at most one rate switch per second (ratesw.h, rule 2); g_cs_audio_ctl

// stream thread, before the first read at `rate`: the shared ring learns it (rate, then rate_gen; readers re-anchor
// on a new generation, vtiaudioshm.h)
static void shm_rate(uint32_t rate)
{
    if (g_shm->rate == rate)
        return;
    g_shm->rate = rate;
    MemoryBarrier();
    InterlockedIncrement((volatile LONG *)&g_shm->rate_gen);
}

static DWORD WINAPI audio_thread(LPVOID unused)
{
    (void)unused;
    g_shm->out_read = g_shm->out_write; // nothing stale from a previous stream
    g_out_playing = 0;
    vua_set_out(audio_out, NULL);
    int r = vua_run(&g_audio_running, audio_in, NULL, blog);
    g_shm->streaming = 0;
    if (g_out_frames)
        blog("audio out: %llu frames played, %llu underruns, %llu frames dropped", (unsigned long long)g_out_frames,
             (unsigned long long)g_out_underruns, (unsigned long long)g_out_dropped);
    g_out_frames = g_out_underruns = g_out_dropped = 0;
    return (DWORD)r;
}

static int audio_clients(void)
{
    int n = 0;
    EnterCriticalSection(&g_cs_audio);
    for (int i = 0; i < MAX_CLIENTS; i++)
        n += g_audio[i].ev != NULL;
    LeaveCriticalSection(&g_cs_audio);
    return n;
}

// The rate a request gets (g_cs_audio_ctl held): like the original's single driver rate, the client that may set it
// (vti_rate_setter: the owning plug-in, or the ASIO driver without one) does; at most one switch per second.
static uint32_t audio_rate_for(uint32_t cur, uint32_t wanted, int setter)
{
    uint32_t now = (uint32_t)GetTickCount(); // 32-bit ms; the guard compares differences
    uint32_t to = vti_rate_choose(cur, wanted, setter, vti_rate_may_switch(&g_rate_guard, now));
    if (to != cur)
        vti_rate_switched(&g_rate_guard, now);
    else if (vti_rate_wanted(wanted, cur) != cur)
        blog("audio: %.1f kHz asked, staying at %.1f kHz (%s)", wanted / 1000.0, cur / 1000.0,
             setter ? "the last switch was less than a second ago" : "only the owning plug-in sets the rate");
    return to;
}

// Start the stream if needed, or switch a running one (g_cs_audio_ctl held): 1 = streaming, 0 = no WinUSB audio
// function, -1 = error.
static int audio_start(uint32_t wanted, int setter)
{
    if (g_audio_thread) {
        DWORD code;
        if (GetExitCodeThread(g_audio_thread, &code) && code == STILL_ACTIVE) {
            uint32_t cur = vua_rate(), to = audio_rate_for(cur, wanted, setter);
            if (to == cur)
                return 1;
            LONG n = vua_switches();
            vua_want_rate(to); // the stream thread switches between two reads (~0.3 s + the restart)
            for (int i = 0; i < 300 && vua_switches() == n; i++)
                if (WaitForSingleObject(g_audio_thread, 10) == WAIT_OBJECT_0)
                    break;
            if (GetExitCodeThread(g_audio_thread, &code) && code == STILL_ACTIVE)
                return 1; // at `to`, or back at `cur` when the Virus refused it (logged; the ring says which)
            blog("audio: the stream stopped during the rate switch (%d)", (int)code);
            CloseHandle(g_audio_thread);
            g_audio_thread = NULL;
            return -1;
        }
        CloseHandle(g_audio_thread);
        g_audio_thread = NULL;
    }
    // a fresh start protects no running stream: any explicit wish applies (still debounced); 0 keeps the last rate
    vua_want_rate(audio_rate_for(g_shm->rate ? g_shm->rate : VUA_RATE, wanted, 1));
    vua_set_rate_fn(shm_rate);
    g_audio_running = 1;
    g_audio_blocks = 0;
    g_audio_thread = CreateThread(NULL, 0, audio_thread, NULL, 0, NULL);
    for (int i = 0; i < 300; i++) { // the first block arrives ~0.1-0.5 s after the alternate setting
        if (g_audio_blocks) {
            g_shm->streaming = 1;
            blog("audio: streaming for %d client(s)", audio_clients());
            return 1;
        }
        if (WaitForSingleObject(g_audio_thread, 10) == WAIT_OBJECT_0)
            break;
    }
    DWORD code = 1;
    g_audio_running = 0;
    WaitForSingleObject(g_audio_thread, 3000);
    GetExitCodeThread(g_audio_thread, &code);
    CloseHandle(g_audio_thread);
    g_audio_thread = NULL;
    blog("audio: stream did not start (%d)", (int)code);
    return (int)code == -1 ? 0 : -1;
}

static void audio_stop_if_unused(void)
{
    if (g_audio_thread && !audio_clients()) {
        g_audio_running = 0;
        WaitForSingleObject(g_audio_thread, 3000);
        CloseHandle(g_audio_thread);
        g_audio_thread = NULL;
        blog("audio: stopped (no audio clients)");
    }
}

// Register (ev != NULL) or unregister (ev == NULL) client c.
static void audio_set(client *c, HANDLE ev)
{
    EnterCriticalSection(&g_cs_audio);
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (g_audio[i].c == c) {
            CloseHandle(g_audio[i].ev);
            g_audio[i].c = NULL;
            g_audio[i].ev = NULL;
        }
    for (int i = 0; ev && i < MAX_CLIENTS; i++)
        if (!g_audio[i].ev) {
            g_audio[i].c = c;
            g_audio[i].ev = ev;
            ev = NULL;
        }
    LeaveCriticalSection(&g_cs_audio);
    if (ev)
        CloseHandle(ev); // list full
}

static int32_t audio_request(client *c, uint32_t on, const char *name, uint32_t rate)
{
    EnterCriticalSection(&g_cs_audio_ctl);
    int32_t r = 1;
    if (on) {
        HANDLE ev = OpenEventA(EVENT_MODIFY_STATE, FALSE, name);
        if (!ev) {
            blog("audio: client event \"%s\" not found (%lu)", name, GetLastError());
            r = -1;
        } else {
            audio_set(c, ev);
            EnterCriticalSection(&g_cs_clients);
            int setter = vti_rate_setter(c == g_plugin_owner, g_plugin_owner != NULL, vti_is_asio(c->name));
            LeaveCriticalSection(&g_cs_clients);
            r = audio_start(rate, setter);
            if (r != 1)
                audio_set(c, NULL);
        }
    } else {
        audio_set(c, NULL);
    }
    audio_stop_if_unused();
    LeaveCriticalSection(&g_cs_audio_ctl);
    return r;
}

// the Virus' identification blocks (VTIMSG_FLASHINFO, flashinfo.h): read once per device open with the original
// driver's fixed read-only sequence, then cached; the device lock is held per request only (MIDI keeps flowing)
static unsigned g_dev_gen; // counts device opens: the cache belongs to one
static uint8_t g_flash[VTI_FLASH_BLOCKS * 16];
static int g_flash_len, g_flash_busy;
static unsigned g_flash_gen;

static int flash_ctl(void *u, uint8_t rt, uint8_t req, uint16_t val, uint16_t idx, uint8_t *d, uint16_t len)
{
    unsigned gen = *(unsigned *)u;
    EnterCriticalSection(&g_cs_dev);
    int r = g_dev && g_dev_gen == gen ? vti_control(g_dev, rt, req, val, idx, d, len) : -1;
    LeaveCriticalSection(&g_cs_dev);
    return r;
}

static void flash_nap(int ms) { Sleep((DWORD)ms); }

static int flash_info(uint8_t *out)
{
    EnterCriticalSection(&g_cs_dev);
    unsigned gen = g_dev_gen;
    int cached = g_dev && g_flash_len > 0 && g_flash_gen == gen, start = g_dev && !cached && !g_flash_busy;
    if (cached)
        memcpy(out, g_flash, (size_t)g_flash_len);
    if (start)
        g_flash_busy = 1;
    int n = cached ? g_flash_len : -1;
    LeaveCriticalSection(&g_cs_dev);
    if (!start)
        return n;
    n = vti_flash_info(flash_ctl, flash_nap, &gen, out);
    blog("flash info read (read-only, E026): %d bytes", n);
    EnterCriticalSection(&g_cs_dev);
    g_flash_busy = 0;
    if (n > 0 && g_dev_gen == gen) {
        memcpy(g_flash, out, (size_t)n);
        g_flash_len = n;
        g_flash_gen = gen;
    }
    LeaveCriticalSection(&g_cs_dev);
    return n;
}

static int send_msg(client *c, uint16_t type, const void *a, int alen, const void *b, int blen)
{
    if (!c->alive || alen + blen > VTIMSG_MAX)
        return -1;
    vtimsg h = {type, (uint16_t)(alen + blen)};
    uint32_t total = sizeof h + alen + blen;
    EnterCriticalSection(&c->q);
    int ok = c->q_len + total <= QUEUE_BYTES;
    if (ok) {
        const uint8_t *parts[3] = {(const uint8_t *)&h, a, b};
        uint32_t lens[3] = {sizeof h, (uint32_t)alen, (uint32_t)blen};
        for (int p = 0; p < 3; p++)
            for (uint32_t i = 0; i < lens[p]; i++)
                c->queue[(c->q_head + c->q_len++) % QUEUE_BYTES] = parts[p][i];
    } else if (c->observer) { // an observer only loses this message (counted in its status)
        InterlockedIncrement64(&c->dropped);
    } else {
        c->alive = 0;
    }
    LeaveCriticalSection(&c->q);
    SetEvent(c->q_event);
    if (!ok && !c->observer)
        blog("client queue overflow: dropping client");
    return ok ? 0 : -1;
}

static DWORD WINAPI sender_thread(LPVOID arg)
{
    client *c = arg;
    uint8_t chunk[8192];
    while (c->alive) {
        WaitForSingleObject(c->q_event, 500);
        for (;;) {
            EnterCriticalSection(&c->q);
            uint32_t n = c->q_len < sizeof chunk ? c->q_len : sizeof chunk;
            for (uint32_t i = 0; i < n; i++)
                chunk[i] = c->queue[(c->q_head + i) % QUEUE_BYTES];
            c->q_head = (c->q_head + n) % QUEUE_BYTES;
            c->q_len -= n;
            LeaveCriticalSection(&c->q);
            if (!n)
                break;
            if (vtipipe_write(c->pipe, chunk, n)) {
                c->alive = 0;
                break;
            }
        }
    }
    CancelIoEx(c->pipe, NULL); // wakes the client thread's pending read
    return 0;
}

static void broadcast(uint16_t type, const void *a, int alen, const void *b, int blen) // not to observers
{
    EnterCriticalSection(&g_cs_clients);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        client *c = g_clients[i];
        if (!c || c->observer)
            continue;
        if (plugin_waits(c)) { // a waiting plug-in: "another program has it" (2) while the Virus is open, no traffic
            if (type == VTIMSG_STATE) {
                uint32_t st = alen >= 4 && *(const uint32_t *)a ? 2 : 0;
                send_msg(c, type, &st, 4, NULL, 0);
            }
            continue;
        }
        send_msg(c, type, a, alen, b, blen);
    }
    LeaveCriticalSection(&g_cs_clients);
}

// ---- observers (protocol 4, the Control Center's Nerd mode; see vtiproto.h) ----
static volatile LONG g_obs_count;
static volatile LONG64 g_cnt_out, g_cnt_in, g_cnt_ctl; // packets passed on (status)
static ULONGLONG g_t0;

static void obs_send(uint16_t type, const void *a, int alen, const void *b, int blen)
{
    if (alen + blen > VTIMSG_MAX)
        blen = VTIMSG_MAX - alen;
    EnterCriticalSection(&g_cs_clients);
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (g_clients[i] && g_clients[i]->observer)
            send_msg(g_clients[i], type, a, alen, b, blen);
    LeaveCriticalSection(&g_cs_clients);
}

static void obs_log(const char *line, int len)
{
    if (g_obs_count && len > 0)
        obs_send(VTIMSG_OBS_LOG, line, len, NULL, 0);
}

static void obs_traffic(uint8_t kind, uint8_t ep, const void *a, int alen, const void *b, int blen)
{
    InterlockedIncrement64(kind == 0 ? &g_cnt_out : kind == 1 ? &g_cnt_in : &g_cnt_ctl);
    if (!g_obs_count)
        return;
    uint8_t hdr[6] = {kind, ep};
    uint32_t t = (uint32_t)(GetTickCount64() - g_t0);
    memcpy(hdr + 2, &t, 4);
    uint8_t body[VTIMSG_MAX];
    int n = alen < (int)sizeof body ? alen : (int)sizeof body;
    memcpy(body, a, n);
    if (blen > 0 && n + blen <= (int)sizeof body)
        memcpy(body + n, b, blen), n += blen;
    obs_send(VTIMSG_OBS_TRAFFIC, hdr, 6, body, n);
}

static void obs_status(int dev_open) // once a second (device thread)
{
    if (!g_obs_count)
        return;
    char clients[600] = "", st[1100];
    EnterCriticalSection(&g_cs_clients);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        client *c = g_clients[i];
        if (!c)
            continue;
        size_t l = strlen(clients);
        snprintf(clients + l, sizeof clients - l, "%s%s%s", l ? ", " : "", c->name[0] ? c->name : "unnamed client",
                 c->observer ? " (observer)" : plugin_waits(c) ? " (waiting)" : c == g_plugin_owner ? " (owner)" : "");
    }
    // Windows: the clients share the Virus, except plug-ins of different programs (the first one owns it); the drop
    // counter is per observer
    for (int i = 0; i < MAX_CLIENTS; i++) {
        client *c = g_clients[i];
        if (!c || !c->observer)
            continue;
        int n = snprintf(st, sizeof st,
                         "device=%d\towner=%s\tclients=%s\taudio=%d\tto_device=%lld\tfrom_device=%lld\tcontrol=%lld"
                         "\tuptime_s=%u\tdropped=%lld",
                         dev_open, g_plugin_owner ? "shared, plug-in " PLUGIN_NAME_TEXT : "shared", clients,
                         g_audio_running ? 1 : 0, (long long)g_cnt_out, (long long)g_cnt_in,
                         (long long)g_cnt_ctl, (unsigned)((GetTickCount64() - g_t0) / 1000), (long long)c->dropped);
        send_msg(c, VTIMSG_OBS_STATUS, st, n < (int)sizeof st ? n : (int)sizeof st - 1, NULL, 0);
    }
    LeaveCriticalSection(&g_cs_clients);
}

static volatile LONG g_rate_lever; // a recovery switch ran (E331): log the Virus' report of the rate's global (E257)

static void on_rx(void *user, uint8_t ep, const uint8_t *data, int len)
{
    (void)user;
    if (g_verbose)
        blog("rx ep%02x %d bytes", ep, len);
    if (g_rate_lever && ep == VTI_EP_MI04_IN) {
        int v = vti_rate_report(data, len);
        if (v >= 0)
            blog("usbaudio: the Virus reports System Clock/Frequency = %s", v ? "48 kHz" : "44.1 kHz");
    }
    broadcast(VTIMSG_RX, &ep, 1, data, len);
    obs_traffic(1, ep, data, len, NULL, 0);
}

static void on_vti_log(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    blog("vticore: %s", buf);
}

// Device thread: open the device when present, reopen after failures (unplug, wedged pipes).
static int g_stay; // -stay: never exit when idle (debugging, or started by hand)

static int client_count_locked(void)
{
    int n = 0;
    for (int i = 0; i < MAX_CLIENTS; i++)
        n += g_clients[i] != NULL;
    return n;
}

static int client_count(void)
{
    int n = 0;
    EnterCriticalSection(&g_cs_clients);
    for (int i = 0; i < MAX_CLIENTS; i++)
        n += g_clients[i] != NULL;
    LeaveCriticalSection(&g_cs_clients);
    return n;
}

static DWORD WINAPI device_thread(LPVOID unused)
{
    (void)unused;
    DWORD last_busy = GetTickCount();
    for (;;) {
        if (client_count())
            last_busy = GetTickCount();
        else if (!g_stay && GetTickCount() - last_busy > IDLE_EXIT_MS) {
            EnterCriticalSection(&g_cs_clients); // no client may connect in between
            if (!client_count_locked()) {
                EnterCriticalSection(&g_cs_dev);
                if (g_dev)
                    vti_close(g_dev);
                blog("no clients for %d s: exiting", IDLE_EXIT_MS / 1000);
                ExitProcess(0);
            }
            LeaveCriticalSection(&g_cs_clients);
        }
        EnterCriticalSection(&g_cs_dev);
        if (g_dev && (g_dev_failed || vti_is_dead(g_dev))) {
            blog("device failed: closing");
            vti_close(g_dev);
            g_dev = NULL;
            uint32_t st = 0;
            broadcast(VTIMSG_STATE, &st, 4, NULL, 0);
        }
        if (!g_dev) {
            g_dev_failed = 0;
            g_dev = vti_open(on_rx, NULL); // starts the IN readers, then sends the handshake
            if (g_dev) {
                g_dev_gen++;
                blog("device open, handshake sent");
                uint32_t st = 1;
                broadcast(VTIMSG_STATE, &st, 4, NULL, 0);
            }
        }
        // the Virus left the stream's rate (E175/E241): the stream switches away and back (vti_relock, E331); log the
        // Virus' own report of the rate's global from now on
        if (g_dev && g_audio_running && vua_rate_lost())
            g_rate_lever = 1;
        int open = g_dev != NULL;
        LeaveCriticalSection(&g_cs_dev);
        static ULONGLONG last_status;
        if (GetTickCount64() - last_status >= 1000) {
            last_status = GetTickCount64();
            obs_status(open);
        }
        Sleep(g_dev ? 500 : 2000);
    }
    return 0;
}

// the device state for one client (2 for a waiting plug-in while the Virus is open)
static void send_state(client *c)
{
    EnterCriticalSection(&g_cs_dev);
    uint32_t st = g_dev != NULL;
    LeaveCriticalSection(&g_cs_dev);
    EnterCriticalSection(&g_cs_clients);
    if (st && plugin_waits(c))
        st = 2;
    LeaveCriticalSection(&g_cs_clients);
    send_msg(c, VTIMSG_STATE, &st, 4, NULL, 0);
}

static DWORD WINAPI client_thread(LPVOID arg)
{
    client *c = arg;
    uint8_t buf[VTIMSG_MAX];
    blog("client connected");
    // the device state goes out after the client's HELLO (its name): a second program's plug-in must not see the Virus
    // even for a moment - it would send its start-up messages into it. Clients say HELLO right after connecting; one
    // that sends nothing within 300 ms gets the state anyway.
    int state_sent = 0;
    for (int i = 0; i < 30; i++) {
        DWORD avail = 0;
        if (!PeekNamedPipe(c->pipe, NULL, 0, NULL, &avail, NULL) || avail >= sizeof(vtimsg))
            break;
        Sleep(10);
    }
    DWORD avail0 = 0;
    if (!PeekNamedPipe(c->pipe, NULL, 0, NULL, &avail0, NULL) || avail0 < sizeof(vtimsg)) {
        send_state(c);
        state_sent = 1;
    }
    for (;;) {
        vtimsg h;
        if (vtipipe_read(c->pipe, &h, sizeof h) || h.len > VTIMSG_MAX || vtipipe_read(c->pipe, buf, h.len))
            break;
        if (!state_sent && h.type != VTIMSG_HELLO) {
            send_state(c);
            state_sent = 1;
        }
        EnterCriticalSection(&g_cs_clients);
        const int waits = plugin_waits(c);
        LeaveCriticalSection(&g_cs_clients);
        switch (h.type) {
        case VTIMSG_HELLO:
            if (h.len > 4) { // protocol 4: the client's name
                size_t n = h.len - 4u < sizeof c->name - 1 ? h.len - 4u : sizeof c->name - 1;
                EnterCriticalSection(&g_cs_clients);
                memcpy(c->name, buf + 4, n);
                c->name[n] = 0;
                for (char *p = c->name; *p; p++)
                    if ((unsigned char)*p < 32)
                        *p = ' ';
                if (is_plugin(c) && !g_plugin_owner)
                    g_plugin_owner = c;
                LeaveCriticalSection(&g_cs_clients);
            }
            blog("client hello, protocol %u%s%s", h.len >= 4 ? *(uint32_t *)buf : 0, c->name[0] ? ": " : "", c->name);
            EnterCriticalSection(&g_cs_clients);
            if (is_plugin(c))
                blog(c == g_plugin_owner ? "the plug-in owns the Virus" : "plug-in waiting: another program's plug-in uses the Virus");
            LeaveCriticalSection(&g_cs_clients);
            if (!state_sent) {
                send_state(c);
                state_sent = 1;
            }
            {
                char info[96];
                send_msg(c, VTIMSG_INFO, info, vti_info_text(info, sizeof info), NULL, 0); // our version (INFO box)
            }
            break;
        case VTIMSG_OBSERVE: { // read-only observer: no RX, no writes; traffic copy, log and status instead
            EnterCriticalSection(&g_cs_clients);
            c->observer = 1;
            InterlockedIncrement(&g_obs_count);
            LeaveCriticalSection(&g_cs_clients);
            uint32_t ver = VTI_PROTO_VERSION, st0 = 0;
            send_msg(c, VTIMSG_STATE, &st0, 4, NULL, 0);
            send_msg(c, VTIMSG_OBSERVE_REPLY, &ver, 4, NULL, 0);
            blog("client is an observer (read-only)");
            break;
        }
        case VTIMSG_WRITE:
            if (h.len >= 1 && !c->observer && !waits) {
                if (buf[0] == VTI_EP_MI04_OUT && !c->notes && vti_has_note_on(buf + 1, h.len - 1))
                    c->notes = 1;
                EnterCriticalSection(&g_cs_dev);
                int r = g_dev ? vti_write(g_dev, buf[0], buf + 1, h.len - 1) : -1;
                LeaveCriticalSection(&g_cs_dev);
                obs_traffic(0, buf[0], buf + 1, h.len - 1, NULL, 0);
                if (g_verbose || r)
                    blog("write ep%02x %d bytes -> %d", buf[0], h.len - 1, r);
            }
            break;
        case VTIMSG_FLASHINFO: // the owner only (never an observer or a waiting plug-in)
            if (!c->observer) {
                uint8_t fl[4 + VTI_FLASH_BLOCKS * 16];
                int32_t n = !waits ? flash_info(fl + 4) : -1;
                memcpy(fl, &n, 4);
                send_msg(c, VTIMSG_FLASHINFO_REPLY, fl, 4 + (n > 0 ? n : 0), NULL, 0);
            }
            break;
        case VTIMSG_CONTROL:
            if (h.len >= 12 && !c->observer) {
                uint32_t seq = *(uint32_t *)buf;
                uint8_t *s = buf + 4, data[VTIMSG_MAX];
                uint16_t wl = s[6] | s[7] << 8;
                if (wl > VTIMSG_MAX - 16)
                    wl = VTIMSG_MAX - 16;
                if (!(s[0] & 0x80))
                    memcpy(data, buf + 12, h.len - 12 < wl ? h.len - 12 : wl);
                int ok = vti_control_allowed(s);
                if (!ok)
                    blog("refused control request %02x %02x (only standard reads pass, rule 2)", s[0], s[1]);
                EnterCriticalSection(&g_cs_dev);
                int32_t r = g_dev && !waits && ok
                                ? vti_control(g_dev, s[0], s[1], s[2] | s[3] << 8, s[4] | s[5] << 8, data, wl)
                                : -1;
                LeaveCriticalSection(&g_cs_dev);
                uint8_t rec[12];
                memcpy(rec, s, 8);
                memcpy(rec + 8, &r, 4);
                obs_traffic(2, 0, rec, 12, data, (s[0] & 0x80) ? (r > 0 ? r : 0) : wl);
                uint8_t hdr[8];
                memcpy(hdr, &seq, 4);
                memcpy(hdr + 4, &r, 4);
                send_msg(c, VTIMSG_CONTROL_REPLY, hdr, 8, data, (s[0] & 0x80) && r > 0 ? r : 0);
            }
            break;
        case VTIMSG_AUDIO:
            if (h.len >= 4 && !c->observer) {
                uint32_t rate = vti_audio_req_rate(buf, h.len); // protocol 5; older clients: 44100
                buf[h.len < VTIMSG_MAX ? h.len : VTIMSG_MAX - 1] = 0;
                int32_t r = waits ? -1 : audio_request(c, *(uint32_t *)buf, h.len > 4 ? (const char *)buf + 4 : "", rate);
                if (*(uint32_t *)buf)
                    blog("client audio on (%.1f kHz asked) -> %d, %.1f kHz", rate / 1000.0, r, g_shm->rate / 1000.0);
                else
                    blog("client audio off -> %d", r);
                send_msg(c, VTIMSG_AUDIO_REPLY, &r, 4, NULL, 0);
            }
            break;
        default:
            blog("client: unknown message %u", h.type);
        }
    }
    blog("client disconnected%s", c->observer ? " (observer)" : "");
    if (c->observer)
        InterlockedDecrement(&g_obs_count);
    if (c->notes) { // a DAW that crashed or was killed cannot send its note-offs any more
        uint8_t off[256];
        int n = vti_pack_notes_off(off, sizeof off);
        EnterCriticalSection(&g_cs_dev);
        int r = g_dev && n > 0 ? vti_write(g_dev, VTI_EP_MI04_OUT, off, n) : -1;
        LeaveCriticalSection(&g_cs_dev);
        blog("all notes off (the client had played notes) -> %d", r);
    }
    audio_request(c, 0, NULL, 0);
    client *next = NULL;
    EnterCriticalSection(&g_cs_clients);
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (g_clients[i] == c)
            g_clients[i] = NULL;
    if (g_plugin_owner == c) { // hand the Virus to the next waiting plug-in (another program)
        g_plugin_owner = NULL;
        for (int i = 0; i < MAX_CLIENTS && !g_plugin_owner; i++)
            if (g_clients[i] && !g_clients[i]->observer && is_plugin(g_clients[i]))
                g_plugin_owner = next = g_clients[i];
    }
    LeaveCriticalSection(&g_cs_clients);
    if (next) {
        blog("the next program's plug-in owns the Virus now");
        send_state(next);
    }
    c->alive = 0;
    SetEvent(c->q_event);
    WaitForSingleObject(c->sender, INFINITE); // removed from g_clients above: no new messages can arrive
    CloseHandle(c->sender);
    DisconnectNamedPipe(c->pipe);
    CloseHandle(c->pipe);
    CloseHandle(c->q_event);
    DeleteCriticalSection(&c->q);
    free(c->queue);
    free(c);
    return 0;
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        g_verbose |= !strcmp(argv[i], "-v");
        g_stay |= !strcmp(argv[i], "-stay");
    }
    g_t0 = GetTickCount64();
    InitializeCriticalSection(&g_cs_log);
    InitializeCriticalSection(&g_cs_dev);
    InitializeCriticalSection(&g_cs_clients);
    InitializeCriticalSection(&g_cs_audio);
    InitializeCriticalSection(&g_cs_audio_ctl);
    char path[MAX_PATH + 32], tmp[MAX_PATH];
    GetTempPathA(sizeof tmp, tmp);
    snprintf(path, sizeof path, "%svtibroker.log", tmp);
    g_log = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, 0, NULL);
    setvbuf(stdout, NULL, _IONBF, 0);
    // Single instance per session.
    HANDLE mtx = CreateMutexA(NULL, TRUE, "Local\\vtibroker");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        blog("another vtibroker is running, exiting");
        return 0;
    }
    (void)mtx;
    vti_log = on_vti_log;
    HANDLE map = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(vti_audio_shm),
                                    VTI_AUDIO_SHM_NAME);
    g_shm = map ? MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(vti_audio_shm)) : NULL;
    if (!g_shm) {
        blog("audio shared memory failed (%lu)", GetLastError());
        return 1;
    }
    g_shm->magic = VTI_AUDIO_MAGIC;
    g_shm->version = VTI_AUDIO_VERSION;
    g_shm->rate = VUA_RATE;
    g_shm->in_ch = VTI_AUDIO_IN_CH;
    g_shm->out_ch = VTI_AUDIO_OUT_CH;
    g_shm->ring_frames = VTI_AUDIO_RING;
    blog("vtibroker starting (protocol %d)", VTI_PROTO_VERSION);
    CloseHandle(CreateThread(NULL, 0, device_thread, NULL, 0, NULL));
    for (;;) {
        HANDLE p = CreateNamedPipeA(VTI_PIPE_NAME, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                                    PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, PIPE_UNLIMITED_INSTANCES, 65536,
                                    65536, 0, NULL);
        if (p == INVALID_HANDLE_VALUE) {
            blog("CreateNamedPipe failed: %lu", GetLastError());
            Sleep(1000);
            continue;
        }
        OVERLAPPED ov = {0};
        ov.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
        BOOL connected = ConnectNamedPipe(p, &ov);
        DWORD e = GetLastError(), dummy;
        if (!connected && e == ERROR_IO_PENDING)
            connected = GetOverlappedResult(p, &ov, &dummy, TRUE);
        else if (!connected && e == ERROR_PIPE_CONNECTED)
            connected = TRUE;
        CloseHandle(ov.hEvent);
        if (!connected) {
            CloseHandle(p);
            continue;
        }
        client *c = calloc(1, sizeof *c);
        c->pipe = p;
        c->alive = 1;
        c->queue = malloc(QUEUE_BYTES);
        c->q_event = CreateEventA(NULL, FALSE, FALSE, NULL);
        InitializeCriticalSection(&c->q);
        int slot = -1;
        EnterCriticalSection(&g_cs_clients);
        for (int i = 0; i < MAX_CLIENTS && slot < 0; i++)
            if (!g_clients[i]) {
                g_clients[i] = c;
                slot = i;
            }
        LeaveCriticalSection(&g_cs_clients);
        if (slot < 0) {
            blog("too many clients");
            DisconnectNamedPipe(p);
            CloseHandle(p);
            CloseHandle(c->q_event);
            DeleteCriticalSection(&c->q);
            free(c->queue);
            free(c);
            continue;
        }
        c->sender = CreateThread(NULL, 0, sender_thread, c, 0, NULL);
        CloseHandle(CreateThread(NULL, 0, client_thread, c, 0, NULL));
    }
}
