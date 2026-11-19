// hostgestures.h - knob moves on the Virus handed to the host as edits, like the original (E304, issue #10): its
// CVSTAutomation queues a parameter changed without a mouse touch, a periodic tick sends beginEdit and performEdit
// with the latest value, and endEdit once 250 ms passed without a change. Ableton Live's Configure mode reacts to
// those edits, not to output parameter changes. The host hands a performed value back to process(): the same value
// while the edit is open (and shortly after) is an echo and is not sent to the Virus again.
// Threads: device_change from the device's receive thread, tick/flush on the UI thread, echo on the audio thread
// (try_lock: if the lock is busy the value counts as no echo).
#pragma once
#include <cmath>
#include <cstdint>
#include <map>
#include <mutex>
#include <vector>

namespace vti {

class HostGestures {
public:
    static constexpr int64_t END_MS = 250;  // the original's tick: endEdit 250 ms after the last change (E304)
    static constexpr int64_t ECHO_MS = 500; // a value handed back this long after the edit ended is an echo

    // a parameter's new value (normalized) reported by the Virus at `now` (ms)
    void device_change(uint32_t id, double value, int64_t now)
    {
        std::lock_guard<std::mutex> lk(mu_);
        auto &s = st_[id];
        if (!s.queued && !s.begun)
            order_.push_back(id);
        s.value = value, s.last = now, s.queued = true;
    }
    // UI thread, periodically: begin(id) on a parameter's first change, perform(id, value) with the latest value,
    // end(id) END_MS after its last change
    template <class Begin, class Perform, class End> void tick(int64_t now, Begin begin, Perform perform, End end)
    {
        std::lock_guard<std::mutex> lk(mu_);
        std::vector<uint32_t> keep;
        for (uint32_t id : order_) {
            auto &s = st_[id];
            if (s.queued) {
                if (!s.begun)
                    begin(id), s.begun = true;
                perform(id, s.value);
                s.queued = false;
                echo_[id] = {s.value, true, 0};
            }
            if (s.begun && now - s.last >= END_MS) {
                end(id);
                s.begun = false;
                echo_[id].active = false, echo_[id].ended = now;
            }
            if (s.begun || s.queued)
                keep.push_back(id);
            else
                st_.erase(id);
        }
        order_ = keep;
    }
    // the editor closes (no more ticks): open edits end now, unsent changes are dropped
    template <class End> void flush(End end)
    {
        std::lock_guard<std::mutex> lk(mu_);
        for (uint32_t id : order_)
            if (st_[id].begun) {
                end(id);
                auto &e = echo_[id];
                e.active = false, e.ended = st_[id].last + END_MS;
            }
        order_.clear(), st_.clear();
    }
    // is `value` for `id`, handed back by the host at `now`, the echo of a performed edit?
    bool echo(uint32_t id, double value, int64_t now)
    {
        std::unique_lock<std::mutex> lk(mu_, std::try_to_lock);
        if (!lk.owns_lock())
            return false;
        auto e = echo_.find(id);
        // the same 7-bit step: the AU wrapper hands back a Float32, off by up to ~4e-8 (audit 2026-11-17 #5)
        return e != echo_.end() && std::fabs(e->second.value - value) < 0.5 / 127 &&
               (e->second.active || now - e->second.ended <= ECHO_MS);
    }
    bool active(uint32_t id)
    {
        std::lock_guard<std::mutex> lk(mu_);
        auto s = st_.find(id);
        return s != st_.end() && (s->second.begun || s->second.queued);
    }

private:
    struct State {
        double value = 0;
        int64_t last = 0;
        bool queued = false, begun = false;
    };
    struct Echo {
        double value = 0;
        bool active = false;
        int64_t ended = 0;
    };
    std::mutex mu_;
    std::map<uint32_t, State> st_;
    std::vector<uint32_t> order_; // parameters with an open edit or a queued change, in the order they moved
    std::map<uint32_t, Echo> echo_;
};

} // namespace vti
