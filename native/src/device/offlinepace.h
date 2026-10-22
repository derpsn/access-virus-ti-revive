// offlinepace.h - offline render (export, freeze, offline bounce) at the Virus' pace. The host calls process() as fast
// as it can, but the Virus makes its sound in real time: without a wait the plug-in's timeline ran ahead of the audio
// ring, re-anchored again and again and the render got repeated or missing audio. The original refuses offline
// rendering (Virus Control 5.1.7: canDo "offline"/"noRealTime" = no, 0x18002d8c0; a warning when the host's process
// level is offline, 0x18002fd40 -> 0x180080ba0). Here each offline block waits until the Virus has produced as many
// frames as the blocks so far: the render is right, at real-time speed. Header only: the plug-in and the tests use it.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace vti {

class OfflinePacer {
public:
    // a slow render never lags more than ~93 ms behind the Virus: the plug-in reads at the paced position, and the
    // audio ring keeps only 16384 frames (VTI_AUDIO_RING)
    static constexpr int64_t MAX_BEHIND = 4096;

    // the ring frame `newest` (the Virus' newest audio, -1 without audio) must reach before this block runs;
    // `frames` = the block in Virus frames (fractional at other host rates). < 0: nothing to pace by.
    int64_t wait_until(int64_t newest, double frames)
    {
        if (newest < 0) {
            reset();
            return -1;
        }
        if (pos_ < 0 || newest < seen_) { // the first offline block, or a new stream (the Virus came back)
            pos_ = (double)newest;
            seen_ = newest;
            return newest;
        }
        if (newest > seen_)
            learn_step(newest - seen_);
        seen_ = newest;
        pos_ += frames;
        if (pos_ < (double)(newest - MAX_BEHIND))
            pos_ = (double)(newest - MAX_BEHIND);
        return (int64_t)std::floor(pos_ + 1e-9);
    }

    // the block reads the ring ~770 frames behind its paced position (latency_frames), so it may run at most this far
    // before the ring reaches that position and still read only frames that are there
    static constexpr int64_t MAX_EARLY = 512;

    // how many frames before its paced position a block of `frames` may run. The ring grows in steps (macOS: Core
    // Audio hands over 512 frames at a time; Linux: the broker's 10 ms USB transfers, 441 frames). Blocks smaller than a step that all
    // waited for the same step ran together when it arrived, the first up to a step minus a block after its position,
    // and its events reached the Virus that much later than in real time (E315: late notes at 128-sample blocks
    // on the Mac, none at 512). Running up to a step minus a block early leaves each block at most one block late.
    int64_t early(double frames) const
    {
        if (step_ <= 0)
            return 0;
        const double e = std::floor((double)step_ - frames);
        return e <= 0 ? 0 : std::min<int64_t>((int64_t)e, MAX_EARLY);
    }

    // back to real time, or the wait gave up (the Virus stopped streaming): the next offline block starts afresh
    void reset()
    {
        pos_ = -1.0;
        seen_ = -1;
        step_ = 0;
        win_min_ = prev_min_ = INT64_MAX;
        win_n_ = 0;
    }

private:
    // the ring's step: the smallest growth over the last 32-64 steps (two chunks arriving together are no step)
    void learn_step(int64_t d)
    {
        win_min_ = std::min(win_min_, d);
        step_ = std::min(prev_min_, win_min_);
        if (++win_n_ >= 32) {
            prev_min_ = win_min_;
            win_min_ = INT64_MAX;
            win_n_ = 0;
        }
    }

    double pos_ = -1.0;
    int64_t seen_ = -1;
    int64_t step_ = 0, win_min_ = INT64_MAX, prev_min_ = INT64_MAX;
    int win_n_ = 0;
};

} // namespace vti
