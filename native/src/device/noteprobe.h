// noteprobe.h - what a host really sends for a note (#30, E403): flags, offset, block and transport per note, in
// diag.log when noteprobe.on lies in the plug-in folder (and the log is on). A field diagnostic, off by default. process() pushes without locks and
// never waits; when the ring is full the record is dropped and counted. One thread pushes, one drains (serialise drains
// from outside). Header only: the plug-in and the tests use it.
#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>

namespace vti {

class NoteProbe
{
public:
    static constexpr int SIZE = 1024;
    struct Rec {
        bool on;
        uint8_t channel;
        uint8_t pitch;
        bool playing;
        uint16_t flags;
        int32_t offset;
        int32_t frames;
    };

    bool push(const Rec &r) // process() only
    {
        const uint32_t w = write_.load(std::memory_order_relaxed), next = (w + 1) % SIZE;
        if (next == read_.load(std::memory_order_acquire)) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        recs_[w] = r;
        write_.store(next, std::memory_order_release);
        return true;
    }

    template <class F> void drain(F &&f)
    {
        uint32_t r = read_.load(std::memory_order_relaxed);
        const uint32_t w = write_.load(std::memory_order_acquire);
        while (r != w) {
            f(recs_[r]);
            r = (r + 1) % SIZE;
        }
        read_.store(r, std::memory_order_release);
    }

    uint32_t dropped() const { return dropped_.load(std::memory_order_relaxed); }

private:
    std::array<Rec, SIZE> recs_{};
    std::atomic<uint32_t> write_{0}, read_{0}, dropped_{0};
};

// largest: the largest block the host announced (in an Audio Unit MaximumFramesPerSlice, not the I/O buffer, E401)
inline std::string noteprobe_line(const NoteProbe::Rec &r, int largest)
{
    char b[128];
    snprintf(b, sizeof b, "note %s ch %d pitch %d flags 0x%x offset %d frames %d largest %d playing %d", r.on ? "on" : "off",
             r.channel + 1, r.pitch, (unsigned)r.flags, r.offset, r.frames, largest, r.playing ? 1 : 0);
    return b;
}

} // namespace vti
