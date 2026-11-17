// The Snow's idle stream (src/broker/idlepace.h): its bulk EP 0x85 answers every read, ~3300 times a second, with
// FF FE 00 00 (cable 15 Active Sensing, E387); one libusb transfer per packet cost a third of a CPU core on an M4 Pro
// (#42, E547). The original drivers read just as fast (E548), so this is our own rule: a read that brought only idle
// packets rests its transfer, the reader lets one rested transfer out per millisecond, and real data wakes them all.
#include "check.h"
extern "C" {
#include "idlepace.h"
}

namespace {
const uint8_t idle[4] = {0xff, 0xfe, 0x00, 0x00};
const uint8_t idle2[8] = {0xff, 0xfe, 0x00, 0x00, 0xff, 0xfe, 0x00, 0x00};
const uint8_t knob[4] = {0x0b, 0xb0, 0x4a, 0x40};               // cable 0 control change
const uint8_t mixed[8] = {0xff, 0xfe, 0x00, 0x00, 0x0b, 0xb0, 0x4a, 0x40};
const uint64_t ms = 1000000ull;
} // namespace

TEST(idlepace_idle_only_needs_whole_idle_packets)
{
    CHECK(vti_idle_only(idle, 4));
    CHECK(vti_idle_only(idle2, 8));
    CHECK(!vti_idle_only(knob, 4));
    CHECK(!vti_idle_only(mixed, 8));  // data inside: handed on at once
    CHECK(!vti_idle_only(idle, 0));   // an empty read is not an idle read
    CHECK(!vti_idle_only(idle2, 6));  // a cut packet is never thrown away
    CHECK(!vti_idle_only(nullptr, 4));
}

TEST(idlepace_lets_one_rested_transfer_out_per_millisecond)
{
    vti_pace p;
    vti_pace_init(&p);
    CHECK_EQ(vti_pace_due(&p, 5 * ms), -1); // nothing rested
    vti_pace_rest(&p, 3);
    vti_pace_rest(&p, 5);
    CHECK_EQ(vti_pace_due(&p, 5 * ms), 3);  // the first one at once
    CHECK_EQ(vti_pace_due(&p, 5 * ms + ms / 2), -1); // the next only a millisecond later
    CHECK_EQ(vti_pace_due(&p, 6 * ms), 5);
    CHECK_EQ(vti_pace_due(&p, 9 * ms), -1); // all out
}

TEST(idlepace_data_wakes_every_rested_transfer)
{
    vti_pace p;
    vti_pace_init(&p);
    for (int s = 0; s < 8; s++)
        vti_pace_rest(&p, s);
    CHECK_EQ(vti_pace_wake_all(&p), 0xffu); // all eight go out now
    CHECK_EQ(vti_pace_wake_all(&p), 0u);
    CHECK_EQ(vti_pace_due(&p, 100 * ms), -1);
}

TEST(idlepace_tells_the_reader_how_long_it_may_sleep)
{
    vti_pace p;
    vti_pace_init(&p);
    const uint64_t cap = 200 * ms;
    CHECK_EQ(vti_pace_wait(&p, 10 * ms, cap), cap); // nothing rested: the normal 200 ms
    vti_pace_rest(&p, 1);
    CHECK_EQ(vti_pace_wait(&p, 10 * ms, cap), 0u); // due now
    CHECK_EQ(vti_pace_due(&p, 10 * ms), 1);
    vti_pace_rest(&p, 1);
    CHECK_EQ(vti_pace_wait(&p, 10 * ms + ms / 4, cap), 3 * ms / 4); // until the next millisecond
    CHECK_EQ(vti_pace_wait(&p, 30 * ms, cap), 0u);
}

TEST(idlepace_ignores_slots_out_of_range)
{
    vti_pace p;
    vti_pace_init(&p);
    vti_pace_rest(&p, -1);
    vti_pace_rest(&p, 32);
    CHECK_EQ(vti_pace_due(&p, ms), -1);
    CHECK_EQ(vti_pace_wake_all(&p), 0u);
    vti_pace_rest(&p, 0); // the edges still count
    vti_pace_rest(&p, 31);
    CHECK_EQ(vti_pace_wake_all(&p), 0x80000001u);
}

// the option "snow-idle-slow" (#42, user 2026-11-17, off by default): after a second without real data the paced reads
// come every 4 ms instead of every millisecond; real data brings back the 1 ms pace (and wakes everything, as before)
TEST(idlepace_slow_mode_after_a_quiet_second)
{
    vti_pace p;
    vti_pace_init(&p);
    CHECK(!p.slow); // off unless the option is set
    p.slow = 1;
    vti_pace_data(&p, 100 * ms); // real data at 100 ms
    vti_pace_rest(&p, 0);
    CHECK_EQ(vti_pace_due(&p, 500 * ms), 0);
    vti_pace_rest(&p, 0);
    CHECK_EQ(vti_pace_due(&p, 501 * ms), 0); // within the quiet second: still 1 ms
    vti_pace_rest(&p, 0);
    CHECK_EQ(vti_pace_due(&p, 1100 * ms), 0); // a second after the data: this one sets the slow pace
    vti_pace_rest(&p, 0);
    CHECK_EQ(vti_pace_due(&p, 1101 * ms), -1);
    CHECK_EQ(vti_pace_wait(&p, 1101 * ms, 200 * ms), 3 * ms);
    CHECK_EQ(vti_pace_due(&p, 1104 * ms), 0);
    vti_pace_data(&p, 1105 * ms); // data again: the next one after 1 ms
    vti_pace_rest(&p, 0);
    CHECK_EQ(vti_pace_due(&p, 1105 * ms), 0);
    vti_pace_rest(&p, 0);
    CHECK_EQ(vti_pace_due(&p, 1106 * ms), 0);
}

TEST(idlepace_slow_mode_off_keeps_one_millisecond)
{
    vti_pace p;
    vti_pace_init(&p);
    vti_pace_data(&p, 0);
    for (uint64_t t = 5000; t < 5010; t++) { // long quiet, option off: every millisecond
        vti_pace_rest(&p, 2);
        CHECK_EQ(vti_pace_due(&p, t * ms), 2);
    }
    p.slow = 1; // without any data seen yet the quiet time counts from the first paced read
    vti_pace_init(&p);
    p.slow = 1;
    vti_pace_rest(&p, 1);
    CHECK_EQ(vti_pace_due(&p, 7000 * ms), 1);
    vti_pace_rest(&p, 1);
    CHECK_EQ(vti_pace_due(&p, 7001 * ms), 1);
}

// where the broker looks for the option: the plug-in's folder of the user it runs as
TEST(idlepace_option_path)
{
    char b[256];
    CHECK(vti_pace_option_path(b, sizeof b, "/home/u", 0) == 0);
    CHECK(std::string(b) == "/home/u/.local/share/virus-ti-native/snow-idle-slow.on");
    CHECK(vti_pace_option_path(b, sizeof b, "/Users/u", 1) == 0);
    CHECK(std::string(b) == "/Users/u/Library/Application Support/virus-ti-native/snow-idle-slow.on");
    CHECK(vti_pace_option_path(b, sizeof b, nullptr, 0) < 0); // no HOME: off
    CHECK(vti_pace_option_path(b, sizeof b, "", 0) < 0);
    CHECK(vti_pace_option_path(b, 16, "/home/u", 0) < 0); // never cut
}
