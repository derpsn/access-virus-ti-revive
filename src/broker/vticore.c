// vticore.c - libusb backend of vticore.h (Linux host tests; packing helpers in vticore_midi.c)
#include "vticore.h"
#include <libusb.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "writeretry.h" // all platforms: vti_write (Linux and macOS)
#include "vtireaderr.h" // when a reader clears a stall, waits, or gives up (#37)
#include "linkrule.h"   // link at open, unlink at close like the original driver (E524)
#include "rxqueue.h"   // 8 reads pending per MIDI endpoint, the callback only queues (E521)
#include <errno.h>
#include <sys/time.h>
#include <unistd.h>     // usleep

#ifdef __linux__
#include "resetguard.h"
#include "giveback.h"
#include <stdio.h>
#include <time.h>
#define RESET_GAP_S 5 // E136, see resetguard.h
static int gb_claim(void *h, int i) { return libusb_claim_interface((libusb_device_handle *)h, i); }
static int gb_release(void *h, int i) { return libusb_release_interface((libusb_device_handle *)h, i); }
static int gb_reset(void *h) { return libusb_reset_device((libusb_device_handle *)h); }
static int reset_allowed(void)
{
    char path[64];
    snprintf(path, sizeof path, "/tmp/tirevive-usbreset-%u", (unsigned)getuid());
    return vti_reset_guard(path, time(NULL), RESET_GAP_S);
}
#endif

#ifdef __APPLE__
#include "resetguard.h"
#include <time.h>
#include <unistd.h>
#define REENUM_GAP_S 60 // E136, E312: USB resets close together have hung the Virus until a power cycle
#endif

static const uint8_t HANDSHAKE[4] = {0x4e, 0x73, 0x52, 0x01}; // spec §3, sent raw on EP 0x05

// One MIDI IN endpoint: RX_PENDING transfers always submitted, like the original's Mac driver (E521 (1)), and a queue
// to the reader thread (rxqueue.h). Invariant under mu: queued + inflight <= RX_QCAP, so a completion always fits.
#define RX_PENDING 8
#define RX_QCAP 512
typedef struct rxep {
    struct vti *v;
    uint8_t ep;
    struct libusb_transfer *t[RX_PENDING];
    uint8_t buf[RX_PENDING][VTI_RX_MAX];
    uint8_t busy[RX_PENDING]; // submitted, its callback not run yet
    int inflight;
    unsigned parked;          // not resubmitted for lack of room: the reader resubmits them
    int quiet;                // the reader clears a halt: transfers cancelled, parked until the clear (S142)
    vti_rx_rec recs[RX_QCAP];
    vti_rxq q;
    pthread_mutex_t mu;
    pthread_cond_t cv;
} rxep;

struct vti {
    libusb_context *ctx;
    libusb_device_handle *h;
    vti_rx_cb cb;
    void *user;
    volatile int stop, dead;
    pthread_t rd[2];
    uint8_t rd_ep[2];
    rxep rx[2];
    pthread_t ev;           // handles libusb events for the transfers (sync calls elsewhere handle them too)
    volatile int ev_stop;
    int started;            // rx_start ran: rx_stop on close
    int linked;             // the last link message sent (E524)
    int leak;               // transfers still out after the close: keep this struct (rx_stop)
    volatile int closing;   // rx_stop runs: transfers cancelled, nothing resubmitted, readers still draining
    uint8_t bulk[16];       // by endpoint number: 1 = bulk (Virus TI Snow), 0 = interrupt (TI/TI2 Desktop, E021) - E387
    unsigned long idle;     // FF FE 00 00 packets dropped on a bulk EP 0x85 (E387)
};

// The Snow has bulk endpoints 05/85/06/86 where the TI2 Desktop has interrupt ones (E021, E387). Linux usbfs rejects
// an interrupt URB on a bulk endpoint (EINVAL, LIBUSB_ERROR_IO on every transfer), so the call follows the descriptor.
static void find_bulk_endpoints(vti *v)
{
    struct libusb_config_descriptor *cfg;
    char list[64] = "";
    if (libusb_get_active_config_descriptor(libusb_get_device(v->h), &cfg))
        return;
    for (int i = 0; i < cfg->bNumInterfaces; i++)
        for (int a = 0; a < cfg->interface[i].num_altsetting; a++) {
            const struct libusb_interface_descriptor *d = &cfg->interface[i].altsetting[a];
            for (int e = 0; e < d->bNumEndpoints; e++) {
                uint8_t ep = d->endpoint[e].bEndpointAddress;
                if ((d->endpoint[e].bmAttributes & 3) == LIBUSB_TRANSFER_TYPE_BULK && strlen(list) < sizeof list - 4) {
                    v->bulk[ep & 15] = 1;
                    snprintf(list + strlen(list), sizeof list - strlen(list), " %02x", ep);
                }
            }
        }
    libusb_free_config_descriptor(cfg);
    if (list[0])
        vti_log("vti: bulk endpoints%s (Virus TI Snow), using bulk transfers there\n", list);
}

static int xfer(vti *v, uint8_t ep, uint8_t *buf, int len, int *got, unsigned timeout)
{
    return v->bulk[ep & 15] ? libusb_bulk_transfer(v->h, ep, buf, len, got, timeout)
                            : libusb_interrupt_transfer(v->h, ep, buf, len, got, timeout);
}

// The Snow's bulk EP 0x85 answers every read, ~2000 times a second, and the extra packets are all FF FE 00 00
// (cable 15, CIN 0xF; nothing in the plug-in reads them - E387). They are dropped here, only on a bulk EP 0x85
// and only that exact 4-byte packet; the TI2's interrupt endpoints never get here.
static int drop_idle(vti *v, uint8_t *b, int n)
{
    static const uint8_t idle[4] = {0xff, 0xfe, 0x00, 0x00};
    int w = 0, r = 0;
    for (; r + 4 <= n; r += 4)
        if (memcmp(b + r, idle, 4)) {
            memmove(b + w, b + r, 4);
            w += 4;
        } else
            v->idle++;
    for (; r < n; r++)
        b[w++] = b[r];
    return w;
}

// under e->mu
static int rx_submit(rxep *e, int slot)
{
    int r = libusb_submit_transfer(e->t[slot]);
    if (r == 0) {
        e->busy[slot] = 1;
        e->inflight++;
    }
    return r;
}

// Any thread that handles libusb events runs this (the event thread, a sync write, the Linux audio thread): queue and
// resubmit only, never block. An error goes to the reader as a record and its transfer waits for the reader.
static void LIBUSB_CALL rx_done(struct libusb_transfer *t)
{
    rxep *e = t->user_data;
    const int slot = (int)((t->buffer - e->buf[0]) / VTI_RX_MAX);
    pthread_mutex_lock(&e->mu);
    e->busy[slot] = 0;
    e->inflight--;
    const int err = vti_xfer_err(t->status);
    switch (vti_rx_done_action(e->v->closing, e->quiet, err)) {
    case VTI_RX_ERROR:
        vti_rxq_push(&e->q, slot, err, NULL, 0); // fits: queued + inflight <= RX_QCAP
        break;
    case VTI_RX_QUEUE: // the close: hand it on, read no more
        if (t->actual_length > 0)
            vti_rxq_push(&e->q, slot, 0, t->buffer, t->actual_length);
        break;
    case VTI_RX_QUEUE_RESUBMIT:
        if (t->actual_length > 0)
            vti_rxq_push(&e->q, slot, 0, t->buffer, t->actual_length);
        if (!vti_rxq_may_resubmit(&e->q, (unsigned)e->inflight + 1))
            vti_park(&e->parked, slot);
        else if (rx_submit(e, slot))
            vti_rxq_push(&e->q, slot, -4, NULL, 0); // a failed resubmit: the device is going, the reader decides
        break;
    case VTI_RX_QUEUE_PARK:
        if (t->actual_length > 0)
            vti_rxq_push(&e->q, slot, 0, t->buffer, t->actual_length);
        vti_park(&e->parked, slot);
        break;
    case VTI_RX_PARK:
        vti_park(&e->parked, slot);
        break;
    default:
        break;
    }
    pthread_cond_broadcast(&e->cv);
    pthread_mutex_unlock(&e->mu);
}

static void *events(void *p)
{
    vti *v = p;
    while (!v->ev_stop) {
        struct timeval tv = {0, 100000};
        libusb_handle_events_timeout_completed(v->ctx, &tv, NULL);
    }
    return NULL;
}

// 0, or -1 when the queue stayed empty for 200 ms (the reader then checks stop)
static int rx_take(rxep *e, vti_rx_rec *rec)
{
    pthread_mutex_lock(&e->mu);
    if (!e->q.count && !e->v->stop) {
        struct timeval now;
        gettimeofday(&now, NULL);
        struct timespec until = {now.tv_sec, (now.tv_usec + 200000) * 1000L};
        if (until.tv_nsec >= 1000000000L) {
            until.tv_sec++;
            until.tv_nsec -= 1000000000L;
        }
        pthread_cond_timedwait(&e->cv, &e->mu, &until);
    }
    int r = vti_rxq_pop(&e->q, rec);
    int slot; // data again (room, or the endpoint works after a stall): the parked transfers go out
    while (r == 0 && vti_rx_unpark_after(rec->err) && !e->v->closing && e->parked && vti_rxq_may_resubmit(&e->q, (unsigned)e->inflight + 1) &&
           (slot = vti_unpark(&e->parked)) >= 0)
        if (rx_submit(e, slot))
            vti_rxq_push(&e->q, slot, -4, NULL, 0);
    pthread_mutex_unlock(&e->mu);
    return r;
}

// a halt is cleared on a quiet endpoint: the kernel warns about CLEAR_HALT with transfers queued (S142, 125 lines).
// Cancel the endpoint's transfers, wait for them (data still handed on), clear, then the reader resubmits them.
// Real TI2 firmware refuses the clear anyway (sim S141); the reset at the give-up ends a halt.
static void rx_clear_halt(rxep *e)
{
    pthread_mutex_lock(&e->mu);
    e->quiet = 1;
    for (int s = 0; s < RX_PENDING; s++)
        if (e->busy[s])
            libusb_cancel_transfer(e->t[s]);
    struct timeval now;
    gettimeofday(&now, NULL);
    long ns = (now.tv_usec + 500000) * 1000L; // 0.5 s at most
    const struct timespec until = {now.tv_sec + ns / 1000000000L, ns % 1000000000L};
    while (e->inflight > 0 && pthread_cond_timedwait(&e->cv, &e->mu, &until) != ETIMEDOUT)
        ;
    pthread_mutex_unlock(&e->mu);
    libusb_clear_halt(e->v->h, e->ep);
    pthread_mutex_lock(&e->mu);
    e->quiet = 0;
    pthread_mutex_unlock(&e->mu);
}

static void *reader(void *p)
{
    rxep *e = p;
    vti *v = e->v;
    const uint8_t ep = e->ep;
    int fails = 0, last = 0; // errors in a row and the last one (vtireaderr.h)
    int gave_up = 0;
    vti_rx_rec rec;
    for (;;) { // after stop, until the queue is empty: the close hands on what still came (S139)
        if (rx_take(e, &rec)) {
            if (v->stop)
                break;
            continue;
        }
        if (gave_up && !vti_rx_hand_on_after_giveup(rec.err))
            continue;
        if (rec.err && !gave_up) {
            const int r = rec.err;
            fails++;
            last = r;
            if (vti_read_error_logged(fails))
                vti_log("vti: read ep %02x: %s\n", ep, libusb_error_name(r));
            if (vti_libusb_read_dead(r, fails)) { // gone or broken: the broker closes it and opens the Virus again
                vti_log("vti: read ep %02x: %d errors in a row (last %s), giving up on the device\n", ep, fails,
                        libusb_error_name(r));
                v->dead = 1;
                gave_up = 1; // keep handing on what the other transfers bring until the close (S138)
                fails = 0;   // so the next data record writes no "then read again" line (S139)
                continue;
            }
            if (vti_libusb_stalled(r)) // the standard CLEAR_FEATURE(ENDPOINT_HALT), as WinUsb_ResetPipe on Windows
                rx_clear_halt(e);
            usleep(10000); // never a tight loop on errors (#37: ~25000 a second, 300 MB of log)
            pthread_mutex_lock(&e->mu); // one probe: the failed transfer goes out again only when none is out (S143)
            if (!v->closing) {
                if (!vti_rx_probe_now(e->inflight))
                    vti_park(&e->parked, rec.slot);
                else if (rx_submit(e, rec.slot))
                    vti_rxq_push(&e->q, rec.slot, -4, NULL, 0);
            }
            pthread_mutex_unlock(&e->mu);
            continue;
        }
        if (fails > 3)
            vti_log("vti: read ep %02x: %d errors in a row (last %s), then read again\n", ep, fails,
                    libusb_error_name(last));
        fails = 0;
        int got = rec.len;
        if (ep == VTI_EP_MI03_IN && v->bulk[ep & 15] && !(got = drop_idle(v, rec.data, got)))
            continue;
        v->cb(v->user, ep, rec.data, got);
    }
    return NULL;
}

// RX_PENDING transfers on each MIDI IN endpoint and their reader; 0 or a libusb error
static int rx_start(vti *v)
{
    if (pthread_create(&v->ev, NULL, events, v))
        return LIBUSB_ERROR_OTHER;
    for (int i = 0; i < 2; i++) {
        rxep *e = &v->rx[i];
        e->v = v;
        e->ep = v->rd_ep[i];
        pthread_mutex_init(&e->mu, NULL);
        pthread_cond_init(&e->cv, NULL);
        vti_rxq_init(&e->q, e->recs, RX_QCAP);
        pthread_mutex_lock(&e->mu);
        for (int s = 0; s < RX_PENDING; s++) {
            e->t[s] = libusb_alloc_transfer(0);
            if (!e->t[s])
                continue;
            if (v->bulk[e->ep & 15])
                libusb_fill_bulk_transfer(e->t[s], v->h, e->ep, e->buf[s], VTI_RX_MAX, rx_done, e, 0);
            else
                libusb_fill_interrupt_transfer(e->t[s], v->h, e->ep, e->buf[s], VTI_RX_MAX, rx_done, e, 0);
            int r = rx_submit(e, s);
            if (r)
                vti_log("vti: read ep %02x: transfer %d not submitted (%s)\n", e->ep, s, libusb_error_name(r));
        }
        pthread_mutex_unlock(&e->mu);
        pthread_create(&v->rd[i], NULL, reader, e);
    }
    return 0;
}

// the close: cancel every transfer while the readers still hand on what completes, wait for all of them, then stop
// the readers (they empty their queue first) and the event thread (S139)
static void rx_stop(vti *v)
{
    v->closing = 1;
    for (int i = 0; i < 2; i++) {
        rxep *e = &v->rx[i];
        pthread_mutex_lock(&e->mu);
        for (int s = 0; s < RX_PENDING; s++)
            if (e->busy[s])
                libusb_cancel_transfer(e->t[s]);
        // the event thread runs their callbacks: wait for all of them, 2 s at most by the clock (every callback
        // wakes this wait, so counting wake-ups ended it early, S138)
        struct timeval now;
        gettimeofday(&now, NULL);
        const struct timespec until = {now.tv_sec + 2, now.tv_usec * 1000L};
        while (e->inflight > 0 && pthread_cond_timedwait(&e->cv, &e->mu, &until) != ETIMEDOUT)
            ;
        if (e->inflight > 0)
            vti_log("vti: read ep %02x: %d transfers still pending at close\n", e->ep, e->inflight);
        const int left = e->inflight;
        pthread_mutex_unlock(&e->mu);
        if (!left)
            for (int s = 0; s < RX_PENDING; s++)
                libusb_free_transfer(e->t[s]); // NULL is fine
        else
            v->leak = 1; // a late callback may still use e: vti_close keeps v (never seen with the clock wait)
    }
    v->stop = 1;
    for (int i = 0; i < 2; i++)
        pthread_join(v->rd[i], NULL);
    v->ev_stop = 1;
    pthread_join(v->ev, NULL);
    if (!v->leak)
        for (int i = 0; i < 2; i++) {
            pthread_mutex_destroy(&v->rx[i].mu);
            pthread_cond_destroy(&v->rx[i].cv);
        }
}

vti *vti_open(vti_rx_cb cb, void *user)
{
    vti *v = calloc(1, sizeof *v);
    v->cb = cb;
    v->user = user;
    int r = libusb_init(&v->ctx);
    if (r) {
        vti_log("vti: libusb_init: %s\n", libusb_error_name(r));
        free(v);
        return NULL;
    }
    v->h = libusb_open_device_with_vid_pid(v->ctx, VTI_VID, VTI_PID);
    if (!v->h) {
        vti_log("vti: Virus TI %04x:%04x not found or no access\n", VTI_VID, VTI_PID);
        goto fail;
    }
    libusb_set_auto_detach_kernel_driver(v->h, 1);
    find_bulk_endpoints(v);
    for (int i = 3; i <= 4; i++)
        if ((r = libusb_claim_interface(v->h, i))) {
            vti_log("vti: claim interface %d: %s\n", i, libusb_error_name(r));
#ifdef __APPLE__
            static int hinted;
            if (r == LIBUSB_ERROR_ACCESS && !hinted++) // E134
                vti_log("vti: another driver holds the Virus - e.g. the old Access drivers (Virus TI MIDI Driver.plugin in"
                        " /Library/Audio/MIDI Drivers, de.access-music.virus_ti.* in /Library/Audio/Plug-Ins/HAL; \"Install"
                        " Access Virus TI software.command\" disables them) or another Virus USB driver such as the one of"
                        " Aura Plugins' Virus Editor: disable it, then restart the Mac\n");
#endif
            goto fail;
        }
    // Readers first: if nobody drains EP 0x85/0x86, the device stops accepting OUT data (E042).
    v->rd_ep[0] = VTI_EP_MI03_IN;
    v->rd_ep[1] = VTI_EP_MI04_IN;
    if ((r = rx_start(v))) {
        vti_log("vti: event thread not started (%s)\n", libusb_error_name(r));
        goto fail;
    }
    v->started = 1;
    if (vti_write(v, VTI_EP_MI03_OUT, HANDSHAKE, sizeof HANDSHAKE)) {
        vti_close(v);
        return NULL;
    }
    v->linked = 1;
    return v;
fail:
    if (v->h)
        libusb_close(v->h);
    libusb_exit(v->ctx);
    free(v);
    return NULL;
}

void vti_close(vti *v)
{
    if (!v)
        return;
    if (v->linked && !v->dead) // the original's driver unlinks when it lets the Virus go (E524); readers still drain
        vti_link(v, 0);
    if (v->started)
        rx_stop(v); // sets stop after the transfers are back
    v->stop = 1;
    if (v->idle)
        vti_log("vti: %lu idle packets (FF FE 00 00) dropped on bulk EP 0x85\n", v->idle);
#ifdef __linux__
    // snd-usb-audio shuts the whole card down when interfaces 3/4 are taken away, and the dead card lives on while
    // any of its interfaces is still bound ("USB device is in the shutdown state", E113). So: take all five (the
    // card is freed), reset the port, and give them back so snd-usb-audio probes a fresh card - the Virus is a
    // normal Linux sound card again after the broker. The reset comes before the release (giveback.h, E312).
    const vti_giveback_ops ops = {gb_claim, gb_release, gb_reset};
    if (!vti_giveback(&ops, v->h, reset_allowed()))
        vti_log("vti: give-back reset skipped (the last one was less than %d s ago, E136)\n", RESET_GAP_S);
#else
    for (int i = 3; i <= 4; i++)
        libusb_release_interface(v->h, i);
#endif
    libusb_close(v->h);
    libusb_exit(v->ctx);
    if (!v->leak) // else a transfer's late callback could still reach its rxep (rx_stop)
        free(v);
}

struct write_ctx {
    vti *v;
    uint8_t ep;
    int err; // last libusb error that was not a timeout
};

static int write_packet(void *ctx, const uint8_t *data, int n, int *done)
{
    struct write_ctx *c = ctx;
    int r = xfer(c->v, c->ep, (uint8_t *)data, n, done, 1000);
    if (r == LIBUSB_ERROR_TIMEOUT)
        return VTI_XFER_TIMEOUT;
    c->err = r;
    return r;
}

static vti_tap_cb g_tap;
void vti_set_tap(vti_tap_cb cb) { g_tap = cb; }

// a control request as the tap's record: setup[8], i32 result, then the data that went or came
static void tap_control(uint8_t rt, uint8_t req, uint16_t val, uint16_t idx, uint16_t len, int32_t r, const uint8_t *data)
{
    if (!g_tap)
        return;
    uint8_t rec[12] = {rt, req, (uint8_t)val, (uint8_t)(val >> 8), (uint8_t)idx, (uint8_t)(idx >> 8), (uint8_t)len, (uint8_t)(len >> 8)};
    memcpy(rec + 8, &r, 4);
    g_tap(2, 0, rec, 12, data, (rt & 0x80) ? (r > 0 ? r : 0) : len);
}

int vti_write(vti *v, uint8_t ep, const uint8_t *data, int len)
{
    if (g_tap) // before the transfer: the order on the wire
        g_tap(0, ep, data, len, NULL, 0);
    int max = ep == VTI_EP_MI03_OUT ? 8 : 32; // wMaxPacketSize (lsusb -v)
    struct write_ctx c = {v, ep, 0};
    int timeouts = 0;
    // a timed-out packet was NAKed, never taken (the link is lossless, E239): try it again, up to 5 s in a row
    int r = vti_write_chunks(&c, write_packet, data, len, max, 5, &timeouts);
    if (timeouts)
        vti_log("vti: write ep %02x: %d timeout(s), %s\n", ep, timeouts, r ? "gave up" : "then accepted");
    if (r && c.err)
        vti_log("vti: write ep %02x: %s\n", ep, libusb_error_name(c.err));
    if (r && vti_libusb_stalled(c.err)) // a stalled OUT endpoint refuses every later write until its halt is cleared
        libusb_clear_halt(v->h, ep);
    return r;
}

int vti_control(vti *v, uint8_t bmRequestType, uint8_t bRequest, uint16_t wValue, uint16_t wIndex, uint8_t *data,
                uint16_t len)
{
    int r = libusb_control_transfer(v->h, bmRequestType, bRequest, wValue, wIndex, data, len, 1000);
    if (r < 0)
        vti_log("vti: control %02x/%02x wIndex %04x: %s\n", bmRequestType, bRequest, wIndex,
                libusb_error_name(r));
    tap_control(bmRequestType, bRequest, wValue, wIndex, len, r < 0 ? -1 : r, data);
    return r < 0 ? -1 : r;
}

int vti_audio(vti *v, int on, const char *event_name) // broker audio exists only behind vtibroker (vticore_pipe.c)
{
    (void)v;
    (void)on;
    (void)event_name;
    return -1;
}

int vti_link(vti *v, int on)
{
    if (!v || v->dead)
        return -1;
    uint8_t m[4];
    vti_link_msg(m, on);
    int r = vti_write(v, VTI_EP_MI03_OUT, m, sizeof m);
    if (!r)
        v->linked = on;
    vti_log("vti: link %s (4E 73 52 %02X) -> %d\n", on ? "on" : "off", m[3], r);
    return r;
}

int vti_linked(vti *v) { return v && v->linked; }

int vti_is_dead(vti *v)
{
    return v->dead != 0;
}

void vti_libusb(vti *v, struct libusb_context **ctx, struct libusb_device_handle **h) // Linux broker audio
{
    *ctx = v->ctx;
    *h = v->h;
}

#ifdef __APPLE__
// E472: after a Mac sleep the Virus' Core Audio stream can stay gapped until macOS re-enumerates the device (a replug
// cured it every time). libusb 1.0.30 makes libusb_reset_device a USBDeviceReEnumerate on macOS when nothing is
// captured (darwin_usb.c darwin_reset_device), and restores our claims afterwards. At most once a minute per user.
int vti_reenumerate(vti *v)
{
    char path[64];
    snprintf(path, sizeof path, "/tmp/tirevive-reenum-%u", (unsigned)getuid());
    if (!v || !vti_reset_guard(path, time(NULL), REENUM_GAP_S))
        return -2;
    return libusb_reset_device(v->h);
}
#else
int vti_reenumerate(vti *v) // Linux and Windows: not needed (E472 is the Mac's Core Audio)
{
    (void)v;
    return -1;
}
#endif

