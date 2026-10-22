// bankdrop.h - patches dragged from one browser pane onto a RAM slot of the other, like the original (E294): each
// source dump goes into the next slot from the drop target on, in order, cut at the bank's end, without asking; the
// original changes nothing but bank/program (bytes 7, 8) and the checksums (bytes 10..12, the patch's origin, stay
// as in the source). "Undo last operation" writes back what the last drop overwrote; used again it puts the drop
// back, and so on - the original toggles within its last operation (E295). Plus the browser's
// selection (click, Shift range, Ctrl toggle). Patches are 524-byte single dumps. Header only. Not thread safe: the
// caller locks.
#pragma once
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace vti {

class BankDrop {
public:
    using Patch = std::vector<uint8_t>;
    struct Write {
        int bank; // RAM bank 0..3 (RAM A..D)
        int slot; // program 0..127
        Patch dump;
    };
    static constexpr int BANKS = 4, SLOTS = 128;

    static bool is_single(const Patch &d)
    {
        return d.size() == 524 && d[0] == 0xF0 && d[1] == 0x00 && d[2] == 0x20 && d[3] == 0x33 && d[6] == 0x10 &&
               d[523] == 0xF7;
    }
    // address a single dump to RAM `bank` (0..3) program `slot` - bank byte 1..4 - and recompute both checksums
    static void address(Patch &d, int bank, int slot)
    {
        d[7] = (uint8_t)(bank + 1), d[8] = (uint8_t)slot;
        checksums(d);
    }
    // the patch with another name (10 characters; printable ASCII, the rest spaces), nothing else changed but the
    // checksums - the original's Alt+Click rename in RAM (E297 (D)); empty if `d` is no single dump
    static Patch renamed(const Patch &d, const std::string &name)
    {
        if (!is_single(d))
            return {};
        Patch r = d;
        for (size_t i = 0; i < 10; i++) {
            const unsigned char c = i < name.size() ? (unsigned char)name[i] : ' ';
            r[249 + i] = c >= 32 && c < 127 ? c : ' ';
        }
        checksums(r);
        return r;
    }
    // the original's name order: without regard to case (E303), spaces and other characters as they are
    static bool name_less(const Patch &a, const Patch &b)
    {
        return std::lexicographical_compare(a.begin() + 249, a.begin() + 259, b.begin() + 249, b.begin() + 259,
                                            [](uint8_t x, uint8_t y) { return std::tolower(x) < std::tolower(y); });
    }
    static void checksums(Patch &d)
    {
        unsigned a = 0;
        for (int i = 5; i < 265; i++)
            a += d[i];
        d[265] = a & 0x7F;
        for (int i = 265; i < 522; i++)
            a += d[i];
        d[522] = a & 0x7F;
    }

    // the dumps to send for `sources` dropped on RAM `bank` at `slot`; `current` is the bank's cache (what they
    // overwrite, kept for undo). Empty, and the previous undo kept, when the drop cannot be done or not be undone.
    std::vector<Write> drop(const std::vector<Patch> &sources, int bank, int slot, const std::vector<Patch> &current)
    {
        if (bank < 0 || bank >= BANKS || slot < 0 || slot >= SLOTS || sources.empty())
            return {};
        const int n = std::min((int)sources.size(), SLOTS - slot);
        std::vector<Write> writes;
        for (int i = 0; i < n; i++) {
            if (!is_single(sources[i]))
                return {};
            Patch d = sources[i];
            address(d, bank, slot + i);
            writes.push_back({bank, slot + i, std::move(d)});
        }
        return remember(std::move(writes), current);
    }

    // a drag within the same RAM bank (E297 (A), (B)): `programs` swap places with the slots from `slot` on, in
    // order; first the selected slots get the target patches, then the targets get the selected ones, like the
    // original. Refused when the two sets overlap or run past the bank's end (the original's result is not known).
    std::vector<Write> swap(const std::vector<int> &programs, int bank, int slot, const std::vector<Patch> &current)
    {
        const int n = (int)programs.size();
        if (bank < 0 || bank >= BANKS || n == 0 || slot < 0 || slot + n > SLOTS || (int)current.size() < SLOTS)
            return {};
        std::set<int> from(programs.begin(), programs.end());
        if ((int)from.size() != n)
            return {};
        for (int i = 0; i < n; i++)
            if (programs[i] < 0 || programs[i] >= SLOTS || from.count(slot + i) || !is_single(current[programs[i]]) ||
                !is_single(current[slot + i]))
                return {};
        std::vector<Write> writes;
        for (int i = 0; i < n; i++) {
            Patch d = current[slot + i];
            address(d, bank, programs[i]);
            writes.push_back({bank, programs[i], std::move(d)});
        }
        for (int i = 0; i < n; i++) {
            Patch d = current[programs[i]];
            address(d, bank, slot + i);
            writes.push_back({bank, slot + i, std::move(d)});
        }
        return remember(std::move(writes), current);
    }
    // "Sort alphabetically" on a RAM bank (E302): by name; only the slots whose patch changes are written, in slot
    // order; not undoable (an earlier undo is dropped too). Empty if nothing moves or a slot is not cached.
    std::vector<Write> sort_ram(int bank, const std::vector<Patch> &current)
    {
        if (bank < 0 || bank >= BANKS || (int)current.size() < SLOTS)
            return {};
        std::vector<int> order(SLOTS);
        for (int p = 0; p < SLOTS; p++) {
            if (!is_single(current[p]))
                return {};
            order[p] = p;
        }
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return name_less(current[a], current[b]); });
        std::vector<Write> writes;
        for (int p = 0; p < SLOTS; p++)
            if (order[p] != p) {
                Patch d = current[order[p]];
                address(d, bank, p);
                writes.push_back({bank, p, std::move(d)});
            }
        if (!writes.empty())
            forget();
        return writes;
    }
    bool can_undo() const { return !undo_.empty() || lib_ >= 0; }
    // "Undo last operation": the writes that restore the slots as they were before the last drop - or, the next
    // time, as the drop left them (the original toggles, E295)
    std::vector<Write> take_undo()
    {
        std::vector<Write> u = undo_;
        undo_.swap(redo_);
        return u;
    }
    // another store into RAM (SAVE, a bank read again): an undo would now write over newer patches
    void forget() { undo_.clear(), redo_.clear(), lib_ = -1, lib_undo_.clear(), lib_redo_.clear(); }

    // a library operation is the last operation (E298 (L4): the original undoes those too): library `lib` was
    // `before` and is now `after`; replaces a RAM undo
    void remember_library(int lib, std::vector<Patch> before, std::vector<Patch> after)
    {
        undo_.clear(), redo_.clear();
        lib_ = lib, lib_undo_ = std::move(before), lib_redo_ = std::move(after);
    }
    int undo_library() const { return lib_; } // the library the next undo restores, -1: the undo is a RAM one
    // the library's patches to put back (and next time the ones after the operation again)
    std::vector<Patch> take_library_undo()
    {
        if (lib_ < 0)
            return {};
        std::vector<Patch> u = lib_undo_;
        lib_undo_.swap(lib_redo_);
        return u;
    }

private:
    // keeps what `writes` overwrite (from the cache `current`) for the undo; empty if a slot is not cached
    std::vector<Write> remember(std::vector<Write> writes, const std::vector<Patch> &current)
    {
        std::vector<Write> undo;
        for (auto &w : writes) {
            if (w.slot >= (int)current.size() || !is_single(current[w.slot]))
                return {};
            Patch old = current[w.slot];
            address(old, w.bank, w.slot);
            undo.push_back({w.bank, w.slot, std::move(old)});
        }
        undo_ = std::move(undo);
        redo_ = writes;
        lib_ = -1, lib_undo_.clear(), lib_redo_.clear();
        return writes;
    }

    std::vector<Write> undo_, redo_; // RAM: what the next undo writes, and what the one after it writes
    int lib_ = -1;                   // a library undo: the library, its patches before and after the operation
    std::vector<Patch> lib_undo_, lib_redo_;
};

// the LCD text while "Download entire bank" runs: "37 / 128, about 1:31 left" - the time left from the average per
// program so far, before the first reply the original's pace (131 s for 128, E301)
inline std::string download_progress(int done, long long elapsed_ms)
{
    std::string t = std::to_string(done) + " / 128";
    if (done >= 128)
        return t;
    const double per = done > 0 ? elapsed_ms / 1000.0 / done : 131.0 / 128;
    const int left = (int)(per * (128 - done) + 0.5);
    char b[32];
    snprintf(b, sizeof b, ", about %d:%02d left", left / 60, left % 60);
    return t + b;
}

// operations on a personal library's patches by position (empty = a gap), like the original's browser (E297, E298):
// files only, nothing goes to the Virus. Each returns the library's new patches, empty when refused.
struct LibraryEdit {
    using Patch = BankDrop::Patch;
    using Lib = std::vector<Patch>;
    static constexpr int SLOTS = BankDrop::SLOTS;

    // `sources` into the positions from `slot` on, overwriting (L2, L3); bank byte 1, program = position
    static Lib drop(const Lib &lib, const std::vector<Patch> &sources, int slot)
    {
        if (sources.empty() || slot < 0 || slot >= SLOTS)
            return {};
        const int n = std::min((int)sources.size(), SLOTS - slot);
        Lib r = lib;
        if ((int)r.size() < slot + n)
            r.resize(slot + n);
        for (int i = 0; i < n; i++) {
            if (!BankDrop::is_single(sources[i]))
                return {};
            r[slot + i] = sources[i];
            BankDrop::address(r[slot + i], 0, slot + i);
        }
        return r;
    }
    // a drag within the library (L5): the selected positions and those from `slot` on change places; onto a gap the
    // patch moves. Refused when the sets overlap or run past 128, or a selected position is a gap.
    static Lib swap(const Lib &lib, const std::vector<int> &programs, int slot)
    {
        const int n = (int)programs.size();
        if (n == 0 || slot < 0 || slot + n > SLOTS)
            return {};
        std::set<int> from(programs.begin(), programs.end());
        if ((int)from.size() != n)
            return {};
        for (int i = 0; i < n; i++)
            if (programs[i] < 0 || programs[i] >= (int)lib.size() || !BankDrop::is_single(lib[programs[i]]) ||
                from.count(slot + i))
                return {};
        Lib r = lib;
        if ((int)r.size() < slot + n)
            r.resize(slot + n);
        for (int i = 0; i < n; i++)
            std::swap(r[programs[i]], r[slot + i]);
        for (int p = 0; p < (int)r.size(); p++)
            if (!r[p].empty())
                BankDrop::address(r[p], 0, p);
        return r;
    }
    // "Delete selection" (E297 (E)): the positions become gaps, the others stay where they are
    static Lib remove(const Lib &lib, const std::vector<int> &programs)
    {
        Lib r = lib;
        bool any = false;
        for (int p : programs)
            if (p >= 0 && p < (int)r.size() && !r[p].empty())
                r[p].clear(), any = true;
        return any ? r : Lib{};
    }
    // "Sort alphabetically" (E297 (F)): by name like the original (case ignored, E303), gaps closed, renumbered
    static Lib sort(const Lib &lib)
    {
        Lib r;
        for (auto &d : lib)
            if (BankDrop::is_single(d))
                r.push_back(d);
        std::stable_sort(r.begin(), r.end(), BankDrop::name_less);
        for (int p = 0; p < (int)r.size(); p++)
            BankDrop::address(r[p], 0, p);
        return r;
    }
    // Alt+Click rename (E297 (D)); a gap has nothing to rename
    static Lib rename(const Lib &lib, int prog, const std::string &name)
    {
        if (prog < 0 || prog >= (int)lib.size() || !BankDrop::is_single(lib[prog]))
            return {};
        Lib r = lib;
        r[prog] = BankDrop::renamed(lib[prog], name);
        return r;
    }
};

// the browser's selected entries (programs 0..127 of the bank a pane shows)
class BrowserSelection {
public:
    // mouse down: plain = this entry only (a selected one stays as it is: the selection may be dragged), Shift = the
    // range from the last plain/Ctrl click, Ctrl = toggle this entry
    void press(int prog, bool shift, bool ctrl)
    {
        if (shift && anchor_ >= 0) {
            sel_.clear();
            for (int p = std::min(anchor_, prog); p <= std::max(anchor_, prog); p++)
                sel_.insert(p);
            return;
        }
        anchor_ = prog;
        if (ctrl) {
            if (!sel_.erase(prog))
                sel_.insert(prog);
            return;
        }
        if (!sel_.count(prog))
            sel_ = {prog};
        kept_ = prog;
    }
    // mouse up: a plain press on a selected entry that was not dragged selects only it
    void release(int prog, bool dragged)
    {
        if (!dragged && kept_ == prog)
            sel_ = {prog};
        kept_ = -1;
    }
    std::vector<int> items() const { return {sel_.begin(), sel_.end()}; } // ascending: the order a drop writes
    bool contains(int prog) const { return sel_.count(prog) != 0; }
    void clear() { sel_.clear(), anchor_ = -1, kept_ = -1; }

private:
    std::set<int> sel_;
    int anchor_ = -1, kept_ = -1;
};

// the browser panes' selections: one per pane, like the original's KPatchTable, which keeps a selected flag per entry
// of each pane (select 0x180051c90, deselect all 0x180051bf0, E318) - the same bank in both panes is framed only in
// the pane that was clicked. A pane's selection belongs to the bank it showed then: the original keeps none per bank.
class PaneSelections {
public:
    // the selection of `pane` (0 upper, 1 lower) while it shows `bank`; emptied when the pane's bank changed
    BrowserSelection &at(int pane, int bank)
    {
        Pane &p = panes_[pane];
        if (p.bank != bank) {
            p.bank = bank;
            p.sel.clear();
        }
        return p.sel;
    }
    // what `pane` frames while it shows `bank`
    std::set<int> framed(int pane, int bank) const
    {
        auto it = panes_.find(pane);
        if (it == panes_.end() || it->second.bank != bank)
            return {};
        auto v = it->second.sel.items();
        return {v.begin(), v.end()};
    }

private:
    struct Pane {
        int bank = -1;
        BrowserSelection sel;
    };
    std::map<int, Pane> panes_;
};

} // namespace vti
