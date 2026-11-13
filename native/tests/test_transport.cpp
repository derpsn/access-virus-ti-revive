// When and how the original re-sends the song position (transport.h): at Play, at a loop restart or a jump, and when
// the position goes backwards it writes F2 and the global 73 00 23 vv, both on the next MIDI-clock tick, from the
// host position + 256 quarters, and FC at Stop (Virus Control.dll 5.1.7 x64 0x1800303b0; captures orig-patchchange-2
// F2 20 08 one tick after the last F8, orig-patchchange-3 FC at Stop; E275, E505). TI Revive clamped a count-in
// (negative position) to 0 and stamped the F2 off its free-running F8 grid (#30).
#include "check.h"
#include "device/transport.h"

#include <cmath>

using namespace vti;

static const double FPT = 918.75; // frames per tick at 44.1 kHz, 120 BPM (44100 * 60 / (120 * 24))

TEST(transport_restart_on_play_on_a_jump_and_at_stop)
{
    TransportWatch w;
    CHECK(w.update(false, 0.0, 0.25, 0, 64) == TransportWatch::None); // stopped
    CHECK(w.update(true, 0.0, 0.25, 0, 64) == TransportWatch::Start);  // Play
    CHECK(w.update(true, 0.25, 0.25, 64, 64) == TransportWatch::None); // runs on
    CHECK(w.update(true, 0.5004, 0.25, 128, 64) == TransportWatch::None); // rounding is not a jump
    CHECK(w.update(true, 0.0, 0.25, 192, 64) == TransportWatch::Start);   // loop back to bar 1
    CHECK(w.update(true, 0.25, 0.25, 256, 64) == TransportWatch::None);
    CHECK(w.update(true, 16.0, 0.25, 320, 64) == TransportWatch::Start); // a jump forward
    CHECK(w.update(false, 16.25, 0.25, 384, 64) == TransportWatch::Stop);  // Stop: FC, once
    CHECK(w.update(false, 16.25, 0.25, 448, 64) == TransportWatch::None);
    CHECK(w.update(true, 16.25, 0.25, 512, 64) == TransportWatch::Start); // Play again
}

// the original's limits: a jump of more than 0.05 quarter, or of more than 128 samples and at least one tick (1/24)
TEST(transport_jump_limits_like_the_original)
{
    TransportWatch a;
    a.update(true, 0.0, 0.25, 0, 64);
    CHECK(a.update(true, 0.25 + 0.049, 0.25, 64, 64) == TransportWatch::None);  // under 0.05, samples steady
    TransportWatch b;
    b.update(true, 0.0, 0.25, 0, 64);
    CHECK(b.update(true, 0.25 + 0.051, 0.25, 64, 64) == TransportWatch::Start); // over 0.05
    TransportWatch c;
    c.update(true, 0.0, 0.25, 0, 64);
    CHECK(c.update(true, 0.25 + 0.045, 0.25, 64 + 129, 64) == TransportWatch::Start); // samples jumped, >= 1 tick
    TransportWatch d;
    d.update(true, 0.0, 0.25, 0, 64);
    CHECK(d.update(true, 0.25 + 0.03, 0.25, 64 + 129, 64) == TransportWatch::None); // samples jumped, < 1 tick
    TransportWatch e;
    e.update(true, 0.0, 0.25, 0, 64);
    CHECK(e.update(true, 0.25 + 0.045, 0.25, 64 + 128, 64) == TransportWatch::None); // exactly 128: not over
    TransportWatch f;
    f.update(true, 0.0, 0.25, 0, 64);
    CHECK(f.update(true, 0.25 + 0.045, 0.25, 64 - 129, 64) == TransportWatch::Start); // samples back by 129
    TransportWatch g;
    g.update(true, 0.0, 0.25, 0, 64);
    CHECK(g.update(true, 0.25 - 0.051, 0.25, 64, 64) == TransportWatch::Start); // 0.051 behind due
}

// backwards against the last block's start restarts even inside the jump limit (short blocks)
TEST(transport_backwards_by_a_hair_restarts)
{
    TransportWatch w;
    w.update(true, 4.0, 0.01, 0, 64);
    CHECK(w.update(true, 3.999, 0.01, 64, 64) == TransportWatch::Start); // 0.011 behind due, but backwards
    TransportWatch s;
    s.update(true, 4.0, 0.01, 0, 64);
    CHECK(s.update(true, 4.0, 0.01, 64, 64) == TransportWatch::None); // stands still: not backwards, within 0.05
}

// a count-in runs from -4 through 0 without a restart: the +256 keeps its position valid (#30)
TEST(transport_count_in_runs_through_zero)
{
    TransportWatch w;
    CHECK(w.update(true, -4.0, 0.25, 0, 64) == TransportWatch::Start);
    int restarts = 0;
    for (int i = 1; i < 32; i++)
        restarts += w.update(true, -4.0 + i * 0.25, 0.25, i * 64, 64) == TransportWatch::Start;
    CHECK(restarts == 0);
    TransportWatch r;
    r.update(true, 0.0, 0.25, 0, 64);
    r.reset(); // a new timeline (re-anchor): the next playing block restarts
    CHECK(r.update(true, 0.25, 0.25, 64, 64) == TransportWatch::Start);
    TransportWatch q;
    q.update(true, 0.0, 0.25, 0, 64);
    q.reset(); // reset while playing is no Stop
    CHECK(q.update(false, 0.25, 0.25, 64, 64) == TransportWatch::None);
}

TEST(transport_start_on_a_sixteenth)
{
    const TransportStart s = transport_start(8.0, FPT); // the capture: song position 8 quarters
    CHECK(s.spp == 1056);                               // 264 quarters x 4: F2 20 08
    CHECK(s.vv == 1);
    CHECK(std::fabs(s.offset - (FPT - 1.0)) < 1e-6); // on a tick: the next one, less a frame
    const TransportStart c = transport_start(-4.0, FPT); // a 1-bar count-in
    CHECK(c.spp == 1008);
    CHECK(c.vv == 1);
}

TEST(transport_start_between_ticks)
{
    const TransportStart s = transport_start(8.0 + 2.5 / 24.0, FPT); // 2.5 ticks into the sixteenth
    CHECK(s.spp == 1056);
    CHECK(s.vv == 3);
    CHECK(std::fabs(s.offset - (0.5 * FPT - 1.0)) < 1e-6);
    const TransportStart l = transport_start(8.0 + 5.5 / 24.0, FPT); // in the last tick: rounds up to the next one
    CHECK(l.spp == 1057);
    CHECK(l.vv == 0);
    const TransportStart z = transport_start(8.0 + 0.99999 / 24.0, FPT); // just before a tick: offset not negative
    CHECK(z.offset >= 0.0);
    const TransportStart n = transport_start(-300.0, FPT); // before the +256: clamped like the original
    CHECK(n.spp == 0);
    CHECK(n.vv == 0);
}

// a host position that is no number or absurdly large must not reach the casts: the F8 loop runs to the offset, so a
// garbage offset would hang the audio thread (review 2026-11-13); far before the +256 the offset stays within a tick
TEST(transport_start_survives_bad_positions)
{
    const double bad[] = {std::nan(""), INFINITY, -INFINITY, 1e12, -1e12, -2000.1};
    for (double ppq : bad) {
        const TransportStart s = transport_start(ppq, FPT);
        CHECK(s.spp >= 0 && s.spp < 16384);
        CHECK(s.vv >= 0 && s.vv <= 6);
        CHECK(s.offset >= 0.0 && s.offset <= FPT);
    }
    CHECK(transport_start(std::nan(""), FPT).spp == 0);
}

// the DSP sets its clock to spp x 6 - 1 and adds vv (E275): the pair puts it on the tick before the F2's tick
TEST(transport_start_puts_the_clock_on_the_position)
{
    for (int i = 0; i < 2400; i++) {
        const double ppq = -4.0 + i * 0.00731;
        const TransportStart s = transport_start(ppq, FPT);
        const double ticks = (ppq + 256.0) * 24.0;
        CHECK(s.spp * 6 - 1 + s.vv == (int)ticks);
        CHECK(s.vv >= 0 && s.vv <= 5);
        CHECK(s.offset >= 0.0 && s.offset < FPT);
    }
}

TEST(transport_messages_like_the_original)
{
    auto m = transport_messages(transport_start(8.0 + 2.5 / 24.0, FPT));
    CHECK(m.size() == 2); // F2 and 73 00 23 vv; the 16 part messages are left out (E280)
    CHECK(m[0] == std::vector<uint8_t>({0xF2, 1056 & 0x7F, 1056 >> 7}));
    CHECK(m[1] == std::vector<uint8_t>({0xF0, 0x00, 0x20, 0x33, 0x01, 0x00, 0x73, 0x00, 0x23, 0x03, 0xF7}));
}

// a restart's stamp can lie past the block: it waits for the block that holds it, so nothing goes out of order
TEST(transport_restart_waits_for_its_block)
{
    PendingRestart p;
    CHECK(!p.due(1000));
    p.arm(transport_start(8.0, FPT), 1917);
    CHECK(p.stamp() == 1917);
    CHECK(!p.due(1064)); // the block ending at 1064 does not hold it
    CHECK(!p.due(1917)); // a block ending at the stamp does not either
    CHECK(p.due(1918));  // this one does
    CHECK(p.f2()[0] == 0xF2 && p.f2()[1] == (1056 & 0x7F) && p.f2()[2] == (1056 >> 7));
    CHECK(p.tick()[8] == 0x23 && p.tick()[9] == 1 && p.tick()[10] == 0xF7);
    p.clear();
    CHECK(!p.due(5000));
    p.arm(transport_start(8.0, FPT), 2000);
    p.clear(); // Stop before it went out
    CHECK(!p.due(5000));
    p.arm(transport_start(8.0, FPT), 2000);
    p.shift(-300); // the timeline followed real time (TimelineFollow): the tick moves with it
    CHECK(p.stamp() == 1700);
    CHECK(p.due(1701));
}
