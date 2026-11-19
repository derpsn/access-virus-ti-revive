// umpconv.h - MIDI 1.0 bytes as Windows MIDI Services wants them: Universal MIDI Packets (UMP, MIDI 2.0 spec). The
// helper tirevive-midiports.exe gets the messages keyport.h's filters pass (src/broker/midifeed.h) and sends them on its
// virtual device: a channel message as one MIDI 1.0 channel voice word (type 2), a system common or real-time message
// as one word (type 1), SysEx as 7-bit data packets (type 3, two words, up to 6 bytes each; status complete / start /
// continue / end). The DIN port passes SysEx in USB-MIDI's 3-byte pieces, so SysEx is collected here across calls.
// One converter per group (one per port). No I/O, no Windows: unit-tested on every platform.
#pragma once
#include <cstdint>

namespace vti {

class UmpConv {
public:
    explicit UmpConv(int group) : group_((uint32_t)(group & 15)) {}

    // One message or SysEx piece; the UMP words go to `out`. Returns the number of words (0: nothing to send yet,
    // incomplete, undefined or no room).
    int push(const uint8_t *b, int n, uint32_t *out, int cap)
    {
        if (n < 1)
            return 0;
        const uint8_t st = b[0];
        if (st >= 0x80 && st < 0xF0) { // channel voice
            static const int len[7] = {3, 3, 3, 3, 2, 2, 3};
            const int need = len[(st >> 4) - 8];
            if (n < need || cap < 1 || (b[1] & 0x80) || (need == 3 && (b[2] & 0x80)))
                return 0;
            out[0] = 0x20000000u | group_ << 24 | (uint32_t)st << 16 | (uint32_t)b[1] << 8 | (need == 3 ? b[2] : 0u);
            return 1;
        }
        if (st > 0xF0 && st != 0xF7) { // system common / real time
            static const int len[16] = {0, 2, 3, 2, 0, 0, 1, 0, 1, 0, 1, 1, 1, 0, 1, 1};
            const int need = len[st & 15];
            if (!need || n < need || cap < 1)
                return 0;
            uint32_t w = 0x10000000u | group_ << 24 | (uint32_t)st << 16;
            if (need > 1)
                w |= (uint32_t)(b[1] & 0x7F) << 8;
            if (need > 2)
                w |= (uint32_t)(b[2] & 0x7F);
            out[0] = w;
            return 1;
        }
        return sysex(b, n, out, cap); // F0, data bytes, F7
    }

private:
    uint32_t group_;
    bool in_sysex_ = false, started_ = false; // started_: a start packet went out already
    uint8_t buf_[6] = {};
    int have_ = 0;

    int packet(int status, uint32_t *out) // status: 0 complete, 1 start, 2 continue, 3 end
    {
        uint8_t d[6] = {};
        for (int i = 0; i < have_; i++)
            d[i] = buf_[i];
        out[0] = 0x30000000u | group_ << 24 | (uint32_t)status << 20 | (uint32_t)have_ << 16 | (uint32_t)d[0] << 8 | d[1];
        out[1] = (uint32_t)d[2] << 24 | (uint32_t)d[3] << 16 | (uint32_t)d[4] << 8 | d[5];
        have_ = 0;
        return 2;
    }

    int sysex(const uint8_t *b, int n, uint32_t *out, int cap)
    {
        int w = 0;
        for (int i = 0; i < n; i++) {
            const uint8_t x = b[i];
            if (x == 0xF0) { // a new one: an unfinished one is dropped
                in_sysex_ = true, started_ = false, have_ = 0;
            } else if (x == 0xF7) {
                if (in_sysex_ && w + 2 <= cap)
                    w += packet(started_ ? 3 : 0, out + w);
                in_sysex_ = false, started_ = false, have_ = 0;
            } else if (in_sysex_ && x < 0x80) {
                if (have_ == 6) { // full: send it, more follows
                    if (w + 2 > cap)
                        return w;
                    w += packet(started_ ? 2 : 1, out + w);
                    started_ = true;
                }
                buf_[have_++] = x;
            }
        }
        return w;
    }
};

} // namespace vti
