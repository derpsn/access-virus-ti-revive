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

constexpr int SLOT_RETRY_MS = 30000; // every free slot tried: wait this long, then try them all again

constexpr unsigned slot_bit(int s) { return s >= 1 && s <= 32 ? 1u << (s - 1) : 0u; }

// `taken[i]`: slot i+1 has an instance of this process. `tried`: the slots this instance gave up (slot_bit), not taken
// again until the mask is cleared (audit 2026-11-18 #1; round 0.8.42 test: with one Virus the second instance went
// round Virus 2 -> 3 -> 2 for good). Then the preferred slot (1..n, else ignored) when free, else the lowest free
// one; 0 when none is left: wait like a second instance always did, so with one Virus nothing changes for the user.
inline int slot_choose(const bool *taken, int n, int preferred, unsigned tried)
{
    auto ok = [&](int s) { return s >= 1 && s <= n && !taken[s - 1] && !(tried & slot_bit(s)); };
    if (ok(preferred))
        return preferred;
    for (int s = 1; s <= n; s++)
        if (ok(s))
            return s;
    return 0;
}

// waiting with slots given up: after SLOT_RETRY_MS they are tried again (a Virus plugged in later, a program that let
// its Virus go)
inline bool slot_retry_due(unsigned tried, int waited_ms) { return tried && waited_ms >= SLOT_RETRY_MS; }

// give the slot up (and try another one) after `absent_ms` in this state: Virus 2+ not there, or any slot another
// program's plug-in has (broker state 2; audit #4: a DAW with one process per plug-in). Virus 1 not there keeps its
// slot and shows the "No Virus TI" page as before.
inline bool slot_give_up(int slot, bool present, bool elsewhere, int absent_ms)
{
    return absent_ms >= SLOT_GIVE_UP_MS && (elsewhere || (slot > 1 && !present));
}

// the slot a project keeps (getState): the one where a Virus answered, else the project's own (audit #2; a slot only
// being tried is not kept: a project saved then reopened on that empty slot, round 0.8.42 test)
inline int slot_to_save(int found, int preferred) { return found ? found : preferred; }

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

// macOS: Core Audio cannot tell which broker a "Virus" device belongs to (with one device, core_audio_pick gives it to
// any slot), so the input opens only when this instance's broker has a Virus and lets us have it. Before, an instance
// trying an empty slot opened the only device and played another instance's Virus (round 0.8.43 Mac test, E566).
inline bool core_audio_allowed(bool present, bool elsewhere) { return present && !elsewhere; }

// macOS (#50, E562): the pick's outcome as one line for the plug-in log (it went to stderr only): how the device was
// matched, never the serial (rule 5)
inline std::string core_audio_note(int devices, int slot, int pick, bool by_serial)
{
    const std::string v = "Virus " + std::to_string(slot);
    if (devices <= 0)
        return "no Virus audio device in Core Audio (" + v + ")";
    if (devices == 1)
        return pick == 0 ? "1 Virus audio device, taken for " + v : "1 Virus audio device, " + v + " not among them";
    return std::to_string(devices) + " Virus audio devices, " + v + " " +
           (pick < 0 ? "not among them" : by_serial ? "found by its USB serial" : "taken by order (no serial match)");
}

// #50: which Virus an instance plays, after the model name in the browser and the menus - only with several Viruses
// (this instance on Virus 2+, or another instance of this process on another one), so one Virus looks as before
inline std::string slot_label(int slot, bool other_instance)
{
    return slot >= 2 || (slot == 1 && other_instance) ? " - Virus " + std::to_string(slot) : "";
}

} // namespace vti
