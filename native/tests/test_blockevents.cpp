// The events process() collects for one block (device/blockevents.h): sorted by stamp, at most MAX; what does not fit
// is counted (it was dropped without a trace before - "missing notes" report, 2026-10-24) and a lost note-off is
// flagged so nothing keeps sounding. Hardware-free.
#include "check.h"
#include "../src/device/blockevents.h"

using vti::BlockEvents;

namespace {
const uint8_t ON[3] = {0x90, 60, 100}, OFF[3] = {0x80, 60, 0}, ON0[3] = {0x90, 60, 0}, CC[3] = {0xB0, 1, 64};
}

TEST(blockevents_keeps_up_to_max)
{
    static BlockEvents b; // ~8 KB: not on the test's stack
    b.clear();
    for (int i = 0; i < BlockEvents::MAX; i++)
        CHECK(b.add(i, ON, 3));
    CHECK(b.n == BlockEvents::MAX);
    CHECK(b.dropped == 0);
    CHECK(!b.dropped_off);
}

TEST(blockevents_counts_what_does_not_fit)
{
    static BlockEvents b;
    b.clear();
    for (int i = 0; i < BlockEvents::MAX + 5; i++)
        b.add(i, CC, 3);
    CHECK(b.n == BlockEvents::MAX);
    CHECK(b.dropped == 5);
    CHECK(!b.dropped_off); // no note-off among them
    uint8_t long_msg[17] = {0xF0};
    b.clear();
    CHECK(!b.add(0, long_msg, sizeof long_msg)); // longer than a slot: dropped, counted
    CHECK(b.dropped == 1 && b.n == 0);
}

TEST(blockevents_flags_a_lost_note_off)
{
    static BlockEvents b;
    b.clear();
    for (int i = 0; i < BlockEvents::MAX; i++)
        b.add(i, ON, 3);
    CHECK(!b.add(0, ON, 3));
    CHECK(!b.dropped_off); // a lost note-on hangs nothing
    CHECK(!b.add(0, OFF, 3));
    CHECK(b.dropped_off);
    b.clear();
    CHECK(b.n == 0 && b.dropped == 0 && !b.dropped_off);
    for (int i = 0; i < BlockEvents::MAX; i++)
        b.add(i, ON, 3);
    b.add(0, ON0, 3); // note-on with velocity 0 is a note-off
    CHECK(b.dropped_off);
}

TEST(blockevents_sorts_by_stamp_keeping_the_order_of_equal_stamps)
{
    static BlockEvents b;
    b.clear();
    b.add(30, ON, 3);
    b.add(10, CC, 3);
    b.add(30, OFF, 3);
    b.add(20, ON, 3);
    b.sort();
    CHECK(b.ev[0].stamp == 10 && b.ev[1].stamp == 20 && b.ev[2].stamp == 30 && b.ev[3].stamp == 30);
    CHECK(b.ev[2].m[0] == 0x90 && b.ev[3].m[0] == 0x80); // the note-on stays before its note-off
}
