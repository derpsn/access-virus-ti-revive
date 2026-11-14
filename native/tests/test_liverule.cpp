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

// a played note-on of a key whose sequenced note-off still waits for its stamp would be cut by that off (E456: from
// 15 ms before the off's stamp, on the real unit); it goes stamped behind the off instead, late in that rare case and
// never silent; its note-off follows it (the last note-on decides). pos = timeline position, window = how far back a
// sequenced note-off can still be waiting (the lead difference between the two paths)
TEST(live_guard_played_note_waits_behind_a_pending_clip_off)
{
    LiveNoteGuard g;
    CHECK(!g.note_on(0, 60, false, 0, 4096));
    CHECK(!g.note_off(0, 60, false, 24000));        // the clip's note-off, stamped with the lead
    CHECK(!g.note_on(0, 60, true, 24000 + 672, 4096)); // played 14 ms later: would be cut, so it waits
    CHECK(!g.note_off(0, 60, true, 24000 + 15072));    // its off follows it
    CHECK(g.note_on(0, 60, true, 24000 + 5472 + 24000, 4096)); // long after: at once, as in the original
    CHECK(!g.note_off(1, 62, false, 50000));
    CHECK(g.note_on(1, 62, true, 50000 + 4096, 4096)); // the window has passed: at once
    CHECK(g.note_on(2, 62, true, 50001, 4096));        // another channel: no pending off there
    CHECK(g.note_on(1, 63, true, 50001, 4096));        // another key: none either
}

// the window the plug-in passes: only while the off still has more than 5 ms to its stamp (E456: cut from 15 ms, 1 of
// 33 at 10 ms, none at 0) - a wider one sent played notes at the clip delay with nothing to protect (simulator S087)
TEST(live_guard_window_ends_before_the_offs_stamp)
{
    CHECK(live_guard_window(3072, 48000) == 3072 - 240);
    CHECK(live_guard_window(3072, 44100) == 3072 - 220);
    CHECK(live_guard_window(200, 48000) == 0); // a lead shorter than the margin: nothing can wait, no window
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

TEST(timing_mode_normal_keeps_the_lead_and_stamps)
{
    // Plugin Latency 3 (the default) = the lead we use today, with stamps
    const TimingMode m = timing_mode(false, 3, 3072);
    CHECK_EQ(m.a, 3072);
    CHECK(m.stamps);
    CHECK_EQ(timing_mode(false, 3, 1024).a, 1024); // timing-classic or VTI_NATIVE_A stay the base
}

TEST(timing_mode_plugin_latency_is_n_times_1024)
{
    // the original: N x 1024 frames (0x18002e170, E449)
    CHECK_EQ(timing_mode(false, 1, 3072).a, 1024);
    CHECK(timing_mode(false, 1, 3072).stamps);
    CHECK_EQ(timing_mode(false, 2, 3072).a, 2048);
    CHECK(timing_mode(false, 2, 3072).stamps);
}

TEST(timing_mode_plugin_latency_keeps_three_blocks_of_lead)
{
    // E476/E477: at Plugin Latency 1 (lead 1024) and host block 512 the safety net moved every 4th chord 5-7 ms late
    // in half the sessions; the simulator puts the clear point near 3 blocks (about 1536 at block 512)
    CHECK_EQ(timing_mode(false, 1, 3072, 512).a, 1536);
    CHECK_EQ(timing_mode(false, 1, 3072, 256).a, 1024); // 768 < 1024: the original's value stays
    CHECK_EQ(timing_mode(false, 1, 3072, 64).a, 1024);
    CHECK_EQ(timing_mode(false, 2, 3072, 512).a, 2048);
    CHECK_EQ(timing_mode(false, 2, 3072, 1024).a, 3072);
    CHECK(timing_mode(false, 1, 3072, 512).stamps);
    // the default keeps the original's lead at every block (its reported delay stays 3584 + block)
    CHECK_EQ(timing_mode(false, 3, 3072, 2048).a, 3072);
    // no stamps, no net: Live Mode and Minimal keep LIVE_A
    CHECK_EQ(timing_mode(true, 1, 3072, 2048).a, LIVE_A);
    CHECK_EQ(timing_mode(false, 0, 3072, 2048).a, LIVE_A);
    CHECK_EQ(timing_mode(false, 1, 3072, 0).a, 1024); // block not known yet
}

TEST(timing_mode_live_and_minimal_send_no_stamps)
{
    // Live Mode on, or Plugin Latency 0 (Minimal): no lead and no F5, as in the original; A = what an unstamped note
    // really takes to sound, measured (E450), so the reported latency stays true (user's choice A, 2026-11-07)
    for (const TimingMode m : {timing_mode(true, 3, 3072), timing_mode(true, 1, 3072), timing_mode(false, 0, 3072)}) {
        CHECK_EQ(m.a, LIVE_A);
        CHECK(!m.stamps);
    }
    CHECK_EQ(LIVE_A, 512);
}

TEST(timing_mode_plugin_latency_only_counts_with_live_off)
{
    // the original applies Plugin Latency only with Live Mode off (0x18002e2ca)
    CHECK_EQ(timing_mode(true, 2, 3072).a, timing_mode(true, 3, 3072).a);
}

TEST(timing_mode_out_of_range_is_the_default)
{
    CHECK_EQ(timing_mode(false, -1, 3072).a, 3072);
    CHECK_EQ(timing_mode(false, 7, 3072).a, 3072);
}

// A window of 0 turns the protection off (a lead too short to protect anything, live_guard_window): a played note then
// goes at once, also when its position lies before the clip's note-off (the timeline jumped back, a loop)
TEST(live_guard_no_window_no_wait)
{
    LiveNoteGuard g;
    CHECK(!g.note_off(0, 60, false, 50000));
    CHECK(g.note_on(0, 60, true, 1000, 0));
}

// The right to go at once belongs to one played note-on and is used up by its note-off: a further flagged note-off of
// that key (here after the clip's own note-off) has no played note-on of its own and goes with the lead
TEST(live_guard_note_off_uses_up_the_live_note_on)
{
    LiveNoteGuard g;
    CHECK(g.note_on(0, 60, true));
    CHECK(g.note_off(0, 60, true));
    CHECK(!g.note_off(0, 60, true)); // a second flagged off: no played note-on left
    CHECK(g.note_on(1, 61, true));
    CHECK(!g.note_off(1, 61, false)); // the clip's note-off goes with the lead
    CHECK(!g.note_off(1, 61, true));  // and uses the played note-on up too
}
