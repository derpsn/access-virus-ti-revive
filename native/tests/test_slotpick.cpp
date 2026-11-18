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

TEST(slotpick_skips_the_slot_just_given_up) // audit 2026-11-18 #1, #5
{
    bool taken[4] = {false, false, false, false};
    // a project saved with Virus 2 on a computer with one Virus: slot 2 was tried and given up - Virus 1 next
    CHECK(vti::slot_choose(taken, 4, 2, 2) == 1);
    // the next round (slot 1 given up, e.g. another program has it): the project's slot again
    CHECK(vti::slot_choose(taken, 4, 2, 1) == 2);
    taken[0] = taken[2] = taken[3] = true;
    CHECK(vti::slot_choose(taken, 4, 0, 2) == 2); // the only free one: tried again rather than none
}

TEST(slotpick_gives_up_a_slot_another_program_has) // audit 2026-11-18 #4: Bitwig sandbox, two DAWs
{
    CHECK(vti::slot_give_up(1, true, true, 3000));   // Virus 1 is another process's: try Virus 2
    CHECK(!vti::slot_give_up(1, true, true, 2999));  // the broker's state needs a moment
    CHECK(!vti::slot_give_up(1, true, false, 60000)); // ours: keep it
}

TEST(slotpick_saves_the_preferred_slot_between_tries) // audit 2026-11-18 #2
{
    CHECK(vti::slot_to_save(2, 1) == 2); // the slot in use
    CHECK(vti::slot_to_save(0, 2) == 2); // between two tries, or not connected yet: the project's own
    CHECK(vti::slot_to_save(0, 0) == 0); // nothing known: no record
}
