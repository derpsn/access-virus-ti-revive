// virusrate.h - the Virus' sample rate for a host rate, as the original decides it (E323: Virus Control 5.1.7
// 0x1800a9dc0): the host rate is doubled while below 44 kHz and halved while above 88 kHz, then 46 kHz and up runs
// the Virus at 48 kHz, anything lower at 44.1 kHz. The Virus' DSP clock follows (synthesis and the stamp clock run at
// the new rate); the plug-in converts what is left (96 kHz = 2 x 48). Header only: the plug-in and the tests use it.
#pragma once
#include <cmath>
#include <cstdint>

namespace vti {

inline int virus_rate_for(double host)
{
    if (!(host > 0))
        return 44100;
    while (host < 44000)
        host *= 2;
    while (host > 88000)
        host /= 2;
    return host >= 46000 ? 48000 : 44100;
}

// a time in the Virus' frames at `rate` (time constants: keepalive, anchor windows, timeouts)
inline int64_t frames_for_ms(int rate, double ms) { return (int64_t)std::llround(rate * ms / 1000.0); }

} // namespace vti
