// ratelever.h - the Virus' report of its sample rate on MIDI (E257, E241, E331).
//
// The Virus' USB chip owns its audio clock; when its stored rate changes it tells the host CPU, which echoes the
// global parameter 0x10 "System Clock/Frequency" (0 = 44.1 kHz, 1 = 48 kHz) to the PC. After a wake or a replug the
// Virus sometimes keeps sending 48 kHz packets although SET_CUR 44100 was accepted (E175/E241): the chip ignores a
// SET_CUR of the rate it has stored, so the stream recovers with a switch away and back (vti_relock, ratesw.h); the
// broker logs the Virus' report from then on as a diagnostic. Until round 0.8.32 the broker also sent this global
// itself - it meets the same stored value and changes nothing (E331), so it is gone. Plain C, shared by the Linux and
// the Windows broker; the unit test (native/tests/test_pktstat.cpp) calls it directly.
#pragma once
#include <stdint.h>

// A report of the global 0x10 in received USB-MIDI packets (any cable): the value 0/1, or -1 when none
static inline int vti_rate_report(const uint8_t *p, int len)
{
    for (int i = 0; i + 7 < len; i += 4)
        if ((p[i] & 0x0F) == 4 && p[i + 1] == 0x73 && p[i + 2] == 0x00 && p[i + 3] == 0x10 && (p[i + 4] & 0x0F) == 6 &&
            p[i + 6] == 0xF7)
            return p[i + 5];
    return -1;
}
