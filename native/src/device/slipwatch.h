// slipwatch.h - a byte-slipped Virus audio stream (E488). In about 0.4 % of stream starts the Virus sends every frame
// of the whole stream with its bytes moved: each 16-bit sample keeps its high byte in place and takes the low byte of
// the sample two places on (one stereo pair). The sync marker is then never found and the notes run early or late,
// until the stream starts again. In any audio, even the analog inputs' noise floor, the low byte changes a lot from
// frame to frame and the high byte slowly; slipped, the two trade places. So per half-second window: the sum of the
// byte changes at the high-byte positions over the sum at both, normal below 0.5 in 977 real streams (single
// full-scale glitches included), slipped 0.99 and up in every window (simulator S120, all captures). Two slipped
// windows in a row count; a window with almost no change decides nothing (a slip is inaudible in silence).
// Header only: the plug-in's device worker and the tests use it. It reads the ring as the device delivers it: on the
// Mac the samples come through Core Audio (not a 1:1 copy, E488), where it only logs.
#pragma once
#include <cstdint>
#include <cstdlib>

namespace vti {

class SlipWatch {
public:
    static constexpr double THRESHOLD = 0.9; // normal windows reach at most 0.5, slipped ones 0.99 (S120)

    void set_rate(int rate)
    {
        window_ = rate > 0 ? rate / 2 : 22050;
        reset();
    }
    void reset() // a new stream
    {
        have_prev_ = false;
        frames_ = changed_ = 0;
        s_lo_ = s_hi_ = 0;
        run_ = 0;
    }

    // one ring frame (6 channels); true once when the second slipped window in a row ends
    bool feed(const int16_t fr[6])
    {
        if (have_prev_) {
            bool any = false;
            for (int k = 0; k < 6; k++) {
                const uint16_t a = (uint16_t)prev_[k], b = (uint16_t)fr[k];
                const int dlo = std::abs((int)(int8_t)(uint8_t)((b & 0xFF) - (a & 0xFF)));
                const int dhi = std::abs((int)(int8_t)(uint8_t)((b >> 8) - (a >> 8)));
                s_lo_ += dlo;
                s_hi_ += dhi;
                any |= dlo != 0 || dhi != 0;
            }
            changed_ += any;
        }
        for (int k = 0; k < 6; k++)
            prev_[k] = fr[k];
        have_prev_ = true;
        if (++frames_ < window_)
            return false;
        bool fired = false;
        // decided only with changes in at least 1 % of the frames: one step into or out of silence decides nothing
        if (changed_ * 100 >= window_ && s_lo_ + s_hi_ > 0) {
            ratio_ = (double)s_hi_ / (double)(s_lo_ + s_hi_);
            state_ = ratio_ > THRESHOLD ? 1 : 0;
            run_ = state_ ? run_ + 1 : 0;
            if (run_ >= 2) {
                run_ = 0;
                fired = true;
            }
        } else
            state_ = -1;
        frames_ = changed_ = 0;
        s_lo_ = s_hi_ = 0;
        return fired;
    }

    int last_state() const { return state_; }    // the last window: 1 slipped, 0 normal, -1 undecided
    double last_ratio() const { return ratio_; } // of the last decided window

private:
    int window_ = 22050;
    bool have_prev_ = false;
    int16_t prev_[6] = {};
    int frames_ = 0, changed_ = 0;
    int64_t s_lo_ = 0, s_hi_ = 0;
    int run_ = 0, state_ = -1;
    double ratio_ = 0;
};

// The cure after a slip: the broker's normal stream stop and start (the alt 0/1 + SET_CUR of every session start).
// Bounded so it can never turn into a loop of restarts: at least 10 s apart, at most 5 per plug-in session.
class SlipRestartGate {
public:
    bool allow(int64_t now_ms)
    {
        if (count_ >= 5 || (count_ > 0 && now_ms - last_ms_ < 10000))
            return false;
        last_ms_ = now_ms;
        count_++;
        return true;
    }
    int count() const { return count_; }

private:
    int64_t last_ms_ = 0;
    int count_ = 0;
};

} // namespace vti
