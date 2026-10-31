// rambackup.h - TI Revive's own addition (write policy of 2026-10-31: a feature that overwrites stored data backs
// it up first). Before a SAVE or a browser operation (drop, swap, rename, sort, undo) writes RAM slots of the Virus,
// the patches the writes would lose go into a personal library of the day, "RAM Backup 2026-10-31" (" 2", " 3", ...
// once one holds 128 patches, the most a library file addresses), so the browser can drag them back. A patch still
// in the bank after the writes (a swap) or written again unchanged is not lost. What is lost comes from the bank
// cache: a slot the cache does not know has nothing to back up. Header only, no files.
#pragma once
#include "model/bankdrop.h"
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace vti {

class RamBackup {
public:
    using Patch = std::vector<uint8_t>;
    static constexpr int kPerLibrary = 128;

    // the same patch, wherever it is stored: everything but the address (bytes 7, 8), the flags (byte 10: 0x20 after
    // a section lock merge, E378) and the two checksums (265, 522; a merge leaves them stale)
    static bool same_sound(const Patch &a, const Patch &b)
    {
        if (!BankDrop::is_single(a) || !BankDrop::is_single(b))
            return false;
        for (int i = 11; i < 522; i++)
            if (i != 265 && a[i] != b[i])
                return false;
        return a[9] == b[9];
    }

    // the patches `writes` would lose: a slot's patch in `before` (bank -> its 128 slots, the cache) that is not
    // in its bank any more once the writes are done; each patch once, in the order of the writes
    static std::vector<Patch> lost(const std::vector<BankDrop::Write> &writes, const std::map<int, std::vector<Patch>> &before)
    {
        std::map<int, std::vector<Patch>> after = before;
        for (const auto &w : writes)
            if (after.count(w.bank) && w.slot >= 0 && w.slot < (int)after[w.bank].size())
                after[w.bank][w.slot] = w.dump;
        std::vector<Patch> out;
        for (const auto &w : writes) {
            auto it = before.find(w.bank);
            if (it == before.end() || w.slot < 0 || w.slot >= (int)it->second.size())
                continue;
            const Patch &old = it->second[w.slot];
            if (!BankDrop::is_single(old) || contains(after[w.bank], old) || contains(out, old))
                continue;
            out.push_back(old);
        }
        return out;
    }

    // the day's library: "RAM Backup <date>", the n-th one (n >= 2) with " n" after it
    static std::string library_name(const std::string &date, int n)
    {
        return "RAM Backup " + date + (n >= 2 ? " " + std::to_string(n) : std::string());
    }

    struct Target {
        std::string name; // library name (file name without ".mid")
        int count;        // patches to append to it
    };
    // where `n` patches go: the day's libraries in order, each filled up to kPerLibrary; `count_of` tells how many a
    // library of that name holds already (0 = none yet)
    static std::vector<Target> plan(const std::string &date, const std::function<int(const std::string &)> &count_of, int n)
    {
        std::vector<Target> out;
        for (int k = 1; n > 0 && k < 1000; k++) {
            const std::string name = library_name(date, k);
            const int room = kPerLibrary - count_of(name);
            if (room <= 0)
                continue;
            const int take = room < n ? room : n;
            out.push_back({name, take});
            n -= take;
        }
        return out;
    }

private:
    static bool contains(const std::vector<Patch> &v, const Patch &p)
    {
        for (const auto &x : v)
            if (same_sound(x, p))
                return true;
        return false;
    }
};

} // namespace vti
