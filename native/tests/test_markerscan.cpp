// MarkerScan (DeviceOut::scan_audio's sync-marker bookkeeping): a ping waits up to 5 s for its marker, the newest
// marker wins, a new stream voids the old one. E497: a re-sync ping that process() sends while a worker scan pass is
// running must keep its 5 s, not be cleared by the timeout of the ping before it.
#include "check.h"
#include "device/markerscan.h"

using namespace vti;

namespace {
const int R = 44100;

// one worker pass with a marker at `at` (or none, -1), ending at ring frame `newest`
bool pass(MarkerScan &m, int64_t at, int64_t newest)
{
    if (at >= 0 && m.candidate(at, false, true))
        m.take(at);
    return m.end(newest, R);
}
} // namespace

TEST(markerscan_first_ping_finds_its_marker)
{
    MarkerScan m;
    CHECK(m.marker.load() == -2);
    CHECK(!m.candidate(1000, false, true)); // no ping yet: nothing is a marker
    m.calibrate(0, true);
    CHECK(m.marker.load() == -1 && m.calibrating());
    pass(m, 1000, 2000);
    CHECK(m.marker.load() == 1000);
    CHECK(!m.calibrating());
}

TEST(markerscan_marker_needs_usb2_alone_after_the_ping)
{
    MarkerScan m;
    m.calibrate(5000, true);
    CHECK(!m.candidate(4999, false, true)); // before the ping
    CHECK(!m.candidate(5000, true, true));  // USB 1 sounds too
    CHECK(!m.candidate(5000, false, false));
    CHECK(m.candidate(5000, false, true));
}

TEST(markerscan_second_channel_of_a_marker_is_not_a_new_one)
{
    MarkerScan m;
    m.calibrate(0, true);
    m.calibrate(10, false); // two pings out
    m.take(100);
    CHECK(!m.candidate(101, false, true)); // the marker's other channel, one frame later
    CHECK(m.candidate(102, false, true));
}

TEST(markerscan_ping_without_marker_times_out_after_5_s)
{
    MarkerScan m;
    m.calibrate(0, true);
    CHECK(!pass(m, -1, 5 * R)); // exactly 5 s: still waiting
    CHECK(m.calibrating());
    CHECK(pass(m, -1, 5 * R + 1)); // past 5 s: not found, worth the log line
    CHECK(m.marker.load() == -2 && !m.calibrating());
}

TEST(markerscan_timeout_keeps_a_known_marker)
{
    // a re-sync ping that gets no answer: the stamps keep the old marker (no "no sync marker" line)
    MarkerScan m;
    m.calibrate(0, true);
    pass(m, 1000, 2000);
    m.calibrate(10 * R, false);
    CHECK(!pass(m, -1, 15 * R + 1));
    CHECK(m.marker.load() == 1000 && !m.calibrating());
}

TEST(markerscan_new_stream_voids_the_old_marker)
{
    MarkerScan m;
    m.calibrate(0, true);
    pass(m, 1000, 2000);
    m.reset_stream();
    m.calibrate(0, true); // the new stream counts from 0 again
    CHECK(m.marker.load() == -1);
    pass(m, 700, 1000);
    CHECK(m.marker.load() == 700);
}

TEST(markerscan_resync_ping_during_a_pass_keeps_its_5_s)
{
    // E497 (simulator S122, plan B on the TI2: 14 of 572 re-syncs missed, never a first sync): the pass snapshots the
    // ping frame at its start; process() pings again while the pass runs; the pass's timeout must not judge the new
    // ping by the old ping's frame and clear it, or the new ping's marker is refused and nothing is logged
    MarkerScan m;
    m.calibrate(0, true);
    pass(m, 1000, 2000);
    CHECK(m.marker.load() == 1000);

    const int64_t B = 10 * R;               // the re-sync ping, 10 s later
    CHECK(!m.candidate(B - 500, false, true)); // a pass runs over frames before it: no ping waits
    m.calibrate(B, false);                  // process() pings during the pass
    CHECK(!m.candidate(B - 400, false, true)); // later frames of the same pass are still before the new ping
    CHECK(!m.end(B + 100, R));              // the pass ends 10 s after the OLD ping, 2 ms after the new one
    CHECK(m.calibrating());         // the new ping still waits

    pass(m, B + 1500, B + 3000);        // its marker arrives 34 ms later
    CHECK(m.marker.load() == B + 1500); // and is taken (it stayed at 1000 before the fix)
    CHECK(!m.calibrating());
}

TEST(markerscan_resync_ping_during_a_pass_still_times_out_on_its_own)
{
    // the fix must not keep a ping forever: the new ping's own 5 s still end it
    MarkerScan m;
    m.calibrate(0, true);
    pass(m, 1000, 2000);
    const int64_t B = 10 * R;
    m.calibrate(B, false);
    m.end(B + 100, R);
    CHECK(!pass(m, -1, B + 5 * R));
    CHECK(m.calibrating());
    CHECK(!pass(m, -1, B + 5 * R + 1)); // timed out; the old marker stays, so no "not found" line
    CHECK(!m.calibrating() && m.marker.load() == 1000);
}

// the edges (mutation run 2026-11-13): a marker at ring frame 0 is a found marker, and with no ping out (the stream
// was reset after the ping) the timeout has nothing to judge
TEST(markerscan_marker_at_frame_0_and_no_ping_out)
{
    MarkerScan m;
    m.calibrate(0, true);
    pass(m, 0, 100);
    CHECK(m.marker.load() == 0);
    m.calibrate(200, false); // a re-sync ping: the known marker at 0 stays until a newer one
    CHECK(m.marker.load() == 0);
    CHECK(!pass(m, -1, 200 + 5 * R + 1)); // it times out, but a marker is known: no log line
    CHECK(m.marker.load() == 0);
    MarkerScan r;
    r.calibrate(0, true);
    r.reset_stream(); // no ping out any more, the marker still unknown
    CHECK(!r.end(10 * R, R));
    CHECK(r.marker.load() == -1);
}
