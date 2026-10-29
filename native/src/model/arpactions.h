// arpactions.h - the ARP page's pattern actions (the buttons below the user pattern and its context menu), like the
// original's (0x180084c90, E371): each works on the steps up to the pattern length L = Arpeggiator/UserPatternLength
// + 1. The plug-in reads the 32 steps, applies one of these and sends what changed.
#pragma once
#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

namespace vti {

struct ArpStep {
    int on = 1, velocity = 100, length = 64; // Step N Bitfield / Velocity / Length
};

// "Shift Sequence to Right" (dir +1: the last step wraps to step 1) / "to Left" (dir -1): steps 1..L rotate by one
inline void arp_shift(std::vector<ArpStep> &s, int len, int dir)
{
    if (len < 2 || len > (int)s.size())
        return;
    if (dir > 0) {
        ArpStep last = s[(size_t)len - 1];
        for (int i = len - 1; i > 0; i--)
            s[(size_t)i] = s[(size_t)i - 1];
        s[0] = last;
    } else {
        ArpStep first = s[0];
        for (int i = 0; i < len - 1; i++)
            s[(size_t)i] = s[(size_t)i + 1];
        s[(size_t)len - 1] = first;
    }
}

// a uniform random value in [0, 1) -> 0..127
inline int arp_random_value(double r) { return (int)std::lround(std::max(0.0, std::min(1.0, r)) * 127); }

// "Randomize Velocity" / "Randomize Note Length": steps 1..L get uniform random values
inline void arp_random_velocity(std::vector<ArpStep> &s, int len, const std::function<double()> &rnd)
{
    for (int i = 0; i < len && i < (int)s.size(); i++)
        s[(size_t)i].velocity = arp_random_value(rnd());
}
inline void arp_random_length(std::vector<ArpStep> &s, int len, const std::function<double()> &rnd)
{
    for (int i = 0; i < len && i < (int)s.size(); i++)
        s[(size_t)i].length = arp_random_value(rnd());
}

// "Randomize Pattern": step 1 on, steps 2..L on for a random value >= 0.5 - but every 4th "off" since the last
// forced "on" is forced on (results that are "on" do not reset that count), so a pattern never stays silent long
inline void arp_random_pattern(std::vector<ArpStep> &s, int len, const std::function<double()> &rnd)
{
    if (len < 1 || s.empty())
        return;
    s[0].on = 1;
    int offs = 0;
    for (int i = 1; i < len && i < (int)s.size(); i++) {
        if (rnd() >= 0.5) {
            s[(size_t)i].on = 1;
        } else if (++offs == 4) {
            s[(size_t)i].on = 1;
            offs = 0;
        } else
            s[(size_t)i].on = 0;
    }
}

// "Reset Pattern": steps 1..L back to the defaults
inline void arp_reset(std::vector<ArpStep> &s, int len, const ArpStep &def)
{
    for (int i = 0; i < len && i < (int)s.size(); i++)
        s[(size_t)i] = def;
}

// "Fill Up Pattern": steps 1..L copied over L+1..32 again and again; returns the new length (doubled if that fits)
inline int arp_fill_up(std::vector<ArpStep> &s, int len)
{
    if (len < 1 || len >= (int)s.size())
        return len;
    for (size_t i = (size_t)len; i < s.size(); i++)
        s[i] = s[i % (size_t)len];
    return 2 * len <= (int)s.size() ? 2 * len : len;
}

} // namespace vti
