// blockevents.h - the events process() collects for one block before they go to the Virus, sorted by stamp. At most
// MAX: automation takes at most 96 slots and clock/bend/aftertouch a few dozen, so notes keep 100+. What does not fit
// used to vanish without a trace; now it is counted (logged off the audio thread) and a lost note-off is flagged, so
// the worker can send all notes off instead of leaving a note hanging ("missing notes" report, 2026-10-24).
// Fixed size, no allocation (audio thread).
#pragma once
#include <cstdint>
#include <cstring>
#include <utility>

namespace vti {

struct BlockEvents {
    static constexpr int MAX = 256;
    struct Ev {
        int64_t stamp;
        uint8_t n, m[16];
    };
    Ev ev[MAX];
    int n = 0;
    int dropped = 0;          // events that did not fit (or were longer than a slot)
    bool dropped_off = false; // a note-off was among them

    void clear() { n = 0, dropped = 0, dropped_off = false; }

    bool add(int64_t stamp, const uint8_t *m, size_t len)
    {
        if (n >= MAX || len > sizeof ev[0].m) {
            dropped++;
            if (len >= 3 && ((m[0] & 0xF0) == 0x80 || ((m[0] & 0xF0) == 0x90 && m[2] == 0)))
                dropped_off = true;
            return false;
        }
        ev[n].stamp = stamp, ev[n].n = (uint8_t)len;
        memcpy(ev[n].m, m, len);
        n++;
        return true;
    }

    // by stamp; equal stamps keep their order (a note-on before its note-off)
    void sort()
    {
        for (int i = 1; i < n; i++)
            for (int j = i; j > 0 && ev[j].stamp < ev[j - 1].stamp; j--)
                std::swap(ev[j], ev[j - 1]);
    }
};

} // namespace vti
