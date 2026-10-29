// hostmidi.h - the host's values (VST3: normalized 0..1, pitch 0..127, tempo in BPM) as MIDI bytes for the Virus.
//
// Why: process() turned velocity, aftertouch and pitch bend into bytes with a plain cast; a value outside the VST3
// range from a misbehaving host gave a byte >= 0x80, which the Virus reads as a status byte (0xF4 is its DSP's
// private command parser, AGENTS.md rule 2). And the MIDI clock's tick distance came from the host tempo as is: 0 or
// a negative tempo made the clock loop endless on the audio thread (review 2026-10-19).
// NaN is caught from the bits: the VST3 SDK builds with /fp:fast (MSVC) and -ffast-math (Xcode), under which the
// compiler may drop `!(v > 0)` for NaN - NaN gave 999 BPM, velocity 127 (Windows CI, 2026-10-20).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace vti {

inline bool usable(double v) // not NaN, not infinite: the exponent bits are not all set
{
    uint64_t b;
    std::memcpy(&b, &v, sizeof b);
    return (b >> 52 & 0x7FF) != 0x7FF;
}

inline uint8_t data7(double v01) // 0..1 -> 0..127
{
    if (!usable(v01) || !(v01 > 0))
        return 0;
    return (uint8_t)std::lround(std::min(v01, 1.0) * 127);
}

inline uint8_t note7(int pitch) { return (uint8_t)std::max(0, std::min(127, pitch)); }

inline int bend14(double v01) // 0..1 -> 0..16383
{
    if (!usable(v01) || !(v01 > 0))
        return 0;
    return (int)std::lround(std::min(v01, 1.0) * 16383);
}

// BPM for the MIDI clock: 120 without a usable tempo, else 1..999 (at most ~5 ticks per 512-sample block)
inline double clock_tempo(double bpm)
{
    if (!usable(bpm) || !(bpm > 0))
        return 120.0;
    return std::max(1.0, std::min(999.0, bpm));
}

// Controllers the host hands over as parameters (VST3 IMidiMapping; the VST3 numbers: 0..127 controllers, 128
// channel aftertouch, 129 pitch bend). A VST3 host sends a plug-in no controller it has not mapped - with only
// aftertouch and pitch bend mapped, the mod wheel never reached the Virus (issue #18). The original, a VST2, got
// every controller from the host and keeps CC 1..16 per channel in its state (docs/formats.md). The channel mode
// messages 120..127 (all sound off, reset, all notes off, ...) stay unmapped: the plug-in sends those itself.
inline bool mapped_controller(int ctl) { return (ctl >= 0 && ctl < 120) || ctl == 128 || ctl == 129; }

// the MIDI message of mapped controller ctl on channel ch (0..15) at value v01; returns its length (0: none)
inline size_t mapped_message(int ch, int ctl, double v01, uint8_t *m)
{
    if (ch < 0 || ch > 15 || !mapped_controller(ctl))
        return 0;
    if (ctl == 128) {
        m[0] = (uint8_t)(0xD0 | ch), m[1] = data7(v01);
        return 2;
    }
    if (ctl == 129) {
        int pb = bend14(v01);
        m[0] = (uint8_t)(0xE0 | ch), m[1] = (uint8_t)(pb & 0x7F), m[2] = (uint8_t)(pb >> 7);
        return 3;
    }
    m[0] = (uint8_t)(0xB0 | ch), m[1] = (uint8_t)ctl, m[2] = data7(v01);
    return 3;
}

} // namespace vti
