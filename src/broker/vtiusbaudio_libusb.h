// vtiusbaudio_libusb.h - Virus TI USB audio on libusb (Linux broker), see vtiusbaudio_libusb.c
#pragma once
#include <stdint.h>

#define VLA_RATE 44100 // the rate of a client that asks for none (protocol < 5)

struct libusb_context;
struct libusb_device_handle;
typedef void (*vla_in_fn)(const int16_t *pcm, uint32_t frames, void *user); // 6 ch interleaved, from the synth
typedef void (*vla_out_fn)(int16_t *pcm, uint32_t frames, void *user);      // 2 ch interleaved, to the synth

// Takes interfaces 0..2 (detaching snd-usb-audio) and streams at `rate` (44100 or 48000) until vla_stop(). Callbacks
// run on the libusb event thread. Returns 0 when streaming.
int vla_start(struct libusb_context *ctx, struct libusb_device_handle *h, uint32_t rate, vla_in_fn in, vla_out_fn out,
              void *user);
void vla_stop(void);
int vla_running(void);
// 1 once per second of the other rate's packets (the Virus left the stream's rate, E241): the caller recovers (ratesw.h)
int vla_rate_lost(void);
// One switch while streaming, in the driver's order (ratesw.h); the caller debounces. before_start (optional) is called
// with the rate the stream restarts at, before its first transfer. 0 = at `rate` now, -1 = refused (back at the old
// rate), -2 = the stream stopped.
int vla_switch_rate(uint32_t rate, void (*before_start)(uint32_t rate));
uint32_t vla_rate(void); // the wanted rate of the running stream
