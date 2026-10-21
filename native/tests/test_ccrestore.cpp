// The Control Center's RAM restore (our own design, not the original's flash path - E313): which slots it writes and
// how one dump goes out. Hardware-free; the round trip against a fake Virus is in test_broker.cpp.
#include "check.h"
#include "cc/controlcenter.h"

using namespace vti;

static Dump patch(int bank, int prog, int seed)
{
    Dump d(524, 0);
    const uint8_t head[9] = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x10, (uint8_t)bank, (uint8_t)prog};
    std::copy(head, head + 9, d.begin());
    for (size_t i = 9; i < 523; i++)
        d[i] = (uint8_t)((i * 3 + seed * 7) & 0x7F);
    d[523] = 0xF7;
    return d;
}

TEST(ccrestore_plan_writes_only_what_differs)
{
    Bank current;
    std::vector<Dump> backup(128);
    for (int p = 0; p < 128; p++) {
        current[p] = patch(2, p, p);
        backup[p] = patch(1, p, p); // a library: bank byte 1, the same sound
    }
    backup[3] = patch(1, 3, 500);  // other sound
    backup[3][265] ^= 1;           // the library's checksum does not matter
    backup[90] = patch(1, 90, 77);
    backup[91].clear();            // a gap in the backup: left alone
    current[100].clear();          // not cached: written
    backup[127][8] = 5;            // a stale program byte alone is no difference
    auto w = cc::restore_plan(2, backup, current);
    CHECK(w.size() == 3);
    if (w.size() == 3) {
        CHECK(w[0].slot == 3 && w[1].slot == 90 && w[2].slot == 100);
        const Dump &d = w[0].dump;
        CHECK(d.size() == 524 && d[7] == 2 && d[8] == 3); // addressed to RAM B, program 3
        unsigned a = 0;
        for (int i = 5; i < 265; i++)
            a += d[i];
        CHECK(d[265] == (a & 0x7F));
        for (int i = 265; i < 522; i++)
            a += d[i];
        CHECK(d[522] == (a & 0x7F));
        CHECK(std::equal(d.begin() + 9, d.begin() + 265, backup[3].begin() + 9)); // the sound unchanged
    }
    CHECK(cc::restore_plan(0, backup, current).empty()); // RAM 1..4 only
    CHECK(cc::restore_plan(5, backup, current).empty());
    std::vector<Dump> junk(128, Dump(10, 0));
    CHECK(cc::restore_plan(1, junk, current).empty()); // no single dumps: nothing
}

TEST(ccrestore_dump_pieces)
{
    // like the plug-in (dumppacer.h): USB-MIDI on cable 3, 64 bytes per write; 524 bytes = 175 packets = 700 bytes
    auto pieces = cc::dump_pieces(patch(1, 0, 0));
    CHECK(pieces.size() == 11);
    size_t total = 0;
    for (auto &p : pieces) {
        total += p.size();
        for (size_t i = 0; i < p.size(); i += 4)
            CHECK((p[i] >> 4) == 3);
    }
    CHECK(total == 700 && pieces[0].size() == 64 && pieces.back().size() == 60);
}

TEST(ccrestore_bank_from_name)
{
    // the bank a backup file is for: the Control Center's backups are "RAM A.mid" .. "RAM D.mid"
    CHECK(cc::bank_from_name("RAM B") == 2);
    CHECK(cc::bank_from_name("Restore RAM D (2026-10-18)") == 4);
    CHECK(cc::bank_from_name("ram a") == 1);
    CHECK(cc::bank_from_name("RAM E") == 0);
    CHECK(cc::bank_from_name("RAMBO") == 0);
    CHECK(cc::bank_from_name("RAM AB") == 0); // a word, not a bank letter
    CHECK(cc::bank_from_name("My Patches") == 0);
}
