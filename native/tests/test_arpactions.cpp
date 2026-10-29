// The ARP page's pattern actions (#19 point 4, E371: the original's 0x180084c90): shift, randomize, reset, fill up -
// on the steps up to the pattern length L (Arpeggiator/UserPatternLength + 1)
#include "check.h"
#include "model/arpactions.h"

using namespace vti;

static std::vector<ArpStep> pattern()
{
    std::vector<ArpStep> s(32);
    for (int i = 0; i < 32; i++)
        s[i] = {i % 2, 10 + i, 20 + i};
    return s;
}

TEST(arp_shift_rotates_up_to_the_length)
{
    auto s = pattern();
    arp_shift(s, 4, +1); // right: step 4 wraps to step 1
    CHECK(s[0].velocity == 13 && s[1].velocity == 10 && s[3].velocity == 12 && s[0].on == 1 && s[0].length == 23);
    CHECK(s[4].velocity == 14); // beyond the length: untouched
    arp_shift(s, 4, -1);        // and back
    CHECK(s[0].velocity == 10 && s[3].velocity == 13);
}

TEST(arp_random_values_up_to_the_length)
{
    auto s = pattern();
    double r[] = {0.0, 0.999, 0.5};
    int k = 0;
    arp_random_velocity(s, 3, [&] { return r[k++ % 3]; });
    CHECK(s[0].velocity == 0 && s[1].velocity == 127 && s[2].velocity == 64 && s[3].velocity == 13);
    k = 0;
    arp_random_length(s, 2, [&] { return r[k++ % 3]; });
    CHECK(s[0].length == 0 && s[1].length == 127 && s[2].length == 22);
}

TEST(arp_random_pattern_forces_every_fourth_off)
{
    auto s = pattern();
    // all "off" results: step 1 on, then every 4th off forced on (steps 5 and 9 of 10)
    arp_random_pattern(s, 10, [] { return 0.1; });
    int on[10];
    for (int i = 0; i < 10; i++)
        on[i] = s[i].on;
    CHECK(on[0] == 1 && on[1] == 0 && on[2] == 0 && on[3] == 0 && on[4] == 1 && on[5] == 0 && on[8] == 1);
    // "on" results do not reset the count of offs
    double r[] = {0.1, 0.9, 0.1, 0.1, 0.1};
    int k = 0;
    arp_random_pattern(s, 6, [&] { return r[k++]; });
    CHECK(s[0].on == 1 && s[1].on == 0 && s[2].on == 1 && s[3].on == 0 && s[4].on == 0 && s[5].on == 1);
}

TEST(arp_reset_and_fill_up)
{
    auto s = pattern();
    arp_reset(s, 2, ArpStep{1, 100, 64});
    CHECK(s[0].velocity == 100 && s[1].length == 64 && s[2].velocity == 12);
    s = pattern();
    CHECK(arp_fill_up(s, 3) == 6); // copies 1..3 over 4..32, the length doubles (it fits)
    CHECK(s[3].velocity == 10 && s[5].velocity == 12 && s[30].velocity == 10 + 30 % 3 && s[31].velocity == 11);
    s = pattern();
    CHECK(arp_fill_up(s, 20) == 20); // 40 does not fit: the length stays
    CHECK(s[20].velocity == 10 && s[31].velocity == 21);
}
