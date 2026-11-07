// The broker's pre-roll (trafficring.h): the newest traffic records, kept from the moment the device opens, so an
// observer that connects later still sees the handshake and the Virus' reply. Hardware-free.
#include "check.h"
#include <cstring>
#include <string>
#include <vector>
extern "C" {
#include "trafficring.h"
}

namespace {
std::vector<std::string> records(const vti_trafring &r)
{
    std::vector<std::string> out;
    vti_trafring_each(
        &r,
        [](void *u, const uint8_t *hdr, const uint8_t *body, int n) {
            std::string s((const char *)hdr, VTI_TRAFRING_HDR);
            s.append((const char *)body, (size_t)n);
            ((std::vector<std::string> *)u)->push_back(s);
        },
        &out);
    return out;
}
std::string rec(uint8_t kind, uint8_t ep, uint32_t ms, const std::string &body)
{
    std::string s = {(char)kind, (char)ep, (char)ms, (char)(ms >> 8), (char)(ms >> 16), (char)(ms >> 24)};
    return s + body;
}
void push(vti_trafring &r, uint8_t kind, uint8_t ep, uint32_t ms, const std::string &a, const std::string &b = "")
{
    uint8_t hdr[VTI_TRAFRING_HDR] = {kind, ep};
    memcpy(hdr + 2, &ms, 4);
    vti_trafring_push(&r, hdr, a.data(), (int)a.size(), b.data(), (int)b.size());
}
} // namespace

TEST(trafring_empty_and_order)
{
    uint8_t mem[256];
    vti_trafring r;
    vti_trafring_init(&r, mem, sizeof mem);
    CHECK(records(r).empty());
    CHECK_EQ(r.lost, (uint64_t)0);
    push(r, 0, 0x05, 10, "\x4e\x73\x52\x01");
    push(r, 1, 0x85, 16, "abcd", "ef"); // two parts make one record (a control's setup + data)
    auto v = records(r);
    CHECK_EQ(v.size(), 2u);
    CHECK(v.size() == 2 && v[0] == rec(0, 0x05, 10, "\x4e\x73\x52\x01") && v[1] == rec(1, 0x85, 16, "abcdef"));
}

TEST(trafring_drops_the_oldest_whole_records_and_counts_them)
{
    uint8_t mem[64];
    vti_trafring r;
    vti_trafring_init(&r, mem, sizeof mem); // a record here takes 2 + 6 + 8 = 16 bytes: four fit
    for (uint32_t i = 0; i < 6; i++)
        push(r, 1, 0x85, i, "12345678");
    auto v = records(r);
    CHECK_EQ(v.size(), 4u);
    CHECK(v.size() == 4 && v[0] == rec(1, 0x85, 2, "12345678") && v[3] == rec(1, 0x85, 5, "12345678"));
    CHECK_EQ(r.lost, (uint64_t)2); // the capture says so: what it starts with is not everything
}

TEST(trafring_wraps_inside_a_record)
{
    uint8_t mem[50];
    vti_trafring r;
    vti_trafring_init(&r, mem, sizeof mem); // 16 bytes each: the fourth write crosses the end of the buffer
    for (uint32_t i = 0; i < 9; i++)
        push(r, 0, 0x06, i, "ABCDEFGH");
    auto v = records(r);
    CHECK_EQ(v.size(), 3u);
    CHECK(v.size() == 3 && v[2] == rec(0, 0x06, 8, "ABCDEFGH") && v[0] == rec(0, 0x06, 6, "ABCDEFGH"));
}

TEST(trafring_refuses_a_record_larger_than_the_ring)
{
    uint8_t mem[32];
    vti_trafring r;
    vti_trafring_init(&r, mem, sizeof mem);
    push(r, 1, 0x85, 1, "keep");
    push(r, 1, 0x85, 2, std::string(100, 'x'));
    auto v = records(r);
    CHECK(v.size() == 1 && v[0] == rec(1, 0x85, 1, "keep")); // kept what it had
    CHECK_EQ(r.lost, (uint64_t)1);
}
