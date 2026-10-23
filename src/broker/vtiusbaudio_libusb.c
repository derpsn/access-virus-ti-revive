// vtiusbaudio_libusb.c - Virus TI USB audio streaming on libusb (Linux), the port of src/broker/vtiusbaudio.c
// (WinUSB, E075) for the Linux broker (findings/design-linux.md). The broker owns the whole device: the audio function
// (interface 0 control, 1 = OUT EP 0x01, 2 = IN EP 0x82) is taken from snd-usb-audio and streamed here:
//   - IN: 6 ch x 16 bit, one packet per 1 ms frame (44 or 45 frames at 44.1 kHz, the synth's clock decides),
//   - OUT (adaptive): 2 ch x 16 bit, every IN packet's frame count mirrored ("frame pattern", the driver's
//     PGFramePatternObserver), so the synth stays the clock master and the frame counter is exactly the number of
//     frames the synth produced.
// libusb has no frame-number scheduling (Linux usbfs uses ISO_ASAP): NR transfers stay queued, and a stall longer
// than that is detected by time and filled with silence of the nominal length, so the counter never slips (E078).
// Rate following (design-rate.md step 3): the stream runs at 44.1 or 48 kHz; vla_switch_rate() changes it in the
// driver's order (ratesw.h) without releasing the interfaces (a release would hand them back to snd-usb-audio).
#include "vtiusbaudio_libusb.h"
#include "pktstat.h"
#include "ratesw.h"
#include <libusb.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define EP_OUT 0x01
#define EP_IN 0x82
#define IN_CH 6
#define OUT_CH 2
#define IN_PKT 592  // wMaxPacketSize EP 0x82
#define OUT_PKT 200 // wMaxPacketSize EP 0x01
#define NP 10       // packets (ms) per transfer
#define NR 8        // IN transfers in flight (80 ms)
#define NW 16       // OUT transfers
#define OUT_LEAD 12 // OUT packets queued before the first IN packet

extern void (*vti_log)(const char *fmt, ...);

static struct {
    libusb_context *ctx;
    libusb_device_handle *h;
    vla_in_fn in;
    vla_out_fn out;
    void *user;
    volatile int running, in_flight, out_flight;
    int ev_started; // the event thread runs (also after an unplug cleared `running`): stream_stop joins it
    struct libusb_transfer *rt[NR], *wt[NW];
    int wfree[NW];
    pthread_mutex_t mx;
    pthread_t ev;
    uint32_t rate;         // the wanted rate (SET_CUR), 44100 or 48000
    unsigned pattern;      // nominal pattern position (44/45 at 44.1 kHz, 48 at 48 kHz)
    volatile int rate_lost; // a run of the other rate's packets was reported (E241); read and cleared by vla_rate_lost()
    double t0_ms;          // monotonic time of the first IN completion
    uint64_t delivered_ms; // USB frames (ms) delivered so far (packets + filled gaps)
    uint64_t frames, errs, gaps, next_log;
    vti_pktstat ps; // packet sizes as sent, before the repair (E175)
    int16_t pcm[(NP + 5000) * 49 * IN_CH]; // one transfer, or a filled gap of up to 5 s
} S;

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

static void out_cb(struct libusb_transfer *t)
{
    pthread_mutex_lock(&S.mx);
    for (int i = 0; i < NW; i++)
        if (S.wt[i] == t)
            S.wfree[i] = 1;
    S.out_flight--;
    pthread_mutex_unlock(&S.mx);
}

// Queue one OUT transfer carrying `n` packets of the given frame counts (audio from the out source, or silence).
// silent[k] (optional) = 1: packet k carries silence (the Virus sends 48 kHz packets, E241).
static void send_out(const unsigned *counts, const int *silent, int n)
{
    pthread_mutex_lock(&S.mx);
    int s = -1;
    for (int i = 0; i < NW && s < 0; i++)
        if (S.wfree[i])
            s = i;
    if (s < 0) { // all busy (should not happen: they complete every ms)
        pthread_mutex_unlock(&S.mx);
        return;
    }
    S.wfree[s] = 0;
    S.out_flight++;
    pthread_mutex_unlock(&S.mx);
    struct libusb_transfer *t = S.wt[s];
    t->num_iso_packets = n;
    int off = 0;
    for (int k = 0; k < n; k++) {
        unsigned len = counts[k] * OUT_CH * 2;
        t->iso_packet_desc[k].length = len;
        if (S.out && !(silent && silent[k]))
            S.out((int16_t *)(t->buffer + off), counts[k], S.user);
        else
            memset(t->buffer + off, 0, len);
        off += (int)len;
    }
    t->length = off;
    if (libusb_submit_transfer(t)) {
        pthread_mutex_lock(&S.mx);
        S.wfree[s] = 1;
        S.out_flight--;
        pthread_mutex_unlock(&S.mx);
    }
}

static uint32_t fill_silence(uint64_t packets) // nominal pattern, delivered in chunks
{
    uint32_t total = 0;
    while (packets) {
        uint64_t k = packets < NP ? packets : NP;
        uint32_t frames = 0;
        unsigned counts[NP];
        for (uint64_t j = 0; j < k; j++)
            frames += counts[j] = vti_pkt_nominal(S.rate, S.pattern++);
        memset(S.pcm, 0, frames * IN_CH * 2);
        S.in(S.pcm, frames, S.user);
        send_out(counts, NULL, (int)k);
        total += frames;
        packets -= k;
    }
    return total;
}

static void in_cb(struct libusb_transfer *t)
{
    if (!S.running || t->status == LIBUSB_TRANSFER_CANCELLED || t->status == LIBUSB_TRANSFER_NO_DEVICE) {
        pthread_mutex_lock(&S.mx);
        S.in_flight--;
        pthread_mutex_unlock(&S.mx);
        if (t->status == LIBUSB_TRANSFER_NO_DEVICE)
            S.running = 0;
        return;
    }
    double now = now_ms();
    if (!S.t0_ms)
        S.t0_ms = now - NP; // this transfer covered the last NP ms
    // stall: more ms passed than were delivered (transfers ran dry and the kernel restarted ASAP) -> fill silence
    double behind = (now - S.t0_ms) - (double)(S.delivered_ms + NP);
    if (behind > 3 * NP) {
        uint64_t g = (uint64_t)behind;
        if (g > 5000)
            g = 5000;
        S.gaps += g;
        if (S.gaps <= 2000)
            vti_log("usbaudio: stream stalled, %llu ms filled with silence", (unsigned long long)g);
        S.frames += fill_silence(g);
        S.delivered_ms += g;
    }
    uint32_t frames = 0;
    unsigned out_counts[NP];
    int out_silent[NP];
    for (int k = 0; k < t->num_iso_packets; k++) {
        struct libusb_iso_packet_descriptor *p = &t->iso_packet_desc[k];
        uint32_t f = p->status == LIBUSB_TRANSFER_COMPLETED ? p->actual_length / (IN_CH * 2) : 0;
        unsigned nominal = vti_pkt_nominal(S.rate, S.pattern++);
        int ev = vti_pktstat_add(&S.ps, f);
        if (ev > 0)
            vti_log("usbaudio: packets out of the %.1f kHz range since %llu ms after the start (last: %u frames, "
                    "%u bytes, status %d) - replaced with silence", S.rate / 1000.0, (unsigned long long)S.ps.bad_since,
                    f, p->actual_length, (int)p->status);
        else if (ev < 0)
            vti_log("usbaudio: packets back in range at %llu ms after the start", (unsigned long long)S.ps.packets);
        if (vti_pktstat_rate_lost(&S.ps) && S.ps.bad_run % VTI_PKT_BAD_RUN == 0)
            S.rate_lost = 1; // once per second of the other rate's packets: the device thread asks for the rate again
        out_counts[k] = vti_pkt_out(S.rate, f, nominal, &out_silent[k]);
        if (!vti_pkt_in_range(S.rate, f)) { // lost, or still the old rate right after SET_CUR: nominal silence ("repair")
            if (p->status != LIBUSB_TRANSFER_COMPLETED)
                S.errs++;
            f = nominal;
            memset(S.pcm + frames * IN_CH, 0, f * IN_CH * 2);
        } else {
            memcpy(S.pcm + frames * IN_CH, libusb_get_iso_packet_buffer_simple(t, (unsigned)k), f * IN_CH * 2);
        }
        frames += f;
    }
    send_out(out_counts, out_silent, t->num_iso_packets); // mirror the synth's packets towards it (E241)
    S.in(S.pcm, frames, S.user);
    S.frames += frames;
    S.delivered_ms += (uint64_t)t->num_iso_packets;
    if (S.frames >= S.next_log) {
        const uint64_t *z = S.ps.size;
        vti_log("usbaudio: %llu frames, packets 43/44/45: %llu/%llu/%llu, errors %llu, stalled %llu ms; out of range "
                "(silence): empty %llu, 1-42 %llu, 46 %llu, 47-49 %llu, more %llu",
                (unsigned long long)S.frames, (unsigned long long)z[VTI_PKT_43], (unsigned long long)z[VTI_PKT_44],
                (unsigned long long)z[VTI_PKT_45], (unsigned long long)S.errs, (unsigned long long)S.gaps,
                (unsigned long long)z[VTI_PKT_EMPTY], (unsigned long long)z[VTI_PKT_SHORT],
                (unsigned long long)z[VTI_PKT_46], (unsigned long long)z[VTI_PKT_48K], (unsigned long long)z[VTI_PKT_LONG]);
        S.next_log += (uint64_t)S.rate * 60;
    }
    if (libusb_submit_transfer(t)) {
        vti_log("usbaudio: IN resubmit failed");
        pthread_mutex_lock(&S.mx);
        S.in_flight--;
        pthread_mutex_unlock(&S.mx);
    }
}

static void *event_thread(void *unused)
{
    (void)unused;
    struct sched_param sp = {.sched_priority = 10};
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp); // best effort (needs rtprio; fine without)
    while (S.running || S.in_flight > 0 || S.out_flight > 0) {
        struct timeval tv = {0, 100000};
        libusb_handle_events_timeout_completed(S.ctx, &tv, NULL);
    }
    return NULL;
}

// UAC1 SET_CUR SAMPLING_FREQ_CONTROL on an isochronous endpoint (class request to the endpoint)
static int set_rate(uint8_t ep, uint32_t rate)
{
    uint8_t d[3] = {(uint8_t)rate, (uint8_t)(rate >> 8), (uint8_t)(rate >> 16)};
    int r = libusb_control_transfer(S.h, 0x22, 0x01, 0x0100, ep, d, 3, 1000);
    if (r != 3)
        vti_log("usbaudio: SET_CUR %u Hz on EP 0x%02x: %s", rate, ep, r < 0 ? libusb_error_name(r) : "short");
    return r == 3 ? 0 : -1;
}

// the transfers and the event thread, at S.rate: IN queued first, then the OUT lead-in (the driver starts IN, then OUT)
static int stream_start(void)
{
    S.running = 1;
    S.t0_ms = 0;
    S.delivered_ms = 0;
    S.pattern = 0;
    S.rate_lost = 0;
    memset(&S.ps, 0, sizeof S.ps);
    vti_pktstat_set_rate(&S.ps, S.rate);
    for (int i = 0; i < NW; i++) {
        S.wt[i] = libusb_alloc_transfer(NP);
        libusb_fill_iso_transfer(S.wt[i], S.h, EP_OUT, malloc(NP * OUT_PKT), NP * OUT_PKT, NP, out_cb, NULL, 1000);
        S.wfree[i] = 1;
    }
    for (int i = 0; i < NR; i++) {
        S.rt[i] = libusb_alloc_transfer(NP);
        libusb_fill_iso_transfer(S.rt[i], S.h, EP_IN, malloc(NP * IN_PKT), NP * IN_PKT, NP, in_cb, NULL, 1000);
        libusb_set_iso_packet_lengths(S.rt[i], IN_PKT);
    }
    S.ev_started = pthread_create(&S.ev, NULL, event_thread, NULL) == 0;
    int in_ok = 0;
    for (int i = 0; i < NR; i++) {
        pthread_mutex_lock(&S.mx);
        S.in_flight++;
        pthread_mutex_unlock(&S.mx);
        if (libusb_submit_transfer(S.rt[i])) {
            vti_log("usbaudio: IN submit failed");
            pthread_mutex_lock(&S.mx);
            S.in_flight--;
            pthread_mutex_unlock(&S.mx);
        } else
            in_ok++;
    }
    unsigned lead[NP];
    for (int done = 0; done < OUT_LEAD;) { // OUT lead-in with the nominal pattern (the first IN packets come 10 ms
        int n = OUT_LEAD - done < NP ? OUT_LEAD - done : NP; // later), then every IN packet is mirrored
        for (int k = 0; k < n; k++)
            lead[k] = vti_pkt_nominal(S.rate, S.pattern++);
        send_out(lead, NULL, n);
        done += n;
    }
    vti_log("usbaudio: streaming %.1f kHz on libusb (IN EP 0x82 %d ch, OUT EP 0x01 %d ch, frame pattern mirrored)",
            S.rate / 1000.0, IN_CH, OUT_CH);
    return in_ok ? 0 : -1;
}

// cancel every transfer, wait until all are back (the event thread runs until then), free them
static void stream_stop(void)
{
    S.running = 0;
    if (S.ev_started) { // also after an unplug (in_cb cleared `running`): callbacks may still be pending
        for (int i = 0; i < NR; i++)
            if (S.rt[i])
                libusb_cancel_transfer(S.rt[i]);
        pthread_mutex_lock(&S.mx);
        for (int i = 0; i < NW; i++)
            if (S.wt[i] && !S.wfree[i])
                libusb_cancel_transfer(S.wt[i]);
        pthread_mutex_unlock(&S.mx);
        pthread_join(S.ev, NULL);
        S.ev_started = 0;
    }
    for (int i = 0; i < NR; i++)
        if (S.rt[i]) {
            free(S.rt[i]->buffer);
            libusb_free_transfer(S.rt[i]);
            S.rt[i] = NULL;
        }
    for (int i = 0; i < NW; i++)
        if (S.wt[i]) {
            free(S.wt[i]->buffer);
            libusb_free_transfer(S.wt[i]);
            S.wt[i] = NULL;
        }
}

// the operations of a switch (ratesw.h)
static void op_stop(void *u)
{
    (void)u;
    stream_stop();
}
static int op_alt(void *u, int iface, int alt)
{
    (void)u;
    int r = libusb_set_interface_alt_setting(S.h, iface, alt);
    if (r)
        vti_log("usbaudio: alternate setting %d of interface %d failed: %s", alt, iface, libusb_error_name(r));
    return r ? -1 : 0;
}
static void op_sleep(void *u, int ms)
{
    (void)u;
    struct timespec ts = {ms / 1000, (long)(ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}
static int op_set_cur(void *u, uint8_t ep, uint32_t rate)
{
    (void)u;
    return set_rate(ep, rate);
}
static void (*g_before_start)(uint32_t rate); // the broker's shared ring learns the rate before frames at it arrive

static int op_start(void *u, uint32_t rate)
{
    (void)u;
    S.rate = rate;
    if (g_before_start)
        g_before_start(rate);
    return stream_start();
}
static const vti_rate_ops OPS = {NULL, op_stop, op_alt, op_sleep, op_set_cur, op_start};

int vla_start(libusb_context *ctx, libusb_device_handle *h, uint32_t rate, vla_in_fn in, vla_out_fn out, void *user)
{
    if (S.running)
        return 0;
    memset(&S, 0, sizeof S);
    pthread_mutex_init(&S.mx, NULL);
    S.ctx = ctx, S.h = h, S.in = in, S.out = out, S.user = user;
    S.rate = vti_rate_valid(rate);
    S.next_log = (uint64_t)S.rate * 60;
    for (int i = 0; i <= 2; i++) { // the audio function: taken from snd-usb-audio (auto-detach is on)
        int r = libusb_claim_interface(h, i);
        if (r) {
            vti_log("usbaudio: claim interface %d: %s", i, libusb_error_name(r));
            for (int k = 0; k < i; k++)
                libusb_release_interface(h, k);
            return -1;
        }
    }
    for (int i = 1; i <= 2; i++) { // the streaming interfaces: the host controller reserves their bandwidth here
        int r = libusb_set_interface_alt_setting(h, i, 1);
        if (r) {
            // behind a USB hub the kernel can refuse it ("Not enough bandwidth for altsetting 1", issue #9, E285)
            vti_log("usbaudio: alternate setting 1 of interface %d failed: %s - if the kernel log (journalctl -k) says "
                    "'Not enough bandwidth', connect the Virus directly to the computer, not through a USB hub",
                    i, libusb_error_name(r));
            vla_stop();
            return -1;
        }
    }
    vti_rate_set_cur(&OPS, S.rate); // EP 0x82, 0x01, 0x82, 0x01 like the driver (E323)
    if (stream_start()) { // no IN transfer queued: give everything back, the caller sees an error
        vla_stop();
        return -1;
    }
    return 0;
}

void vla_stop(void)
{
    if (!S.h)
        return;
    stream_stop();
    libusb_set_interface_alt_setting(S.h, 1, 0);
    libusb_set_interface_alt_setting(S.h, 2, 0);
    for (int i = 0; i <= 2; i++)
        libusb_release_interface(S.h, i);
    vti_log("usbaudio: stopped after %llu frames (errors %llu, stalled %llu ms)", (unsigned long long)S.frames,
            (unsigned long long)S.errs, (unsigned long long)S.gaps);
    S.h = NULL;
}

// from the device thread with the device lock held; 0 = now at `rate`, -1 = refused (back at the old rate), -2 = the
// stream is stopped (alternate setting or restart failed)
int vla_switch_rate(uint32_t rate, void (*before_start)(uint32_t rate))
{
    if (!S.h || !S.running)
        return -2;
    uint32_t from = S.rate, to = vti_rate_valid(rate);
    vti_log("usbaudio: switching %.1f -> %.1f kHz", from / 1000.0, to / 1000.0);
    g_before_start = before_start;
    int r = vti_rate_switch(&OPS, from, to);
    g_before_start = NULL;
    if (r)
        vti_log("usbaudio: rate switch to %.1f kHz %s", to / 1000.0,
                r == -1 ? "refused by the Virus - back at the old rate" : "failed - the stream is stopped");
    return r;
}

uint32_t vla_rate(void) { return S.rate; }

int vla_running(void) { return S.running; }

int vla_rate_lost(void)
{
    if (!S.running || !S.rate_lost)
        return 0;
    S.rate_lost = 0;
    return 1;
}

int vla_set_rate_again(void) // from the device thread, while streaming (libusb's synchronous API is thread-safe)
{
    if (!S.running)
        return -1;
    int a = set_rate(EP_OUT, S.rate), b = set_rate(EP_IN, S.rate);
    return a || b ? -1 : 0;
}
