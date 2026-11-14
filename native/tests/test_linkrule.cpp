// The link global 0x52 (src/broker/linkrule.h, E524): the original's MIDI driver links the Virus with the raw
// handshake 4E 73 52 01 when the first stream opens and unlinks it with 4E 73 52 00 when the last one closes (vtimidi.sys;
// the Mac driver at device release). TI Revive linked at open and never unlinked, so a Keyboard or Polar stayed silent
// on its own keys after the DAW closed until a power cycle (E520 (3), sim S134).
#include "check.h"
#include <cstring>
extern "C" {
#include "linkrule.h"
}

TEST(link_messages_are_the_originals_raw_handshake)
{
    uint8_t m[4];
    vti_link_msg(m, 1);
    CHECK(m[0] == 0x4E && m[1] == 0x73 && m[2] == 0x52 && m[3] == 0x01);
    vti_link_msg(m, 0);
    CHECK(m[0] == 0x4E && m[1] == 0x73 && m[2] == 0x52 && m[3] == 0x00); // as in 16 captures of the original (E524 (3))
}

TEST(link_goes_off_when_the_last_user_leaves)
{
    CHECK(vti_link_off_now(1, 0));  // linked, nobody left: unlink
    CHECK(!vti_link_off_now(1, 1)); // another plug-in or the Control Center still works with it
    CHECK(!vti_link_off_now(0, 0)); // already off: nothing twice
}

TEST(link_comes_back_before_a_user_sends)
{
    CHECK(vti_link_on_now(0));  // unlinked since the last user left: link first, like the original's first stream
    CHECK(!vti_link_on_now(1)); // linked: nothing
}
