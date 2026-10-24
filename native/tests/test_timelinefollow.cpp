// The Virus timeline after its anchor (E337): an uneven host cadence must not make it jump back and forth (each jump
// is a click), while a real lasting offset (REAPER after a render, E279) still jumps soon. The tests run a small model:
// the ring grows in 441-frame USB chunks (10 ms), the host calls 256-frame blocks at the given times, the timeline
// moves by the block plus what TimelineFollow says - the same arithmetic as process().
#include "check.h"
#include "device/timelinefollow.h"
#include <cstdint>
#include <cstdlib>
#include <vector>

using namespace vti;

namespace {

constexpr int RATE = 44100, BLOCK = 256;

struct Lcg { // deterministic jitter
    uint64_t s;
    int64_t next(int64_t n)
    {
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        return (int64_t)((s >> 33) % (uint64_t)n);
    }
};

struct Ring {
    double ppm = 0;          // the Virus' clock against the host's
    int64_t offset_at = -1;  // from this host time on the ring is `offset` frames further (a real offset)
    int64_t offset = 0;
    int64_t newest(int64_t t) const
    {
        int64_t v = (int64_t)((double)t * (1.0 + ppm * 1e-6));
        v = v / 441 * 441;
        return offset_at >= 0 && t >= offset_at ? v + offset : v;
    }
};

struct Result {
    int jumps = 0, steps = 0;
    std::vector<int64_t> jump_at, jump_by;
    std::vector<int64_t> t, err;
};

Result run(const std::vector<int64_t> &calls, const Ring &ring, bool marker = true)
{
    TimelineFollow f;
    f.set_window(RATE / 2);
    Result r;
    int64_t tl = ring.newest(calls[0]); // anchored at the first block
    for (int64_t c : calls) {
        const int64_t err = ring.newest(c) - tl;
        r.t.push_back(c), r.err.push_back(err);
        const TimelineFollow::Move m = f.update(err, BLOCK, marker);
        if (m.jump)
            r.jumps++, r.jump_at.push_back(c), r.jump_by.push_back(m.delta);
        else if (m.delta != 0)
            r.steps++;
        tl += m.delta + BLOCK;
    }
    return r;
}

// even calls, `seconds` long
std::vector<int64_t> steady(int seconds)
{
    std::vector<int64_t> c;
    for (int64_t i = 0; i < (int64_t)seconds * RATE / BLOCK; i++)
        c.push_back(i * BLOCK + 50);
    return c;
}

// steady, then `rough` seconds of bursts of 8 blocks around the burst's middle with +-jit frames of jitter and a burst
// 40 ms late every ~5 s, then steady again (the cadence of E337's episodes, exaggerated)
std::vector<int64_t> uneven(uint64_t seed, int calm1, int rough, int calm2, int64_t jit)
{
    Lcg rng{seed};
    const int64_t n1 = (int64_t)calm1 * RATE / BLOCK / 8 * 8, n2 = (int64_t)rough * RATE / BLOCK / 8 * 8,
                  n3 = (int64_t)calm2 * RATE / BLOCK;
    std::vector<int64_t> c;
    int64_t last = 0, burst = 0;
    for (int64_t i = 0; i < n1 + n2 + n3; i++) {
        int64_t t = i * BLOCK;
        if (i >= n1 && i < n1 + n2) {
            const int64_t b = (i - n1) / 8, k = (i - n1) % 8;
            if (k == 0)
                burst = b % 108 == 50 ? (i + 4) * BLOCK + 1764 : (i + 4) * BLOCK - BLOCK / 2 + rng.next(2 * jit + 1) - jit;
            t = burst + k * 10;
        }
        last = t > last ? t : last;
        c.push_back(last);
    }
    return c;
}

} // namespace

TEST(timeline_rough_host_cadence_does_not_jump)
{
    // E337: err swung -767..852 within a second; the old rule (jump at abs(err) > 512 per block) jumped ~3700 times
    // and stepped ~3000 times in this minute of the model - here not a single jump and few drift steps
    for (uint64_t seed = 1; seed <= 4; seed++) {
        const Result r = run(uneven(seed, 10, 60, 10, 882), Ring{});
        CHECK(r.jumps == 0);
        CHECK(r.steps < 400);
    }
}

TEST(timeline_lasting_offset_jumps_after_half_a_second)
{
    // REAPER after a render (E279): the timeline is ~1000 frames off for good - one jump, half a second later
    Ring ring;
    ring.offset_at = 10 * RATE, ring.offset = 1000;
    const Result r = run(steady(20), ring);
    CHECK(r.jumps == 1);
    if (r.jumps == 1) {
        CHECK(r.jump_at[0] - ring.offset_at >= RATE * 4 / 10 && r.jump_at[0] - ring.offset_at <= RATE * 6 / 10);
        CHECK(r.jump_by[0] >= 700 && r.jump_by[0] <= 1100);
    }
    for (size_t i = 0; i < r.t.size(); i++)
        if (r.t[i] > 12 * RATE)
            CHECK(std::llabs(r.err[i]) < 450); // back at real time
}

TEST(timeline_far_offset_jumps_at_once)
{
    // REAPER processing ahead after Play (status.md: up to -8548): far beyond any jitter, no half-second wait
    Ring ring;
    ring.offset_at = 10 * RATE, ring.offset = 5000;
    const Result r = run(steady(12), ring);
    CHECK(r.jumps == 1);
    if (r.jumps == 1)
        CHECK(r.jump_at[0] - ring.offset_at < BLOCK);
}

TEST(timeline_offset_in_rough_cadence_still_jumps)
{
    // the same offset while the host's cadence is uneven: the 2 s mean finds it
    Ring ring;
    ring.offset_at = 20 * RATE, ring.offset = 1000;
    const Result r = run(uneven(1, 10, 30, 0, 400), ring);
    CHECK(r.jumps == 1);
    if (r.jumps == 1)
        CHECK(r.jump_at[0] > ring.offset_at && r.jump_at[0] - ring.offset_at <= RATE * 5 / 2);
}

TEST(timeline_without_marker_only_steps)
{
    // without the marker the Virus' stamp clock follows our anchor: no jump, the offset is stepped out one frame per block
    Ring ring;
    ring.offset_at = 5 * RATE, ring.offset = 1000;
    const Result r = run(steady(20), ring, false);
    CHECK(r.jumps == 0);
    CHECK(r.steps > 500);
    CHECK(std::llabs(r.err.back()) < 450);
}

TEST(timeline_follows_clock_drift)
{
    // the Virus' clock against the sound card's: one-frame steps keep the error inside the read margin (pos is
    // block + 640 frames behind; below err -640 a block would read frames that are not there yet)
    for (double ppm : {-200.0, 200.0}) {
        Ring ring;
        ring.ppm = ppm;
        const Result r = run(steady(60), ring);
        CHECK(r.jumps == 0);
        CHECK(r.steps > 100); // 200 ppm = 529 frames a minute, part of it inside the band
        for (size_t i = 3000; i < r.err.size(); i++)
            CHECK(r.err[i] > -600 && r.err[i] < 600);
    }
}
