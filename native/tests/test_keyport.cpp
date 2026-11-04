// What the broker's MIDI port passes from the Virus to the DAW (keyport.h, #29): the Virus keys and controls, from EP 0x85
// cable 1 (E392), and nothing else. Hardware-free.
#include "check.h"
#include <string>
#include <vector>
extern "C" {
#include "keyport.h"
}

namespace {
std::vector<std::vector<uint8_t>> run(uint8_t ep, std::vector<uint8_t> packets)
{
    std::vector<std::vector<uint8_t>> out;
    vti_keyport_filter(
        ep, packets.data(), (int)packets.size(),
        [](void *u, const uint8_t *m, int n) { ((std::vector<std::vector<uint8_t>> *)u)->push_back(std::vector<uint8_t>(m, m + n)); },
        &out);
    return out;
}
} // namespace

TEST(keyport_passes_key_and_controller_messages_from_cable_1)
{
    auto o = run(0x85, {0x19, 0x90, 60, 100,   // note on
                        0x18, 0x80, 60, 0,     // note off
                        0x1B, 0xB0, 1, 64,     // mod wheel
                        0x1E, 0xE0, 0, 64,     // pitch bend
                        0x1D, 0xD0, 50, 0,     // pressure
                        0x1A, 0xA0, 60, 50,    // poly pressure
                        0x1C, 0xC0, 5, 0});    // program change
    CHECK_EQ(o.size(), 7u);
    CHECK(o.size() == 7 && o[0] == (std::vector<uint8_t>{0x90, 60, 100}) && o[1] == (std::vector<uint8_t>{0x80, 60, 0}));
    CHECK(o.size() == 7 && o[2] == (std::vector<uint8_t>{0xB0, 1, 64}) && o[3] == (std::vector<uint8_t>{0xE0, 0, 64}));
    // pressure and program change are two bytes, not the packet's three
    CHECK(o.size() == 7 && o[4] == (std::vector<uint8_t>{0xD0, 50}) && o[5] == (std::vector<uint8_t>{0xA0, 60, 50}) &&
          o[6] == (std::vector<uint8_t>{0xC0, 5}));
}

TEST(keyport_keeps_the_channel)
{
    auto o = run(0x85, {0x19, 0x93, 60, 100});
    CHECK(o.size() == 1 && o[0][0] == 0x93);
}

TEST(keyport_never_passes_sysex)
{
    // the handshake reply and bank data use this cable outside a session (protocol.md): never into a DAW
    CHECK(run(0x85, {0x14, 0xF0, 0x00, 0x20, 0x14, 0x33, 0x01, 0x10, 0x14, 0x73, 0x40, 0x40, 0x16, 0x00, 0xF7, 0x00}).empty());
    CHECK(run(0x85, {0x15, 0xF7, 0, 0, 0x17, 0x01, 0x02, 0xF7}).empty());
}

TEST(keyport_never_passes_system_messages)
{
    CHECK(run(0x85, {0x1F, 0xF8, 0, 0, 0x1F, 0xFE, 0, 0, 0x12, 0xF3, 1, 0, 0x13, 0xF2, 0, 0}).empty());
}

TEST(keyport_only_this_endpoint_and_cable)
{
    CHECK(run(0x86, {0x19, 0x90, 60, 100}).empty());         // the plug-in's interface
    CHECK(run(0x85, {0x09, 0x90, 60, 100}).empty());         // cable 0: DIN
    CHECK(run(0x85, {0x29, 0x90, 60, 100}).empty());         // cable 2
    CHECK(run(0x85, {0xFF, 0xFE, 0, 0}).empty());            // the Snow's idle packets (E387)
}

TEST(keyport_rejects_a_packet_whose_status_does_not_fit)
{
    // CIN says note on, the status byte is not one: a garbled packet must not become MIDI in the DAW
    CHECK(run(0x85, {0x19, 0x40, 60, 100}).empty());
    CHECK(run(0x85, {0x19, 0x80, 60, 100}).empty());
    // data bytes with the top bit set
    CHECK(run(0x85, {0x19, 0x90, 0x80, 100}).empty());
    CHECK(run(0x85, {0x19, 0x90, 60, 0xFF}).empty());
    // a short tail is no packet
    CHECK(run(0x85, {0x19, 0x90, 60}).empty());
    auto o = run(0x85, {0x00, 0x00, 0x00, 0x00, 0x19, 0x90, 60, 100}); // padding before a real one
    CHECK(o.size() == 1);
}
