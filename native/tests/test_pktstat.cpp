// The USB audio IN packet statistics (E175): sizes of the packets the Virus sends, and when the stream leaves and
// re-enters the 44.1 kHz range for longer than the short 48 kHz phase right after SET_CUR. Hardware-free.
#include "check.h"
#include <cstring>
extern "C" {
#include "pktstat.h"
}

TEST(pktstat_buckets)
{
    vti_pktstat s = {};
    const uint32_t sizes[] = {0, 12, 43, 44, 44, 45, 46, 48, 49, 64};
    for (uint32_t f : sizes)
        vti_pktstat_add(&s, f);
    CHECK(s.size[VTI_PKT_EMPTY] == 1);
    CHECK(s.size[VTI_PKT_SHORT] == 1);
    CHECK(s.size[VTI_PKT_43] == 1);
    CHECK(s.size[VTI_PKT_44] == 2);
    CHECK(s.size[VTI_PKT_45] == 1);
    CHECK(s.size[VTI_PKT_46] == 1);
    CHECK(s.size[VTI_PKT_48K] == 2);
    CHECK(s.size[VTI_PKT_LONG] == 1);
}

TEST(pktstat_bad_run)
{
    vti_pktstat s = {};
    // the 48 kHz phase after SET_CUR (~0.3 s) is no event
    for (int i = 0; i < 300; i++)
        CHECK(vti_pktstat_add(&s, 48) == 0);
    for (int i = 0; i < 2000; i++)
        CHECK(vti_pktstat_add(&s, 44) == 0);
    CHECK(s.packets == 2300);
    // a long run of out-of-range packets is reported once, when it reaches VTI_PKT_BAD_RUN packets
    int events = 0;
    for (int i = 0; i < VTI_PKT_BAD_RUN - 1; i++)
        events += vti_pktstat_add(&s, 0) != 0;
    CHECK(events == 0 && !s.bad);
    CHECK(vti_pktstat_add(&s, 0) == 1);
    CHECK(s.bad && s.bad_since == 2300 && s.bad_frames == 0);
    for (int i = 0; i < 5000; i++)
        CHECK(vti_pktstat_add(&s, 0) == 0);
    // one good packet inside a bad run is not the end of it, a run of good ones is
    CHECK(vti_pktstat_add(&s, 44) == 0);
    CHECK(vti_pktstat_add(&s, 0) == 0);
    int end = 0;
    for (int i = 0; i < VTI_PKT_GOOD_RUN; i++)
        end += vti_pktstat_add(&s, 44 + (i % 10 == 0)) == -1;
    CHECK(end == 1 && !s.bad);
}

// E241: after a wake the Virus kept sending 48 kHz packets while the broker mirrored 44/45-frame OUT packets with
// audio at it; a loud signal on its outputs, then a freeze. The OUT packet now matches a 48 kHz packet's size and is
// silent; a lost or empty packet keeps the old rule (nominal size, audio).
TEST(pktstat_out_matches_48k_with_silence)
{
    int silent = -1;
    CHECK(vti_pkt_out(44100, 44, 44, &silent) == 44 && silent == 0);
    CHECK(vti_pkt_out(44100, 46, 45, &silent) == 46 && silent == 0);
    CHECK(vti_pkt_out(44100, 48, 44, &silent) == 48 && silent == 1);
    CHECK(vti_pkt_out(44100, 47, 45, &silent) == 47 && silent == 1);
    CHECK(vti_pkt_out(44100, 49, 44, &silent) == 49 && silent == 1);
    CHECK(vti_pkt_out(44100, 0, 45, &silent) == 45 && silent == 0);  // lost packet: as before
    CHECK(vti_pkt_out(44100, 60, 44, &silent) == 44 && silent == 0); // nonsense size: as before
}

// the rate is lost when a reported bad run consists of 48 kHz packets (not when packets are merely lost)
TEST(pktstat_rate_lost_only_for_48k_runs)
{
    vti_pktstat s = {};
    for (int i = 0; i < VTI_PKT_BAD_RUN; i++)
        vti_pktstat_add(&s, 48);
    CHECK(s.bad && vti_pktstat_rate_lost(&s));
    vti_pktstat t = {};
    for (int i = 0; i < VTI_PKT_BAD_RUN; i++)
        vti_pktstat_add(&t, 0);
    CHECK(t.bad && !vti_pktstat_rate_lost(&t));
    vti_pktstat u = {};
    for (int i = 0; i < 300; i++) // the short 48 kHz phase right after SET_CUR is not "lost"
        vti_pktstat_add(&u, 48);
    CHECK(!vti_pktstat_rate_lost(&u));
}

// E257 (round 0.8.27, T6): the global "System Clock/Frequency = 44.1 kHz" the broker sends next to the SET_CUR
// retry, and the Virus' report of that parameter when it arrives.
extern "C" {
#include "ratelever.h"
}

TEST(ratelever_packs_the_global_on_cable_2)
{
    uint8_t out[16];
    CHECK(vti_pack_rate_44k1(out, 15) == -1);
    CHECK(vti_pack_rate_44k1(out, sizeof out) == 16);
    const uint8_t want[16] = {0x24, 0xF0, 0x00, 0x20, 0x24, 0x33, 0x01, 0x10,
                              0x24, 0x73, 0x00, 0x10, 0x26, 0x00, 0xF7, 0x00};
    CHECK(memcmp(out, want, 16) == 0);
}

TEST(ratelever_finds_the_report)
{
    const uint8_t rx[] = {0x2B, 0xB0, 0x01, 0x40, 0x24, 0xF0, 0x00, 0x20, 0x24, 0x33, 0x01, 0x10,
                          0x24, 0x73, 0x00, 0x10, 0x26, 0x01, 0xF7, 0x00};
    CHECK(vti_rate_report(rx, sizeof rx) == 1);
    const uint8_t other[] = {0x24, 0xF0, 0x00, 0x20, 0x24, 0x33, 0x01, 0x10, 0x24, 0x73, 0x00, 0x54, 0x26, 0x01, 0xF7, 0x00};
    CHECK(vti_rate_report(other, sizeof other) == -1); // the sync information event
    CHECK(vti_rate_report(rx, 12) == -1);               // cut off before the value
}

// Rate following (design-rate.md step 3): at a wanted 48 kHz the good packets are 47-49 frames (48 nominal) and a run
// of 44.1 kHz packets is the lost rate; the OUT packet still mirrors the Virus' real size, silent when it is the other
// rate (E241's rule, both ways). A zeroed statistic is at 44.1 kHz (the old behaviour).
TEST(pktstat_at_48k)
{
    vti_pktstat s = {};
    vti_pktstat_set_rate(&s, 48000);
    for (int i = 0; i < 2000; i++)
        CHECK(vti_pktstat_add(&s, 48 + (i % 7 == 0)) == 0);
    CHECK(!s.bad);
    for (int i = 0; i < 230; i++) // IN keeps 44.1 kHz sizes ~230 ms after SET_CUR (E323): no event
        CHECK(vti_pktstat_add(&s, 44) == 0);
    for (int i = 0; i < 2000; i++)
        vti_pktstat_add(&s, 48);
    int events = 0;
    for (int i = 0; i < VTI_PKT_BAD_RUN; i++)
        events += vti_pktstat_add(&s, 44 + (i % 10 == 0)) == 1;
    CHECK(events == 1 && s.bad && vti_pktstat_rate_lost(&s));
    vti_pktstat t = {};
    vti_pktstat_set_rate(&t, 48000);
    for (int i = 0; i < VTI_PKT_BAD_RUN; i++)
        vti_pktstat_add(&t, 0);
    CHECK(t.bad && !vti_pktstat_rate_lost(&t)); // lost packets are no rate problem at 48 kHz either
    vti_pktstat z = {};
    CHECK(vti_pktstat_rate(&z) == 44100);
}

TEST(pktstat_out_at_48k)
{
    int silent = -1;
    CHECK(vti_pkt_out(48000, 48, 48, &silent) == 48 && silent == 0);
    CHECK(vti_pkt_out(48000, 47, 48, &silent) == 47 && silent == 0);
    CHECK(vti_pkt_out(48000, 49, 48, &silent) == 49 && silent == 0);
    CHECK(vti_pkt_out(48000, 44, 48, &silent) == 44 && silent == 1); // the Virus still at 44.1 kHz
    CHECK(vti_pkt_out(48000, 45, 48, &silent) == 45 && silent == 1);
    CHECK(vti_pkt_out(48000, 0, 48, &silent) == 48 && silent == 0);
    CHECK(vti_pkt_out(48000, 60, 48, &silent) == 48 && silent == 0);
}

TEST(pktstat_nominal_pattern)
{
    unsigned sum = 0;
    for (unsigned i = 0; i < 1000; i++)
        sum += vti_pkt_nominal(44100, i);
    CHECK(sum == 44100); // 44 x 9 + 45 per 10 ms
    CHECK(vti_pkt_nominal(44100, 0) == 45 && vti_pkt_nominal(44100, 1) == 44);
    for (unsigned i = 0; i < 20; i++)
        CHECK(vti_pkt_nominal(48000, i) == 48);
    CHECK(vti_pkt_in_range(44100, 43) && vti_pkt_in_range(44100, 46) && !vti_pkt_in_range(44100, 47));
    CHECK(vti_pkt_in_range(48000, 47) && vti_pkt_in_range(48000, 49) && !vti_pkt_in_range(48000, 46));
    CHECK(!vti_pkt_in_range(48000, 50));
}

TEST(ratelever_packs_48k)
{
    uint8_t out[16];
    CHECK(vti_pack_rate(out, sizeof out, 48000) == 16);
    const uint8_t want[16] = {0x24, 0xF0, 0x00, 0x20, 0x24, 0x33, 0x01, 0x10,
                              0x24, 0x73, 0x00, 0x10, 0x26, 0x01, 0xF7, 0x00};
    CHECK(memcmp(out, want, 16) == 0);
    CHECK(vti_pack_rate(out, sizeof out, 44100) == 16 && out[13] == 0x00);
    CHECK(vti_pack_rate(out, 15, 48000) == -1);
}
