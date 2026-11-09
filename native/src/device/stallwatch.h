// stallwatch.h - a Virus audio stream that stood still while time went on, then ran on (E207 M3: macOS sleep pauses
// Core Audio; the ring keeps its frame counter and continues after wake, without a USB error). The Virus' stamp clock
// kept running meanwhile, so the plug-in's anchor between ring frames and that clock is off by the pause: every note
// was stamped ~19 s wrong and nothing sounded. update() reports the resume once; the plug-in then starts a new
// timeline (re-anchor, sync ping), as after a reconnect. Header only: the plug-in and the tests use it.
#pragma once
#include <atomic>
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

// The plug-in's read position ahead of the Virus' audio in every block for a full second (E472: after a Virus power
// cycle with the cable in, the Mac's plug-in anchored on a stream not yet steady and every read was pulled back from
// then on; dropouts that never settled). update() reports it once; the plug-in then starts a new timeline, as after
// a reconnect. A good read in between clears it: single pulled-back reads are hiccups the margin is there for.
// Audio thread: no allocation.
class ClampWatch {
public:
    bool update(bool pulled_back, int frames, int rate)
    {
        if (!pulled_back) {
            frames_ = 0;
            return false;
        }
        frames_ += frames;
        if (rate > 0 && frames_ >= rate) {
            frames_ = 0;
            return true;
        }
        return false;
    }

private:
    int64_t frames_ = 0;
};

// Frames a read of n at pos lacks: what the host asks for beyond the newest frame that has arrived (avail). The
// plug-in then reads n frames ending at avail instead, so the block puts out older audio and a note in it sounds late
// by about this much (E490: hosts that hand over blocks in bursts).
inline int64_t read_shortfall(int64_t pos, int n, int64_t avail)
{
    const int64_t s = pos + n - avail;
    return s > 0 ? s : 0;
}

// Pulled-back reads per stats window (the audio thread adds, the worker takes every 10 s): how many blocks and the
// largest shortfall. Short clamps do not reach ClampWatch's second, so this is what shows them in diag.log.
struct PullStats {
    void add(int64_t shortfall)
    {
        if (shortfall <= 0)
            return;
        blocks_.fetch_add(1, std::memory_order_relaxed);
        int64_t m = max_.load(std::memory_order_relaxed);
        while (shortfall > m && !max_.compare_exchange_weak(m, shortfall, std::memory_order_relaxed)) {
        }
    }
    void take(int64_t &blocks, int64_t &max)
    {
        blocks = blocks_.exchange(0, std::memory_order_relaxed);
        max = max_.exchange(0, std::memory_order_relaxed);
    }

private:
    std::atomic<int64_t> blocks_{0}, max_{0};
};

// After a detected sleep on the Mac the plug-in asks the broker to make macOS re-enumerate the Virus (a software
// replug; E472: the stream can stay gapped until then). At most once a minute: USB resets close together have hung
// the Virus until a power cycle (E136, E312). The broker keeps its own gap too (resetguard.h).
class ReplugGate {
public:
    static constexpr int64_t GAP_MS = 60000;
    bool allow(int64_t now_ms)
    {
        if (last_ms_ >= 0 && now_ms - last_ms_ < GAP_MS)
            return false;
        last_ms_ = now_ms;
        return true;
    }

private:
    int64_t last_ms_ = -1;
};

} // namespace vti
