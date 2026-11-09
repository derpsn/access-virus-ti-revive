// Byte-slipped iso IN streams (E488): in about 0.4 % of stream starts every sample of the whole stream takes its high
// byte in place and the low byte of the word two places on (one stereo pair), so the plug-in finds no sync marker and
// the notes run early or late. The audio's low bytes change a lot from frame to frame, the high bytes slowly, at any
// content; SlipWatch compares the byte changes at the two positions per half-second window (simulator S120: normal
// windows at most 0.5 in 977 streams, slipped ones 0.99 and up in every window).
#include "check.h"
#include "device/slipwatch.h"
#include <cmath>
#include <cstdint>

using namespace vti;

namespace {

// one frame of the six ring channels: the analog inputs at the ADC's noise floor, music on USB 1, a bass on USB 2
void normal_frame(long f, int16_t out[6])
{
    out[0] = (int16_t)(-4 + (f * 7 % 3) - 1);
    out[1] = (int16_t)(-1 + (f * 5 % 3) - 1);
    out[2] = out[3] = (int16_t)(9000 * std::sin(f * 0.031) + 3000 * std::sin(f * 0.17));
    out[4] = (int16_t)(12000 * std::sin(f * 0.0041));
    out[5] = (int16_t)(11000 * std::sin(f * 0.0043 + 1));
}

// the slip as measured: byte 2k = high byte of word k, byte 2k+1 = low byte of word (k + 2) mod 6
void slip(const int16_t in[6], int16_t out[6])
{
    for (int k = 0; k < 6; k++) {
        const uint16_t hi = ((uint16_t)in[k] >> 8) & 0xFF, lo = (uint16_t)in[(k + 2) % 6] & 0xFF;
        out[k] = (int16_t)(uint16_t)(hi | (lo << 8)); // little endian: byte 2k is the word's low byte
    }
}

int feed(SlipWatch &w, long from, long n, bool slipped, long glitch_at = -1)
{
    int fired = 0;
    for (long f = from; f < from + n; f++) {
        int16_t fr[6], s[6];
        normal_frame(f, fr);
        if (f == glitch_at)
            fr[0] = -32768; // a single full-scale sample on the analog input (seen in several real captures)
        if (slipped) {
            slip(fr, s);
            fired += w.feed(s);
        } else
            fired += w.feed(fr);
    }
    return fired;
}

} // namespace

TEST(slipwatch_normal_music_never_fires)
{
    SlipWatch w;
    w.set_rate(44100);
    CHECK_EQ(feed(w, 0, 44100 * 20, false), 0);
    CHECK(w.last_state() == 0);
    CHECK(w.last_ratio() < 0.1);
}

TEST(slipwatch_slipped_stream_fires_after_two_windows)
{
    SlipWatch w;
    w.set_rate(44100);
    CHECK_EQ(feed(w, 0, 22050 * 2 - 1, true), 0); // not yet: the second window is not complete
    CHECK_EQ(feed(w, 22050 * 2 - 1, 1, true), 1); // fires at the end of the second slipped window
    CHECK(w.last_ratio() > 0.9);
    CHECK_EQ(feed(w, 22050 * 2, 22050, true), 0); // once: then it waits for two new windows
    CHECK_EQ(feed(w, 22050 * 3, 22050, true), 1);
}

TEST(slipwatch_a_glitch_does_not_fire)
{
    SlipWatch w;
    w.set_rate(44100);
    CHECK_EQ(feed(w, 0, 44100 * 5, false, 30000), 0);
    CHECK_EQ(feed(w, 44100 * 5, 44100 * 5, false, 44100 * 5 + 11025), 0);
}

TEST(slipwatch_one_slipped_window_alone_does_not_fire)
{
    SlipWatch w;
    w.set_rate(48000);
    CHECK_EQ(feed(w, 0, 24000, true), 0);
    CHECK_EQ(feed(w, 24000, 24000, false), 0);
    CHECK_EQ(feed(w, 48000, 24000, true), 0);
    CHECK_EQ(feed(w, 72000, 24000, false), 0);
}

TEST(slipwatch_silence_is_undecided)
{
    SlipWatch w;
    w.set_rate(44100);
    int16_t zero[6] = {0, 0, 0, 0, 0, 0};
    int fired = 0;
    for (int i = 0; i < 44100 * 3; i++)
        fired += w.feed(zero);
    CHECK_EQ(fired, 0);
    CHECK(w.last_state() == -1);
}

TEST(slipwatch_silence_between_slipped_windows_does_not_count_as_normal_or_slip)
{
    // an undecided window neither confirms nor breaks a run (the slip is inaudible in silence)
    SlipWatch w;
    w.set_rate(44100);
    int16_t zero[6] = {0, 0, 0, 0, 0, 0};
    CHECK_EQ(feed(w, 0, 22050, true), 0);
    int fired = 0;
    for (int i = 0; i < 22050; i++)
        fired += w.feed(zero);
    CHECK_EQ(fired, 0);
    CHECK_EQ(feed(w, 44100, 22050, true), 1);
}

TEST(slipwatch_reset_starts_afresh)
{
    SlipWatch w;
    w.set_rate(44100);
    CHECK_EQ(feed(w, 0, 22050 + 100, true), 0);
    w.reset(); // a new stream
    CHECK_EQ(feed(w, 0, 22050, true), 0);
    CHECK_EQ(feed(w, 22050, 22050, true), 1);
}

// The restart after a slip goes through the broker's normal stream stop and start (what every session start does,
// alt 0/1 + SET_CUR); bounded so it can never become a loop: at least 10 s apart, at most 5 per plug-in session
TEST(sliprestartgate_spacing_and_limit)
{
    SlipRestartGate g;
    CHECK(g.allow(1000));
    CHECK(!g.allow(5000));  // 4 s later: too soon
    CHECK(g.allow(11000));  // 10 s after the first
    CHECK(g.allow(21000));
    CHECK(g.allow(31000));
    CHECK(g.allow(41000));  // the fifth
    CHECK(!g.allow(51000)); // no sixth in this session
    CHECK_EQ(g.count(), 5);
}
