// transport.h - Play and loop restarts, and what the original sends at them.
//
// Why: at every Play, loop restart or jump the original writes the song position F2 and the global
// `F0 00 20 33 01 00 73 00 23 vv F7`, and `6F <part> 7F 00` for all 16 parts (captures of the original, E275); at
// Stop it writes FC. In the Virus' DSP the F2 handler sets its MIDI-clock position (0x540ad/0x540b4: position x 6 - 1)
// and the handler of global 0x23 (0x540ca) adds its value to that position, so the pair puts the clock exactly on the
// song position: the arpeggiator and clock-synced LFOs restart with the bar. 6F <part> 7F (handler 0x565a8) sets a
// per-part value the original always sets to 0 - what it controls is open (LOW).
//
// How the original computes them (Virus Control.dll 5.1.7 x64, 0x1800303b0, E505): its position is the host's
// position in quarters + 256, so a count-in (negative positions) stays valid and runs through 0 without a restart;
// F2 = position x 4 + 1/6 (a position in the last tick of a sixteenth counts as the next one), vv = the ticks into
// that sixteenth + 1 (0..5), and both go out on the next MIDI-clock tick, where its F8 grid restarts too. A restart
// comes at Play, when the position goes backwards, and on a jump of more than 0.05 quarter, or of more than 128
// samples and at least one tick. TI Revive before 0.8.41 clamped negative positions to 0, always sent vv = 1 and
// stamped the pair at the block start off a free-running F8 grid: a count-in left the arpeggiator a count-in ahead,
// and every Play put the ticks 0..1 tick off the beat (#30).
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace vti {

class TransportWatch {
public:
    enum Event { None, Start, Stop };
    // per block: playing, the block's start in quarters and samples, its length in quarters and samples
    Event update(bool playing, double ppq, double len_qn, int64_t samples, int64_t len_samples)
    {
        Event e = None;
        if (playing) {
            const double dq = ppq - next_;
            const int64_t ds = samples - next_samples_;
            const bool jump = std::fabs(dq) > 0.05 || ((ds > 128 || ds < -128) && std::fabs(dq) * 24.0 >= 1.0);
            if (!was_ || jump || ppq < last_)
                e = Start;
        } else if (was_)
            e = Stop;
        was_ = playing;
        last_ = ppq;
        next_ = ppq + len_qn;
        next_samples_ = samples + len_samples;
        return e;
    }
    // a new timeline (re-anchor): the F8 grid lost the beat, so the next playing block restarts; no Stop for it
    void reset() { was_ = false; }

private:
    bool was_ = false;
    double last_ = 0, next_ = 0;
    int64_t next_samples_ = 0;
};

struct TransportStart {
    int spp;       // F2's song position in sixteenths
    int vv;        // 73 00 23 vv: ticks into the sixteenth + 1
    double offset; // frames from the block start to the next tick, where F2, vv and the F8 grid go
};

// from the host position at the block start and the frames per MIDI-clock tick, as the original (0x1800303b0)
inline TransportStart transport_start(double ppq, double frames_per_tick)
{
    // a host position that is no number or out of range (a million quarters: 138 h at 120 BPM) would make the casts
    // undefined and the offset garbage, and the F8 loop runs to that offset (the audio thread would hang)
    if (!(std::fabs(ppq) < 1e6))
        return {0, 0, 0.0};
    const double pos = ppq + 256.0, ticks = pos * 24.0;
    int spp = (int)(pos * 4.0 + 1.0 / 6.0);
    if (spp < 0)
        spp = 0;
    int vv = (int)(ticks + 1.0) - spp * 6;
    if (vv < 0)
        vv = 0;
    double offset = ((double)(int)(ticks + 1.0) - ticks) * frames_per_tick - 1.0;
    if (!(offset >= 0)) // NaN too
        offset = 0;
    if (offset > frames_per_tick) // before the +256 (int) rounds towards 0, not down: at most one tick
        offset = frames_per_tick;
    return {spp, vv, offset};
}

// the pieces of a (re)start without allocation (the audio thread builds them). The original also sends
// 6F <part> 7F 00 for all 16 parts (E276: an Atomizer value); left out - on the device each set delayed the next
// note by 29-37 ms (E280)
inline void transport_f2(int spp, uint8_t m[3])
{
    m[0] = 0xF2, m[1] = (uint8_t)(spp & 0x7F), m[2] = (uint8_t)((spp >> 7) & 0x7F);
}
inline void transport_tick(int vv, uint8_t m[11])
{
    static const uint8_t t[11] = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x00, 0x73, 0x00, 0x23, 0x01, 0xF7}; // device 00
    std::memcpy(m, t, 11);
    m[9] = (uint8_t)(vv & 0x7F);
}
constexpr uint8_t TRANSPORT_STOP = 0xFC;

// the messages of a (re)start (for tests and tools: it allocates)
inline std::vector<std::vector<uint8_t>> transport_messages(const TransportStart &s)
{
    uint8_t f2[3], tick[11];
    transport_f2(s.spp, f2);
    transport_tick(s.vv, tick);
    return {std::vector<uint8_t>(f2, f2 + 3), std::vector<uint8_t>(tick, tick + 11)};
}

// a restart waits for the block that holds its tick: sent earlier, its later stamp would go out before the next
// block's notes, and the stamp floor (stamps never go backwards) would move those notes late
class PendingRestart {
public:
    void arm(const TransportStart &s, int64_t stamp)
    {
        transport_f2(s.spp, f2_);
        transport_tick(s.vv, tick_);
        stamp_ = stamp;
        armed_ = true;
    }
    // true once for the block ending at block_end (exclusive) that holds the stamp
    bool due(int64_t block_end)
    {
        if (!armed_ || stamp_ >= block_end)
            return false;
        armed_ = false;
        return true;
    }
    void clear() { armed_ = false; }
    // the timeline moved by delta frames (TimelineFollow): the tick it waits for moves with the F8 grid
    void shift(int64_t delta) { stamp_ += delta; }
    int64_t stamp() const { return stamp_; }
    const uint8_t *f2() const { return f2_; }
    const uint8_t *tick() const { return tick_; }

private:
    bool armed_ = false;
    int64_t stamp_ = 0;
    uint8_t f2_[3] = {}, tick_[11] = {};
};

} // namespace vti
