// The full traffic log (src/cc/capture): every transfer as raw bytes and decoded, with endpoint and cable, the clock
// lines included, broker log lines, what an observer lost, and a summary. Hardware-free.
#include "cc/capture.h"
#include "check.h"
#include <algorithm>
#include <string>
#include <vector>

using namespace vti::cc;

namespace {
std::vector<uint8_t> traffic(int kind, uint8_t ep, uint32_t ms, std::vector<uint8_t> packets)
{
    std::vector<uint8_t> p = {(uint8_t)kind, ep, (uint8_t)ms, (uint8_t)(ms >> 8), (uint8_t)(ms >> 16), (uint8_t)(ms >> 24)};
    p.insert(p.end(), packets.begin(), packets.end());
    return p;
}
struct Fix {
    std::vector<std::string> lines;
    CaptureLog log{nullptr, [this](const std::string &l) { lines.push_back(l); }};
    void feed(const std::vector<uint8_t> &p) { log.traffic(p.data(), p.size()); }
    bool has(const std::string &s) const
    {
        return std::any_of(lines.begin(), lines.end(), [&](const std::string &l) { return l.find(s) != std::string::npos; });
    }
};
} // namespace

TEST(capture_key_note_from_the_virus_with_endpoint_cable_and_raw_bytes)
{
    Fix f;
    f.feed(traffic(1, 0x85, 12345, {0x19, 0x90, 60, 100}));
    CHECK_EQ(f.lines.size(), 2u);
    // the transfer as it came, then what it means
    CHECK(f.lines.size() == 2 && f.lines[0] == "  12.345 <- Virus ep85 raw 19 90 3C 64");
    CHECK(f.lines.size() == 2 && f.lines[1] == "  12.345 <- Virus ep85 c1 note Note on  ch1 C4 vel 100");
}

TEST(capture_keeps_what_the_decoder_cannot_read)
{
    Fix f;
    f.feed(traffic(1, 0x85, 1, {0x10, 0x01, 0x02, 0x03})); // CIN 0: no message, but it is on the wire
    CHECK(f.lines.size() == 1 && f.lines[0] == "   0.001 <- Virus ep85 raw 10 01 02 03");
    // two SysEx interleaved on one cable (E022): the decoded line may be wrong, the raw lines never are
    f.lines.clear();
    f.feed(traffic(1, 0x85, 2, {0x14, 0xF0, 0x00, 0x20, 0x14, 0xF0, 0x00, 0x20}));
    CHECK(f.has("raw 14 F0 00 20 14 F0 00 20"));
}

TEST(capture_includes_clock_and_the_handshake)
{
    Fix f;
    f.feed(traffic(0, 0x06, 5, {0x2F, 0xF8, 0, 0}));
    CHECK(f.has("-> Virus ep06 c2 clock Clock"));
    f.feed(traffic(0, 0x05, 6, {0x4E, 0x73, 0x52, 0x01}));
    CHECK(f.has("-> Virus ep05 raw 4E 73 52 01"));
    CHECK(f.has("-> Virus ep05 control Handshake 4E 73 52 01"));
}

TEST(capture_sysex_shows_all_its_bytes)
{
    Fix f;
    // the reply to the handshake: F0 00 20 33 01 10 73 40 40 00 F7 on cable 1
    f.feed(traffic(1, 0x85, 7, {0x14, 0xF0, 0x00, 0x20, 0x14, 0x33, 0x01, 0x10, 0x14, 0x73, 0x40, 0x40, 0x16, 0x00, 0xF7, 0x00}));
    CHECK(f.has("ep85 c1"));
    CHECK(f.has("F0 00 20 33 01 10 73 40 40 00 F7"));
}

TEST(capture_control_request_with_its_data)
{
    Fix f;
    // IN, GET_DESCRIPTOR device, 18 bytes, result 2 (record: setup[8], i32 result, data)
    f.feed(traffic(2, 0, 9, {0x80, 6, 0, 1, 0, 0, 18, 0, 2, 0, 0, 0, 0x12, 0x01}));
    CHECK(f.has("USB ctl USB control IN req 06 value 0100 index 0000 len 18 -> 2"));
    CHECK(f.has("raw 80 06 00 01 00 00 12 00 02 00 00 00 12 01"));
}

TEST(capture_log_lines_and_losses_are_recorded)
{
    Fix f;
    f.log.log("vti: write ep 06: 1 timeout(s), then accepted");
    CHECK(f.has("log     vti: write ep 06: 1 timeout(s), then accepted"));
    f.lines.clear();
    f.log.status("device=1\towner=x\tdropped=0");
    size_t first = f.lines.size();
    CHECK_EQ(first, 1u); // the first status goes in
    f.log.status("device=1\towner=x\tdropped=0");
    CHECK_EQ(f.lines.size(), first); // one a second would only fill the file
    f.log.status("device=1\towner=x\tdropped=3");
    CHECK(f.has("LOST 3 messages")); // a gap has to be visible where it happened
    CHECK_EQ(f.log.lost(), (uint64_t)3);
    f.log.status("device=1\towner=x\tdropped=5");
    CHECK(f.has("LOST 2 messages"));
    CHECK_EQ(f.log.lost(), (uint64_t)5);
}

TEST(capture_preroll_loss_is_reported)
{
    Fix f;
    f.log.log("pre-roll: 7 older records were overwritten before this capture started");
    CHECK(f.has("pre-roll: 7 older"));
}

TEST(capture_summary_counts_per_direction_endpoint_cable_and_kind)
{
    Fix f;
    f.feed(traffic(1, 0x85, 1, {0x19, 0x90, 60, 100, 0x18, 0x80, 60, 0}));
    f.feed(traffic(1, 0x85, 2, {0x1B, 0xB0, 1, 64}));
    f.feed(traffic(0, 0x06, 3, {0x29, 0x90, 60, 100}));
    f.lines.clear();
    f.log.summary();
    CHECK(f.has("<- Virus ep85 c1 note: 2"));
    CHECK(f.has("<- Virus ep85 c1 cc: 1"));
    CHECK(f.has("-> Virus ep06 c2 note: 1"));
    CHECK(f.has("lost messages: 0"));
    CHECK_EQ(f.log.count(1, 0x85, 1, "note"), (uint64_t)2);
    CHECK_EQ(f.log.count(1, 0x85, 2, "note"), (uint64_t)0);
}
