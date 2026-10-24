// outputdelay.h - holding back the audio of parts on the Virus' analog outputs (E344). The USB audio reaches the host
// a latency later than the Virus plays (the plug-in reports it, the host lines the track up); an analog output plays
// at once, so a part there sounded early against the USB parts (user, 2026-10-24). Like the original (0x180084970 /
// 0x1800a9c20): every part whose output is analog gets "delay its audio by the latency" (F0 00 20 33 01 10 6F pp 7F ms
// F7), USB parts 0; sent for the parts that changed, and for all 16 at Play/Stop and after a new connection
// (captures/ref-outdelay-1). No allocation; the worker thread uses it.
#pragma once
#include <array>
#include <cstdint>

namespace vti {

// Output Select (72 pp 29 vv): 0-8 = Out1/2/3 (L, L+R, R), from 9 on USB (E343; the original: below 9 = analog)
inline bool output_is_analog(int v) { return v >= 0 && v < 9; }

class OutputDelay {
public:
    static std::array<uint8_t, 11> message(int part, int ms)
    {
        const int v = ms < 0 ? 0 : ms > 127 ? 127 : ms; // a data byte
        return {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x6F, (uint8_t)(part & 15), 0x7F, (uint8_t)v, 0xF7};
    }

    // the Virus' values are unknown again (a new connection)
    void reset()
    {
        for (int &s : sent_)
            s = -1;
    }

    // `outputs`: each part's Output Select; `ms`: the delay for analog parts. Fills the parts to send and their values
    // (all 16 with `all`: Play/Stop) and counts them as sent; returns how many.
    int due(const int outputs[16], int ms, bool all, int parts[16], int values[16])
    {
        const int d = ms < 0 ? 0 : ms > 127 ? 127 : ms;
        int n = 0;
        for (int p = 0; p < 16; p++) {
            const int v = output_is_analog(outputs[p]) ? d : 0;
            if (all || v != sent_[p]) {
                parts[n] = p, values[n] = v, n++;
                sent_[p] = v;
            }
        }
        return n;
    }

private:
    int sent_[16] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
};

} // namespace vti
