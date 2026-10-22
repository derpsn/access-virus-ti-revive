// Core Audio on macOS skips IO cycles under load and says so only through the next buffer's input sample time; the
// ring must stay as long as the Virus' sample clock says, else every stamp after the gap lands late on the Virus'
// clock (E243/E244: ~5600 frames late after a Live export, sync errors until a power cycle).
#include "check.h"
#include "device/gapfill.h"

using namespace vti;

TEST(gapfill_stream_in_step_fills_nothing)
{
    GapFill g;
    int64_t ring = 0;
    double t = 123456.0; // the device's counter did not start with the ring
    for (int i = 0; i < 100; i++, t += 512, ring += 512)
        CHECK_EQ(g.update(t, ring), 0);
    CHECK_EQ(g.filled(), 0);
}

TEST(gapfill_skipped_cycles_become_silence)
{
    GapFill g;
    g.update(1000, 0);
    CHECK_EQ(g.update(1512, 512), 0);
    // 11 cycles later the ring got one of them: 10 are missing (E243: ~5600 frames)
    CHECK_EQ(g.update(1512 + 11 * 512, 1024), 10 * 512);
    CHECK_EQ(g.update(1512 + 12 * 512, 1024 + 10 * 512 + 512), 0); // in step again
    CHECK_EQ(g.filled(), 10 * 512);
}

TEST(gapfill_fractional_sample_times_are_no_gap)
{
    GapFill g;
    g.update(1000.0, 0);
    CHECK_EQ(g.update(1512.4, 512), 0);
    CHECK_EQ(g.update(2025.9, 1024), 0);
    CHECK_EQ(g.filled(), 0);
}

TEST(gapfill_device_restart_rebases)
{
    GapFill g;
    g.update(900000, 0);
    g.update(900512, 512);
    CHECK_EQ(g.update(0, 1024), 0);   // the counter started again: not a gap of -900000
    CHECK_EQ(g.update(512, 1536), 0); // and runs on from there
    CHECK_EQ(g.filled(), 0);
}

TEST(gapfill_sleep_is_a_new_stream_not_a_fill)
{
    GapFill g;
    g.update(1000, 0);
    g.update(1512, 512);
    CHECK_EQ(g.update(1512 + 19 * 44100, 1024), 0); // 19 s (E207): the StallWatch re-anchors instead
    CHECK_EQ(g.update(1512 + 19 * 44100 + 512, 1536), 0);
    CHECK_EQ(g.filled(), 0);
}

TEST(gapfill_limit_is_five_seconds_at_any_rate)
{
    // E323: the device may run at 48 kHz; the limit is a time, 5 s
    GapFill g;
    CHECK_EQ(g.max_fill(), 5 * 44100);
    g.set_rate(48000);
    CHECK_EQ(g.max_fill(), 5 * 48000);
    g.update(1000, 0);
    CHECK_EQ(g.update(1000 + 512 + 5 * 48000, 512), 5 * 48000); // 5 s at 48 kHz is still a gap
}

// The boundaries (mutation run 2026-10-20): exactly MAX_FILL is still a gap, SLACK frames either way are rounding
TEST(gapfill_boundaries)
{
    GapFill a;
    a.update(1000, 0);
    CHECK_EQ(a.update(1000 + 512 + GapFill::MAX_FILL, 512), GapFill::MAX_FILL);
    GapFill b;
    b.update(1000, 0);
    CHECK_EQ(b.update(1512 + GapFill::SLACK, 512), 0);
    CHECK_EQ(b.update(2024 + GapFill::SLACK + 1, 1024), GapFill::SLACK + 1);
    GapFill c;
    c.update(1000, 0);
    CHECK_EQ(c.update(1512 - GapFill::SLACK, 512), 0); // early by SLACK: no re-base
    CHECK_EQ(c.update(2024 + 1, 1024), 0);             // so one frame late against the old origin stays rounding
}
