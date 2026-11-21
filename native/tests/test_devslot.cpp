// Several Viruses at once (#45, E553): one broker per Virus. Broker N takes the N-th Virus in the order of a stable
// per-device key (the USB port path on libusb, the parent device instance on Windows), and its pipe, socket, audio
// memory and log get the suffix "-N"; slot 1 keeps today's names (src/broker/devslot.h).
#include "check.h"
#include <cstring>
extern "C" {
#include "devslot.h"
}

TEST(slot_argument_is_one_to_four)
{
    CHECK(vti_slot_parse("1") == 1);
    CHECK(vti_slot_parse("4") == 4);
    CHECK(vti_slot_parse("0") == 0);
    CHECK(vti_slot_parse("5") == 0);
    CHECK(vti_slot_parse("2x") == 0);
    CHECK(vti_slot_parse("") == 0);
    CHECK(vti_slot_parse(nullptr) == 0);
}

TEST(slot_picks_the_nth_device_in_key_order)
{
    const char *keys[] = {"3-2", "1-4", "2-1"}; // the order the system listed them in
    CHECK(vti_slot_pick(keys, 3, 1) == 1);      // "1-4"
    CHECK(vti_slot_pick(keys, 3, 2) == 2);      // "2-1"
    CHECK(vti_slot_pick(keys, 3, 3) == 0);      // "3-2"
    CHECK(vti_slot_pick(keys, 3, 4) == -1);     // no fourth Virus
    CHECK(vti_slot_pick(keys, 0, 1) == -1);
}

TEST(slot_pick_is_stable_for_equal_keys)
{
    const char *keys[] = {"x", "x"}; // never two at one port, but the pick must not hand both brokers the same one
    CHECK(vti_slot_pick(keys, 2, 1) == 0);
    CHECK(vti_slot_pick(keys, 2, 2) == 1);
}

TEST(slot_one_keeps_todays_names)
{
    char s[4];
    vti_slot_suffix(s, 1);
    CHECK(strcmp(s, "") == 0);
    vti_slot_suffix(s, 2);
    CHECK(strcmp(s, "-2") == 0);
    vti_slot_suffix(s, 4);
    CHECK(strcmp(s, "-4") == 0);
}

TEST(slot_names_the_midi_ports_like_the_first_virus)
{
    char n[32];
    vti_slot_port_name(n, sizeof n, "Synth", 1);
    CHECK(strcmp(n, "TI Revive Synth") == 0); // was "Virus TI Synth" until 0.8.42 (branding cleanup, 2026-11-19)
    vti_slot_port_name(n, sizeof n, "MIDI", 2);
    CHECK(strcmp(n, "TI Revive 2 MIDI") == 0);
    vti_slot_port_name(n, sizeof n, "", 3);
    CHECK(strcmp(n, "TI Revive 3") == 0); // the Linux sequencer client
}

TEST(slot_port_ids_never_collide)
{
    CHECK(vti_slot_port_id(1, 1) == 0x56544931); // "VTI1": Virus 1 keeps the IDs DAWs remember
    CHECK(vti_slot_port_id(0, 1) == 0x56544930);
    CHECK(vti_slot_port_id(1, 2) == 0x56543231); // "VT21"
    for (int s = 1; s <= VTI_SLOTS_MAX; s++)
        for (int t = 1; t <= VTI_SLOTS_MAX; t++)
            for (int c = 0; c < 2; c++)
                for (int d = 0; d < 2; d++)
                    CHECK((s == t && c == d) || vti_slot_port_id(c, s) != vti_slot_port_id(d, t));
}
