// vtiaudioprobe.c - feasibility test (2026-11-13, #37): can a program take the Virus' USB audio away from the system's
// driver and stream it itself with libusb, as the Linux broker does? On macOS that would avoid Apple's USB audio
// driver and Core Audio for the Virus (Catalina slipped the stream in 9 of 22 starts, E510). On Linux it is the same
// path as the broker (cross-check).
//
// It opens the Virus, lets libusb detach the system driver from the audio interfaces 0..2 (on macOS libusb does that
// by capturing the whole device, which needs root), streams with the broker's own code (vtiusbaudio_libusb.c: alt
// setting 1 + SET_CUR, the messages the Linux broker sends at every start), prints frames, rate and peaks once a second
// and writes the 6 input channels to a WAV. With -note it also claims the MIDI interfaces 3/4, sends the known
// handshake (spec §3) and plays a C4 every second on EP 0x06, cable 2 (E056) - nothing else goes to the Virus. At the
// end it stops the stream (alt 0), releases the interfaces and gives the device back to the system driver.
// With -usb (implies -note) part 1's Output Select is set to 10 (USB 1) first, the value the plug-in sends at every
// start (72 00 29 0A, E343; an edit-buffer parameter, nothing stored): without it the notes play on the analog outputs
// only and USB 1 stays silent (first Mac run, 2026-11-13).
//
// usage: vtiaudioprobe [-note] [-usb] [-rate 44100|48000] [seconds] [out.wav]      (macOS: sudo ./vtiaudioprobe ...)
#include "vticore.h"
#include "vtiusbaudio_libusb.h"
#include <libusb.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static const uint8_t HANDSHAKE[4] = {0x4e, 0x73, 0x52, 0x01}; // spec §3, raw on EP 0x05 (as vticore.c)

static pthread_mutex_t g_mx = PTHREAD_MUTEX_INITIALIZER;
static FILE *g_wav;
static unsigned long long g_frames, g_wav_frames;
static int g_peak[6];
static volatile int g_stop;
static libusb_device_handle *g_h;
static int g_bulk[16]; // the Snow's MIDI endpoints are bulk, the TI's interrupt

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

static void on_in(const int16_t *pcm, uint32_t frames, void *user) // libusb event thread (vtiusbaudio_libusb.c)
{
    (void)user;
    pthread_mutex_lock(&g_mx);
    for (uint32_t i = 0; i < frames * 6; i++) {
        int v = abs(pcm[i]);
        if (v > g_peak[i % 6])
            g_peak[i % 6] = v;
    }
    if (g_wav)
        g_wav_frames += fwrite(pcm, 12, frames, g_wav);
    g_frames += frames;
    pthread_mutex_unlock(&g_mx);
}

static void on_out(int16_t *pcm, uint32_t frames, void *user) { (void)user, memset(pcm, 0, frames * 4); } // silence

static int xfer(uint8_t ep, uint8_t *b, int n, int *got, unsigned ms)
{
    return g_bulk[ep & 15] ? libusb_bulk_transfer(g_h, ep, b, n, got, ms) : libusb_interrupt_transfer(g_h, ep, b, n, got, ms);
}

static void *drain(void *arg) // nobody draining EP 0x85/0x86 makes the Virus stop taking OUT data (E042)
{
    uint8_t ep = (uint8_t)(uintptr_t)arg, b[64];
    while (!g_stop) {
        int got = 0, r = xfer(ep, b, sizeof b, &got, 200);
        if (r && r != LIBUSB_ERROR_TIMEOUT)
            usleep(10000);
    }
    return NULL;
}

static const uint8_t MODE3[11] = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x73, 0x00, 0x7A, 0x03, 0xF7}; // start (E391)
static const uint8_t MODE0[11] = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x73, 0x00, 0x7A, 0x00, 0xF7}; // close (closeseq.h)

static int send_sysex(const uint8_t msg[11]) // on cable 2 of EP 0x06, like the plug-in's SysEx (usbmidi.h)
{
    uint8_t pk[32];
    int n = vti_pack_sysex(2, msg, 11, pk, sizeof pk), got = 0;
    return n > 0 ? xfer(VTI_EP_MI04_OUT, pk, n, &got, 1000) : -1;
}

static void find_bulk(void)
{
    struct libusb_config_descriptor *c;
    if (libusb_get_active_config_descriptor(libusb_get_device(g_h), &c))
        return;
    for (int i = 0; i < c->bNumInterfaces; i++)
        for (int a = 0; a < c->interface[i].num_altsetting; a++)
            for (int e = 0; e < c->interface[i].altsetting[a].bNumEndpoints; e++) {
                const struct libusb_endpoint_descriptor *d = &c->interface[i].altsetting[a].endpoint[e];
                if ((d->bmAttributes & 3) == LIBUSB_TRANSFER_TYPE_BULK)
                    g_bulk[d->bEndpointAddress & 15] = 1;
            }
    libusb_free_config_descriptor(c);
}

static void wav_header(FILE *f, unsigned long long frames, int rate)
{
    uint32_t data = (uint32_t)(frames * 12), riff = 36 + data, fmt = 16, br = (uint32_t)rate * 12;
    uint16_t pcm = 1, ch = 6, align = 12, bits = 16;
    uint32_t sr = (uint32_t)rate;
    fseek(f, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, f), fwrite(&riff, 4, 1, f), fwrite("WAVEfmt ", 1, 8, f), fwrite(&fmt, 4, 1, f);
    fwrite(&pcm, 2, 1, f), fwrite(&ch, 2, 1, f), fwrite(&sr, 4, 1, f), fwrite(&br, 4, 1, f), fwrite(&align, 2, 1, f);
    fwrite(&bits, 2, 1, f), fwrite("data", 1, 4, f), fwrite(&data, 4, 1, f);
}

static void say(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    fflush(stdout);
}

static void log_line(const char *fmt, ...) // vtiusbaudio_libusb.c's lines come without a newline (the broker adds it)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    if (fmt[0] && fmt[strlen(fmt) - 1] != '\n')
        putchar('\n');
    fflush(stdout);
}

int main(int argc, char **argv)
{
    int note = 0, usb = 0, rate = 44100, secs = 10;
    const char *out = "vtiaudioprobe.wav";
    vti_log = log_line;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-note"))
            note = 1;
        else if (!strcmp(argv[i], "-usb"))
            note = usb = 1;
        else if (!strcmp(argv[i], "-rate") && i + 1 < argc)
            rate = atoi(argv[++i]);
        else if (atoi(argv[i]) > 0)
            secs = atoi(argv[i]);
        else
            out = argv[i];
    }
    if ((rate != 44100 && rate != 48000) || secs > 120) {
        fprintf(stderr, "vtiaudioprobe: -rate 44100 or 48000, at most 120 s\n");
        return 2;
    }
    libusb_context *ctx;
    if (libusb_init(&ctx))
        return 1;
    g_h = libusb_open_device_with_vid_pid(ctx, VTI_VID, VTI_PID);
    if (!g_h) {
        say("no Virus TI (133e:0815) found, or no access\n");
        return 1;
    }
    find_bulk();
    libusb_set_auto_detach_kernel_driver(g_h, 1);
    for (int i = 0; i <= 2; i++) // what holds the audio interfaces now (1 = a kernel driver, e.g. AppleUSBAudio)
        say("interface %d: kernel driver active = %d\n", i, libusb_kernel_driver_active(g_h, i));

    g_wav = fopen(out, "wb");
    if (g_wav)
        wav_header(g_wav, 0, rate);
    const double t0 = now();
    if (vla_start(ctx, g_h, (uint32_t)rate, on_in, on_out, NULL)) {
        say("FAILED: could not take and start the audio interfaces (see the usbaudio lines above; macOS needs root)\n");
        libusb_close(g_h);
        return 1;
    }
    say("streaming at %d Hz after %.2f s (the system driver gave the interfaces up)\n", rate, now() - t0);

    pthread_t th[2];
    int midi = 0;
    if (note) { // the plug-in's MIDI path: interfaces 3/4, readers first, then the handshake
        midi = libusb_claim_interface(g_h, 3) == 0 && libusb_claim_interface(g_h, 4) == 0;
        if (midi) {
            pthread_create(&th[0], NULL, drain, (void *)(uintptr_t)VTI_EP_MI03_IN);
            pthread_create(&th[1], NULL, drain, (void *)(uintptr_t)VTI_EP_MI04_IN);
            int got;
            uint8_t hs[4];
            memcpy(hs, HANDSHAKE, 4);
            midi = xfer(VTI_EP_MI03_OUT, hs, 4, &got, 1000) == 0;
        }
        say("MIDI (interfaces 3/4, handshake): %s\n", midi ? "ok" : "FAILED - no notes");
        if (midi && usb) {
            // Part Output Select only counts in the mode the plug-in and the original start in: Global Mode 3 (73 00 7A
            // 03, E391 from captures of the original); at their close they send Mode 0 (Single, closeseq.h), so after a
            // plug-in session the Virus is in Single mode and the part's USB output does nothing (second Mac run)
            say("Global Mode 3 (as at the plug-in's start): %s\n", send_sysex(MODE3) == 0 ? "sent" : "FAILED");
            usleep(300000);
            // part 1 to USB 1 (Output Select 10), as the plug-in at every start
            const uint8_t os[11] = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x72, 0x00, 0x29, 0x0A, 0xF7};
            say("part 1 Output Select -> USB 1 (10): %s\n", send_sysex(os) == 0 ? "sent" : "FAILED");
            usleep(200000);
        }
    }

    int sec_on = -1;
    double last = now(), start = last;
    unsigned long long last_frames = 0;
    while (now() - start < secs) {
        usleep(20000);
        double t = now();
        if (midi) { // note on in the first half of every second, off in the second (as vtiaudiotest -note)
            int want = t - start - (int)(t - start) < 0.5;
            if (want != sec_on) {
                uint8_t m[4] = {(uint8_t)(0x20 | (want ? 9 : 8)), want ? 0x90 : 0x80, 60, want ? 100 : 0};
                int got;
                xfer(VTI_EP_MI04_OUT, m, 4, &got, 200);
                sec_on = want;
            }
        }
        if (t - last >= 1) {
            pthread_mutex_lock(&g_mx);
            unsigned long long f = g_frames;
            say("%5.1f s: %llu frames (%.0f Hz), peaks", t - start, f, (f - last_frames) / (t - last));
            for (int c = 0; c < 6; c++)
                say(" %d", g_peak[c]), g_peak[c] = 0;
            say(", stream running %d\n", vla_running());
            pthread_mutex_unlock(&g_mx);
            last_frames = f, last = t;
        }
    }
    if (midi) { // all notes off on cable 2 before leaving
        uint8_t m[4] = {0x2b, 0xb0, 0x7b, 0x00};
        int got;
        xfer(VTI_EP_MI04_OUT, m, 4, &got, 200);
        if (usb) // back to Single mode, as the plug-in's and the original's close sequence
            say("Global Mode 0 (Single, as at the plug-in's close): %s\n", send_sysex(MODE0) == 0 ? "sent" : "FAILED");
    }
    vla_stop();
    g_stop = 1;
    if (midi) {
        pthread_join(th[0], NULL), pthread_join(th[1], NULL);
        libusb_release_interface(g_h, 3), libusb_release_interface(g_h, 4);
    }
    pthread_mutex_lock(&g_mx);
    if (g_wav) {
        wav_header(g_wav, g_wav_frames, rate);
        fclose(g_wav);
        g_wav = NULL;
    }
    say("done: %llu frames in %.1f s (%.1f Hz); WAV %s\n", g_frames, now() - start, g_frames / (now() - start), out);
    pthread_mutex_unlock(&g_mx);
    for (int i = 0; i <= 2; i++) // auto-detach gives them back on release; say what holds them now
        say("interface %d: kernel driver active = %d\n", i, libusb_kernel_driver_active(g_h, i));
    libusb_close(g_h);
    libusb_exit(ctx);
    return 0;
}
