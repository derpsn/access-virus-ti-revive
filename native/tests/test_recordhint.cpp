// The recording hint (device/recordhint.h, #39, user 2026-11-15: "the hint is ok"): a take played through the plug-in
// in Ableton Live comes back about 55 ms later than heard unless Live Mode is on (E518), as with the original's VST3.
// The plug-in says so once on its LCD when the host records, a note flagged as played arrives and Live Mode is off.
// Hardware-free.
#include "check.h"
#include "device/recordhint.h"

using namespace vti;

TEST(recordhint_once_when_recording_a_played_note_without_live_mode)
{
    RecordHint h;
    CHECK(!h.note_on(false, true, false)); // playing along, not recording: nothing
    CHECK(!h.note_on(true, false, false)); // recording a clip or a note not flagged as played: nothing
    CHECK(!h.note_on(true, true, true));   // Live Mode on: the take lands where it sounded
    CHECK(h.note_on(true, true, false));   // the case of E518
    CHECK(!h.note_on(true, true, false));  // once per plug-in instance
    CHECK(h.shown());
}
