// nativehost's host clock (tools/hostclock.h): time and frames stay continuous when --rate-at changes the host rate in
// the middle of a run, like a DAW whose project rate is switched while it plays. Hardware-free.
#include "check.h"
#include "../tools/hostclock.h"
#include <cmath>

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
