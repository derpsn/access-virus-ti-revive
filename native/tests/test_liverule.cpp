// Notes played live skip the lead, as in the original (#30, E400, E448, E449).
#include "check.h"
#include "device/blockevents.h"
#include "device/stamprule.h"

using namespace vti;

TEST(live_note_needs_the_option_and_the_flag)
{
    constexpr int kIsLive = 1; // Steinberg::Vst::Event::kIsLive
    CHECK(is_live_note(true, kIsLive));
    CHECK(!is_live_note(true, 0));
    CHECK(!is_live_note(false, kIsLive));
    CHECK(!is_live_note(false, 0));
    CHECK(is_live_note(true, kIsLive | 0x4000)); // other flag bits do not matter
}

TEST(live_guard_note_off_never_passes_a_waiting_note_on)
{
    // our one difference from the original, by the user's word 2026-11-07: a flagged note-off of a clip note still
    // waiting on its lead would land first and leave that note hanging (simulator S065 C4: from 35 ms before its due)
    LiveNoteGuard g;
    CHECK(!g.note_on(3, 60, false));
    CHECK(!g.note_off(3, 60, true)); // waits with its note-on
    // a played note goes at once, on and off, as in the original
    CHECK(g.note_on(3, 60, true));
    CHECK(g.note_off(3, 60, true));
    // the last note-on of that key decides
    CHECK(g.note_on(4, 62, true));
    CHECK(!g.note_on(4, 62, false));
    CHECK(!g.note_off(4, 62, true));
    // channels and keys apart
    CHECK(g.note_on(5, 64, true));
    CHECK(!g.note_on(6, 64, false));
    CHECK(g.note_off(5, 64, true));
    CHECK(!g.note_off(6, 64, true));
}

TEST(live_guard_unflagged_note_off_stays_unflagged)
{
    LiveNoteGuard g;
    CHECK(g.note_on(0, 60, true));
    CHECK(!g.note_off(0, 60, false)); // a note-off the host did not flag keeps its lead, as in the original
}

TEST(live_guard_range_is_safe)
{
    LiveNoteGuard g;
    CHECK(g.note_on(15, 127, true));
    CHECK(g.note_off(15, 127, true));
    CHECK(g.note_on(31, 255, true)); // masked like the bytes that go to the Virus: channel 15, key 127
    CHECK(g.note_off(15, 127, true));
}

TEST(live_stamp_is_the_originals)
{
    // device position - anchor, offset -1 x the rate ratio, truncated, then 0x400 behind (0x1800a8420, E449)
    CHECK_EQ(live_stamp(100000, 90000, 1.0), 100000 - 90000 - 1 - 1024);
    CHECK_EQ(live_stamp(100000, 90000, 2.0), 100000 - 90000 - 2 - 1024); // host at half the Virus' rate
    CHECK_EQ(live_stamp(100000, 90000, 0.5), 100000 - 90000 - 0 - 1024); // -0.5 truncates to 0
    CHECK_EQ(live_stamp(100000, 90000, 48000.0 / 44100.0), 100000 - 90000 - 1 - 1024);
}

TEST(live_stamp_has_no_floor)
{
    // nothing clamps it: below the Virus' clock's zero it wraps in the F5's 14 bits like any stamp
    CHECK_EQ(live_stamp(90500, 90000, 1.0), 500 - 1 - 1024);
    CHECK_EQ((live_stamp(90500, 90000, 1.0) & 0x7F), ((16384 - 525) & 0x7F));
    CHECK_EQ(((live_stamp(90500, 90000, 1.0) >> 7) & 0x7F), (((16384 - 525) >> 7) & 0x7F));
}

TEST(controllers_inherit_the_stamp_with_the_live_path)
{
    // CC, bend, aftertouch, program have no stamp of their own in the original (0x1800a8452, E449)
    for (uint8_t s : {0xA0, 0xB0, 0xC0, 0xD0, 0xE0, 0xB5, 0xEF}) {
        CHECK(inherits_stamp(true, s));
        CHECK(!inherits_stamp(false, s));
    }
    for (uint8_t s : {0x80, 0x90, 0x8F, 0x9F, 0xF8, 0xFE, 0xF2, 0xF0}) // notes, clock, sensing, position, SysEx
        CHECK(!inherits_stamp(true, s));
}

TEST(a_live_note_leaves_the_floor_alone)
{
    // no call to the floor for a live note: the sequenced stamps' memory stays, so the next clip note is where it was
    StampFloor f;
    CHECK_EQ(f.apply(10000, 7000, 1024), 10000);
    (void)live_stamp(7000, 0, 1.0);
    CHECK_EQ(f.apply(10100, 7100, 1024), 10100);
    CHECK_EQ(f.moved, 0);
}

TEST(blockevents_keeps_the_live_mark_through_the_sort)
{
    BlockEvents e;
    const uint8_t a[3] = {0x90, 60, 100}, b[3] = {0x90, 62, 100}, c[3] = {0x90, 64, 100};
    e.add(300, a, 3);
    e.add(100, b, 3, true);
    e.add(200, c, 3);
    e.sort();
    CHECK_EQ(e.n, 3);
    CHECK_EQ(e.ev[0].m[1], 62);
    CHECK(e.ev[0].live);
    CHECK(!e.ev[1].live);
    CHECK(!e.ev[2].live);
}
