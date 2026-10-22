// keepalive.h - Active Sensing (FE) on the plug-in's stamped timeline.
//
// Why: the Virus' DSP arms a watchdog with the first FE it receives and, about one second after the last one,
// releases every voice and clears its controller state (firmware, E237). The original Virus Control sends FE every
// 0.31 s (E025). Sending it once (as the plug-in did at each anchor) is the worst of both worlds: a one-shot note cut
// a second later. So FE goes out every 300 ms on the timeline, and never in a burst after a timeline jump.
#pragma once
#include <cstdint>

namespace vti {

struct ActiveSensing {
    int64_t period = 13230; // 300 ms in Virus frames (44.1 kHz; set_rate for 48 kHz, E323)
    int64_t next = -1;      // timeline frame of the next FE, -1 = not anchored

    void reset(int64_t at) { next = at; }
    void set_rate(int rate) { period = (int64_t)rate * 3 / 10; } // 300 ms at the Virus' current rate

    // Emits at most one FE due before `end` (the end of this block on the timeline) and returns how many were sent.
    // A timeline jump in either direction keeps the next FE within one period: no backlog burst after a stall, no
    // silent second after a jump back.
    template <class Emit> int run(int64_t end, Emit emit)
    {
        if (next < 0)
            return 0;
        if (next > end + period)
            next = end;
        if (next < end - period)
            next = end - period;
        if (next >= end)
            return 0;
        emit(next);
        next += period;
        return 1;
    }
};

} // namespace vti
