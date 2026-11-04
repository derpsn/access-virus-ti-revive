// decode.h - Nerd mode, part 3: the broker's traffic copy (VTIMSG_OBS_TRAFFIC) as readable lines.
// USB-MIDI packets are put back together into MIDI messages per direction and cable; Access SysEx commands are named
// as far as findings/spec.md §6 documents them, parameter changes by the device model (the user's Access files) when
// it is loaded. Anything else is shown as hex. Only for display: nothing here is sent anywhere.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace vti {
class RemoteDevice;
}

namespace vti::cc {

struct TrafficLine {
    uint32_t ms = 0;    // broker time
    int dir = 0;        // 0 to the Virus, 1 from the Virus, 2 USB control request
    int ep = 0;         // USB endpoint (0 for a control request)
    int cable = -1;     // USB-MIDI cable, -1 when the data is not USB-MIDI
    std::string kind;   // "note", "cc", "sysex", "param", "dump", "clock", "control", "other"
    std::string text;
    std::vector<uint8_t> bytes; // the complete MIDI message (empty for control requests)
};

class TrafficDecoder {
public:
    explicit TrafficDecoder(const RemoteDevice *model = nullptr) : model_(model) {}
    // one VTIMSG_OBS_TRAFFIC payload; complete messages go to `out`
    void feed(const uint8_t *payload, size_t len, const std::function<void(TrafficLine &&)> &out);

    // a complete MIDI message (F0..F7 or a channel/system message) as text; public for the tests
    std::string describe(const std::vector<uint8_t> &msg, std::string &kind) const;

private:
    const RemoteDevice *model_;
    std::vector<uint8_t> sysex_[2][16]; // per direction and cable
};

} // namespace vti::cc
