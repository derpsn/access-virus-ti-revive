// stamprule.h - which messages to the Virus get their own F5 time stamp (cable 5) in front of them.
//
// Why: the DSP schedules only what follows a stamp (E235): channel messages, the MIDI clock, Active Sensing, the
// song position and the sync ping always get one. A parameter SysEx is parsed by the DSP from the same byte stream
// (E258: the host CPU stores it but does not forward it), so it lands when its preceding stamp is due. Host
// automation from process() therefore gets its own stamp at the block time, like a note: without it the change
// inherited the previous event's stamp - on Windows (A = 3072) up to ~1400 frames apart from where the host put it.
// Messages from other threads (the editor's knobs, the state, dumps, requests) keep inheriting: they have no
// block time, and a stamp behind the DSP's newest one would move its newest-stamp register and can raise a Sync
// Information Event (E251). Dumps and requests are for the host CPU and never stamped.
#pragma once
#include <cstddef>
#include <cstdint>

namespace vti {

// an Access parameter message of page A-E: F0 00 20 33 01 <dev> 6E..72 <part> <param> <value> F7
inline bool access_param(const uint8_t *m, size_t n)
{
    return n == 11 && m[0] == 0xF0 && m[1] == 0x00 && m[2] == 0x20 && m[3] == 0x33 && m[6] >= 0x6E && m[6] <= 0x72;
}

// m = a complete MIDI message; from_process = sent by process() with the stamp of its block
inline bool needs_stamp(const uint8_t *m, size_t n, bool from_process)
{
    if (n == 0)
        return false;
    if (m[0] >= 0x80 && m[0] < 0xF0) // channel messages
        return true;
    if (m[0] == 0xF8 || m[0] == 0xFE || m[0] == 0xF2) // clock, Active Sensing, song position
        return true;
    if (m[0] != 0xF0 || n < 11 || m[1] != 0x00 || m[2] != 0x20 || m[3] != 0x33) // not an Access SysEx
        return false;
    if (m[6] == 0x73 && m[8] == 0x22) // the sync ping: stamped like the original (3072)
        return true;
    return from_process && access_param(m, n); // a parameter page A-E, from automation
}

// One F5 per stamp value within one USB write: the DSP keeps the current stamp for every byte that follows it
// (E261), so the events of a block share one F5. Each F5 is a 4-byte packet that counts against the 4 kB/s
// budget (sendbudget.h): with one per message, automation at the 250/s cap alone filled the budget and the notes
// and the readiness probe starved behind it (hardware run 2026-10-19). A new StampRun per write: what the DSP
// saw in between is not tracked. The sync ping is exempt (its stamp is raw, the marker logic needs its own F5).
struct StampRun {
    int64_t last = -1;
    bool fresh(int64_t st)
    {
        if (st == last)
            return false;
        last = st;
        return true;
    }
};

// The lead A (frames from the newest ring frame to a note's sound): the original uses 3072 on every host (E364:
// forced at every start, = its reported latency). Before 0.8.35 TI Revive used 1024 on Linux/macOS, which Logic's
// uneven block timing overran (late notes, the sync error, E363); 'classic' keeps that for comparison.
inline int timing_lead(bool classic, bool windows)
{
    return classic && !windows ? 1024 : 3072;
}

// The safety net (0.8.35): a stamp from the timeline can lie in the past when the host hands over a block late. The
// original cannot have that (it stamps from the Virus' position now, E364); here a stamp keeps at least min_ahead
// frames past the newest ring frame, and stamps never go backwards (a stamp behind the DSP's newest one can raise
// a Sync Information Event, E251) - except for a jump of a second or more (a new timeline after a reconnect).
struct StampFloor {
    int64_t last = -1;
    int64_t moved = 0; // stamps moved forward to keep the reserve (logged)
    int64_t apply(int64_t due, int64_t newest, int64_t min_ahead)
    {
        if (newest >= 0 && due < newest + min_ahead) {
            due = newest + min_ahead;
            moved++;
        }
        if (due < last && last - due < 48000)
            due = last;
        last = due;
        return due;
    }
};

} // namespace vti
