// User ROM banks (userrom.h, E381/E382, issue #22): which ROM banks hold the user's own patches, read like the
// original from the identification blocks the broker already reads at every start (flashinfo.h order: FF7FA0..FF7FF0,
// then 800000..800050). The original: 'FLSH' at FF7FC0, FF7FE0 = 6 or 7, then the big-endian u32 at FF7FD2..D5,
// bit 31 = ROM A .. bit 6 = ROM Z.
#include "check.h"
#include "device/userrom.h"
#include <string>
#include <vector>

using namespace vti;

// identification blocks like our TI2's: 'FLSH' + stamp at FF7FC0, FF7FE0 = 7, a bank mask at FF7FD2..D5
static std::vector<uint8_t> blocks(uint32_t mask_be, bool flsh = true, uint8_t fe0 = 7)
{
    std::vector<uint8_t> b(12 * 16, 0);
    if (flsh) {
        const char *s = "FLSH200801010000";
        for (int i = 0; i < 16; i++)
            b[0x20 + i] = (uint8_t)s[i];
        b[0x30] = '0';
        b[0x31] = '1';
    }
    b[0x32] = (uint8_t)(mask_be >> 24);
    b[0x33] = (uint8_t)(mask_be >> 16);
    b[0x34] = (uint8_t)(mask_be >> 8);
    b[0x35] = (uint8_t)mask_be;
    b[0x40] = fe0;
    return b;
}

TEST(userrom_factory_unit_has_none)
{
    // our TI2 Desktop (2026-11-01): FF7FD2..D5 = 00 00 00 00
    CHECK_EQ(user_rom_mask(blocks(0), 26), 0u);
}

TEST(userrom_bit_order)
{
    // bit i of the result = ROM bank A+i
    CHECK_EQ(user_rom_mask(blocks(0x80000000u), 26), 1u);         // FF7FD2 bit 7 = ROM A
    CHECK_EQ(user_rom_mask(blocks(0x40000000u), 26), 2u);         // ROM B
    CHECK_EQ(user_rom_mask(blocks(0x00000040u), 26), 1u << 25);   // FF7FD5 bit 6 = ROM Z
    CHECK_EQ(user_rom_mask(blocks(0x0000003Fu), 26), 0u);         // the low 6 bits mean nothing
    CHECK_EQ(user_rom_mask(blocks(0xFFFFFFFFu), 26), (1u << 26) - 1);
}

TEST(userrom_only_existing_banks)
{
    // letters beyond the model's ROM banks have no bank (the original creates banks only up to its count)
    CHECK_EQ(user_rom_mask(blocks(0xFFFFFFFFu), 2), 3u);
    CHECK_EQ(user_rom_mask(blocks(0xFFFFFFFFu), 0), 0u);
}

TEST(userrom_invalid_block_means_factory)
{
    // without 'FLSH' the original clears the record (all banks factory); FF7FE0 other than 6/7 or a short read:
    // the original rejects the device - for us nothing is a user bank, as before
    CHECK_EQ(user_rom_mask(blocks(0x80000000u, false), 26), 0u);
    CHECK_EQ(user_rom_mask(blocks(0x80000000u, true, 5), 26), 0u);
    CHECK_EQ(user_rom_mask(blocks(0x80000000u, true, 6), 26), 1u);
    CHECK_EQ(user_rom_mask(std::vector<uint8_t>(40, 0), 26), 0u);
    CHECK_EQ(user_rom_mask({}, 26), 0u);
}

TEST(userrom_letters)
{
    // the test switch (rom-user.on) and the log use letters
    CHECK_EQ(rom_letters_mask("Z"), 1u << 25);
    CHECK_EQ(rom_letters_mask("a, C\n"), 5u);
    CHECK_EQ(rom_letters_mask(""), 0u);
    CHECK_EQ(rom_letters_mask("1-?"), 0u);
    CHECK(rom_mask_letters(5u) == "AC");
    CHECK(rom_mask_letters(0u).empty());
    CHECK(rom_mask_letters(1u << 25) == "Z");
}

TEST(userrom_cache_key)
{
    // one cache folder per Virus and flash state: another unit (other identification bytes), banks burned again
    // (new stamp) or another mask read their user banks anew; the same unit finds its folder again
    auto a = blocks(0x80000000u);
    std::string k = user_rom_key(a, 1u);
    CHECK_EQ(k.size(), 16u);
    CHECK(k == user_rom_key(a, 1u));
    auto other = a;
    other[12 * 16 - 1] ^= 1; // another unit's identification data
    CHECK(k != user_rom_key(other, 1u));
    auto restamped = a;
    restamped[0x2B] = '2'; // the stamp changes when banks are burned
    CHECK(k != user_rom_key(restamped, 1u));
    CHECK(k != user_rom_key(a, 3u)); // the test switch adds a bank
    for (char c : k)
        CHECK((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
}
