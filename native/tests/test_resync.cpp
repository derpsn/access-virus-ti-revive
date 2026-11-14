// Developer switch VTI_NATIVE_RESYNC=<s> (S094): a new sync ping (re-anchor) every <s> seconds of host time, each
// only when the Virus' outputs have been quiet for a second, as the original does before every ping (E491).
#include "check.h"
#include "device/resync.h"

using namespace vti;

TEST(resync_off_never_due)
{
    ResyncTimer r; // period 0: the switch is not set
    r.start(0);
    CHECK(!r.due(10 * 44100, true));
}

TEST(resync_due_after_the_period_when_quiet)
{
    ResyncTimer r;
    r.period = 15 * 44100;
    r.start(1000); // the first anchor
    CHECK(!r.due(1000 + 15 * 44100 - 1, true));
    CHECK(r.due(1000 + 15 * 44100, true));
}

TEST(resync_waits_for_quiet)
{
    ResyncTimer r;
    r.period = 15 * 44100;
    r.start(0);
    CHECK(!r.due(20 * 44100, false)); // a note still sounds: wait
    CHECK(r.due(21 * 44100, true));
}

TEST(resync_counts_from_the_last_anchor)
{
    // any anchor (start, retry, re-anchor after a jump) restarts the count, so syncs stay a period apart
    ResyncTimer r;
    r.period = 15 * 44100;
    r.start(0);
    r.start(10 * 44100);
    CHECK(!r.due(15 * 44100, true));
    CHECK(r.due(25 * 44100, true));
}

TEST(resync_not_due_before_the_first_anchor)
{
    ResyncTimer r;
    r.period = 44100;
    CHECK(!r.due(100 * 44100, true));
}
