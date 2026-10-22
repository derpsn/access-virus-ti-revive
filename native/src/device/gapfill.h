// gapfill.h - keeps the macOS audio ring in step with the Virus' sample clock. Core Audio skips IO cycles when the
// system is overloaded (a Live export renders at full CPU) and says so only through the input time stamp of the next
// cycle; the ring then holds fewer frames than the Virus made. The plug-in stamps every event as a ring frame plus a
// lead relative to the sync marker, so after such a gap every note lands behind the Virus' stamp clock by the missing
// frames and plays late with sync errors until the next marker (E243/E244: ~5600 frames after a Mac export; not on
// Linux, where the broker fills USB gaps with silence, E242). The device's sample time counts what the Virus produced,
// so the ring follows it: the missing frames become silence. Header only: the plug-in and the tests use it.
#pragma once
#include <atomic>
#include <cstdint>

namespace vti {

class GapFill {
public:
    static constexpr int64_t MAX_FILL = 5 * 44100; // a longer jump (sleep, device restart) is a new stream, not a gap
    static constexpr int64_t SLACK = 2;            // sample times are Float64 and can be off by a fraction

    // the limit is 5 s at the device's rate (E323: the Virus may run at 48 kHz)
    void set_rate(int rate) { max_fill_ = 5 * (int64_t)rate; }
    int64_t max_fill() const { return max_fill_; }

    // `sample_time` = the device time of the buffer's first frame (Core Audio's inInputTime), `ring` = the frames the
    // ring holds before this buffer. Returns the frames of silence to append before the buffer so that the ring stays
    // sample_time - origin frames long: 0 when in step, on the first call and after a re-base (the device's counter
    // started again, or a jump longer than MAX_FILL: the StallWatch re-anchors for those).
    int64_t update(double sample_time, int64_t ring)
    {
        const int64_t t = (int64_t)sample_time; // whole frames of the device's own counter
        if (!valid_) {
            origin_ = t - ring;
            valid_ = true;
            return 0;
        }
        const int64_t gap = (t - origin_) - ring;
        if (gap > max_fill_ || gap < -SLACK) { // a new run of sample times: re-base, nothing to fill
            origin_ = t - ring;
            return 0;
        }
        if (gap <= SLACK)
            return 0;
        filled_.fetch_add(gap, std::memory_order_relaxed);
        return gap;
    }
    int64_t filled() const { return filled_.load(std::memory_order_relaxed); } // frames of silence so far (stats)

private:
    bool valid_ = false;
    int64_t origin_ = 0;
    int64_t max_fill_ = MAX_FILL;
    std::atomic<int64_t> filled_{0};
};

} // namespace vti
