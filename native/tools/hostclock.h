// hostclock.h - nativehost's time line. Block b's time was b * block / rate; with --rate-at the host rate changes in
// the middle of a run (a DAW whose project rate is switched while it plays), so the time is counted from the last
// change instead: no jump at the change, and frames at the new rate continue the same seconds (the note grid stays on
// half seconds).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>

struct HostClock {
    int rate;
    int block;
    double base_s = 0; // time of block base_b
    long base_b = 0;

    double seconds(long b) const { return base_s + (double)(b - base_b) * block / rate; }
    long frame(long b) const { return std::lround(seconds(b) * rate); }
    void set_rate(long b, int r)
    {
        base_s = seconds(b);
        base_b = b;
        rate = r;
    }
};

struct RateAt {
    int rate;
    double t;
    bool done = false;
};

// "RATE@T" (--rate-at 48000@10): the host rate RATE from T seconds on
inline bool parse_rate_at(const char *s, RateAt &out)
{
    char *end = nullptr;
    long r = std::strtol(s, &end, 10);
    if (end == s || *end != '@' || r <= 0)
        return false;
    const char *ts = end + 1;
    double t = std::strtod(ts, &end);
    if (end == ts || *end != '\0' || t < 0)
        return false;
    out = RateAt{(int)r, t};
    return true;
}

// --rough T1:T2: from T1 to T2 seconds the host's blocks come unevenly, like Ableton's at a 2048 sound-card buffer in
// E337's episodes: in bursts of 8 blocks woken together around the burst's middle, +-20 ms of jitter per burst and one
// burst 40 ms late about every 5 s. shift_us: how much later (or earlier) than its even time block b wakes.
struct RoughCadence {
    double from = -1, to = -1;
    uint64_t seed = 1;

    long long shift_us(const HostClock &c, long b) const
    {
        const double t = c.seconds(b);
        if (t < from || t >= to)
            return 0;
        const long burst = b / 8;
        const long every = (std::max)(1L, std::lround(5.0 * c.rate / (8.0 * c.block))); // bursts per ~5 s
        const double period = (double)c.block / c.rate;
        double wake = c.seconds(burst * 8 + 4) - period / 2;
        if (burst % every == every / 2)
            wake += 0.040;
        else {
            uint64_t h = (uint64_t)burst * 0x9E3779B97F4A7C15ull ^ seed; // splitmix64: a fixed jitter per burst
            h = (h ^ (h >> 30)) * 0xBF58476D1CE4E5B9ull;
            h = (h ^ (h >> 27)) * 0x94D049BB133111EBull;
            h ^= h >> 31;
            wake += ((double)(h % 40001) - 20000) * 1e-6;
        }
        return std::llround((wake - t) * 1e6);
    }
};

// "T1:T2" (--rough 10:70): an uneven cadence from T1 to T2 seconds
inline bool parse_rough(const char *s, RoughCadence &out)
{
    char *end = nullptr;
    const double a = std::strtod(s, &end);
    if (end == s || *end != ':' || a < 0)
        return false;
    const char *bs = end + 1;
    const double b = std::strtod(bs, &end);
    if (end == bs || *end != '\0' || b <= a)
        return false;
    out.from = a, out.to = b;
    return true;
}
