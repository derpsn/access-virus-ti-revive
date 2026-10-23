// vtiusbaudio.h - Virus TI USB audio streaming on WinUSB (vtiusbaudio.c, findings E075).
#pragma once
#include <windows.h>
#include <stdint.h>

#define VUA_RATE 44100 // the rate before any client asked for one (protocol < 5 clients get it)

// Called once per completed IN read with `frames` interleaved 6-channel int16 frames (device channel order).
typedef void (*vua_in_fn)(const int16_t *pcm, uint32_t frames, void *user);
typedef void (*vua_log_fn)(const char *fmt, ...);

// Source of the audio sent to the synth (EP 0x01): fill `frames` interleaved 2-channel int16 frames at dst (called
// once per OUT packet from the streaming thread). Not set (default): silence.
typedef void (*vua_out_fn)(int16_t *dst, uint32_t frames, void *user);
void vua_set_out(vua_out_fn fn, void *user);

// Stream until *running becomes 0. Returns -1 at once if the WinUSB audio function (MI_00) is not present or
// cannot be opened (the caller then uses WASAPI), 0 after a normal stop, 1 after a setup/stream error.
int vua_run(volatile LONG *running, vua_in_fn in, void *user, vua_log_fn log);

// Seconds of 48 kHz packets the stream saw since the last call (E241; the stream itself repeats SET_CUR): the
// broker sends the panel's global parameter "System Clock/Frequency = 44.1 kHz" for each (ratelever.h).
LONG vua_rate_lost(void);

// Rate following (design-rate.md step 4): the rate the stream should run at (44100 or 48000; vua_run starts at it, a
// running stream switches between two reads in the driver's order, ratesw.h - once per call, a refusal is not
// retried). vua_switches counts the attempts (the caller waits for the next one); vua_rate = the rate set now.
// before_start is called on the stream thread with the rate, before the first read at it (the shared ring).
void vua_want_rate(uint32_t rate);
uint32_t vua_rate(void);
LONG vua_switches(void);
void vua_set_rate_fn(void (*before_start)(uint32_t rate));
