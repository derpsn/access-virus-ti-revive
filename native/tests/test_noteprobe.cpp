// What hosts really send for a note (#30, E403): flags and offset per note in diag.log, and Apple's dual scheduling
// mode, which tells a note played live from a sequenced one in an Audio Unit.
#include "check.h"
#include "device/noteprobe.h"

#include <string>
#include <vector>

using namespace vti;

static NoteProbe::Rec rec(int pitch, int offset = 0)
{
    NoteProbe::Rec r{};
    r.on = true;
    r.channel = 0;
    r.pitch = (uint8_t)pitch;
    r.flags = 1;
    r.offset = offset;
    r.frames = 128;
    r.playing = true;
    return r;
}

TEST(noteprobe_gives_records_back_in_order)
{
    NoteProbe p;
    CHECK(p.push(rec(60)));
    CHECK(p.push(rec(61)));
    std::vector<int> got;
    p.drain([&](const NoteProbe::Rec &r) { got.push_back(r.pitch); });
    CHECK_EQ(got.size(), 2u);
    CHECK_EQ(got[0], 60);
    CHECK_EQ(got[1], 61);
}

TEST(noteprobe_drained_records_are_gone)
{
    NoteProbe p;
    p.push(rec(60));
    int n = 0;
    p.drain([&](const NoteProbe::Rec &) { n++; });
    p.drain([&](const NoteProbe::Rec &) { n++; });
    CHECK_EQ(n, 1);
}

TEST(noteprobe_full_drops_and_counts_it_never_blocks)
{
    NoteProbe p;
    int pushed = 0;
    for (int i = 0; i < NoteProbe::SIZE + 10; i++)
        pushed += p.push(rec(60)) ? 1 : 0;
    CHECK_EQ(pushed, NoteProbe::SIZE - 1);
    CHECK_EQ(p.dropped(), 11u);
}

TEST(noteprobe_wraps_around)
{
    NoteProbe p;
    int n = 0;
    for (int round = 0; round < 5; round++) {
        for (int i = 0; i < NoteProbe::SIZE - 1; i++)
            CHECK(p.push(rec(i & 127)));
        p.drain([&](const NoteProbe::Rec &) { n++; });
    }
    CHECK_EQ(n, 5 * (NoteProbe::SIZE - 1));
    CHECK_EQ(p.dropped(), 0u);
}

TEST(noteprobe_line_has_everything_a_report_needs)
{
    NoteProbe::Rec r = rec(60, 17);
    const std::string s = noteprobe_line(r, 1024);
    CHECK(s == "note on ch 1 pitch 60 flags 0x1 offset 17 frames 128 largest 1024 playing 1");
}

TEST(noteprobe_line_shows_a_note_off_and_a_raw_offset)
{
    NoteProbe::Rec r = rec(61, 0x01000040); // a host setting the scheduled bit without the mode: the raw value shows it
    r.on = false;
    r.flags = 0;
    r.playing = false;
    const std::string s = noteprobe_line(r, 512);
    CHECK(s == "note off ch 1 pitch 61 flags 0x0 offset 16777280 frames 128 largest 512 playing 0");
}
