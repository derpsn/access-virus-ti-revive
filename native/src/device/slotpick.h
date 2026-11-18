// slotpick.h - several Viruses (#45): which broker slot (src/broker/devslot.h) a plug-in instance takes. Like the
// original (E553): a new instance takes a free Virus, the one its project was saved with when that one is free. An
// instance on Virus 2+ whose broker finds no Virus gives the slot up and waits like a second instance always did, so
// with one Virus nothing changes for the user.
#pragma once
#include <algorithm>
#include <string>
#include <vector>

namespace vti {

constexpr int SLOT_GIVE_UP_MS = 3000; // a broker just started opens its Virus within ~0.1 s; 3 s leaves room

// `taken[i]`: slot i+1 has an instance of this process. `skip`: the slot this instance just gave up (0 = none), not
// taken again at once unless it is the only free one (audit 2026-11-18 #1: a project saved with Virus 2 on a computer
// with one Virus kept trying Virus 2). Then the preferred slot (1..n, else ignored) when free, else the lowest free
// one; 0 when all are taken.
inline int slot_choose(const bool *taken, int n, int preferred, int skip)
{
    auto free_ = [&](int s) { return s >= 1 && s <= n && !taken[s - 1]; };
    if (preferred != skip && free_(preferred))
        return preferred;
    for (int s = 1; s <= n; s++)
        if (s != skip && free_(s))
            return s;
    return free_(skip) ? skip : 0;
}

// give the slot up (and try another one) after `absent_ms` in this state: Virus 2+ not there, or any slot another
// program's plug-in has (broker state 2; audit #4: a DAW with one process per plug-in). Virus 1 not there keeps its
// slot and shows the "No Virus TI" page as before.
inline bool slot_give_up(int slot, bool present, bool elsewhere, int absent_ms)
{
    return absent_ms >= SLOT_GIVE_UP_MS && (elsewhere || (slot > 1 && !present));
}

// the slot a project keeps (getState): the one in use, between two tries the project's own (audit #2)
inline int slot_to_save(int slot, int preferred) { return slot ? slot : preferred; }

// macOS: the plug-in reads its Virus' audio from Core Audio itself. Of the "Virus" devices (their UIDs): the only one;
// with several, the one whose UID carries the broker's USB serial string, else the slot-th by UID order; -1 = none.
// shortcut: the UID format of Apple's USB audio driver is not verified with two Viruses, upgrade to a locationID
// match through IOKit when a log shows the serial is not in the UID.
inline int core_audio_pick(const std::vector<std::string> &uids, const std::string &serial, int slot)
{
    const int n = (int)uids.size();
    if (n == 1)
        return 0;
    int match = -1, matches = 0;
    for (int i = 0; i < n && !serial.empty(); i++)
        if (uids[i].find(serial) != std::string::npos)
            match = i, matches++;
    if (matches == 1)
        return match;
    std::vector<int> order(n);
    for (int i = 0; i < n; i++)
        order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return uids[a] < uids[b]; });
    return slot >= 1 && slot <= n ? order[slot - 1] : -1;
}

} // namespace vti
