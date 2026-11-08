// A Virus audio stream that stood still while time went on (macOS sleep: Core Audio pauses, the ring keeps its frame
// counter and runs on after wake, E207 M3): the plug-in must treat it as a new stream and re-anchor, else every
// note is stamped by the length of the pause off and nothing sounds.
#include "check.h"
#include "device/stallwatch.h"

using namespace vti;

TEST(stallwatch_steady_stream_is_no_stall)
{
    StallWatch w;
    int64_t frame = 1000;
    for (int64_t t = 0; t < 10000; t += 10, frame += 441)
        CHECK(!w.update(frame, t));
}

TEST(stallwatch_short_gaps_are_no_stall)
{
    // the worker polls faster than USB packets arrive, and a busy system can hold the ring for a few hundred ms
    StallWatch w;
    w.update(1000, 0);
    CHECK(!w.update(1000, 400)); // standing still is not reported by itself
    CHECK(!w.update(5000, 500)); // moved on after 0.5 s: a hiccup, the timeline follows it
}

TEST(stallwatch_resume_after_sleep_is_reported_once)
{
    StallWatch w;
    w.update(968590, 0);
    w.update(969472, 20);            // running
    CHECK(!w.update(969472, 19000)); // asleep: no movement, nothing yet
    CHECK(w.update(970000, 19050));  // awake: the stream runs on - a new stream for the timeline
    CHECK(!w.update(970441, 19060)); // once
}

TEST(stallwatch_worker_paused_too)
{
    // the whole process slept (the worker did not run): the first call after wake sees the jump at once
    StallWatch w;
    w.update(1000, 0);
    w.update(1441, 10); // running
    CHECK(w.update(1882, 30000));
}

// the worker itself hung in writes the Virus did not take (it refused MIDI for 8-19 s during the state upload at 44.1
// kHz, every write waiting 1 s, simulator S089): the stream ran on meanwhile, so its frames are all there - no stall.
// Reporting one sent the whole state again into the same block and left the session silent (E464)
TEST(stallwatch_blocked_worker_with_a_running_stream_is_no_stall)
{
    StallWatch w;
    w.update(1000, 0, 44100);
    w.update(1441, 10, 44100);                             // running
    CHECK(!w.update(1441 + 88200, 2010, 44100));           // 2 s later, 2 s of frames: the stream ran
    CHECK(!w.update(1441 + 88200 + 44100 * 16, 18010, 44100)); // 16 s later, all frames there
    // a real pause in between: 19 s with 0.05 s of frames is still a sleep
    StallWatch v;
    v.update(1000, 0, 48000);
    v.update(1480, 10, 48000);
    CHECK(v.update(1480 + 2400, 19010, 48000));
}

TEST(stallwatch_no_stream_resets)
{
    StallWatch w;
    w.update(1000, 0);
    CHECK(!w.update(-1, 5000));   // no audio (Virus gone): the reconnect path handles it
    CHECK(!w.update(2000, 9000)); // the new stream's first frame is no stall
}

TEST(stallwatch_starting_stream_is_no_stall)
{
    // a new broker stream: the ring shows a frame number before the Virus' audio really runs, and starts moving
    // seconds later (E209: "audio stood still" on 9/9 first connects) - a start, not a sleep
    StallWatch w;
    w.update(0, 0);
    CHECK(!w.update(0, 3000));    // waiting for the stream
    CHECK(!w.update(441, 3010));  // it starts: no stall
    CHECK(!w.update(882, 3020));
    CHECK(!w.update(882, 9000));  // now running, then standing still (sleep)
    CHECK(w.update(1323, 9010));  // that one is reported
}

// Mutation run 2026-10-20: a stream whose counter starts at frame 0, and the limit itself
TEST(stallwatch_stream_from_frame_zero_and_the_limit)
{
    StallWatch w;
    w.update(0, 0);
    w.update(441, 10);                                      // running from frame 0
    // since S089 the limit counts frames MISSING for that long: 441 frames arrived = 10 ms that are not missing
    CHECK(w.update(882, 10 + 10 + StallWatch::LIMIT_MS));   // missing exactly LIMIT_MS: a stall
    StallWatch v;
    v.update(1000, 0);
    v.update(1441, 10);
    CHECK(!v.update(1882, 10 + 10 + StallWatch::LIMIT_MS - 1)); // 1 ms less: a hiccup
}

// The read position ahead of the Virus' audio in every block: after a Virus power cycle with the USB cable left in
// (Mac, E472) the plug-in anchored on a stream not yet running steadily and its reads were pulled back in every block
// from then on (read margin min 0 in every 10 s window, dropouts that never settled). A full second of nothing but
// pulled-back reads starts a new timeline, once; a single pulled-back read (a hiccup) does not.
TEST(clampwatch_a_second_of_pulled_back_reads_reanchors)
{
    ClampWatch w;
    for (int i = 0; i < 100; i++) // 100 blocks of 64 at 48 kHz: 133 ms, not enough
        CHECK(!w.update(true, 64, 48000));
    CHECK(!w.update(false, 64, 48000)); // one good read clears it
    bool fired = false;
    int blocks = 0;
    while (!fired && blocks < 2000) {
        fired = w.update(true, 64, 48000);
        blocks++;
    }
    CHECK(fired && blocks == 750);       // exactly one second of 64-frame blocks
    CHECK(!w.update(true, 64, 48000));   // once: the next second counts afresh
}

// After a Mac sleep the stream can stay gapped until macOS re-enumerates the Virus (E472, E473); the plug-in asks the
// broker for that software replug, at most once a minute: USB resets close together have hung the Virus (E136, E312)
TEST(repluggate_once_a_minute)
{
    ReplugGate g;
    CHECK(g.allow(1000));          // the first sleep
    CHECK(!g.allow(1000 + 30000)); // a second one 30 s later: not yet
    CHECK(g.allow(1000 + 60000));  // a minute later
    CHECK(!g.allow(1000 + 60001));
}
