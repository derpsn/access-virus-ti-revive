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

// The live path, like the original (Control Center option live-notes, E400, E449). A note the host flags as played live
// (Event::kIsLive; Ableton Live flags played notes, not clip notes, E448) gets no lead: the original stamps it from the
// Virus' position, offset -1 x the rate ratio, 0x400 frames behind (0x1800a8420), which the DSP plays at once (E247).
// A new, lower F5, nothing clamps it, and by the flag alone for note-on and note-off alike; so the safety net below does
// not touch it and it does not move the net's memory. In VST3 hosts the flag is the only way in: the original's offset
// rule is off there (E400 (3)).
inline bool is_live_note(bool option, int event_flags)
{
    return option && (event_flags & 1); // Event::kIsLive
}

inline int64_t live_stamp(int64_t newest, int64_t marker, double ratio)
{
    return newest - marker + (int64_t)(-1.0 * ratio) - 0x400;
}

// Our one difference from the original's live path, by the user's word 2026-11-07: a flagged note-off goes at once only
// when the last note-on of that key went at once. The original goes by the flag alone, so a flagged note-off of a clip
// note still waiting on its lead lands first and the note hangs (simulator S065 C4, from about 35 ms before its due;
// not seen on a real unit). The note-on side stays the original's. Audio thread: fixed table, no allocation.
class LiveNoteGuard {
public:
    bool note_on(int channel, int key, bool flagged)
    {
        live_[channel & 15][key & 127] = flagged;
        return flagged;
    }
    bool note_off(int channel, int key, bool flagged)
    {
        bool &l = live_[channel & 15][key & 127];
        const bool now = flagged && l;
        l = false;
        return now;
    }

private:
    bool live_[16][128] = {};
};

// With the live path the controllers go as in the original: CC, aftertouch, program, pressure, bend have no stamp of their
// own and inherit the last F5 (0x1800a8452): at once behind a live note, waiting with a sequenced one.
inline bool inherits_stamp(bool option, uint8_t status)
{
    return option && status >= 0xA0 && status < 0xF0;
}

// Live Mode and Plugin Latency, the original's switches (0x18002e170, E449). Plugin Latency N = 0..3 sets the lead to
// N x 1024 frames, 3 (the default) being the 3072 we use anyway. Live Mode on, or N = 0, sends no lead and no F5 at all,
// sequenced notes too, and the original then reports only 2X; we report what an unstamped note really takes instead
// (LIVE_A: about 512 frames from send to the ring, measured E450), so the host's alignment stays true (the user's
// choice, 2026-11-07). Plugin Latency counts only with Live Mode off, as in the original (0x18002e2ca).
constexpr int LIVE_A = 512;
struct TimingMode {
    int a;       // the lead A in frames
    bool stamps; // F5 time stamps sent
};
inline TimingMode timing_mode(bool live_mode, int plugin_latency, int base_a)
{
    if (live_mode || plugin_latency == 0)
        return {LIVE_A, false};
    if (plugin_latency == 1 || plugin_latency == 2)
        return {plugin_latency * 1024, true};
    return {base_a, true};
}

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
