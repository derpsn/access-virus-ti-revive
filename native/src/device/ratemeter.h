// ratemeter.h - the rate the Virus' audio really arrives at (macOS, E377). Core Audio can report one nominal rate while
// the Virus streams at the other: after a power cycle or after another program used it, the device said 44.1 kHz and
// delivered 48 kHz (and the other way round); the plug-in believed the nominal rate, its timeline re-anchored twice a
// second and the Virus sent a sync report at nearly every block. Asking Core Audio for the same rate changes nothing
// (the Virus' USB chip ignores a SET_CUR with its stored rate, E331), so this meter counts the ring's frames against a
// clock and says when two whole seconds in a row matched the other rate; the caller then switches away and back
// (vti_relock_step, ratesw.h), like a rate change in the DAW does. Header only: the plug-in and the tests use it.
#pragma once
#include <cmath>
#include <cstdint>

namespace vti {

class RateMeter {
public:
    // every worker pass: the ring's newest frame (-1 = not streaming), a ms clock and the ring's rate. True once per
    // window from the second window in a row whose frames match the other rate (within 3 %)
    bool other_rate(int64_t frames, uint32_t now_ms, int ring)
    {
        if (frames < 0 || ring != ring_) {
            reset();
            ring_ = ring;
            return false;
        }
        if (!started_) {
            started_ = true;
            start_ms_ = progress_ms_ = now_ms;
            last_ = frames;
            return false;
        }
        if (frames != last_) {
            last_ = frames;
            progress_ms_ = now_ms;
        } else if (now_ms - progress_ms_ > STALL_MS) { // no frames (sleep, a stopped stream): start over
            reset();
            return false;
        }
        if (base_ms_ < 0) {
            if (now_ms - start_ms_ < WARMUP_MS) // the stream's first packets come in bursts
                return false;
            base_ms_ = now_ms;
            base_ = frames;
            return false;
        }
        const uint32_t span = now_ms - (uint32_t)base_ms_;
        if (span < WINDOW_MS)
            return false;
        const double measured = (double)(frames - base_) * 1000.0 / span;
        base_ms_ = now_ms;
        base_ = frames;
        const double other = ring == 48000 ? 44100.0 : 48000.0;
        streak_ = std::fabs(measured - other) < 0.03 * other ? streak_ + 1 : 0;
        return streak_ >= 2;
    }

    void reset()
    {
        started_ = false;
        base_ms_ = -1;
        streak_ = 0;
    }

private:
    static constexpr uint32_t WARMUP_MS = 500, WINDOW_MS = 1000, STALL_MS = 250;
    int ring_ = 0;
    bool started_ = false;
    uint32_t start_ms_ = 0, progress_ms_ = 0;
    int64_t last_ = 0, base_ = 0;
    int64_t base_ms_ = -1;
    int streak_ = 0;
};

} // namespace vti
