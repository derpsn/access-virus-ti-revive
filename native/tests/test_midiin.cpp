// nativehost --midi-in (tools/midiin.h): the simulator's key tap writes "ns status data1 data2" lines into a FIFO;
// nativehost hands each note to the plug-in as a played (kIsLive) event once ns + delay has passed, as Ableton Live's
// Monitor In passes a played key on (#39). Hardware-free.
#include "check.h"
#include "../tools/midiin.h"

TEST(midiin_parses_complete_lines_only)
{
    MidiInTap tap;
    const char a[] = "1000 144 60 100\n2000 128 60 0\n30", b[] = "00 144 64 90\n";
    tap.feed(a, sizeof a - 1);
    CHECK(tap.pending() == 2);
    tap.feed(b, sizeof b - 1); // the rest of the third line arrives with the next read
    CHECK(tap.pending() == 3);
}

TEST(midiin_releases_events_when_due)
{
    MidiInTap tap;
    const char s[] = "1000 144 60 100\n5000 128 60 0\n";
    tap.feed(s, sizeof s - 1);
    std::vector<MidiInEvent> out;
    tap.take_due(999, 0, out);
    CHECK(out.empty());
    tap.take_due(1000, 0, out);
    CHECK(out.size() == 1 && out[0].status == 144 && out[0].data1 == 60 && out[0].data2 == 100 && out[0].ns == 1000);
    out.clear();
    tap.take_due(4000, 1500, out); // 5000 + 1500 > 4000: not yet
    CHECK(out.empty());
    tap.take_due(6500, 1500, out);
    CHECK(out.size() == 1 && out[0].status == 128 && tap.pending() == 0);
}

TEST(midiin_note_kinds)
{
    CHECK(midiin_is_note_on({0, 0x90, 60, 1}));
    CHECK(!midiin_is_note_on({0, 0x90, 60, 0})); // velocity 0 = note-off
    CHECK(midiin_is_note_off({0, 0x90, 60, 0}));
    CHECK(midiin_is_note_off({0, 0x83, 60, 64}));
    CHECK(!midiin_is_note_on({0, 0xB0, 1, 64}) && !midiin_is_note_off({0, 0xB0, 1, 64})); // others are ignored
    CHECK(midiin_channel({0, 0x9F, 60, 1}) == 15);
}

TEST(midiin_skips_garbage_lines)
{
    MidiInTap tap;
    const char s[] = "hello\n1000 144 60\n1000 300 60 1\n2000 144 61 1\n";
    tap.feed(s, sizeof s - 1);
    CHECK(tap.pending() == 1); // short line, text, status out of range: dropped
}
