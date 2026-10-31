// heldnotes.h - what still sounds when the host deactivates the plug-in. setActive(false) used to send sustain off +
// all notes off on all 16 channels; REAPER deactivates at every Play, so those 32 messages came right in front of
// its first notes and the Virus reported a late event at every Play (E373: 20 of 20 Plays, none without them). Now
// only channels with a held key or a pressed sustain pedal are released. process() (audio thread) records, setActive
// (another thread, processing stopped) releases: atomics, no allocation.
#pragma once
#include <atomic>
#include <cstdint>

namespace vti {

class HeldNotes {
public:
    void note_on(int ch, int key) { keys_[ch & 15][(key >> 6) & 1].fetch_or(bit(key), std::memory_order_relaxed); }
    void note_off(int ch, int key) { keys_[ch & 15][(key >> 6) & 1].fetch_and(~bit(key), std::memory_order_relaxed); }
    // CC 64: down from 64 on, like the Virus
    void sustain(int ch, int value)
    {
        const uint16_t b = (uint16_t)(1u << (ch & 15));
        if (value >= 64)
            pedal_.fetch_or(b, std::memory_order_relaxed);
        else
            pedal_.fetch_and((uint16_t)~b, std::memory_order_relaxed);
    }

    // The messages that silence what still sounds, per channel in order: pedal up (CC 64 = 0), then all notes off
    // (CC 123 = 0); forgets them. Returns how many (at most 32).
    int release(uint8_t out[32][3])
    {
        int n = 0;
        const uint16_t pedals = pedal_.exchange(0, std::memory_order_relaxed);
        for (int ch = 0; ch < 16; ch++) {
            const bool keys = (keys_[ch][0].exchange(0, std::memory_order_relaxed) |
                               keys_[ch][1].exchange(0, std::memory_order_relaxed)) != 0;
            if (pedals & (1u << ch))
                put(out[n++], ch, 64);
            if (keys)
                put(out[n++], ch, 123);
        }
        return n;
    }
    // Panic (the editor's "?" menu, TI Revive's own addition): pedal up + all notes off on every channel, 32
    // messages in the order of release(); the Virus took them from setActive before E373
    static int panic(uint8_t out[32][3])
    {
        for (int ch = 0; ch < 16; ch++) {
            put(out[2 * ch], ch, 64);
            put(out[2 * ch + 1], ch, 123);
        }
        return 32;
    }
    // after a panic: nothing is held any more
    void forget()
    {
        pedal_.store(0, std::memory_order_relaxed);
        for (auto &k : keys_)
            k[0].store(0, std::memory_order_relaxed), k[1].store(0, std::memory_order_relaxed);
    }

private:
    std::atomic<uint64_t> keys_[16][2] = {};
    std::atomic<uint16_t> pedal_{0};

    static uint64_t bit(int key) { return 1ull << (key & 63); }
    static void put(uint8_t *m, int ch, uint8_t cc) { m[0] = (uint8_t)(0xB0 | ch), m[1] = cc, m[2] = 0; }
};

} // namespace vti
