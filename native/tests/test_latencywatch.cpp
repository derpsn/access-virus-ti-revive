// latencywatch.h (#37, #33, E544): Logic was "slightly out of sync" after inserting the plug-in, reopening the project or
// restarting Logic, and in sync after a buffer size change. The plug-in's latency follows the host's block (stamprule.h
// reported_frames), and a host that read it before its last setupProcessing keeps a wrong value. The plug-in then
// announces the change once more from its UI timer, after the host's own set-up, and logs what the host read.
#include "check.h"
#include "device/latencywatch.h"

using namespace vti;

TEST(latencywatch_announces_a_latency_the_host_did_not_read)
{
    LatencyWatch w;
    CHECK(!w.announce(false, 4096, 5632)); // not active: the host reads it at the activation
    CHECK(!w.announce(true, -1, 5632));    // the host never read one: nothing it could have wrong
    CHECK(!w.announce(true, 5632, 5632));  // read and still right
    CHECK(w.announce(true, 4096, 5632));   // read at block 512, the block is now 2048: once
    CHECK(!w.announce(true, 4096, 5632));  // not again while nothing changes (a host that ignores it: no flood)
    CHECK(!w.announce(true, 5632, 5632));  // the host read the new one
}

TEST(latencywatch_a_new_mismatch_is_announced_again)
{
    LatencyWatch w;
    CHECK(w.announce(true, 4096, 5632));
    CHECK(w.announce(true, 4096, 4608)); // another value: announced
    CHECK(!w.announce(true, 4096, 4096)); // back to what the host has: nothing
    CHECK(w.announce(true, 4096, 4608));  // and away again: announced again
}

TEST(latencywatch_logs_each_new_value_the_host_read)
{
    LatencyWatch w;
    CHECK(!w.read_changed(-1));
    CHECK(w.read_changed(4096));
    CHECK(!w.read_changed(4096));
    CHECK(w.read_changed(5632));
}
