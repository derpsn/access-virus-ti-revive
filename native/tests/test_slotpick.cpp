// Several Viruses (#45): which broker slot a plug-in instance takes (src/device/slotpick.h). Like the original
// (E553): a new instance takes a free Virus, the one of its project when that one is free. An instance on Virus 2+
// whose broker finds no Virus gives the slot up and waits like a second instance did before: with one Virus nothing
// changes for the user.
#include "check.h"
#include "device/slotpick.h"
#include <string>
#include <vector>

TEST(slotpick_takes_the_lowest_free_slot)
{
    bool taken[4] = {false, false, false, false};
    CHECK(vti::slot_choose(taken, 4, 0, 0) == 1);
    taken[0] = true;
    CHECK(vti::slot_choose(taken, 4, 0, 0) == 2);
    taken[1] = taken[2] = taken[3] = true;
    CHECK(vti::slot_choose(taken, 4, 0, 0) == 0); // all taken: wait
}

TEST(slotpick_prefers_the_projects_slot_when_free)
{
    bool taken[4] = {false, false, false, false};
    CHECK(vti::slot_choose(taken, 4, 2, 0) == 2); // the project was saved with Virus 2
    taken[1] = true;
    CHECK(vti::slot_choose(taken, 4, 2, 0) == 1); // another instance has it: the lowest free one
    CHECK(vti::slot_choose(taken, 4, 9, 0) == 1); // a bad number from a project: ignored
}

TEST(slotpick_gives_up_an_empty_higher_slot)
{
    CHECK(!vti::slot_give_up(1, false, false, 60000)); // Virus 1 absent: the "No Virus TI" page, as today
    CHECK(!vti::slot_give_up(2, true, false, 60000));  // Virus 2 there: keep it
    CHECK(!vti::slot_give_up(2, false, false, 2999));  // a new broker needs a moment to open its Virus
    CHECK(vti::slot_give_up(2, false, false, 3000));   // no Virus 2: wait for Virus 1 like a second instance
}

TEST(slotpick_core_audio_device_by_usb_serial)
{
    // macOS: the plug-in reads its Virus from Core Audio itself; with several "Virus" devices the one whose UID
    // carries this broker's USB serial string, else the slot-th by UID (made-up UIDs; format not verified on a Mac)
    const std::vector<std::string> one = {"uid-b"};
    CHECK(vti::core_audio_pick(one, "", 2) == 0); // one Virus device: it, as before
    const std::vector<std::string> two = {"x:Virus TI:SERB:1", "x:Virus TI:SERA:1"};
    CHECK(vti::core_audio_pick(two, "SERA", 1) == 1);
    CHECK(vti::core_audio_pick(two, "SERB", 1) == 0); // the serial wins over the slot
    CHECK(vti::core_audio_pick(two, "", 1) == 1);     // no serial: by UID order, "SERA" first
    CHECK(vti::core_audio_pick(two, "", 2) == 0);
    CHECK(vti::core_audio_pick(two, "NONE", 3) == -1);
    CHECK(vti::core_audio_pick({}, "SERA", 1) == -1);
}

TEST(slotpick_skips_the_slots_given_up) // audit 2026-11-18 #1, #5; round 0.8.42 test: no cycling
{
    using vti::slot_bit;
    bool taken[4] = {false, false, false, false};
    // a project saved with Virus 2 on a computer with one Virus: slot 2 was tried and given up - Virus 1 next
    CHECK(vti::slot_choose(taken, 4, 2, slot_bit(2)) == 1);
    // slot 1 given up (another program has it): the project's slot
    CHECK(vti::slot_choose(taken, 4, 2, slot_bit(1)) == 2);
    // one Virus, two instances: the second one has tried Virus 2 - Virus 3 next, never Virus 2 again in this round
    taken[0] = true;
    CHECK(vti::slot_choose(taken, 4, 0, slot_bit(2)) == 3);
    CHECK(vti::slot_choose(taken, 4, 0, slot_bit(2) | slot_bit(3)) == 4);
    // every free slot tried: wait like a second instance (0.8.41) instead of going round 2 -> 3 -> 2
    CHECK(vti::slot_choose(taken, 4, 0, slot_bit(2) | slot_bit(3) | slot_bit(4)) == 0);
    CHECK(vti::slot_choose(taken, 4, 3, slot_bit(2) | slot_bit(3) | slot_bit(4)) == 0); // also the project's
}

TEST(slotpick_tries_again_after_a_long_wait) // a Virus plugged in later, a program that let go
{
    CHECK(!vti::slot_retry_due(0, 600000));                      // nothing given up: nothing to retry
    CHECK(!vti::slot_retry_due(vti::slot_bit(2), vti::SLOT_RETRY_MS - 1));
    CHECK(vti::slot_retry_due(vti::slot_bit(2), vti::SLOT_RETRY_MS));
    CHECK(vti::SLOT_RETRY_MS >= 10 * vti::SLOT_GIVE_UP_MS); // rare against the give-up time
}

TEST(slotpick_gives_up_a_slot_another_program_has) // audit 2026-11-18 #4: Bitwig sandbox, two DAWs
{
    CHECK(vti::slot_give_up(1, true, true, 3000));   // Virus 1 is another process's: try Virus 2
    CHECK(!vti::slot_give_up(1, true, true, 2999));  // the broker's state needs a moment
    CHECK(!vti::slot_give_up(1, true, false, 60000)); // ours: keep it
}

TEST(slotpick_saves_the_preferred_slot_between_tries) // audit 2026-11-18 #2
{
    // the first argument is the slot where a Virus answered (round 0.8.42 test: a project saved while the second
    // instance tried the empty Virus 3 reopened on Virus 3)
    CHECK(vti::slot_to_save(2, 1) == 2); // the Virus in use
    CHECK(vti::slot_to_save(0, 2) == 2); // between two tries, or no Virus found yet: the project's own
    CHECK(vti::slot_to_save(0, 0) == 0); // nothing known: no record
}

// #50 (E562): with two Viruses on a Mac the second instance never opened its Core Audio input and the reason went to
// stderr only. The pick's outcome is now one line for the plug-in log - how it was matched, never the serial (rule 5)
TEST(slotpick_core_audio_note_names_the_outcome_without_the_serial)
{
    CHECK(vti::core_audio_note(0, 1, -1, false) == "no Virus audio device in Core Audio (Virus 1)");
    CHECK(vti::core_audio_note(1, 1, 0, false) == "1 Virus audio device, taken for Virus 1");
    CHECK(vti::core_audio_note(2, 1, 1, true) == "2 Virus audio devices, Virus 1 found by its USB serial");
    CHECK(vti::core_audio_note(2, 2, 0, false) == "2 Virus audio devices, Virus 2 taken by order (no serial match)");
    CHECK(vti::core_audio_note(2, 3, -1, false) == "2 Virus audio devices, Virus 3 not among them");
}

// #50: which Virus an instance plays, next to the model in the browser and the menus - only with several Viruses
// (this instance on Virus 2+, or another instance of this process on another Virus), so one Virus looks as before
TEST(slotpick_label_only_with_several_viruses)
{
    CHECK(vti::slot_label(0, false) == "");
    CHECK(vti::slot_label(1, false) == "");         // one Virus: as before
    CHECK(vti::slot_label(1, true) == " - Virus 1"); // another instance has Virus 2
    CHECK(vti::slot_label(2, false) == " - Virus 2");
    CHECK(vti::slot_label(0, true) == "");          // not connected yet: no number
}
