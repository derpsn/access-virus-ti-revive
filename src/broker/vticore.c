// vticore.c - libusb backend of vticore.h (Linux host tests; packing helpers in vticore_midi.c)
#include "vticore.h"
#include <libusb.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "writeretry.h" // all platforms: vti_write (Linux and macOS)

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

static const uint8_t HANDSHAKE[4] = {0x4e, 0x73, 0x52, 0x01}; // spec §3, sent raw on EP 0x05

struct vti {
    libusb_context *ctx;
    libusb_device_handle *h;
    vti_rx_cb cb;
    void *user;
    volatile int stop, dead;
    pthread_t rd[2];
    uint8_t rd_ep[2];
};

typedef struct {
    vti *v;
    int i;
} rd_arg;

static void *reader(void *p)
{
    rd_arg a = *(rd_arg *)p;
    free(p);
    vti *v = a.v;
    uint8_t ep = v->rd_ep[a.i], buf[64];
    while (!v->stop) {
        int got = 0, r = libusb_interrupt_transfer(v->h, ep, buf, sizeof buf, &got, 200);
        if (r == 0 && got > 0)
            v->cb(v->user, ep, buf, got);
        else if (r != LIBUSB_ERROR_TIMEOUT && r != 0) {
            vti_log("vti: read ep %02x: %s\n", ep, libusb_error_name(r));
            if (r == LIBUSB_ERROR_NO_DEVICE) {
                v->dead = 1;
                break;
            }
        }
    }
    return NULL;
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
    for (int i = 0; i < 2; i++) {
        rd_arg *a = malloc(sizeof *a);
        a->v = v;
        a->i = i;
        pthread_create(&v->rd[i], NULL, reader, a);
    }
    if (vti_write(v, VTI_EP_MI03_OUT, HANDSHAKE, sizeof HANDSHAKE)) {
        vti_close(v);
        return NULL;
    }
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
    v->stop = 1;
    for (int i = 0; i < 2; i++)
        pthread_join(v->rd[i], NULL);
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
    int r = libusb_interrupt_transfer(c->v->h, c->ep, (uint8_t *)data, n, done, 1000);
    if (r == LIBUSB_ERROR_TIMEOUT)
        return VTI_XFER_TIMEOUT;
    c->err = r;
    return r;
}

int vti_write(vti *v, uint8_t ep, const uint8_t *data, int len)
{
    int max = ep == VTI_EP_MI03_OUT ? 8 : 32; // wMaxPacketSize (lsusb -v)
    struct write_ctx c = {v, ep, 0};
    int timeouts = 0;
    // a timed-out packet was NAKed, never taken (the link is lossless, E239): try it again, up to 5 s in a row
    int r = vti_write_chunks(&c, write_packet, data, len, max, 5, &timeouts);
    if (timeouts)
        vti_log("vti: write ep %02x: %d timeout(s), %s\n", ep, timeouts, r ? "gave up" : "then accepted");
    if (r && c.err)
        vti_log("vti: write ep %02x: %s\n", ep, libusb_error_name(c.err));
    return r;
}

int vti_control(vti *v, uint8_t bmRequestType, uint8_t bRequest, uint16_t wValue, uint16_t wIndex, uint8_t *data,
                uint16_t len)
{
    int r = libusb_control_transfer(v->h, bmRequestType, bRequest, wValue, wIndex, data, len, 1000);
    if (r < 0)
        vti_log("vti: control %02x/%02x wIndex %04x: %s\n", bmRequestType, bRequest, wIndex,
                libusb_error_name(r));
    return r < 0 ? -1 : r;
}

int vti_audio(vti *v, int on, const char *event_name) // broker audio exists only behind vtibroker (vticore_pipe.c)
{
    (void)v;
    (void)on;
    (void)event_name;
    return -1;
}

int vti_is_dead(vti *v)
{
    return v->dead != 0;
}

void vti_libusb(vti *v, struct libusb_context **ctx, struct libusb_device_handle **h) // Linux broker audio
{
    *ctx = v->ctx;
    *h = v->h;
}
