// Windows MIDI ports (user 2026-11-15: only where Windows supports it, read-only): the broker hands the Virus keys and
// its DIN MIDI In to the helper tirevive-midiports.exe over a pipe (src/broker/midifeed.h), and the helper sends them to
// Windows MIDI Services as UMP words (src/midiports-win/umpconv.h): channel messages as MIDI 1.0 channel voice (type 2),
// system messages as type 1, SysEx as 7-bit data packets (type 3, up to 6 bytes each). Hardware-free, all platforms.
#include "check.h"
#include <algorithm>
#include <cstdint>
#include <vector>
extern "C" {
#include "midifeed.h"
}
#include "../../src/midiports-win/umpconv.h"

namespace {
std::vector<uint32_t> ump(vti::UmpConv &c, std::vector<uint8_t> msg)
{
    uint32_t w[16];
    const int n = c.push(msg.data(), (int)msg.size(), w, 16);
    return std::vector<uint32_t>(w, w + (n > 0 ? n : 0));
}

struct Frames {
    std::vector<std::pair<int, std::vector<uint8_t>>> got;
};
void on_frame(void *u, int port, const uint8_t *m, int n)
{
    ((Frames *)u)->got.push_back({port, std::vector<uint8_t>(m, m + n)});
}
} // namespace

TEST(midifeed_frames_round_trip_in_any_pieces)
{
    uint8_t buf[64];
    int len = 0;
    const uint8_t note[] = {0x90, 60, 100}, clock[] = {0xF8}, sx[] = {0xF0, 0x00, 0x20};
    len += vti_midifeed_frame(1, note, 3, buf + len, (int)sizeof buf - len);
    len += vti_midifeed_frame(0, clock, 1, buf + len, (int)sizeof buf - len);
    len += vti_midifeed_frame(0, sx, 3, buf + len, (int)sizeof buf - len);
    CHECK(len == 5 + 3 + 5);
    Frames f;
    vti_midifeed_reader r;
    vti_midifeed_init(&r);
    for (int i = 0; i < len; i++) // one byte at a time, as a pipe may deliver it
        vti_midifeed_read(&r, buf + i, 1, on_frame, &f);
    CHECK(f.got.size() == 3);
    CHECK(f.got[0].first == 1 && f.got[0].second == (std::vector<uint8_t>{0x90, 60, 100}));
    CHECK(f.got[1].first == 0 && f.got[1].second == (std::vector<uint8_t>{0xF8}));
    CHECK(f.got[2].first == 0 && f.got[2].second.size() == 3);
}

TEST(midifeed_refuses_bad_frames_and_finds_the_next_one)
{
    uint8_t buf[16];
    const uint8_t big[4] = {1, 2, 3, 4};
    CHECK(vti_midifeed_frame(1, big, 4, buf, 16) == 0); // more than one USB-MIDI packet's bytes
    CHECK(vti_midifeed_frame(2, big, 1, buf, 16) == 0); // no such port
    CHECK(vti_midifeed_frame(1, big, 3, buf, 4) == 0);  // no room
    // garbage, then a good frame: the reader skips to the next frame marker
    const uint8_t stream[] = {0x12, 0x34, 0xA1, 9, 0x99, 0xA1, 3, 0x80, 60, 0};
    Frames f;
    vti_midifeed_reader r;
    vti_midifeed_init(&r);
    vti_midifeed_read(&r, stream, (int)sizeof stream, on_frame, &f);
    CHECK(f.got.size() == 1 && f.got[0].second == (std::vector<uint8_t>{0x80, 60, 0}));
}

TEST(umpconv_channel_and_system_messages)
{
    vti::UmpConv c(1); // group 2 on the wire: groups count from 0
    CHECK(ump(c, {0x92, 60, 100}) == (std::vector<uint32_t>{0x21923C64u}));
    CHECK(ump(c, {0xC5, 7}) == (std::vector<uint32_t>{0x21C50700u}));
    CHECK(ump(c, {0xE0, 0x00, 0x40}) == (std::vector<uint32_t>{0x21E00040u}));
    CHECK(ump(c, {0xF8}) == (std::vector<uint32_t>{0x11F80000u}));
    CHECK(ump(c, {0xF2, 0x10, 0x02}) == (std::vector<uint32_t>{0x11F21002u}));
    CHECK(ump(c, {0xF3, 5}) == (std::vector<uint32_t>{0x11F30500u}));
    CHECK(ump(c, {0x90, 60}).empty());      // incomplete
    CHECK(ump(c, {0xF4}).empty());          // undefined
    CHECK(ump(c, {0x90, 0x80, 1}).empty()); // a status byte where data belongs
}

// SysEx comes from the DIN port in USB-MIDI's 3-byte pieces; UMP carries up to 6 bytes per packet: start, continue,
// end, or complete when it fits in one
TEST(umpconv_sysex_packets)
{
    vti::UmpConv c(0);
    // F0 7E 7F 06 01 F7 (identity request): 4 data bytes, one complete packet
    std::vector<uint32_t> w = ump(c, {0xF0, 0x7E, 0x7F});
    CHECK(w.empty()); // still waiting for more
    w = ump(c, {0x06, 0x01, 0xF7});
    CHECK(w == (std::vector<uint32_t>{0x30047E7Fu, 0x06010000u}));
    // 8 data bytes: a start packet of 6, an end packet of 2
    std::vector<uint32_t> all;
    for (auto piece : std::vector<std::vector<uint8_t>>{{0xF0, 1, 2}, {3, 4, 5}, {6, 7, 8}, {0xF7}}) {
        auto p = ump(c, piece);
        all.insert(all.end(), p.begin(), p.end());
    }
    CHECK(all == (std::vector<uint32_t>{0x30160102u, 0x03040506u, 0x30320708u, 0x00000000u}));
    // a new F0 in the middle drops the unfinished one; data bytes without an F0 are dropped
    vti::UmpConv d(0);
    CHECK(ump(d, {1, 2, 3}).empty());
    ump(d, {0xF0, 1, 2});
    w = ump(d, {0xF0, 9, 0xF7});
    CHECK(w == (std::vector<uint32_t>{0x30010900u, 0x00000000u}));
}

// a SysEx longer than 12 data bytes: start, continue and end packets
TEST(umpconv_sysex_continue_packets)
{
    vti::UmpConv c(0);
    std::vector<uint32_t> all;
    std::vector<uint8_t> bytes = {0xF0};
    for (int i = 1; i <= 14; i++)
        bytes.push_back((uint8_t)i);
    bytes.push_back(0xF7);
    for (size_t i = 0; i < bytes.size(); i += 3) {
        std::vector<uint8_t> piece(bytes.begin() + i, bytes.begin() + std::min(bytes.size(), i + 3));
        auto p = ump(c, piece);
        all.insert(all.end(), p.begin(), p.end());
    }
    // 14 data bytes: start (6), continue (6), end (2)
    CHECK(all.size() == 6);
    CHECK((all[0] >> 20) == 0x301 && ((all[0] >> 16) & 15) == 6);
    CHECK((all[2] >> 20) == 0x302 && ((all[2] >> 16) & 15) == 6);
    CHECK((all[4] >> 20) == 0x303 && ((all[4] >> 16) & 15) == 2);
}

namespace {
struct Pipe { // a non-blocking pipe that takes at most `room` bytes per write
    std::vector<uint8_t> data;
    int room = 64;
};
int pipe_write(void *u, const uint8_t *b, int n)
{
    auto *p = (Pipe *)u;
    const int w = n < p->room ? n : p->room;
    p->data.insert(p->data.end(), b, b + w);
    return w;
}
} // namespace

TEST(midifeed_partial_write_keeps_the_frames_whole)
{
    // audit #26: a non-blocking WriteFile can take part of a frame; the next frame then started inside it
    const uint8_t note[] = {0x90, 60, 100}, at[] = {0xA0, 60, 10}, clock[] = {0xF8};
    uint8_t f[8];
    Pipe p;
    vti_midifeed_out o = {};
    p.room = 2; // only marker + length go through
    CHECK(vti_midifeed_send(&o, f, vti_midifeed_frame(1, note, 3, f, 8), pipe_write, &p) == 1);
    p.room = 0; // full: the tail waits, the next frame is dropped, not mixed in
    CHECK(vti_midifeed_send(&o, f, vti_midifeed_frame(0, clock, 1, f, 8), pipe_write, &p) == 0);
    p.room = 64;
    CHECK(vti_midifeed_send(&o, f, vti_midifeed_frame(1, at, 3, f, 8), pipe_write, &p) == 1);
    Frames got;
    vti_midifeed_reader r;
    vti_midifeed_init(&r);
    vti_midifeed_read(&r, p.data.data(), (int)p.data.size(), on_frame, &got);
    CHECK(got.got.size() == 2);
    CHECK(got.got.size() == 2 && got.got[0].second == (std::vector<uint8_t>{0x90, 60, 100}) &&
          got.got[1].second == (std::vector<uint8_t>{0xA0, 60, 10}));
}
