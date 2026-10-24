// nativehost's host clock (tools/hostclock.h): time and frames stay continuous when --rate-at changes the host rate in
// the middle of a run, like a DAW whose project rate is switched while it plays. Hardware-free.
#include "check.h"
#include "../tools/hostclock.h"
#include <cmath>
#include <algorithm>

TEST(hostclock_without_change_is_blocks_times_block)
{
    HostClock c{44100, 512};
    CHECK(c.seconds(0) == 0.0);
    CHECK(std::fabs(c.seconds(100) - 100 * 512 / 44100.0) < 1e-12);
    CHECK(c.frame(100) == 100L * 512);
}

TEST(hostclock_continues_across_a_rate_change)
{
    HostClock c{44100, 512};
    const double before = c.seconds(861); // ~10 s
    c.set_rate(861, 48000);
    CHECK(c.rate == 48000);
    CHECK(std::fabs(c.seconds(861) - before) < 1e-12); // no jump at the change
    CHECK(std::fabs(c.seconds(861 + 375) - (before + 375 * 512 / 48000.0)) < 1e-9); // 48 kHz blocks after it
    // frames count at the current rate from the same time line: the note grid (rate / 2) stays on half seconds
    CHECK(c.frame(861) == std::lround(before * 48000));
}

TEST(hostclock_back_and_forth_keeps_the_time)
{
    HostClock c{44100, 512};
    c.set_rate(100, 48000);
    c.set_rate(200, 44100);
    const double t = 100 * 512 / 44100.0 + 100 * 512 / 48000.0;
    CHECK(std::fabs(c.seconds(200) - t) < 1e-12);
    CHECK(std::fabs(c.seconds(300) - (t + 100 * 512 / 44100.0)) < 1e-9);
}

TEST(hostclock_parses_rate_at)
{
    RateAt r{};
    CHECK(parse_rate_at("48000@10", r));
    CHECK(r.rate == 48000 && r.t == 10.0);
    CHECK(parse_rate_at("44100@10.5", r));
    CHECK(r.rate == 44100 && r.t == 10.5);
    CHECK(!parse_rate_at("48000", r)); // no time
    CHECK(!parse_rate_at("0@10", r)); // no rate
    CHECK(!parse_rate_at("48000@-1", r)); // negative time
    CHECK(!parse_rate_at("48000@10x", r)); // trailing junk
}

TEST(hostclock_rough_cadence_only_inside_its_window)
{
    HostClock c{44100, 256};
    RoughCadence r{10, 20};
    for (long b = 0; b < 3000; b++) { // ~17 s
        const double t = c.seconds(b);
        if (t < 10 || t >= 20)
            CHECK(r.shift_us(c, b) == 0);
    }
}

TEST(hostclock_rough_cadence_comes_in_bursts_of_eight)
{
    HostClock c{44100, 256};
    RoughCadence r{0, 60};
    const double period_us = 256 * 1e6 / 44100;
    for (long burst = 0; burst < 1000; burst++) {
        // the eight blocks of a burst wake together (within 1 us), so the next block waits for the next burst
        const double w0 = c.seconds(burst * 8) * 1e6 + (double)r.shift_us(c, burst * 8);
        for (long k = 1; k < 8; k++) {
            const double w = c.seconds(burst * 8 + k) * 1e6 + (double)r.shift_us(c, burst * 8 + k);
            CHECK(std::fabs(w - w0) < 1.0);
        }
        // around the burst's middle: +-20 ms jitter, or 40 ms late now and then
        const double mid = c.seconds(burst * 8 + 4) * 1e6 - period_us / 2;
        CHECK(w0 - mid >= -20000 - 1 && w0 - mid <= 40000 + 1);
    }
}

TEST(hostclock_rough_cadence_has_late_bursts_and_varies)
{
    HostClock c{44100, 256};
    RoughCadence r{0, 60};
    const double period_us = 256 * 1e6 / 44100;
    int late = 0;
    double lo = 1e9, hi = -1e9;
    for (long burst = 0; burst < 1290; burst++) { // ~60 s
        const double d = c.seconds(burst * 8) * 1e6 + (double)r.shift_us(c, burst * 8) -
                         (c.seconds(burst * 8 + 4) * 1e6 - period_us / 2);
        if (d > 39000)
            late++;
        else
            lo = std::min(lo, d), hi = std::max(hi, d);
    }
    CHECK(late >= 10 && late <= 14); // one every ~5 s
    CHECK(lo < -15000 && hi > 15000); // the jitter uses its range
}

TEST(hostclock_parses_rough)
{
    RoughCadence r{};
    CHECK(parse_rough("10:70", r));
    CHECK(r.from == 10.0 && r.to == 70.0);
    CHECK(!parse_rough("10", r));
    CHECK(!parse_rough("70:10", r)); // empty window
    CHECK(!parse_rough("-1:10", r));
    CHECK(!parse_rough("10:70x", r));
}
