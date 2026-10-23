// pktstat.h - sizes of the USB audio IN packets the Virus sends, for E175 (Linux: after a replug / power-on the
// stream keeps running but its packets leave the 44.1 kHz range ~2.8 s after every start; the code only counted the
// good ones, so the bad sizes were never seen). Diagnostics only: counting, no request to the Virus. Plain C, used by
// the libusb (Linux) and the WinUSB (Windows) stream; the unit test calls it directly.
// Rate following (design-rate.md step 3): "in range" is relative to the rate the broker asked for - 43-46 frames at
// 44.1 kHz, 47-49 at 48 kHz; a long run of the other rate's sizes is the lost rate. A zeroed statistic is at 44.1 kHz.
#pragma once
#include <stdint.h>

enum { VTI_PKT_EMPTY, VTI_PKT_SHORT, VTI_PKT_43, VTI_PKT_44, VTI_PKT_45, VTI_PKT_46, VTI_PKT_48K, VTI_PKT_LONG,
       VTI_PKT_BUCKETS };
#define VTI_PKT_BAD_RUN 1000 // 1 s of out-of-range packets: longer than the 48 kHz phase right after SET_CUR (~0.3 s)
#define VTI_PKT_GOOD_RUN 1000

typedef struct {
    uint64_t size[VTI_PKT_BUCKETS]; // packets per size bucket, frames: 0, 1-42, 43, 44, 45, 46, 47-49, more
    uint64_t packets;
    uint32_t bad_run, good_run;     // current run lengths
    int bad;                        // 1 while in a reported bad run
    uint64_t bad_since;             // packet number (= ms since the start) of the first packet of that run
    uint32_t bad_frames;            // size of the run's last bad packet, in frames
    uint32_t rate;                  // the wanted rate (0 = 44100)
} vti_pktstat;

static inline uint32_t vti_pktstat_rate(const vti_pktstat *s) { return s->rate == 48000 ? 48000 : 44100; }
static inline void vti_pktstat_set_rate(vti_pktstat *s, uint32_t rate) { s->rate = rate == 48000 ? 48000 : 44100; }

// a packet of f frames is the size of `rate` (44.1 kHz: 44/45 with jitter 43-46; 48 kHz: 48 with jitter 47-49)
static inline int vti_pkt_in_range(uint32_t rate, uint32_t f)
{
    return rate == 48000 ? f >= 47 && f <= 49 : f >= 43 && f <= 46;
}

// frames of the i-th nominal packet: 44.1 kHz = 45, then nine of 44 (per 10 ms); 48 kHz = 48
static inline unsigned vti_pkt_nominal(uint32_t rate, unsigned i) { return rate == 48000 ? 48 : i % 10 ? 44 : 45; }

static inline int vti_pktstat_bucket(uint32_t f)
{
    return f == 0 ? VTI_PKT_EMPTY : f < 43 ? VTI_PKT_SHORT : f <= 46 ? VTI_PKT_43 + (int)(f - 43)
           : f <= 49 ? VTI_PKT_48K : VTI_PKT_LONG;
}

// one packet of f frames: +1 = a bad run just reached VTI_PKT_BAD_RUN, -1 = it ended (VTI_PKT_GOOD_RUN good ones)
static inline int vti_pktstat_add(vti_pktstat *s, uint32_t f)
{
    int ok = vti_pkt_in_range(vti_pktstat_rate(s), f), ev = 0;
    s->size[vti_pktstat_bucket(f)]++;
    if (ok) {
        s->good_run++;
        if (s->bad && s->good_run >= VTI_PKT_GOOD_RUN) {
            s->bad = 0;
            ev = -1;
        }
        if (s->good_run >= VTI_PKT_GOOD_RUN || !s->bad)
            s->bad_run = 0;
    } else {
        s->good_run = 0;
        if (s->bad_run++ == 0 && !s->bad)
            s->bad_since = s->packets;
        s->bad_frames = f;
        if (!s->bad && s->bad_run >= VTI_PKT_BAD_RUN) {
            s->bad = 1;
            ev = 1;
        }
    }
    s->packets++;
    return ev;
}

// The OUT packet that answers an IN packet of f frames (E241), at the wanted rate. In range: the same size, with audio.
// A packet of the other rate (44.1 kHz: 47-49, 48 kHz: 43-46): the same size but silent - the Virus runs at that rate
// then, and packets of the wanted size with audio starve or flood its output every ms (E241: a loud signal on its
// outputs after a wake, then a freeze). Anything else (lost, empty, nonsense): the nominal size with audio, as before.
static inline unsigned vti_pkt_out(uint32_t rate, uint32_t f, unsigned nominal, int *silent)
{
    if (vti_pkt_in_range(rate, f)) {
        *silent = 0;
        return f;
    }
    if (vti_pkt_in_range(rate == 48000 ? 44100 : 48000, f)) {
        *silent = 1;
        return f;
    }
    *silent = 0;
    return nominal;
}

// the Virus left the wanted rate: a reported bad run (VTI_PKT_BAD_RUN packets, longer than the phase after SET_CUR)
// of the other rate's packets - lost packets alone are not a rate problem
static inline int vti_pktstat_rate_lost(const vti_pktstat *s)
{
    return s->bad && vti_pkt_in_range(vti_pktstat_rate(s) == 48000 ? 44100 : 48000, s->bad_frames);
}
