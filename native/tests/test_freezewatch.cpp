// A frozen Virus audio stream (E493): every frame the same, the analog inputs held at one value with no noise, the
// USB outputs at 0, for the rest of the stream; only a power cycle cleared it. On a live unit identical frames last at
// most ~175 frames in a row (simulator, 1039 streams); all-zero stretches occur in real captures with the original's
// software (not a freeze) and on the Mac (channels 0/1 always 0), so those never count. And only when a note was due
// and did not come: a note sent during the run, at least half a second ago.
#include "check.h"
#include "device/freezewatch.h"
#include <cstdint>

using namespace vti;

namespace {

const int16_t kFrozen[6] = {-4672, -4672, 0, 0, 0, 0}; // what the frozen TI2 sent (c0 ed c0 ed 00 ..)

void live_frame(long f, int16_t out[6])
{
    out[0] = (int16_t)(-4 - (f * 7 % 3)); // the ADC's noise floor
    out[1] = (int16_t)(-1 - (f * 5 % 2));
    out[2] = out[3] = out[4] = out[5] = 0;
}

void feed_n(FreezeWatch &w, const int16_t fr[6], long n)
{
    for (long i = 0; i < n; i++)
        w.feed(fr);
}

} // namespace

TEST(freezewatch_live_stream_never_frozen)
{
    FreezeWatch w;
    for (long f = 0; f < 44100 * 5; f++) {
        int16_t fr[6];
        live_frame(f, fr);
        w.feed(fr);
    }
    CHECK(!w.frozen(44100));
    CHECK(!w.check(10000, 9000, 44100)); // a note was due, but the stream is alive
}

TEST(freezewatch_a_frozen_stream_with_a_note_due_fires_once)
{
    FreezeWatch w;
    feed_n(w, kFrozen, 44100 * 2); // two seconds frozen, ending "now" = 10000 ms: the run began at 8000 ms
    CHECK(w.frozen(44100));
    CHECK(!w.check(10000, 7000, 44100)); // the last note went out before the freeze: nothing was due since
    CHECK(!w.check(10000, 9700, 44100)); // a note 300 ms ago: maybe still on its way
    CHECK(w.check(10000, 9400, 44100));  // a note 600 ms ago, during the freeze, nothing came: fire
    CHECK(!w.check(10500, 10000, 44100)); // once per freeze
}

TEST(freezewatch_short_runs_are_normal)
{
    FreezeWatch w;
    int16_t fr[6];
    for (int k = 0; k < 50; k++) {
        feed_n(w, kFrozen, 175); // the longest identical run of a live unit
        live_frame(k, fr);
        fr[0] = 99;
        w.feed(fr);
    }
    CHECK(!w.frozen(44100));
}

TEST(freezewatch_all_zero_never_counts)
{
    // silence with the original's software and the Mac's channels 0/1: exact zeros, not a freeze
    FreezeWatch w;
    const int16_t zero[6] = {0, 0, 0, 0, 0, 0};
    feed_n(w, zero, 44100 * 10);
    CHECK(!w.frozen(44100));
    CHECK(!w.check(20000, 19000, 44100));
}

TEST(freezewatch_a_new_run_can_fire_again)
{
    FreezeWatch w;
    feed_n(w, kFrozen, 44100 * 2);
    CHECK(w.check(10000, 9400, 44100));
    int16_t fr[6];
    live_frame(1, fr);
    w.feed(fr); // the stream moved again (a power cycle, a new stream)
    feed_n(w, kFrozen, 44100 * 2);
    CHECK(w.check(20000, 19400, 44100));
}

TEST(freezewatch_reset_forgets_the_run)
{
    FreezeWatch w;
    feed_n(w, kFrozen, 44100 * 2);
    w.reset(); // a new stream
    feed_n(w, kFrozen, 100);
    CHECK(!w.frozen(44100));
}
