// freezewatch.h - a frozen Virus audio stream (E493): from one moment on every frame is the same (the analog inputs
// held at one value without their noise, the USB outputs at 0), while USB and MIDI go on; only a power cycle of the
// Virus cleared it, so the plug-in tells the user. On a live unit identical frames last at most ~175 frames in a row
// (simulator, 1039 real streams); exact all-zero stretches occur with the original's software and on the Mac (where
// channels 0/1 are always 0), so an all-zero frame never counts. It fires only when a note was due and did not come:
// a note sent during the run, at least half a second ago. In the IO configurations "3 Outs" (Global 0x09 = 1 or 2,
// E158/E164) words 0/1 carry USB 3, not the analog inputs: with USB 3 silent a freeze may hold an all-zero frame and
// is then missed (never a false alarm). Header only: the plug-in's device worker and the tests.
#pragma once
#include <cstdint>
#include <cstring>

namespace vti {

class FreezeWatch {
public:
    void reset() // a new stream
    {
        run_ = 0;
        fired_ = false;
    }

    void feed(const int16_t fr[6])
    {
        if (run_ > 0 && std::memcmp(fr, last_, sizeof last_) == 0) {
            run_++;
            return;
        }
        std::memcpy(last_, fr, sizeof last_);
        zero_ = true;
        for (int k = 0; k < 6; k++)
            zero_ = zero_ && fr[k] == 0;
        run_ = 1;
        fired_ = false; // the stream moved: a later freeze reports again
    }

    // the same non-zero frame for at least a second
    bool frozen(int rate) const { return rate > 0 && run_ >= rate && !zero_; }

    // true once per freeze: frozen, and a note went out after the run began and at least 500 ms ago (now_ms and
    // last_note_ms on the same clock; the run's start is estimated from its length at `rate`)
    bool check(int64_t now_ms, int64_t last_note_ms, int rate)
    {
        if (fired_ || !frozen(rate))
            return false;
        const int64_t run_start_ms = now_ms - run_ * 1000 / rate;
        if (last_note_ms <= run_start_ms || now_ms - last_note_ms < 500)
            return false;
        fired_ = true;
        return true;
    }

    int64_t run_frames() const { return run_; }

private:
    int16_t last_[6] = {};
    int64_t run_ = 0;
    bool zero_ = true;
    bool fired_ = false;
};

} // namespace vti
