// userrom.h - which ROM banks hold the user's own patches (issue #22, E381/E382).
//
// Why: with the original Control Center a user can burn own banks into the ROM slots A-Z. TI Revive took every ROM
// bank from the factory firmware.bin, so the browser showed factory names, and a program chosen on the Virus was
// answered with the factory dump (E152 loads from the cache). The original reads a record in the identification
// blocks it fetches at start (the same read-only reads as our broker, flashinfo.h): 'FLSH' at FF7FC0, FF7FE0 = 6 or
// 7, then the big-endian u32 at FF7FD2..FF7FD5 whose bit 31 is ROM A .. bit 6 ROM Z. A marked bank is not taken from
// firmware.bin but read from the Virus once (30 <5+i> pp, captured E382) and cached.
#pragma once
#include "memsync.h"
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace vti {

// `blocks` = the broker's identification blocks (flashinfo.h order: FF7FA0..FF7FF0, then 800000..800050).
// Returns bit i = ROM bank A+i is a user bank, for the first `rom_banks` banks; 0 when the record is not valid.
inline uint32_t user_rom_mask(const std::vector<uint8_t> &blocks, int rom_banks)
{
    if (blocks.size() < 0x50 || blocks[0x20] != 'F' || blocks[0x21] != 'L' || blocks[0x22] != 'S' ||
        blocks[0x23] != 'H' || (blocks[0x40] != 6 && blocks[0x40] != 7))
        return 0;
    uint32_t be = (uint32_t)blocks[0x32] << 24 | (uint32_t)blocks[0x33] << 16 | (uint32_t)blocks[0x34] << 8 | blocks[0x35];
    uint32_t mask = 0;
    for (int i = 0; i < 26 && i < rom_banks; i++)
        if (be >> (31 - i) & 1)
            mask |= 1u << i;
    return mask;
}

// letters A..Z (either case) -> bit i = ROM bank A+i; everything else is skipped (the test switch rom-user.on)
inline uint32_t rom_letters_mask(const std::string &s)
{
    uint32_t mask = 0;
    for (char c : s) {
        if (c >= 'a' && c <= 'z')
            c = (char)(c - 'a' + 'A');
        if (c >= 'A' && c <= 'Z')
            mask |= 1u << (c - 'A');
    }
    return mask;
}

inline std::string rom_mask_letters(uint32_t mask)
{
    std::string s;
    for (int i = 0; i < 26; i++)
        if (mask >> i & 1)
            s += (char)('A' + i);
    return s;
}

// The cache folder of a Virus' user banks: a hash (FNV-1a, 64 bit) of all identification bytes and the mask, so each
// unit and each flash state gets its own folder - the original keys its cache by device and re-reads the user banks
// when the FLSH stamp changes. The identification bytes themselves (they hold the serial number) are not written.
inline std::string user_rom_key(const std::vector<uint8_t> &blocks, uint32_t mask)
{
    uint64_t h = 1469598103934665603ull;
    auto add = [&h](uint8_t b) {
        h ^= b;
        h *= 1099511628211ull;
    };
    for (uint8_t b : blocks)
        add(b);
    for (int i = 0; i < 4; i++)
        add((uint8_t)(mask >> (8 * i)));
    char out[17];
    snprintf(out, sizeof out, "%016llx", (unsigned long long)h);
    return out;
}

// A user bank's cache file (<cache>/rom-<key>/ROM X) into `bank`: programs that are not a single dump stay empty and
// are read from the Virus; a file that does not load completely (cut short, not a bank file) counts for nothing - an
// entry cut inside would otherwise look like a patch of zeros. Returns the number of programs to read.
inline int load_user_rom_bank(const std::string &path, Bank &bank)
{
    Bank b;
    if (!load_bank_file(path, b))
        b = Bank();
    int missing = 0;
    for (auto &d : b)
        if (d.size() != 524) {
            d.clear();
            missing++;
        }
    bank = std::move(b);
    return missing;
}

} // namespace vti
