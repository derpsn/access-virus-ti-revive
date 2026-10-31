// RAM backup (model/rambackup.h, TI Revive's own addition, write policy 2026-10-31): before a SAVE, a browser drop,
// swap, rename, sort or undo writes RAM slots of the Virus, the patches it would lose go into a personal library of
// the day ("RAM Backup 2026-10-31", then " 2", ... at 128 patches), so the browser can drag them back.
// Hardware-free.
#include "check.h"
#include "model/rambackup.h"

using namespace vti;
using Patch = RamBackup::Patch;

static Patch single(int bank, int program, uint8_t sound)
{
    Patch d(524, 0);
    d[0] = 0xF0, d[1] = 0x00, d[2] = 0x20, d[3] = 0x33, d[4] = 0x01, d[5] = 0x10, d[6] = 0x10;
    d[523] = 0xF7;
    d[7] = (uint8_t)bank, d[8] = (uint8_t)program, d[9] = 0x0C;
    for (int i = 11; i < 265; i++)
        d[i] = (uint8_t)((sound + i) & 0x7F);
    for (int i = 266; i < 522; i++)
        d[i] = (uint8_t)((sound * 3 + i) & 0x7F);
    BankDrop::checksums(d);
    return d;
}

static std::map<int, std::vector<Patch>> bank_a(int n)
{
    std::map<int, std::vector<Patch>> b;
    b[0].resize(128);
    for (int p = 0; p < n; p++)
        b[0][p] = single(1, p, (uint8_t)p);
    return b;
}

TEST(rambackup_same_sound_ignores_address_and_checksums)
{
    Patch a = single(1, 5, 7), b = single(3, 99, 7), c = single(1, 5, 8);
    CHECK(RamBackup::same_sound(a, b)); // another slot, the same patch
    CHECK(!RamBackup::same_sound(a, c));
    b[265] ^= 1; // a stale checksum (E378 merge leaves them) is no other sound
    CHECK(RamBackup::same_sound(a, b));
    CHECK(!RamBackup::same_sound(a, Patch(10, 0)));
}

TEST(rambackup_overwritten_patch_is_lost)
{
    auto before = bank_a(4);
    std::vector<BankDrop::Write> w = {{0, 2, single(1, 2, 100)}};
    auto lost = RamBackup::lost(w, before);
    CHECK(lost.size() == 1);
    CHECK(RamBackup::same_sound(lost[0], before[0][2]));
}

TEST(rambackup_same_patch_again_loses_nothing)
{
    auto before = bank_a(4);
    std::vector<BankDrop::Write> w = {{0, 2, single(1, 2, 2)}}; // stored again unchanged
    CHECK(RamBackup::lost(w, before).empty());
}

TEST(rambackup_swap_within_the_bank_loses_nothing)
{
    auto before = bank_a(4);
    std::vector<BankDrop::Write> w = {{0, 0, single(1, 0, 1)}, {0, 1, single(1, 1, 0)}};
    CHECK(RamBackup::lost(w, before).empty()); // both patches are still in the bank afterwards
}

TEST(rambackup_empty_or_unknown_slots_have_nothing_to_lose)
{
    auto before = bank_a(2);
    std::vector<BankDrop::Write> w = {{0, 50, single(1, 50, 9)}, {2, 0, single(3, 0, 9)}};
    CHECK(RamBackup::lost(w, before).empty()); // slot 50 empty, RAM C not in the cache
}

TEST(rambackup_each_lost_patch_once)
{
    auto before = bank_a(4);
    before[0][3] = single(1, 3, 1); // slot 3 holds the same patch as slot 1
    std::vector<BankDrop::Write> w = {{0, 1, single(1, 1, 90)}, {0, 3, single(1, 3, 91)}};
    auto lost = RamBackup::lost(w, before);
    CHECK(lost.size() == 1);
}

TEST(rambackup_library_names)
{
    CHECK(RamBackup::library_name("2026-10-31", 1) == "RAM Backup 2026-10-31");
    CHECK(RamBackup::library_name("2026-10-31", 2) == "RAM Backup 2026-10-31 2");
}

TEST(rambackup_plan_fills_the_day_library_then_the_next)
{
    std::map<std::string, int> have = {{"RAM Backup 2026-10-31", 120}};
    auto count = [&](const std::string &n) {
        auto it = have.find(n);
        return it == have.end() ? 0 : it->second;
    };
    auto plan = RamBackup::plan("2026-10-31", count, 20);
    CHECK(plan.size() == 2);
    CHECK(plan[0].name == "RAM Backup 2026-10-31" && plan[0].count == 8);
    CHECK(plan[1].name == "RAM Backup 2026-10-31 2" && plan[1].count == 12);
    have["RAM Backup 2026-10-31"] = 128;
    plan = RamBackup::plan("2026-10-31", count, 1);
    CHECK(plan.size() == 1 && plan[0].name == "RAM Backup 2026-10-31 2" && plan[0].count == 1);
    CHECK(RamBackup::plan("2026-10-31", count, 0).empty());
}
