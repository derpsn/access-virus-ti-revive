// ratelever.h - the MIDI-side lever for the Virus' sample rate (E257, E241).
//
// Why: the Virus' USB chip owns its audio clock and has two masters for the rate: the PC's UAC SET_CUR on the
// endpoints and the global parameter 0x10 "System Clock/Frequency" (0 = 44.1 kHz, 1 = 48 kHz), which the panel
// sets and the host CPU forwards to the chip (firmware, E257). After a wake or a replug the Virus sometimes keeps
// sending 48 kHz packets although SET_CUR 44100 was accepted (E175/E241); when the broker sees that it asks with
// SET_CUR again and, since round 0.8.27, also sends `F0 00 20 33 01 10 73 00 10 00 F7` on the plug-in cable - a
// normal global parameter, no vendor request (rule 2). Plain C, shared by the Linux and the Windows broker; the
// unit test (native/tests/test_pktstat.cpp) calls it directly. A read-back request for the global is not in our
// evidence (open); the broker logs the Virus' own report of the parameter when one arrives.
#pragma once
#include <stdint.h>

// USB-MIDI packets (cable 2) of the global parameter message for `rate` (48000 -> 1, anything else 0): 16 bytes, or -1
// when cap is too small. Rate following (design-rate.md step 3): the lever asks for the rate the broker wants.
static inline int vti_pack_rate(uint8_t *out, int cap, uint32_t rate)
{
    static const uint8_t pk[16] = {0x24, 0xF0, 0x00, 0x20, 0x24, 0x33, 0x01, 0x10,
                                   0x24, 0x73, 0x00, 0x10, 0x26, 0x00, 0xF7, 0x00};
    if (cap < 16)
        return -1;
    for (int i = 0; i < 16; i++)
        out[i] = pk[i];
    out[13] = rate == 48000 ? 0x01 : 0x00;
    return 16;
}

static inline int vti_pack_rate_44k1(uint8_t *out, int cap) { return vti_pack_rate(out, cap, 44100); }

// A report of the global 0x10 in received USB-MIDI packets (any cable): the value 0/1, or -1 when none
static inline int vti_rate_report(const uint8_t *p, int len)
{
    for (int i = 0; i + 7 < len; i += 4)
        if ((p[i] & 0x0F) == 4 && p[i + 1] == 0x73 && p[i + 2] == 0x00 && p[i + 3] == 0x10 && (p[i + 4] & 0x0F) == 6 &&
            p[i + 6] == 0xF7)
            return p[i + 5];
    return -1;
}
