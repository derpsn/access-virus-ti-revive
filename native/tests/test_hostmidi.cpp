// What the host hands process() becomes MIDI bytes for the Virus (hostmidi.h, review 2026-10-19): a value outside the
// VST3 range (velocity 1.5, a negative aftertouch, pitch 200) gave a byte >= 0x80, which the Virus reads as a status
// byte (0xF4: its DSP's private command parser, rule 2); a tempo of 0 or below made the MIDI clock loop endless on
// the audio thread.
#include "check.h"
#include "device/hostmidi.h"
#include <cmath>
#include <limits>

using namespace vti;

TEST(hostmidi_data_bytes_stay_below_0x80)
{
    CHECK(data7(0.0) == 0 && data7(1.0) == 127 && data7(0.5) == 64);
    CHECK(data7(1.5) == 127);
    CHECK(data7(-0.3) == 0);
    CHECK(data7(std::numeric_limits<double>::quiet_NaN()) == 0);
    CHECK(note7(60) == 60 && note7(200) == 127 && note7(-5) == 0);
}

TEST(hostmidi_pitch_bend_stays_14_bit)
{
    CHECK(bend14(0.5) == 8192);
    CHECK(bend14(1.0) == 16383 && bend14(2.0) == 16383);
    CHECK(bend14(-1.0) == 0);
    CHECK((bend14(7.0) >> 7) < 0x80);
}

TEST(hostmidi_clock_tempo_is_sane)
{
    CHECK(clock_tempo(120.0) == 120.0);
    CHECK(clock_tempo(0.0) == 120.0);   // no tempo: the default, like without a process context
    CHECK(clock_tempo(-60.0) == 120.0);
    CHECK(clock_tempo(std::numeric_limits<double>::quiet_NaN()) == 120.0);
    CHECK(clock_tempo(std::numeric_limits<double>::infinity()) == 120.0); // no usable tempo either
    CHECK(data7(std::numeric_limits<double>::infinity()) == 0);
    CHECK(bend14(-std::numeric_limits<double>::infinity()) == 0);
    CHECK(clock_tempo(5000.0) == 999.0); // at most ~5 ticks per 512-sample block
    CHECK(clock_tempo(0.5) == 1.0);
}

// Issue #18: the mod wheel did nothing in Ableton Live - a VST3 plug-in gets a controller only when it maps it to a
// parameter (IMidiMapping), and only aftertouch and pitch bend were mapped. Controllers 0..119 now map too; the
// channel mode messages 120..127 (all notes off, reset, ...) stay unmapped.
TEST(hostmidi_mapped_controllers)
{
    CHECK(mapped_controller(1) && mapped_controller(0) && mapped_controller(119));
    CHECK(mapped_controller(128) && mapped_controller(129)); // VST3 kAfterTouch, kPitchBend
    CHECK(!mapped_controller(120) && !mapped_controller(127) && !mapped_controller(130) && !mapped_controller(-1));
    uint8_t m[3] = {};
    CHECK(mapped_message(2, 1, 1.0, m) == 3 && m[0] == 0xB2 && m[1] == 1 && m[2] == 127); // mod wheel, channel 3
    CHECK(mapped_message(0, 64, 0.0, m) == 3 && m[0] == 0xB0 && m[1] == 64 && m[2] == 0);
    CHECK(mapped_message(15, 128, 0.5, m) == 2 && m[0] == 0xDF && m[1] == 64);              // aftertouch
    CHECK(mapped_message(0, 129, 0.5, m) == 3 && m[0] == 0xE0 && m[1] == 0 && m[2] == 64);  // pitch bend centre
    CHECK(mapped_message(0, 121, 1.0, m) == 0);                                            // reset: never
    CHECK(mapped_message(0, 7, 2.0, m) == 3 && m[2] == 127);                               // data byte < 0x80
}
