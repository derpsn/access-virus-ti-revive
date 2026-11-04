#include "cc/capture.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace vti::cc {

namespace {
const char *const kDir[] = {"-> Virus", "<- Virus", "USB ctl"};

std::string stamp(uint32_t ms)
{
    char b[24]; // by hand: GTK sets the locale's decimal comma
    snprintf(b, sizeof b, "%4u.%03u ", ms / 1000, ms % 1000);
    return b;
}

std::string hex_of(const uint8_t *p, size_t n)
{
    std::string s;
    s.reserve(n * 3);
    char b[4];
    for (size_t i = 0; i < n; i++) {
        snprintf(b, sizeof b, i ? " %02X" : "%02X", p[i]);
        s += b;
    }
    return s;
}

std::string endpoint(int ep)
{
    char b[8];
    snprintf(b, sizeof b, "ep%02x", ep);
    return b;
}
} // namespace

void CaptureLog::traffic(const uint8_t *p, size_t len)
{
    if (len < 6)
        return;
    uint32_t ms;
    memcpy(&ms, p + 2, 4);
    const int kind = p[0], ep = p[1], dir = kind == 2 ? 2 : kind ? 1 : 0;
    // the transfer as it came first: the decoded lines rest on it, and it keeps what they cannot show (E022: two SysEx
    // interleaved on one cable; packets without a message)
    std::string head = stamp(ms) + kDir[dir] + (kind == 2 ? "" : " " + endpoint(ep));
    if (len > 6)
        sink_(head + " raw " + hex_of(p + 6, len - 6));
    dec_.feed(p, len, [&](TrafficLine &&t) {
        const std::string what = t.kind.empty() ? "other" : t.kind;
        counts_[{dir, ep, t.cable, what}]++;
        if (kind == 2) {
            sink_(head + " " + t.text);
            return;
        }
        std::string text = t.text, cable;
        if (t.cable >= 0) { // the decoder's text starts with "c<cable> "
            cable = "c" + std::to_string(t.cable) + " ";
            if (text.rfind(cable, 0) == 0)
                text.erase(0, cable.size());
        }
        if (!t.bytes.empty() && t.bytes[0] == 0xF0) { // a whole SysEx in one piece, not spread over the raw packets
            const std::string h = hex_of(t.bytes.data(), t.bytes.size());
            if (text.find(h) == std::string::npos)
                text += " | " + h;
        }
        sink_(head + " " + cable + what + " " + text);
    });
}

void CaptureLog::log(const std::string &text)
{
    sink_("log     " + text);
}

void CaptureLog::status(const std::string &text)
{
    uint64_t dropped = 0;
    const size_t at = text.find("dropped=");
    if (at != std::string::npos)
        dropped = strtoull(text.c_str() + at + 8, nullptr, 10);
    if (dropped > lost_) { // where it happened: a gap nobody sees is worse than a gap
        sink_("LOST " + std::to_string(dropped - lost_) + " messages here (the observer's queue was full)");
        lost_ = dropped;
    }
    if (!status_seen_) {
        status_seen_ = true;
        std::string s = text;
        for (auto &c : s)
            if (c == '\t')
                c = ' ';
        sink_("status  " + s);
    }
}

void CaptureLog::summary()
{
    sink_("---- summary ----");
    for (auto &e : counts_) {
        const int dir = std::get<0>(e.first), ep = std::get<1>(e.first), cable = std::get<2>(e.first);
        std::string l = kDir[dir];
        if (dir != 2)
            l += " " + endpoint(ep);
        if (cable >= 0)
            l += " c" + std::to_string(cable);
        sink_(l + " " + std::get<3>(e.first) + ": " + std::to_string(e.second));
    }
    sink_("lost messages: " + std::to_string(lost_));
}

uint64_t CaptureLog::count(int dir, int ep, int cable, const std::string &kind) const
{
    auto it = counts_.find({dir, ep, cable, kind});
    return it == counts_.end() ? 0 : it->second;
}

} // namespace vti::cc
