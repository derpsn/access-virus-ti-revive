// Which messages to the Virus get their own F5 time stamp (stamprule.h, round 0.8.27 T3): everything the DSP
// schedules (channel messages, clock, Active Sensing, song position, the sync ping) and, new, a parameter SysEx
// that process() sends for host automation - so it lands at its block time on every platform instead of
// inheriting the previous stamp (on Windows up to ~1400 frames apart). Queued messages (editor, state, dumps)
// keep inheriting: a "late" stamp on them would move the DSP's newest-stamp register and can raise a Sync
// Information Event (E251).
#include "check.h"
#include "device/stamprule.h"

using namespace vti;

static bool stamped(std::initializer_list<uint8_t> m, bool from_process)
{
    std::vector<uint8_t> v(m);
    return needs_stamp(v.data(), v.size(), from_process);
}

TEST(stamprule_channel_and_system_realtime)
{
    CHECK(stamped({0x90, 60, 100}, true));
    CHECK(stamped({0x80, 60, 0}, false)); // a note from another thread (all notes off) still needs its stamp
    CHECK(stamped({0xB0, 1, 64}, true));
    CHECK(stamped({0xE0, 0, 64}, true));
    CHECK(stamped({0xF8}, true));
    CHECK(stamped({0xFE}, true));
    CHECK(stamped({0xF2, 0, 0}, true));
    CHECK(stamped({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x73, 0x00, 0x22, 0x00, 0xF7}, true)); // the sync ping
    CHECK(stamped({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x73, 0x00, 0x22, 0x00, 0xF7}, false));
}

TEST(stamprule_parameter_sysex_only_from_process)
{
    const std::initializer_list<uint8_t> cutoff = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x70, 0x00, 0x28, 0x40, 0xF7};
    CHECK(stamped(cutoff, true));   // automation: stamped at its block time
    CHECK(!stamped(cutoff, false)); // the editor's knob: inherits
    CHECK(stamped({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x72, 0x03, 0x25, 0x40, 0xF7}, true)); // a part parameter
    CHECK(stamped({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x6E, 0x00, 0x10, 0x01, 0xF7}, true)); // page C
}

TEST(stamprule_dumps_and_requests_never)
{
    CHECK(!stamped({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x10, 0x00, 0x00, 0x00, 0xF7}, true)); // a single dump
    CHECK(!stamped({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x30, 0x01, 0x05, 0xF7}, true));       // a request
    CHECK(!stamped({0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7}, true));                               // identity request
    CHECK(!stamped({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x73, 0x00, 0x10, 0x00, 0xF7}, true)); // a global: host CPU
    CHECK(!stamped({0xF7}, true));
    CHECK(!stamped({}, true));
}

// Ping-shaped messages that are not the Access ping (mutation run 2026-10-20: each header check of the Access
// SysEx could be weakened without a test noticing): a stamp on them would put an F5 in front of foreign data.
TEST(stamprule_only_the_access_header_gets_the_ping_stamp)
{
    CHECK(!stamped({0xF0, 0x01, 0x20, 0x33, 0x01, 0x10, 0x73, 0x00, 0x22, 0x00, 0xF7}, false)); // manufacturer 01..
    CHECK(!stamped({0xF0, 0x00, 0x21, 0x33, 0x01, 0x10, 0x73, 0x00, 0x22, 0x00, 0xF7}, false)); // 00 21 ..
    CHECK(!stamped({0xF0, 0x00, 0x20, 0x34, 0x01, 0x10, 0x73, 0x00, 0x22, 0x00, 0xF7}, false)); // 00 20 34
    CHECK(!stamped({0xF4, 0x00, 0x20, 0x33, 0x01, 0x10, 0x73, 0x00, 0x22, 0x00, 0xF7}, false)); // not a SysEx
    const uint8_t ping[] = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x73, 0x00, 0x22, 0x00, 0xF7};
    CHECK(!needs_stamp(ping, 10, false)); // shorter than a parameter message: its bytes past n are not read
}

TEST(stamprule_one_stamp_per_value_within_a_write)
{
    // the DSP keeps the current stamp for every byte that follows it (E261), so a block's events share one F5:
    // 16 B instead of 12 B per automation message at the 250/s cap is the whole 4 kB/s budget (hardware run
    // 2026-10-19: notes and the probe starved behind stamped automation)
    StampRun run;
    CHECK(run.fresh(100));
    CHECK(!run.fresh(100)); // the same block: no second F5
    CHECK(run.fresh(101));
    CHECK(run.fresh(100));  // back to an earlier value: the DSP's current stamp moved, so it is stamped again
    StampRun next;          // every USB write starts fresh (the DSP got other traffic in between)
    CHECK(next.fresh(100));
}

// Round 0.8.35 (E363, E364): late notes in Logic. The original stamps every note from the Virus' position now plus a
// fixed lead of 3072 frames, so a late or bursty host block cannot make a note late. TI Revive stamps from its
// timeline; the safety net keeps each stamp at least min_ahead frames past the newest ring frame, and in order.
TEST(stamp_floor_keeps_the_reserve)
{
    StampFloor f;
    CHECK(f.apply(10000, 8000, 1024) == 10000 && f.moved == 0); // 2000 ahead: unchanged
    CHECK(f.apply(10500, 10000, 1024) == 11024 && f.moved == 1); // only 500 ahead: moved to newest + 1024
    CHECK(f.apply(11000, 9000, 1024) == 11024 && f.moved == 1);  // reserve kept, but behind the moved one: in order
    CHECK(f.apply(12000, 10000, 1024) == 12000);                // ahead again: unchanged
    CHECK(f.apply(9000, -1, 1024) == 12000);                    // no newest frame known: only the order rule
    CHECK(f.apply(11500, 10000, 1024) == 12000);                // under a second behind: still in order
    CHECK(f.apply(200000, 150000, 1024) == 200000);
    CHECK(f.apply(500, 100, 1024) == 1124 && f.moved == 2);     // a new timeline far behind (reconnect): accepted
}

TEST(stamp_lead_by_mode)
{
    CHECK(timing_lead(false, false) == 3072 && timing_lead(false, true) == 3072); // like the original, everywhere
    CHECK(timing_lead(true, false) == 1024 && timing_lead(true, true) == 3072);   // classic: as before 0.8.35
}

// the reported delay follows the host block all the way down, like the original's 3584 + block (E402); until 0.8.39
// it never went below a 256-sample block (max(block, 256)), which gave away up to 224 frames at small buffers (user,
// 2026-11-08: "the same logic the original has")
TEST(stamp_reported_delay_follows_the_block)
{
    // since 0.8.39 the read margin is the original's 512 (E465): at lead 3072 exactly its 3584 + block
    CHECK(reported_frames(3072, 256, 0) == 3584 + 256);
    CHECK(reported_frames(3072, 64, 0) == 3584 + 64);
    CHECK(reported_frames(3072, 32, 0) == 3584 + 32);
    CHECK(reported_frames(3072, 512, 0) == 3584 + 512);
    CHECK(reported_frames(1088, 64, 34) == 1088 + 64 + 512 + 34); // the resampler's look-ahead on top
    CHECK(reported_frames(3072, 64, 0, 640) == 3072 + 64 + 640);  // the margin before (VTI_NATIVE_MARGIN=640)
    CHECK(reported_frames(3072, 32, 0) < reported_frames(3072, 64, 0)); // smaller buffer, smaller delay
}

// Messages not from process() (the panic, the editor, the output delays at Play) are not part of a DAW block: they
// get their own stamp and never move the safety net. With live-notes on (the default since 0.8.39) the panic's CC 123
// took over the last F5 (E472), and an unstamped queued SysEx moved the net's last stamp so the first note after Play
// came a block late under bursting hosts (simulator S097 = S068)
TEST(stamp_plan_queued_messages_own_stamp_no_net)
{
    const uint8_t cc123[3] = {0xB0, 0x7B, 0x00}, note[3] = {0x90, 60, 100};
    const uint8_t outdelay[11] = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x6F, 0x00, 0x7F, 0x00, 0xF7};
    // from process with live-notes on: a controller inherits the last F5, as the original's live path
    StampPlan p = stamp_plan(cc123, 3, true, true, false);
    CHECK(p.inherits && !p.stamp && !p.net);
    // the same controller from the queue (panic): its own stamp through the net, even with live-notes on
    p = stamp_plan(cc123, 3, false, true, false);
    CHECK(!p.inherits && p.stamp && p.net);
    // a queued SysEx that gets no stamp must not touch the net either
    p = stamp_plan(outdelay, 11, false, true, false);
    CHECK(!p.stamp && !p.net);
    // notes from process: stamped through the net as before
    p = stamp_plan(note, 3, true, true, false);
    CHECK(!p.inherits && p.stamp && p.net);
    // Live Mode (no stamps at all, as the original): nothing stamped, nothing through the net
    p = stamp_plan(note, 3, true, false, true);
    CHECK(p.inherits && !p.stamp && !p.net);
}

// The experimental lower lead (Control Center option low-latency, 0.8.41, E496): per host block and Virus rate, the
// values measured exact in 10 or more sessions each on the real TI2; above block 512 nothing was measured, so the
// default lead stays there
TEST(stamprule_experimental_lead_per_block_and_rate)
{
    CHECK_EQ(experimental_lead(32, 44100), 1152);
    CHECK_EQ(experimental_lead(64, 44100), 1152);
    CHECK_EQ(experimental_lead(100, 44100), 1280);
    CHECK_EQ(experimental_lead(128, 44100), 1280);
    CHECK_EQ(experimental_lead(256, 44100), 1344);
    CHECK_EQ(experimental_lead(512, 44100), 1408);
    CHECK_EQ(experimental_lead(512, 48000), 1792); // 48 kHz b512: the net fires up to ~1700 (err max 671)
    CHECK_EQ(experimental_lead(64, 48000), 1152);
    CHECK_EQ(experimental_lead(1024, 44100), 3072); // not measured: the default
    CHECK_EQ(experimental_lead(0, 44100), 3072);    // block unknown yet: the default
}

TEST(stamprule_timing_mode_takes_the_experimental_lead_as_its_base)
{
    // the option only changes the base: Live Mode and Plugin Latency 1/2 still decide over it
    CHECK_EQ(timing_mode(false, 3, experimental_lead(128, 44100), 128).a, 1280);
    CHECK(timing_mode(true, 3, experimental_lead(128, 44100), 128).a == LIVE_A);
}

// The safety net's edges. Ring frame 0 is a real newest frame (the first one of a stream), so a stamp behind it gets
// the reserve too; a stamp exactly at the reserve is not "moved" (the count goes to diag.log and would report moves
// that did not happen); and a jump of exactly one second back (48000 frames) is a new timeline, accepted as it is,
// while one frame less is still held in order (a stamp behind the DSP's newest one can raise a Sync Information
// Event, E251)
TEST(stamp_floor_edges)
{
    StampFloor f;
    CHECK(f.apply(10, 0, 1024) == 1024 && f.moved == 1); // newest frame 0: the reserve still counts
    StampFloor g;
    CHECK(g.apply(9024, 8000, 1024) == 9024 && g.moved == 0); // exactly at the reserve: not moved
    StampFloor h;
    CHECK(h.apply(100000, -1, 1024) == 100000);
    CHECK(h.apply(100000 - 47999, -1, 1024) == 100000);         // just under a second behind: in order
    CHECK(h.apply(100000 - 48000, -1, 1024) == 100000 - 48000); // a full second behind: a new timeline
}

// An empty message from process() has no status byte: the plan must not read one (a stale byte in the buffer would
// make it inherit the last F5), it gets neither its own stamp nor the inherited one
TEST(stamp_plan_empty_message_reads_nothing)
{
    const uint8_t stale[3] = {0xB0, 0x7B, 0x00};
    StampPlan p = stamp_plan(stale, 0, true, true, false);
    CHECK(!p.inherits && !p.stamp && !p.net);
}

// #39 (E512): a live note was stamped from the ring's newest frame at send time. On macOS Core Audio fills the ring in
// bursts of several hundred frames, so the same played rhythm sounded 10.5 / 13.2 / 15.9 / 18.5 ms late in turn (klog,
// our Mac, 2026-11-13). Stamped from the note's place on the smoothed timeline (st + S stands for the newest frame the
// timeline follows), the spacing stays the spacing the player played.
TEST(stamprule_live_stamp_from_the_timeline_is_steady)
{
    const int S = 5000;
    const int64_t mk = 1000;
    const double ratio = 1.0;
    // the same frame as the ring-based stamp when the ring is exactly where the timeline expects it
    CHECK(live_stamp_tl(100000, S, mk, ratio) == live_stamp(100000 + S, mk, ratio));
    // notes every 24000 frames on the timeline; the ring's newest frame lags by 0..384 frames in turn (bursts)
    const int64_t lag[4] = {0, 128, 256, 384};
    int64_t prev_tl = -1, prev_ring = -1;
    bool ring_uneven = false;
    for (int i = 0; i < 8; i++) {
        const int64_t tl = 200000 + 24000 * i;
        const int64_t a = live_stamp_tl(tl, S, mk, ratio), b = live_stamp(tl + S - lag[i % 4], mk, ratio);
        if (prev_tl >= 0) {
            CHECK(a - prev_tl == 24000);       // the played spacing, exactly
            ring_uneven |= b - prev_ring != 24000; // the old rule passed the bursts on
        }
        prev_tl = a, prev_ring = b;
    }
    CHECK(ring_uneven);
}
