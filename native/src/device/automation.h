// automation.h - the rate limit for host automation going to the Virus. A flood of parameter messages hung the
// Virus' audio until a power cycle (stress test 2026-10-13, Linux: 1000/s together with dense notes; the firmware
// mechanism is still open - not the DSP's stamp queue, E261), so process() coalesces the changes per parameter (only
// the latest value waits) and sends at most `rate` per second, oldest parameter first. Since round 0.8.27 each
// change goes out with its own time stamp at the block time (stamprule.h) and within the byte budget (sendbudget.h).
// Realtime safe: fixed ring, no allocation. The pending values live with the caller (one slot per parameter).
#pragma once
#include <algorithm>
#include <cstdint>

namespace vti {

class AutomationQueue {
public:
    static constexpr int MAX = 8192; // > 2703 automatable parameters (E111) + 16 x 122 mapped controllers: each queued at most once
    explicit AutomationQueue(double rate = 250, double burst = 64) : rate_(rate), burst_(burst) {}

    // a parameter got a new value and was not waiting yet (the caller keeps the "waiting" flag); false if full
    bool push(uint32_t id)
    {
        if (count_ >= MAX)
            return false;
        ids_[(head_ + count_++) % MAX] = id;
        return true;
    }
    // per block: budget for `samples` at `sample_rate`, up to the burst size
    void refill(int samples, double sample_rate) { budget_ = std::min(burst_, budget_ + rate_ * samples / sample_rate); }
    // sends waiting parameters while budget and `max` last: send(id) returns true if a message went out (costs one
    // unit of budget), false for a parameter that is no longer waiting. Returns the number sent.
    template <class Send> int drain(int max, Send &&send)
    {
        int sent = 0;
        while (count_ > 0 && budget_ >= 1 && sent < max) {
            uint32_t id = ids_[head_];
            head_ = (head_ + 1) % MAX;
            count_--;
            if (send(id)) {
                budget_ -= 1;
                sent++;
            }
        }
        return sent;
    }
    int waiting() const { return count_; }

private:
    double rate_, burst_, budget_ = 0;
    uint32_t ids_[MAX];
    int head_ = 0, count_ = 0;
};

} // namespace vti
