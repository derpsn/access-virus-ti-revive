// capture.h - the full traffic log, like a USB sniffer on the MIDI interfaces: every transfer to and from the Virus as
// raw bytes and decoded (endpoint, cable, kind), the clock lines included, the broker's log lines, what the observer
// lost, and a summary. Fed from a broker observer, read-only. Not in it: the isochronous audio stream.
#pragma once
#include "cc/decode.h"
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <tuple>

namespace vti {
class RemoteDevice;
}

namespace vti::cc {

class CaptureLog {
public:
    using Sink = std::function<void(const std::string &)>; // one line, no newline
    CaptureLog(const RemoteDevice *model, Sink sink) : dec_(model), sink_(std::move(sink)) {}

    void traffic(const uint8_t *payload, size_t len); // a VTIMSG_OBS_TRAFFIC payload
    void log(const std::string &text);                // a VTIMSG_OBS_LOG line
    void status(const std::string &text);             // a VTIMSG_OBS_STATUS line: "key=value" fields, tabs between
    void summary();                                   // what was seen, per direction, endpoint, cable and kind

    uint64_t lost() const { return lost_; }
    uint64_t count(int dir, int ep, int cable, const std::string &kind) const;

private:
    TrafficDecoder dec_;
    Sink sink_;
    std::map<std::tuple<int, int, int, std::string>, uint64_t> counts_;
    uint64_t lost_ = 0;
    bool status_seen_ = false;
};

} // namespace vti::cc
