// What the host hands process() becomes MIDI bytes for the Virus (hostmidi.h, review 2026-10-19): a value outside the
// VST3 range (velocity 1.5, a negative aftertouch, pitch 200) gave a byte >= 0x80, which the Virus reads as a status
// byte (0xF4: its DSP's private command parser, rule 2); a tempo of 0 or below made the MIDI clock loop endless on
// the audio thread.
#include "check.h"
#include "device/automation.h"
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

// audit 2026-11-17 #4: pitch bend, aftertouch and controllers the host hands over as parameters went to the Virus
// once per block, past the automation budget (a bend ramp at 32 samples / 48 kHz: 1500/s, rule 2). Now only the
// latest value per channel and controller waits, and it shares the AutomationQueue's budget with the parameters.
TEST(hostmidi_mapped_controllers_share_the_automation_budget)
{
    AutomationQueue q(250);
    MappedPending p;
    const uint32_t bend = 0x100000 + 3 * 0x1000 + 129; // channel 4 pitch bend
    int sent = 0;
    for (int block = 0; block < 1500; block++) { // one second
        uint8_t m[3];
        size_t n = mapped_message(3, 129, (block % 100) / 100.0, m);
        if (p.set(3, 129, m, n, 7, block))
            CHECK(q.push(bend));
        q.refill(32, 48000);
        q.drain(96, [&](uint32_t id) {
            MappedPending::Slot s;
            if (id != bend || !p.take(3, 129, s))
                return false;
            CHECK(s.n == 3 && (s.m[0] & 0xF0) == 0xE0);
            sent++;
            return true;
        });
    }
    CHECK(sent >= 240 && sent <= 250 + 64); // the budget, not 1500
}

TEST(hostmidi_mapped_pending_keeps_the_latest_value_and_its_time)
{
    MappedPending p;
    const uint8_t on[3] = {0xB0, 64, 127}, off[3] = {0xB0, 64, 0};
    CHECK(p.set(0, 64, on, 3, 10, 5));   // not waiting yet: the caller queues it
    CHECK(!p.set(0, 64, off, 3, 20, 5)); // already waiting: only the value changes
    MappedPending::Slot s;
    CHECK(p.take(0, 64, s));
    CHECK(s.m[2] == 0 && s.offset == 20 && s.block == 5); // the latest, at its own offset in its block
    CHECK(!p.take(0, 64, s));                              // taken: nothing waits
    CHECK(!p.set(16, 64, on, 3, 0, 0) && !p.set(0, 130, on, 3, 0, 0)); // out of range: ignored
    CHECK(!p.take(16, 64, s) && !p.take(0, -1, s));
}

// review of #4: pedals (CC 64..69) are switches (down from 64 on, like the Virus): they skip the automation queue
// (a sustain-off would wait behind every automated lane) and go out at their own offset, but only when the switch
// flips, so a drawn pedal ramp cannot flood the Virus either
TEST(hostmidi_pedals_go_at_once_but_only_on_a_flip)
{
    PedalSwitch p;
    CHECK(PedalSwitch::pedal(64) && PedalSwitch::pedal(69) && !PedalSwitch::pedal(63) && !PedalSwitch::pedal(70));
    CHECK(p.flips(0, 64, 127));  // the first value always goes (the Virus' state is unknown)
    CHECK(!p.flips(0, 64, 100)); // still down
    CHECK(p.flips(0, 64, 10));   // up
    CHECK(!p.flips(0, 64, 0));
    CHECK(p.flips(1, 64, 0));    // another channel: its own state
    CHECK(p.flips(0, 66, 64));   // another pedal: its own state, 64 = down
    int sent = 0;
    for (int i = 0; i < 1500; i++) // a 0..127 saw, 12 cycles in a second: two flips each, not 1500 messages
        sent += p.flips(2, 64, (i * 127 / 125) % 128);
    CHECK(sent >= 20 && sent <= 25);
}
