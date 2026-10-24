// timelinefollow.h - how the plug-in's Virus timeline follows the audio ring once it is anchored (E337). Per block the
// plug-in measures err = (newest ring frame - S) - timeline. With an uneven host cadence (Ableton at a 2048 sound-card
// buffer, every ~6 min for about a minute) that error swings +-800 frames within a second while its average stays put;
// reacting to each block's err jumped the read position back and forth (each jump skips or repeats 12-19 ms = a click)
// and stepped up to ~59 times a second. Here a jump needs the error to stay on one side beyond JUMP for a while, and
// the one-frame drift steps follow the error's mean over a few seconds. A real lasting offset (REAPER after a render,
// E279; a DAW stall) still jumps: after half a second when the error is steady, after ~2 s when the cadence is rough,
// at once when it is far off.
// Header only, no allocation (runs on the audio thread): the plug-in and the tests use it.
#pragma once
#include <cstdint>

namespace vti {

class TimelineFollow {
public:
    static constexpr int64_t JUMP = 512; // jump back to real time beyond this (with the marker only)...
    static constexpr int64_t FAR = 4096; // ...at once beyond this: no jitter comes near (E337: 852), REAPER ahead does
    static constexpr int64_t STEP = 256; // drift: start stepping when the mean error is beyond this...
    static constexpr int64_t HOLD = 128; // ...and step until it is back inside this

    struct Move {
        int64_t delta; // added to the timeline: 0, +-1 (a drift step) or the jump
        bool jump;
    };

    // `frames`: the Virus frames of half a second (the jump window; the mean window is four of them)
    void set_window(int64_t frames) { window_ = frames > 0 ? frames : 1; }

    // a new anchor or a jump: the windows start afresh
    void reset()
    {
        far_side_ = 0, far_frames_ = 0, far_sum_ = 0, far_n_ = 0;
        mean_frames_ = 0, mean_sum_ = 0, mean_n_ = 0;
        owed_ = 0;
    }

    // one block: its err and its length in Virus frames; `marker` = the Virus' stamp clock is tied to the ring (only
    // then may the timeline jump without a new anchor, E279)
    Move update(int64_t err, int64_t frames, bool marker)
    {
        if (marker && (err > FAR || err < -FAR)) { // REAPER processing ahead after Play (up to -8548): no waiting
            reset();
            return {err, true};
        }
        const int side = err > JUMP ? 1 : err < -JUMP ? -1 : 0;
        if (marker && side != 0) {
            if (side != far_side_)
                far_side_ = side, far_frames_ = 0, far_sum_ = 0, far_n_ = 0;
            far_frames_ += frames, far_sum_ += err, far_n_++;
            if (far_frames_ >= window_) { // beyond JUMP on one side for half a second: a real offset
                const int64_t d = far_sum_ / far_n_;
                reset();
                return {d, true};
            }
        } else
            far_side_ = 0, far_frames_ = 0, far_sum_ = 0, far_n_ = 0;

        mean_frames_ += frames, mean_sum_ += err, mean_n_++;
        if (mean_frames_ >= 4 * window_) {
            const int64_t mean = mean_sum_ / mean_n_;
            mean_frames_ = 0, mean_sum_ = 0, mean_n_ = 0;
            if (marker && (mean > JUMP || mean < -JUMP)) { // an offset hidden in a rough cadence
                reset();
                return {mean, true};
            }
            owed_ = mean > STEP ? mean - HOLD : mean < -STEP ? mean + HOLD : 0;
        }
        if (owed_ != 0) {
            const int64_t d = owed_ > 0 ? 1 : -1;
            owed_ -= d;
            return {d, false};
        }
        return {0, false};
    }

private:
    int64_t window_ = 22050;
    int far_side_ = 0;
    int64_t far_frames_ = 0, far_sum_ = 0, far_n_ = 0;
    int64_t mean_frames_ = 0, mean_sum_ = 0, mean_n_ = 0;
    int64_t owed_ = 0;
};

} // namespace vti
