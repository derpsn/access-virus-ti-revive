// midiin.h - nativehost --midi-in: notes from outside, handed to the plug-in as played (kIsLive) events, as Ableton
// Live's Monitor In passes a key played on a Keyboard/Polar back to the plug-in (#39 "doubling notes"). The source is
// the simulator's key tap: one line "ns status data1 data2" per event (decimal, ns = CLOCK_MONOTONIC when the broker
// got the key). An event is due once ns + delay has passed; the delay stands in for the DAW's own input latency.
#pragma once
#include <cstdint>
#include <cstdio>
#include <deque>
#include <string>
#include <vector>

struct MidiInEvent {
    int64_t ns;
    int status, data1, data2;
};

inline bool midiin_is_note_on(const MidiInEvent &e) { return (e.status & 0xF0) == 0x90 && e.data2 > 0; }
inline bool midiin_is_note_off(const MidiInEvent &e)
{
    return (e.status & 0xF0) == 0x80 || ((e.status & 0xF0) == 0x90 && e.data2 == 0);
}
inline int midiin_channel(const MidiInEvent &e) { return e.status & 0x0F; }

class MidiInTap {
public:
    // bytes as read from the FIFO; a line cut by the read waits for the rest
    void feed(const char *p, size_t n)
    {
        for (size_t i = 0; i < n; i++) {
            if (p[i] != '\n') {
                part_ += p[i];
                continue;
            }
            parse(part_);
            part_.clear();
        }
    }
    // moves every event with ns + delay_ns <= now_ns to `out`, oldest first
    void take_due(int64_t now_ns, int64_t delay_ns, std::vector<MidiInEvent> &out)
    {
        while (!q_.empty() && q_.front().ns + delay_ns <= now_ns) {
            out.push_back(q_.front());
            q_.pop_front();
        }
    }
    size_t pending() const { return q_.size(); }

private:
    void parse(const std::string &line)
    {
        long long ns;
        int s, d1, d2;
        char extra;
        if (sscanf(line.c_str(), "%lld %d %d %d %c", &ns, &s, &d1, &d2, &extra) != 4)
            return;
        if (s < 0x80 || s > 0xFF || d1 < 0 || d1 > 127 || d2 < 0 || d2 > 127)
            return;
        q_.push_back({(int64_t)ns, s, d1, d2});
    }
    std::string part_;
    std::deque<MidiInEvent> q_;
};
