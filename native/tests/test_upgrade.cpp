// RemoteDevice::upgrade_single on generated single dumps: every format version 0..13, edit buffer and stored banks,
// byte values biased towards the ones the upgrade reacts to. Only hashes of the results are compared (recorded once,
// 2026-10-14), so any change of behaviour in the upgrade shows up here.
#include "check.h"
#include "model/remotedevice.h"

using namespace vti;
using Bytes = std::vector<uint8_t>;

namespace {
struct Rng {
    uint64_t s = 0x9E3779B97F4A7C15ull;
    uint32_t next()
    {
        s ^= s << 13, s ^= s >> 7, s ^= s << 17;
        return (uint32_t)(s >> 11);
    }
    uint8_t byte() // 7-bit, often a value that upgrade conditions test
    {
        static const uint8_t hot[] = {0, 1, 2, 3, 4, 5, 6, 0x12, 0x14, 0x17, 0x3A, 0x40, 0x52, 0x7F};
        uint32_t r = next();
        return r % 3 ? hot[(r >> 4) % sizeof hot] : (uint8_t)((r >> 4) & 0x7F);
    }
};

uint64_t fnv(uint64_t h, const Bytes &b)
{
    for (uint8_t c : b)
        h = (h ^ c) * 0x100000001B3ull;
    return h;
}

Bytes dump(Rng &r, int version, int bank)
{
    Bytes m(524);
    for (auto &c : m)
        c = r.byte();
    const uint8_t head[] = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x10};
    for (int i = 0; i < 7; i++)
        m[i] = head[i];
    m[7] = (uint8_t)bank, m[8] = (uint8_t)(r.next() & 0x7F), m[9] = (uint8_t)version, m[523] = 0xF7;
    return m;
}
} // namespace

TEST(upgrade_single_golden)
{
    Rng r;
    Bytes page_b(256);
    for (auto &c : page_b)
        c = r.byte();
    uint64_t all = 0xCBF29CE484222325ull;
    uint64_t per_version[14];
    int changed = 0;
    for (int v = 0; v < 14; v++) {
        uint64_t h = 0xCBF29CE484222325ull;
        for (int i = 0; i < 220; i++) {
            Bytes m = dump(r, v, i % 5);
            Bytes b = i % 11 == 10 ? Bytes{} : page_b; // sometimes without page B: versions < 8 stay as they are
            bool c = RemoteDevice::upgrade_single(m, b);
            changed += c;
            m.push_back(c);
            h = fnv(h, m);
        }
        per_version[v] = h;
        all = fnv(all, Bytes((uint8_t *)&h, (uint8_t *)&h + 8));
    }
    static const uint64_t golden[14] = {
        0x732f41ccd5ef78abull, 0x0e215080a058b610ull, 0x80e9bf4676b6cc28ull, 0x2b45a0bb50155c33ull,
        0x550e614308b2b982ull, 0xd63c5a96461db930ull, 0x75965e1a1640119aull, 0x5e150d43e89973baull,
        0x7711338b7e0260afull, 0x2a6554ae328184e3ull, 0xcf3f6979db7bb4c3ull, 0xe0eb4dffc2965c95ull,
        0x4589de0a2cb28b29ull, 0xec6c021e6159535full};
    for (int v = 0; v < 14; v++)
        if (per_version[v] != golden[v])
            std::fprintf(stderr, "  version %d differs: %016llx\n", v, (unsigned long long)per_version[v]);
    CHECK(all == 0xc708b30d1ca844f9ull);
    CHECK(changed == 1952);
}

// #14: the Virus shows a part's patch location from single-dump bytes 10..12 (E156). Like the original before a patch
// load (0x180083ae0, E369): a patch from a bank of the Virus carries 0, the bank (RAM A..D = 1..4, ROM A.. = 5..: our
// bank 0.. + 1) and its index - the display then reads "RAM-B 4"; any other source sets byte 10 = 0x20 ("eLIB").
TEST(patch_origin_bytes)
{
    std::vector<uint8_t> d(524, 0);
    d[10] = 7, d[11] = 8, d[12] = 9;
    set_patch_origin(d, 1, 4, true); // RAM B, the 5th entry
    CHECK(d[10] == 0 && d[11] == 2 && d[12] == 4);
    set_patch_origin(d, 4, 127, true); // ROM A, the last entry
    CHECK(d[10] == 0 && d[11] == 5 && d[12] == 127);
    set_patch_origin(d, 100, 3, false); // a library: only the marker
    CHECK(d[10] == 0x20 && d[11] == 5 && d[12] == 127);
    std::vector<uint8_t> shortd(10, 0);
    set_patch_origin(shortd, 1, 4, true); // not a single dump: left alone
    CHECK(shortd == std::vector<uint8_t>(10, 0));
}
