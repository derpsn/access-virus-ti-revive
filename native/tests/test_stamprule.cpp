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
