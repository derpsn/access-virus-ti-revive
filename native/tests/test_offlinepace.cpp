// Offline render (export, freeze, offline bounce): the host calls process() faster than real time, but the Virus only
// makes sound in real time. The original refuses it (a warning, Virus Control 0x18002fd40 / 0x180080ba0); the native
// plug-in holds each offline block until the Virus has produced that much audio, so the render is right at 1x speed.
#include "check.h"
#include "device/offlinepace.h"

using namespace vti;

TEST(offlinepace_first_block_runs_at_once)
{
    OfflinePacer p;
    CHECK(p.wait_until(10000, 512) <= 10000); // nothing to wait for: the render starts where the Virus is now
}

TEST(offlinepace_holds_blocks_to_real_time)
{
    // the host asks for blocks back to back; each one needs the Virus' next 512 frames
    OfflinePacer p;
    p.wait_until(10000, 512);
    CHECK(p.wait_until(10000, 512) == 10512); // the Virus has not moved on yet: wait for 512 more frames
    CHECK(p.wait_until(10512, 512) == 11024);
    CHECK(p.wait_until(11024, 512) == 11536);
}

TEST(offlinepace_fractional_blocks_do_not_drift)
{
    // 48 kHz host: 512 host samples are 470.4 Virus frames; 10 s of blocks must ask for 441000 frames, not 440000
    OfflinePacer p;
    const double frames = 512 * 44100.0 / 48000.0;
    int64_t newest = 0, need = 0;
    p.wait_until(newest, frames);
    const int blocks = (int)(10 * 48000 / 512); // 937 blocks = 9.995 s
    for (int i = 0; i < blocks; i++) {
        need = p.wait_until(newest, frames);
        newest = need; // the Virus delivers exactly what was asked for
    }
    CHECK(need >= (int64_t)(blocks * frames) - 1 && need <= (int64_t)(blocks * frames) + 1);
}

TEST(offlinepace_slow_render_catches_up_without_a_burst)
{
    // a heavy project renders slower than real time: the Virus runs ahead; the paced position never falls further
    // behind than MAX_BEHIND, so the frames read there are still in the audio ring and no backlog builds up
    OfflinePacer p;
    p.wait_until(0, 512);
    CHECK(p.wait_until(200000, 512) == 200000 - OfflinePacer::MAX_BEHIND);
    CHECK(p.wait_until(200000, 512) == 200000 - OfflinePacer::MAX_BEHIND + 512);
    CHECK(OfflinePacer::MAX_BEHIND <= 16384 / 4); // lag + latency (~2-4k frames) + a block stay inside the ring
}

TEST(offlinepace_new_stream_restarts)
{
    // the Virus came back (a new audio stream starts low) or the wait gave up: start again where the Virus is
    OfflinePacer p;
    p.wait_until(500000, 512);
    p.wait_until(500000, 512);
    CHECK(p.wait_until(1000, 512) <= 1000); // the ring frame went backwards: a new stream, no wait
    p.reset();
    CHECK(p.wait_until(5000, 512) <= 5000); // after a reset (timeout, back to real time) the next block runs at once
}

TEST(offlinepace_no_audio_never_waits)
{
    OfflinePacer p;
    CHECK(p.wait_until(-1, 512) < 0); // no Virus audio: nothing to pace by
    CHECK(p.wait_until(-1, 512) < 0);
}

// the Mac's ring grows in 512-frame chunks (Core Audio IOProc, E315); the host renders blocks of `frames` and each
// waits until the ring reaches its release point. Returns how far past its paced position a block ran at worst.
static int64_t worst_run_late(double frames, int64_t chunk, int blocks)
{
    OfflinePacer p;
    int64_t ring = 100 * chunk, worst = 0;
    p.wait_until(ring, frames);
    for (int i = 0; i < blocks; i++) {
        const int64_t need = p.wait_until(ring, frames);
        while (ring < need - p.early(frames))
            ring += chunk; // the next chunk arrives
        if (i > 64 && ring - need > worst) // after the step is known
            worst = ring - need;
    }
    return worst;
}

TEST(offlinepace_small_blocks_run_by_their_own_position)
{
    // E315: 48 kHz / 128 (117.6 Virus frames) against 512-frame chunks - the first blocks waiting for a chunk ran up
    // to ~394 frames after their position, their notes reached the Virus late. At most one block late now.
    CHECK(worst_run_late(128 * 44100.0 / 48000.0, 512, 4000) <= 118);
    CHECK(worst_run_late(128, 512, 4000) <= 128); // 44.1 kHz / 128
    CHECK(worst_run_late(64, 512, 4000) <= 64);
}

TEST(offlinepace_blocks_as_large_as_the_step_wait_as_before)
{
    OfflinePacer p;
    int64_t ring = 51200;
    p.wait_until(ring, 512);
    for (int i = 0; i < 100; i++) {
        ring = p.wait_until(ring, 512); // the Virus delivers exactly one block per chunk (E315: 44.1/512 and 48/512 clean)
        CHECK(p.early(512) == 0);
    }
    OfflinePacer q; // a ring that grows in steps smaller than the block: nothing to run early
    int64_t r = 10000;
    q.wait_until(r, 117.6);
    for (int i = 0; i < 100; i++) {
        r += 44;
        q.wait_until(r, 117.6);
    }
    CHECK(q.early(117.6) == 0);
    CHECK(q.early(32) <= 12);
}

TEST(offlinepace_early_release_stays_inside_the_read_margin)
{
    // the block reads the ring ~770 frames behind its paced position (latency_frames: block + 640 + resampler), so
    // running early may never reach past what is there: capped at MAX_EARLY whatever the step
    CHECK(OfflinePacer::MAX_EARLY <= 512);
    OfflinePacer p;
    int64_t ring = 100000;
    p.wait_until(ring, 117.6);
    for (int i = 0; i < 100; i++) {
        ring += 4096;
        p.wait_until(ring, 117.6);
    }
    CHECK(p.early(117.6) == OfflinePacer::MAX_EARLY);
    // one late chunk (two arriving together) does not stretch the step: the smallest recent step counts
    OfflinePacer q;
    int64_t r = 51200;
    q.wait_until(r, 117.6);
    for (int i = 0; i < 200; i++) {
        r += i == 50 ? 1024 : 512;
        q.wait_until(r, 117.6);
    }
    CHECK(q.early(117.6) == 512 - 118);
    q.reset(); // a new stream: the step is learned again
    CHECK(q.early(117.6) == 0);
}
