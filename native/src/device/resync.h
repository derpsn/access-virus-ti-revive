// resync.h - developer switch VTI_NATIVE_RESYNC=<s> for the simulator's S094 test: a new sync ping (re-anchor) every
// <s> seconds, so one session gives several syncs and shows whether a one-block-late session (S094/S095) sits in an
// anchor (it changes at a re-sync) or in the unit (it holds through all of them). The original pings again within a
// session too (marker missing, position jumps, stamps going back, rate switches), always after about a second with
// the Virus' outputs quiet (E491); this waits for the same quiet. Off (period 0) in every normal session.
#pragma once
#include <cstdint>

namespace vti {

struct ResyncTimer {
    int64_t period = 0; // in Virus frames; 0 = off
    int64_t last = -1;  // Virus frame (ring position) of the last anchor, -1 = none yet

    void start(int64_t newest) { last = newest; } // every anchor restarts the count
    // quiet: the outputs have been silent for a second (process()'s `quiet`)
    bool due(int64_t newest, bool quiet) const { return period > 0 && last >= 0 && quiet && newest - last >= period; }
};

} // namespace vti
