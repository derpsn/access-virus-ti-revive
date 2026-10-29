// The rate the Virus' audio really arrives at (macOS, E377): Core Audio can report one rate while the Virus streams at
// the other; the meter says so after two whole seconds of frames at the other rate, never at the start or after a stall.
#include "check.h"
#include "device/ratemeter.h"

using namespace vti;

// feeds `secs` seconds of frames at `real` Hz in 10 ms steps while the ring says `ring`; counts the reports
static int feed(RateMeter &m, int64_t &frames, uint32_t &ms, int real, int ring, double secs)
{
    int reports = 0;
    for (int i = 0; i < (int)(secs * 100); i++) {
        frames += real / 100;
        ms += 10;
        reports += m.other_rate(frames, ms, ring);
    }
    return reports;
}

TEST(ratemeter_matching_rate_is_quiet)
{
    RateMeter m;
    int64_t f = 0;
    uint32_t ms = 1000;
    CHECK_EQ(feed(m, f, ms, 44100, 44100, 10), 0);
    CHECK_EQ(feed(m, f, ms, 48000, 48000, 10), 0);
}

TEST(ratemeter_reports_the_other_rate_after_two_seconds)
{
    RateMeter m;
    int64_t f = 0;
    uint32_t ms = 1000;
    CHECK_EQ(feed(m, f, ms, 48000, 44100, 2.4), 0); // warm-up + the first window only
    CHECK_EQ(feed(m, f, ms, 48000, 44100, 1.0), 1); // the second window in a row
    RateMeter n;
    f = 0;
    CHECK_EQ(feed(n, f, ms, 44100, 48000, 3.0), 1); // the other way round
}

TEST(ratemeter_once_per_window_while_it_lasts)
{
    RateMeter m;
    int64_t f = 0;
    uint32_t ms = 0;
    CHECK_EQ(feed(m, f, ms, 48000, 44100, 10.4), 8); // 9 whole windows after the 0.5 s warm-up, reports from the 2nd
}

TEST(ratemeter_ignores_jitter_and_drift)
{
    RateMeter m;
    int64_t f = 0;
    uint32_t ms = 0;
    for (int i = 0; i < 2000; i++) { // 20 s at 44.1 kHz delivered in uneven pieces (512-frame IO cycles) and +2 % drift
        ms += 10;
        if (i % 3 == 0)
            f += 3 * 450;
        CHECK(!m.other_rate(f, ms, 44100));
    }
}

TEST(ratemeter_resets_on_a_rate_switch_and_a_stall)
{
    RateMeter m;
    int64_t f = 0;
    uint32_t ms = 0;
    CHECK_EQ(feed(m, f, ms, 48000, 44100, 2.0), 0);
    CHECK_EQ(feed(m, f, ms, 48000, 48000, 1.0), 0); // the ring follows: a new start, nothing reported
    CHECK_EQ(feed(m, f, ms, 48000, 44100, 2.4), 0); // a new warm-up and first window
    ms += 3000;                                     // no frames for 3 s (sleep, a stopped stream)
    CHECK(!m.other_rate(f, ms, 44100));
    CHECK_EQ(feed(m, f, ms, 48000, 44100, 2.4), 0); // starts over
    CHECK(!m.other_rate(-1, ms, 44100));            // not streaming
    CHECK_EQ(feed(m, f, ms, 48000, 44100, 2.4), 0);
}
