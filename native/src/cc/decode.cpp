// decode.cpp - see decode.h.
#include "decode.h"
#include "../model/remotedevice.h"
#include <cstdio>
#include <cstring>

namespace vti::cc {

static const char *kNotes[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

static std::string hex(const std::vector<uint8_t> &m, size_t max = 24)
{
    std::string s;
    char b[4];
    for (size_t i = 0; i < m.size() && i < max; i++) {
        snprintf(b, sizeof b, i ? " %02X" : "%02X", m[i]);
        s += b;
    }
    if (m.size() > max)
        s += " ...";
    return s;
}

static std::string bank_name(int b)
{
    if (b == 0)
        return "edit buffer";
    if (b >= 1 && b <= 4)
        return std::string("RAM ") + (char)('A' + b - 1);
    return "bank " + std::to_string(b);
}

std::string TrafficDecoder::describe(const std::vector<uint8_t> &m, std::string &kind) const
{
    char b[160];
    if (m.empty())
        return {};
    uint8_t st = m[0];
    if (st == 0xF0) {
        kind = "sysex";
        bool access = m.size() >= 8 && m[1] == 0x00 && m[2] == 0x20 && m[3] == 0x33 && m[4] == 0x01;
        if (!access) {
            if (m.size() >= 6 && m[1] == 0x7E && m[3] == 0x06 && m[4] == 0x01)
                return "Identity Request";
            return "SysEx (" + std::to_string(m.size()) + " bytes): " + hex(m);
        }
        uint8_t cmd = m[6];
        // spec.md §6 (E027, E031, E033); 39/14 = bank info request / reply (native/src/device/memsync.h)
        if (cmd == 0x10 && m.size() >= 9) {
            kind = "dump";
            std::string s = "Single dump " + bank_name(m[7]) + (m[7] ? " #" + std::to_string(m[8] + 1)
                                                                      : " part " + std::to_string(m[8] + 1));
            if (m.size() == 524) {
                std::string name(m.begin() + 249, m.begin() + 259);
                s += " \"" + name + "\"";
            } else {
                s += " (" + std::to_string(m.size()) + " bytes)";
            }
            return s;
        }
        if (cmd == 0x11 && m.size() >= 9)
            return kind = "dump", "Multi dump (" + std::to_string(m.size()) + " bytes)";
        if (cmd == 0x30 && m.size() >= 10)
            return kind = "request", "Single dump request " + bank_name(m[7]) + " #" + std::to_string(m[8] + 1);
        if (cmd == 0x39 && m.size() >= 9)
            return kind = "request", "Bank info request " + bank_name(m[7]);
        if (cmd == 0x14 && m.size() >= 9)
            return kind = "dump", "Bank info " + bank_name(m[7]) + " (fingerprints)";
        if (cmd >= 0x6E && cmd <= 0x74 && m.size() >= 11) {
            kind = "param";
            if (model_)
                for (auto &c : model_->decode(m)) {
                    if (c.path.rfind("Actions/", 0) == 0) { // page 74 actions (display messages): no part
                        snprintf(b, sizeof b, "%s = %d", c.path.c_str(), c.value);
                        return b;
                    }
                    bool global = c.part == 16 || cmd == 0x73;
                    if (global && c.path.rfind("Global", 0) == 0) // the path says it already
                        snprintf(b, sizeof b, "%s = %d", c.path.c_str(), c.value);
                    else
                        snprintf(b, sizeof b, "%s %s = %d", global ? "global" : ("part " + std::to_string(c.part + 1)).c_str(),
                                 c.path.c_str(), c.value);
                    return b;
                }
            if (cmd == 0x73 && m[8] == 0x22)
                return "Sync ping (global 0x22)";
            snprintf(b, sizeof b, "Parameter page %02X part %02X id %02X = %d", cmd, m[7], m[8], m[9]);
            return b;
        }
        snprintf(b, sizeof b, "Access SysEx cmd %02X (%zu bytes): ", cmd, m.size());
        return b + hex(m);
    }
    switch (st) {
    case 0xF8: return kind = "clock", "Clock";
    case 0xFE: return kind = "clock", "Active Sensing";
    case 0xFA: return kind = "clock", "Start";
    case 0xFC: return kind = "clock", "Stop";
    case 0xFB: return kind = "clock", "Continue";
    case 0xF2:
        snprintf(b, sizeof b, "Song position %d", m.size() > 2 ? (m[1] | m[2] << 7) : 0);
        return kind = "clock", b;
    case 0xF5: // TI timing messages (spec.md §7, docs/protocol.md)
        snprintf(b, sizeof b, "Timing F5 %02X %02X", m.size() > 1 ? m[1] : 0, m.size() > 2 ? m[2] : 0);
        return kind = "clock", b;
    }
    int ch = (st & 0x0F) + 1;
    switch (st & 0xF0) {
    case 0x80:
    case 0x90:
        if (m.size() >= 3) {
            bool on = (st & 0xF0) == 0x90 && m[2];
            kind = "note";
            snprintf(b, sizeof b, "Note %s ch%d %s%d vel %d", on ? "on " : "off", ch, kNotes[m[1] % 12], m[1] / 12 - 1,
                     m[2]);
            return b;
        }
        break;
    case 0xA0: // poly pressure = parameter page B
    case 0xB0:
        if (m.size() >= 3) {
            kind = "cc";
            if (model_)
                for (auto &c : model_->decode(m)) {
                    snprintf(b, sizeof b, "ch%d %s = %d", ch, c.path.c_str(), c.value);
                    return b;
                }
            snprintf(b, sizeof b, "%s ch%d #%d = %d", (st & 0xF0) == 0xB0 ? "CC" : "Poly pressure", ch, m[1], m[2]);
            return b;
        }
        break;
    case 0xC0:
        if (m.size() >= 2) {
            snprintf(b, sizeof b, "Program change ch%d %d", ch, m[1] + 1);
            return kind = "cc", b;
        }
        break;
    case 0xE0:
        if (m.size() >= 3) {
            snprintf(b, sizeof b, "Pitch bend ch%d %d", ch, (m[1] | m[2] << 7) - 8192);
            return kind = "cc", b;
        }
        break;
    }
    kind = "other";
    return hex(m);
}

void TrafficDecoder::feed(const uint8_t *p, size_t len, const std::function<void(TrafficLine &&)> &out)
{
    if (len < 6)
        return;
    TrafficLine base;
    int kind = p[0];
    uint8_t ep = p[1];
    memcpy(&base.ms, p + 2, 4);
    const uint8_t *d = p + 6;
    size_t n = len - 6;
    if (kind == 2) { // control request: setup[8], i32 result, data
        base.dir = 2;
        base.kind = "control";
        if (n >= 12) {
            int32_t r;
            memcpy(&r, d + 8, 4);
            char b[160];
            snprintf(b, sizeof b, "USB control %s req %02X value %04X index %04X len %d -> %d",
                     d[0] & 0x80 ? "IN" : "OUT", d[1], d[2] | d[3] << 8, d[4] | d[5] << 8, d[6] | d[7] << 8, r);
            base.text = b;
        }
        out(std::move(base));
        return;
    }
    base.dir = kind ? 1 : 0;
    base.ep = ep;
    // raw on EP 0x05, not USB-MIDI (spec 3): read as packets it would be a pitch bend on cable 4
    static const uint8_t kHandshake[4] = {0x4E, 0x73, 0x52, 0x01};
    if (kind == 0 && ep == 0x05 && n == 4 && !memcmp(d, kHandshake, 4)) {
        base.kind = "control";
        base.text = "Handshake 4E 73 52 01";
        base.bytes.assign(d, d + 4);
        out(std::move(base));
        return;
    }
    // USB-MIDI event packets: byte 0 = cable << 4 | CIN
    for (size_t i = 0; i + 4 <= n; i += 4) {
        uint8_t cin = d[i] & 0x0F, cable = d[i] >> 4;
        if (!d[i] && !d[i + 1] && !d[i + 2] && !d[i + 3])
            continue; // padding
        auto &sx = sysex_[base.dir][cable];
        int bytes = 0;
        bool sysex_part = false, sysex_end = false;
        switch (cin) {
        case 0x4: bytes = 3, sysex_part = true; break;               // SysEx start / continue
        case 0x5: bytes = 1, sysex_part = sysex_end = !sx.empty() || d[i + 1] == 0xF7; break; // SysEx end, else system
        case 0x6: bytes = 2, sysex_part = sysex_end = true; break;
        case 0x7: bytes = 3, sysex_part = sysex_end = true; break;
        case 0x2: case 0xC: case 0xD: bytes = 2; break;
        case 0x3: case 0x8: case 0x9: case 0xA: case 0xB: case 0xE: bytes = 3; break;
        case 0xF: bytes = 1; break;
        default: continue;
        }
        if (sysex_part) {
            if (d[i + 1] == 0xF0 && !sx.empty()) { // a new start: the one before never ended (the Virus' repeated handshake reply)
                TrafficLine l = base;
                l.cable = cable;
                l.kind = "sysex";
                l.text = "c" + std::to_string(cable) + " Unfinished SysEx (" + std::to_string(sx.size()) + " bytes): " + hex(sx);
                l.bytes = std::move(sx);
                out(std::move(l));
                sx.clear();
            }
            sx.insert(sx.end(), d + i + 1, d + i + 1 + bytes);
            if (sx.size() > 4096) // runaway: show what there is
                sysex_end = true;
            if (!sysex_end)
                continue;
            TrafficLine l = base;
            l.cable = cable;
            l.text = "c" + std::to_string(cable) + " " + describe(sx, l.kind);
            l.bytes = std::move(sx);
            out(std::move(l));
            sx.clear();
            continue;
        }
        std::vector<uint8_t> msg(d + i + 1, d + i + 1 + bytes);
        TrafficLine l = base;
        l.cable = cable;
        l.text = "c" + std::to_string(cable) + " " + describe(msg, l.kind);
        l.bytes = std::move(msg);
        out(std::move(l));
    }
}

} // namespace vti::cc
