// What the plug-in leaves the Virus with when it closes (closeseq.h): the original's sequence (E391), byte for byte as it
// is on the wire in its captures. Only known writes: rule 2.
#include "check.h"
#include "device/closeseq.h"

using namespace vti;

TEST(closeseq_is_the_originals_order)
{
    const auto m = close_messages_first();
    CHECK_EQ(m.size(), 4u);
    using B = std::vector<uint8_t>;
    CHECK(m.size() == 4 && m[0] == (B{0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x73, 0x00, 0x3B, 0x01, 0xF7})); // MIDI data cable 1
    CHECK(m.size() == 4 && m[1] == (B{0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x73, 0x00, 0x3A, 0x01, 0xF7})); // sound dump cable 1
    CHECK(m.size() == 4 && m[2] == (B{0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x73, 0x00, 0x3C, 0x01, 0xF7})); // panel cable 1
    CHECK(m.size() == 4 && m[3] == (B{0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x73, 0x00, 0x7A, 0x00, 0xF7})); // Mode SINGLE
    CHECK(close_message_last() == (B{0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x73, 0x00, 0x7A, 0x00, 0xF7}));  // again, later
    CHECK_EQ(CLOSE_WAIT_MS, 350);
}

TEST(closeseq_never_touches_a_forbidden_parameter)
{
    // the Multi parameter "Original Patch" (72 00 03) must never go out (rule 2), and nothing but page 73 belongs here
    for (const auto &m : close_messages_first())
        CHECK(m.size() == 11 && m[6] == 0x73 && m[7] == 0x00);
    CHECK(close_message_last()[6] == 0x73);
}
