// stallwatch.h - a Virus audio stream that stood still while time went on, then ran on (E207 M3: macOS sleep pauses
// Core Audio; the ring keeps its frame counter and continues after wake, without a USB error). The Virus' stamp clock
// kept running meanwhile, so the plug-in's anchor between ring frames and that clock is off by the pause: every note
// was stamped ~19 s wrong and nothing sounded. update() reports the resume once; the plug-in then starts a new
// timeline (re-anchor, sync ping), as after a reconnect. Header only: the plug-in and the tests use it.
#pragma once
#include <cstdint>

namespace vti {

class StallWatch {
public:
    static constexpr int64_t LIMIT_MS = 1500; // longer than any hiccup of a busy system, shorter than a sleep

    // `newest` = the ring's newest frame (-1: no stream), `now_ms` = a steady clock, `rate` = the stream's frames per
    // second; true once when a RUNNING stream moves again after standing still for LIMIT_MS or longer. A stream that
    // has not moved since its first frame is starting (a new broker stream shows a frame number seconds before the
    // audio runs, E209), not asleep. Time the caller itself was away (its worker blocked in writes the Virus did not
    // take, S089) is no stall when the frames for that time arrived: only frames missing for LIMIT_MS count.
    bool update(int64_t newest, int64_t now_ms, int rate = 44100)
    {
        if (newest < 0) {
            last_ = -1;
            return false;
        }
        if (last_ < 0 || newest < last_) { // first frame, or a new stream (its counter starts again)
            last_ = newest;
            moved_ms_ = now_ms;
            running_ = false;
            return false;
        }
        if (newest == last_)
            return false;
        const int64_t frames_ms = rate > 0 ? (newest - last_) * 1000 / rate : 0; // what arrived, in time
        const bool resumed = running_ && now_ms - moved_ms_ - frames_ms >= LIMIT_MS;
        last_ = newest;
        moved_ms_ = now_ms;
        running_ = true;
        return resumed;
    }

private:
    int64_t last_ = -1, moved_ms_ = 0;
    bool running_ = false; // the stream has moved since its first frame
};

} // namespace vti
