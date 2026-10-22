// Library and bank patches dropped onto a RAM slot of the browser's other pane, like the original (E294): each source
// dump goes unchanged into the next slot from the target on (only bank/program and the checksums are new), cut at
// the bank's end; "Undo last operation" writes back what the last drop overwrote. Plus the browser's selection.
#include "check.h"
#include "model/bankdrop.h"
#include <cstring>
using namespace vti;

namespace {
// a library patch as the original's files hold it: bank byte 1, program = its position, bytes 10..12 its origin
std::vector<uint8_t> patch(const char *name, int program, uint8_t flags = 0x20, uint8_t obank = 6, uint8_t oprog = 0x7E)
{
    std::vector<uint8_t> d(524, 0);
    const uint8_t head[] = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x10, 0x01};
    memcpy(d.data(), head, sizeof head);
    d[8] = (uint8_t)program, d[9] = 0x0C, d[10] = flags, d[11] = obank, d[12] = oprog;
    for (int i = 13; i < 249; i++)
        d[i] = (uint8_t)((i * 7 + program) & 0x7F); // sound data
    const size_t n = strlen(name);
    for (size_t i = 0; i < 10; i++)
        d[249 + i] = (uint8_t)(i < n ? name[i] : ' ');
    d[523] = 0xF7;
    BankDrop::address(d, 0, program); // valid checksums
    return d;
}
bool checksums_ok(const std::vector<uint8_t> &d)
{
    unsigned a = 0;
    for (int i = 5; i < 265; i++)
        a += d[i];
    if (d[265] != (a & 0x7F))
        return false;
    for (int i = 265; i < 522; i++)
        a += d[i];
    return d[522] == (a & 0x7F);
}
std::vector<std::vector<uint8_t>> ram_bank(int bank)
{
    std::vector<std::vector<uint8_t>> b;
    for (int p = 0; p < 128; p++) {
        auto d = patch("RAM", p, 0x00, (uint8_t)(bank + 1), (uint8_t)p);
        BankDrop::address(d, bank, p);
        b.push_back(d);
    }
    return b;
}
// every byte but bank (7), program (8) and the two checksums equals the source (E294: the original changes nothing else)
bool same_but_address(const std::vector<uint8_t> &a, const std::vector<uint8_t> &b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); i++)
        if (i != 7 && i != 8 && i != 265 && i != 522 && a[i] != b[i])
            return false;
    return true;
}
} // namespace

// one library patch onto RAM A slot 4 (E294 (2)): one dump to bank 1 program 3, bytes 10..12 as in the file
TEST(bankdrop_single)
{
    BankDrop drop;
    auto src = patch("StarSky", 0);
    auto w = drop.drop({src}, 0, 3, ram_bank(0));
    CHECK(w.size() == 1);
    CHECK(w[0].bank == 0 && w[0].slot == 3);
    CHECK(w[0].dump[7] == 1 && w[0].dump[8] == 3);
    CHECK(w[0].dump[10] == 0x20 && w[0].dump[11] == 6 && w[0].dump[12] == 0x7E);
    CHECK(same_but_address(w[0].dump, src) && checksums_ok(w[0].dump));
    CHECK(drop.can_undo());
}

// nine patches onto slot 8 (E294 (3)): consecutive slots in the given order; RAM D = bank byte 4
TEST(bankdrop_several_consecutive)
{
    BankDrop drop;
    std::vector<std::vector<uint8_t>> src;
    for (int i = 0; i < 9; i++)
        src.push_back(patch("Lib", 4 + i, (uint8_t)i));
    auto w = drop.drop(src, 3, 7, ram_bank(3));
    CHECK(w.size() == 9);
    for (int i = 0; i < (int)w.size(); i++) {
        CHECK(w[i].bank == 3 && w[i].slot == 7 + i);
        CHECK(w[i].dump[7] == 4 && w[i].dump[8] == 7 + i);
        CHECK(same_but_address(w[i].dump, src[i]) && checksums_ok(w[i].dump));
    }
}

// more patches than slots left: cut at the bank's end, nothing wraps to slot 1
TEST(bankdrop_cut_at_bank_end)
{
    BankDrop drop;
    std::vector<std::vector<uint8_t>> src;
    for (int i = 0; i < 5; i++)
        src.push_back(patch("Lib", i));
    auto w = drop.drop(src, 1, 125, ram_bank(1));
    CHECK(w.size() == 3);
    CHECK(w[0].slot == 125 && w[2].slot == 127);
}

// undo writes back what the drop overwrote, addressed to the same slots (the old dump's bytes 10..12 kept); the
// original's "Undo last operation" toggles: the next undo puts the dropped patches back, and so on (E295)
TEST(bankdrop_undo_toggles_last_operation)
{
    BankDrop drop;
    auto before = ram_bank(2);
    auto w = drop.drop({patch("A", 0), patch("B", 1)}, 2, 10, before);
    auto u = drop.take_undo();
    CHECK(u.size() == 2);
    CHECK(u[0].bank == 2 && u[0].slot == 10 && u[0].dump == before[10]);
    CHECK(u[1].bank == 2 && u[1].slot == 11 && u[1].dump == before[11]);
    CHECK(drop.can_undo());
    auto r = drop.take_undo(); // again: the drop comes back
    CHECK(r.size() == 2 && r[0].slot == 10 && r[0].dump == w[0].dump && r[1].dump == w[1].dump);
    auto u2 = drop.take_undo(); // and again the old patches
    CHECK(u2.size() == 2 && u2[0].dump == before[10] && u2[1].dump == before[11]);
}

// a later drop replaces the undo; another store into RAM (SAVE) clears it so undo never writes over newer patches
TEST(bankdrop_undo_is_last_operation_only)
{
    BankDrop drop;
    auto bank = ram_bank(0);
    drop.drop({patch("A", 0)}, 0, 0, bank);
    drop.drop({patch("B", 0)}, 0, 5, bank);
    auto u = drop.take_undo();
    CHECK(u.size() == 1 && u[0].slot == 5);
    CHECK(drop.take_undo().size() == 1); // toggles within slot 5 only, never back to the first drop (slot 0)
    drop.drop({patch("C", 0)}, 0, 6, bank);
    drop.forget();
    CHECK(!drop.can_undo());
}

// refused (nothing to send, the undo of the last drop stays): no RAM bank, slot outside, no or broken source, an
// overwritten slot the cache does not know (bank not synced: undo could not restore it)
TEST(bankdrop_refuses)
{
    BankDrop drop;
    auto bank = ram_bank(0);
    drop.drop({patch("A", 0)}, 0, 0, bank);
    CHECK(drop.drop({patch("X", 0)}, 4, 0, bank).empty()); // ROM A
    CHECK(drop.drop({patch("X", 0)}, -1, 0, bank).empty());
    CHECK(drop.drop({patch("X", 0)}, 0, 128, bank).empty());
    CHECK(drop.drop({}, 0, 0, bank).empty());
    auto broken = patch("X", 0);
    broken.resize(300);
    CHECK(drop.drop({broken}, 0, 0, bank).empty());
    auto multi = patch("X", 0);
    multi[6] = 0x11; // not a single dump
    CHECK(drop.drop({multi}, 0, 0, bank).empty());
    auto unknown = bank;
    unknown[9].clear();
    CHECK(drop.drop({patch("X", 0), patch("Y", 1)}, 0, 8, unknown).empty());
    CHECK(drop.drop({patch("X", 0)}, 0, 8, std::vector<std::vector<uint8_t>>(3)).empty()); // short cache
    auto u = drop.take_undo();
    CHECK(u.size() == 1 && u[0].slot == 0);
}

// click selects one entry, Shift-click a range from the anchor, Ctrl-click toggles one; a press on a selected entry
// keeps the selection (it may be dragged), releasing it without a drag selects only that entry
TEST(browserselection_click_shift_ctrl)
{
    BrowserSelection s;
    s.press(5, false, false);
    CHECK(s.items() == std::vector<int>{5});
    s.press(8, true, false);
    CHECK((s.items() == std::vector<int>{5, 6, 7, 8}));
    s.press(2, true, false); // the range goes from the anchor (5) the other way
    CHECK((s.items() == std::vector<int>{2, 3, 4, 5}));
    s.press(9, false, true);
    CHECK((s.items() == std::vector<int>{2, 3, 4, 5, 9}));
    s.press(3, false, true);
    CHECK((s.items() == std::vector<int>{2, 4, 5, 9}));
    s.press(4, false, false); // already selected: kept for a drag
    CHECK((s.items() == std::vector<int>{2, 4, 5, 9}));
    s.release(4, true);
    CHECK((s.items() == std::vector<int>{2, 4, 5, 9}));
    s.press(4, false, false);
    s.release(4, false);
    CHECK(s.items() == std::vector<int>{4});
    CHECK(s.contains(4) && !s.contains(5));
    s.clear();
    CHECK(s.items().empty());
}

// a drag within the same RAM bank swaps (E297 (A)): the source slot gets the target's patch first, then the target
// gets the source's; each dump keeps its bytes 10..12 (the patch's origin travels with it)
TEST(bankdrop_swap_single)
{
    BankDrop drop;
    auto bank = ram_bank(0);
    auto w = drop.swap({25}, 0, 27, bank);
    CHECK(w.size() == 2);
    CHECK(w[0].slot == 25 && same_but_address(w[0].dump, bank[27]) && w[0].dump[8] == 25 && checksums_ok(w[0].dump));
    CHECK(w[1].slot == 27 && same_but_address(w[1].dump, bank[25]) && w[1].dump[8] == 27 && checksums_ok(w[1].dump));
    auto u = drop.take_undo(); // back: both slots as before
    CHECK(u.size() == 2 && u[0].slot == 25 && u[0].dump == bank[25] && u[1].slot == 27 && u[1].dump == bank[27]);
    CHECK(drop.take_undo().size() == 2); // toggles like a drop
}

// a block 24-26 onto 29 (E297 (B)): six dumps, 24, 25, 26 first (29-31's patches), then 29, 30, 31 (the old 24-26)
TEST(bankdrop_swap_block)
{
    BankDrop drop;
    auto bank = ram_bank(1);
    auto w = drop.swap({24, 25, 26}, 1, 29, bank);
    CHECK(w.size() == 6);
    const int slots[] = {24, 25, 26, 29, 30, 31}, from[] = {29, 30, 31, 24, 25, 26};
    for (int i = 0; i < 6 && i < (int)w.size(); i++)
        CHECK(w[i].bank == 1 && w[i].slot == slots[i] && same_but_address(w[i].dump, bank[from[i]]));
}

// refused, the undo kept: the selection overlaps its target (the original's result is unknown), the target runs past
// the bank's end, a slot is not cached, not a RAM bank, the target is the slot itself
TEST(bankdrop_swap_refuses)
{
    BankDrop drop;
    auto bank = ram_bank(0);
    drop.drop({patch("A", 0)}, 0, 0, bank);
    CHECK(drop.swap({24, 25, 26}, 0, 25, bank).empty());
    CHECK(drop.swap({10, 11, 12}, 0, 126, bank).empty());
    CHECK(drop.swap({5}, 0, 5, bank).empty());
    CHECK(drop.swap({5}, 4, 9, bank).empty());
    CHECK(drop.swap({}, 0, 9, bank).empty());
    auto gap = bank;
    gap[9].clear();
    CHECK(drop.swap({5}, 0, 9, gap).empty());
    auto u = drop.take_undo();
    CHECK(u.size() == 1 && u[0].slot == 0);
}

// Alt+Click rename of a RAM patch (E297 (D)): one dump with the new name (10 characters, printable ASCII, padded with
// spaces), everything else as before - bytes 10..12 included - and both checksums new
TEST(bankdrop_renamed)
{
    auto d = patch("Bubble PX", 24, 0x00, 0x01, 0x46);
    BankDrop::address(d, 0, 24);
    auto r = BankDrop::renamed(d, "RenTest");
    CHECK(r.size() == 524 && std::string(r.begin() + 249, r.begin() + 259) == "RenTest   ");
    CHECK(r[10] == 0x00 && r[11] == 0x01 && r[12] == 0x46 && r[7] == 1 && r[8] == 24 && checksums_ok(r));
    for (int i = 0; i < 524; i++)
        if (i < 249 || (i >= 259 && i != 265 && i != 522))
            CHECK(r[i] == d[i]);
    auto longer = BankDrop::renamed(d, "A very long name");
    CHECK(std::string(longer.begin() + 249, longer.begin() + 259) == "A very lon");
    auto u = BankDrop::renamed(d, "Ä\tx");
    CHECK(u[249] == ' ' && u[250] == ' ' && u[251] == ' ' && u[252] == 'x'); // UTF-8 bytes and controls -> spaces
    CHECK(BankDrop::renamed(std::vector<uint8_t>(10), "X").empty()); // not a single dump
}

namespace {
std::vector<std::vector<uint8_t>> library(std::initializer_list<std::pair<int, const char *>> at)
{
    std::vector<std::vector<uint8_t>> l;
    for (auto &a : at) {
        if (a.first >= (int)l.size())
            l.resize(a.first + 1);
        l[a.first] = patch(a.second, a.first);
    }
    return l;
}
std::string name_at(const std::vector<std::vector<uint8_t>> &l, int p)
{
    if (p >= (int)l.size() || l[p].empty())
        return "";
    std::string s(l[p].begin() + 249, l[p].begin() + 259);
    return s.substr(0, s.find_last_not_of(' ') + 1);
}
} // namespace

// libraries (E298): a drop lands at the target position and overwrites (L2, L3), gaps fill, nothing moves; cut at 128
TEST(libraryedit_drop)
{
    auto l = library({{0, "Awakening"}, {1, "LibRen"}, {27, "TuchDub"}});
    auto r = LibraryEdit::drop(l, {patch("SineCrush", 9), patch("PulsHarp", 6)}, 10);
    CHECK(r.size() == 28 && name_at(r, 10) == "SineCrush" && name_at(r, 11) == "PulsHarp" && name_at(r, 27) == "TuchDub");
    CHECK(same_but_address(r[10], patch("SineCrush", 9))); // bytes 10..12 from the source
    auto o = LibraryEdit::drop(l, {patch("PulsHarp", 6)}, 0);
    CHECK(o.size() == 28 && name_at(o, 0) == "PulsHarp" && name_at(o, 1) == "LibRen"); // overwritten, nothing moved
    auto far = LibraryEdit::drop({}, {patch("X", 0), patch("Y", 0)}, 127);
    CHECK(far.size() == 128 && name_at(far, 127) == "X"); // cut at the end
    CHECK(LibraryEdit::drop(l, {}, 3).empty() && LibraryEdit::drop(l, {patch("X", 0)}, 128).empty());
}

// within one library a drag swaps (L5), Delete selection leaves gaps (L6, E297 (E)), Sort closes them by name (E297 (F))
TEST(libraryedit_swap_delete_sort_rename)
{
    auto l = library({{0, "Awakening"}, {1, "LibRen"}, {10, "SineCrush"}, {27, "TuchDub"}});
    auto s = LibraryEdit::swap(l, {0}, 1);
    CHECK(name_at(s, 0) == "LibRen" && name_at(s, 1) == "Awakening" && name_at(s, 10) == "SineCrush");
    auto e = LibraryEdit::swap(l, {0}, 5); // onto an empty position: a move, the old one empty
    CHECK(name_at(e, 5) == "Awakening" && name_at(e, 0).empty());
    CHECK(LibraryEdit::swap(l, {0, 1}, 1).empty()); // overlapping sets: refused like in RAM
    auto d = LibraryEdit::remove(l, {1, 10});
    CHECK(d.size() == 28 && name_at(d, 0) == "Awakening" && d[1].empty() && d[10].empty() && name_at(d, 27) == "TuchDub");
    auto t = LibraryEdit::sort(d);
    CHECK(t.size() == 2 && name_at(t, 0) == "Awakening" && name_at(t, 1) == "TuchDub");
    auto n = LibraryEdit::rename(l, 1, "NewName");
    CHECK(name_at(n, 1) == "NewName" && name_at(n, 0) == "Awakening");
    CHECK(LibraryEdit::rename(l, 5, "X").empty()); // a gap has nothing to rename
}

// "Undo last operation" also undoes a library operation (L4) and toggles; a RAM operation after it replaces it
TEST(bankdrop_undo_library)
{
    BankDrop drop;
    auto before = library({{0, "Awakening"}});
    auto after = LibraryEdit::drop(before, {patch("PulsHarp", 6)}, 0);
    drop.remember_library(3, before, after);
    CHECK(drop.can_undo() && drop.undo_library() == 3);
    CHECK(drop.take_library_undo() == before);
    CHECK(drop.take_library_undo() == after);
    CHECK(drop.take_undo().empty()); // not a RAM undo
    drop.drop({patch("A", 0)}, 0, 0, ram_bank(0));
    CHECK(drop.undo_library() == -1 && drop.take_library_undo().empty() && drop.take_undo().size() == 1);
}

// "Sort alphabetically" on a RAM bank (E302): by name, only the slots whose patch changes are written (in slot order),
// and it cannot be undone - an earlier operation's undo is gone too (it would write over the sorted bank)
TEST(bankdrop_sort_ram_writes_changed_only)
{
    BankDrop drop;
    std::vector<std::vector<uint8_t>> bank;
    const char *names[] = {"Awakening", "Acidia  AV", "Anaconda M", "Aphex   RD"};
    for (int p = 0; p < 128; p++) {
        char n[11];
        snprintf(n, sizeof n, "Z%03d", p);
        auto d = patch(p < 4 ? names[p] : n, p, 0x00, 0x02, (uint8_t)p);
        BankDrop::address(d, 1, p);
        bank.push_back(d);
    }
    drop.drop({patch("X", 0)}, 1, 50, bank);
    auto w = drop.sort_ram(1, bank);
    CHECK(w.size() == 4);
    const int slot[] = {0, 1, 2, 3};
    const int from[] = {1, 2, 3, 0}; // Acidia, Anaconda, Aphex, Awakening
    for (int i = 0; i < 4 && i < (int)w.size(); i++)
        CHECK(w[i].bank == 1 && w[i].slot == slot[i] && same_but_address(w[i].dump, bank[from[i]]) && checksums_ok(w[i].dump));
    CHECK(!drop.can_undo());
    std::vector<std::vector<uint8_t>> sorted = bank;
    for (auto &x : w)
        sorted[x.slot] = x.dump;
    CHECK(drop.sort_ram(1, sorted).empty()); // already sorted: nothing to write
    auto gap = bank;
    gap[7].clear();
    CHECK(drop.sort_ram(1, gap).empty() && drop.sort_ram(4, bank).empty()); // not cached / not RAM
}

// the original sorts by name without regard to case (E303): its sorted RAM B has "eTalking M" before "EXLightsRD" and
// "Felon    M" before "FMeUp   BC"; a bank already in that order needs no write
TEST(bankdrop_sort_ignores_case)
{
    BankDrop drop;
    std::vector<std::vector<uint8_t>> bank;
    const char *names[] = {"eTalking M", "EXLightsRD", "Felon    M", "FMeUp   BC", "IconArp XM", "IOC 3-4 XM"};
    for (int p = 0; p < 128; p++) {
        char n[11];
        snprintf(n, sizeof n, "Z%03d", p);
        auto d = patch(p < 6 ? names[p] : n, p);
        BankDrop::address(d, 2, p);
        bank.push_back(d);
    }
    CHECK(drop.sort_ram(2, bank).empty());
    auto lib = LibraryEdit::sort({bank[1], bank[0], bank[3], bank[2]});
    CHECK(name_at(lib, 0) == "eTalking M" && name_at(lib, 1) == "EXLightsRD" && name_at(lib, 2) == "Felon    M");
}

// the LCD while a bank is downloaded (user wish 2026-10-21; the original shows progress only on the Virus): programs
// read and the time left - from the average so far, before the first reply the original's pace (131 s / 128, E301)
TEST(download_progress_text)
{
    CHECK(download_progress(0, 0) == "0 / 128, about 2:11 left");
    CHECK(download_progress(37, 37000) == "37 / 128, about 1:31 left");
    CHECK(download_progress(127, 130000) == "127 / 128, about 0:01 left");
    CHECK(download_progress(128, 131000) == "128 / 128");
}

// one selection per browser pane like the original (KPatchTable keeps the selected flags per pane, E318): the same
// bank in both panes is framed only in the pane that was clicked; a pane that switches banks loses its selection
TEST(paneselections_per_pane_not_per_bank)
{
    PaneSelections s;
    s.at(0, 1).press(5, false, false); // upper pane, RAM B
    CHECK(s.framed(0, 1) == std::set<int>{5});
    CHECK(s.framed(1, 1).empty()); // lower pane shows RAM B too: nothing framed there
    s.at(1, 1).press(9, false, false);
    CHECK(s.framed(1, 1) == std::set<int>{9});
    CHECK(s.framed(0, 1) == std::set<int>{5}); // each pane keeps its own
    CHECK(s.framed(0, 2).empty()); // what a pane frames belongs to the bank it was made in
    CHECK(s.at(0, 2).items().empty()); // the upper pane switched to RAM C: empty
    s.at(0, 1);
    CHECK(s.framed(0, 1).empty()); // and back to RAM B: the old selection does not come back
}
