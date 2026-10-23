// hostclock.h - nativehost's time line. Block b's time was b * block / rate; with --rate-at the host rate changes in
// the middle of a run (a DAW whose project rate is switched while it plays), so the time is counted from the last
// change instead: no jump at the change, and frames at the new rate continue the same seconds (the note grid stays on
// half seconds).
#pragma once
#include <cmath>
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
