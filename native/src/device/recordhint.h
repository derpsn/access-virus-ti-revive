// recordhint.h - the once-only LCD hint for recording a played part (#39, user 2026-11-15: "the hint is ok").
// A VST3 plug-in can report one latency; it is the sequenced notes' (lead + block + 512), and a note the host flags as
// played skips the lead. So Ableton Live puts a take played through a monitored track about 55 ms later than it was
// heard (E518 run A); with Live Mode on (no stamps, the small latency) it lands where it sounded (run B). The original's
// VST3 has the same limit (only its VST2 told Live a second latency, E516), and Live Mode is the way out, as with the
// original: the plug-in only says so, once, and changes no timing. Audio thread: no allocation, no lock.
#pragma once

namespace vti {

class RecordHint {
public:
    // a note-on: `recording` = the host's transport records, `played` = flagged as played and sent without the lead
    // (is_live_note), `live_mode` = Live Mode / Plugin Latency Minimal in force (no stamps). True once: show the hint.
    bool note_on(bool recording, bool played, bool live_mode)
    {
        if (shown_ || !recording || !played || live_mode)
            return false;
        shown_ = true;
        return true;
    }
    bool shown() const { return shown_; }

private:
    bool shown_ = false;
};

} // namespace vti
