// vtiaudioshm.h - Virus audio shared between vtibroker.exe and its audio clients (plug-in, ASIO driver).
//
// The broker owns the USB audio function (MI_00) and streams it on WinUSB (vtiusbaudio.c, synth = clock master).
// Every received block of frames is appended to the input ring below; `in_frames` is the total number of frames
// the synth produced since the stream started (gaps from stalls are filled with silence, so it never slips), and
// each registered client's event is signalled. Clients read from their own position, so several of them (one
// plug-in per DAW, an ASIO driver) follow the same synth clock. Created by the broker as a named mapping in the
// session namespace; clients open it read/write after VTIMSG_AUDIO returned 1.
// Output (version 2): one client (the ASIO driver) appends 2-channel frames to `out` and advances out_write; the
// broker takes them for the OUT packets towards the synth (out_read), at the synth's rate, silence when empty.
#pragma once
#include <stdint.h>

#define VTI_AUDIO_SHM_NAME "Local\\vtibroker_audio"
#define VTI_AUDIO_SHM_LINUX "/tirevive-audio-%u" // Linux broker: POSIX shm (/dev/shm/tirevive-audio-<uid>); in_qpc
                                                 // = CLOCK_MONOTONIC_RAW in 100 ns (Wine's QueryPerformanceCounter)
#define VTI_AUDIO_MAGIC 0x41415456 // "VTAA"
#define VTI_AUDIO_VERSION 2
#define VTI_AUDIO_RING 16384 // frames per ring (power of 2, ~370 ms at 44.1 kHz)
#define VTI_AUDIO_IN_CH 6    // device order (E082): 0/1 analog inputs (ASIO In 1/2), 2/3 USB 1, 4/5 USB 2
#define VTI_AUDIO_OUT_CH 2

typedef struct {
    uint32_t magic, version, rate, in_ch, out_ch, ring_frames;
    volatile int64_t in_frames; // frames written to `in` since the stream started (written after the data)
    volatile int64_t in_qpc;    // QueryPerformanceCounter at the last in_frames update
    volatile int32_t streaming; // 1 while the broker streams
    volatile int64_t out_write; // frames appended to `out` by the writing client (written after the data)
    volatile int64_t out_read;  // frames taken by the broker
    // rate following (findings/design-rate.md, E323): `rate` is the Virus' current sample rate (44100 or 48000; 0 from
    // an older broker = 44100); the broker writes it, then counts rate_gen up at every switch, before the stream runs
    // at the new rate. Readers re-anchor on a new rate_gen. Same layout as before (one reserved word), version stays 2.
    volatile uint32_t rate_gen;
    uint32_t reserved[12];
    int16_t in[VTI_AUDIO_RING * VTI_AUDIO_IN_CH];   // frame f at (f & (VTI_AUDIO_RING - 1)) * VTI_AUDIO_IN_CH
    int16_t out[VTI_AUDIO_RING * VTI_AUDIO_OUT_CH]; // frame f at (f & (VTI_AUDIO_RING - 1)) * VTI_AUDIO_OUT_CH
} vti_audio_shm;
