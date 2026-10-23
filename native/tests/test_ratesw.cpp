// The broker's sample rate switch (design-rate.md step 3, E323): the driver's order on the wire (stream stopped and
// every transfer back, alt 0, ~250 ms, alt 1, SET_CUR to EP 0x82 and 0x01 twice, then the stream), never a SET_CUR
// on a running stream, at most one switch per second (rule 2), and the AUDIO request's wanted rate. Against a fake
// USB layer that records every call. Hardware-free.
#include "check.h"
#include <cstring>
#include <string>
extern "C" {
#include "ratesw.h"
#include "vtiproto.h"
}

namespace {
struct Fake {
    std::string log;
    bool streaming = true;
    int fail_set_cur_rate = 0; // SET_CUR with this rate fails
    int fail_alt = -1;         // alt setting with this value fails
    bool set_cur_while_streaming = false;
};

void f_stop(void *u)
{
    Fake *f = (Fake *)u;
    f->streaming = false;
    f->log += "stop;";
}
int f_alt(void *u, int iface, int alt)
{
    Fake *f = (Fake *)u;
    f->log += "alt" + std::to_string(iface) + "=" + std::to_string(alt) + ";";
    return alt == f->fail_alt ? -1 : 0;
}
void f_sleep(void *u, int ms) { ((Fake *)u)->log += "sleep" + std::to_string(ms) + ";"; }
int f_set_cur(void *u, uint8_t ep, uint32_t rate)
{
    Fake *f = (Fake *)u;
    if (f->streaming)
        f->set_cur_while_streaming = true;
    char b[32];
    snprintf(b, sizeof b, "cur%02x=%u;", ep, rate);
    f->log += b;
    return (int)rate == f->fail_set_cur_rate ? -1 : 0;
}
int f_start(void *u, uint32_t rate)
{
    Fake *f = (Fake *)u;
    f->streaming = true;
    f->log += "start" + std::to_string(rate) + ";";
    return 0;
}

vti_rate_ops ops(Fake &f) { return vti_rate_ops{&f, f_stop, f_alt, f_sleep, f_set_cur, f_start}; }
} // namespace

TEST(ratesw_set_cur_in_the_drivers_order)
{
    Fake f;
    f.streaming = false;
    vti_rate_ops o = ops(f);
    CHECK(vti_rate_set_cur(&o, 48000) == 0);
    CHECK(f.log == std::string("cur82=48000;cur01=48000;cur82=48000;cur01=48000;"));
}

TEST(ratesw_set_cur_reports_a_refusal)
{
    Fake f;
    f.streaming = false;
    f.fail_set_cur_rate = 48000;
    vti_rate_ops o = ops(f);
    CHECK(vti_rate_set_cur(&o, 48000) == -1);
    CHECK(f.log == std::string("cur82=48000;cur01=48000;cur82=48000;cur01=48000;")); // all four tried
}

TEST(ratesw_switch_like_the_driver)
{
    Fake f;
    vti_rate_ops o = ops(f);
    CHECK(vti_rate_switch(&o, 44100, 48000) == 0);
    CHECK(f.log == std::string("stop;alt1=0;alt2=0;sleep250;alt1=1;alt2=1;"
                                "cur82=48000;cur01=48000;cur82=48000;cur01=48000;start48000;"));
    CHECK(!f.set_cur_while_streaming);
}

// a refused SET_CUR: the old rate once more and the stream at it - no back and forth
TEST(ratesw_switch_refused_goes_back_once)
{
    Fake f;
    f.fail_set_cur_rate = 48000;
    vti_rate_ops o = ops(f);
    CHECK(vti_rate_switch(&o, 44100, 48000) == -1);
    CHECK(f.log == std::string("stop;alt1=0;alt2=0;sleep250;alt1=1;alt2=1;"
                                "cur82=48000;cur01=48000;cur82=48000;cur01=48000;"
                                "cur82=44100;cur01=44100;cur82=44100;cur01=44100;start44100;"));
    CHECK(!f.set_cur_while_streaming);
}

// no alternate setting 1 (USB bandwidth, E285): the stream stays stopped, nothing more is sent
TEST(ratesw_switch_without_alt1_stays_stopped)
{
    Fake f;
    f.fail_alt = 1;
    vti_rate_ops o = ops(f);
    CHECK(vti_rate_switch(&o, 44100, 48000) == -2);
    CHECK(f.log == std::string("stop;alt1=0;alt2=0;sleep250;alt1=1;"));
    CHECK(!f.streaming);
}

TEST(ratesw_same_rate_is_no_switch)
{
    Fake f;
    vti_rate_ops o = ops(f);
    CHECK(vti_rate_switch(&o, 48000, 48000) == 0);
    CHECK(f.log.empty());
}

// rule 2: every switch re-locks the Virus' DSP clock; at most one per VTI_RATE_DEBOUNCE_MS
TEST(ratesw_debounce)
{
    vti_rate_guard g = {};
    CHECK(vti_rate_may_switch(&g, 5)); // the first one at once
    vti_rate_switched(&g, 5);
    CHECK(!vti_rate_may_switch(&g, 6));
    CHECK(!vti_rate_may_switch(&g, 5 + VTI_RATE_DEBOUNCE_MS - 1));
    CHECK(vti_rate_may_switch(&g, 5 + VTI_RATE_DEBOUNCE_MS));
    vti_rate_switched(&g, 5000);
    CHECK(!vti_rate_may_switch(&g, 5500));
    CHECK(vti_rate_may_switch(&g, 0xFFFFFFF0u)); // long after (32-bit ms wrap is a difference)
    vti_rate_guard w = {};
    vti_rate_switched(&w, 0xFFFFFF00u);
    CHECK(!vti_rate_may_switch(&w, 0x100u)); // 512 ms later, across the wrap
}

TEST(ratesw_only_two_rates)
{
    CHECK_EQ(vti_rate_valid(44100), 44100u);
    CHECK_EQ(vti_rate_valid(48000), 48000u);
    CHECK_EQ(vti_rate_valid(96000), 44100u); // the plug-in maps the host rate (virusrate.h); anything else = 44.1
    CHECK_EQ(vti_rate_valid(0), 44100u);
}

// VTIMSG_AUDIO: u32 on, the event name (NUL-terminated), then (protocol 5) u32 wanted rate; older clients = 44100
TEST(ratesw_audio_request_rate)
{
    CHECK(VTI_PROTO_VERSION >= 5);
    uint8_t m[32] = {1, 0, 0, 0, 'e', 'v', 0};
    const uint32_t r = 48000;
    memcpy(m + 7, &r, 4);
    CHECK_EQ(vti_audio_req_rate(m, 11), 48000u);
    CHECK_EQ(vti_audio_req_rate(m, 7), 44100u);  // an older client: no rate
    CHECK_EQ(vti_audio_req_rate(m, 10), 44100u); // cut off
    uint8_t e[9] = {1, 0, 0, 0, 0};             // empty name (Linux)
    memcpy(e + 5, &r, 4);
    CHECK_EQ(vti_audio_req_rate(e, 9), 48000u);
    uint8_t nonul[6] = {1, 0, 0, 0, 'e', 'v'}; // no NUL: no rate
    CHECK_EQ(vti_audio_req_rate(nonul, 6), 44100u);
    CHECK_EQ(vti_audio_req_rate(m, 2), 44100u);
    const uint32_t odd = 96000;
    memcpy(m + 7, &odd, 4);
    CHECK_EQ(vti_audio_req_rate(m, 11), 44100u); // only the Virus' two rates
}

// which rate the stream runs at after a client's request: the owner's wish when a switch is allowed now; otherwise
// it stays (a sharing client never switches the owner's Virus; the debounce holds a second request back)
TEST(ratesw_choose)
{
    CHECK_EQ(vti_rate_choose(44100, 48000, 1, 1), 48000u);
    CHECK_EQ(vti_rate_choose(44100, 48000, 0, 1), 44100u);
    CHECK_EQ(vti_rate_choose(44100, 48000, 1, 0), 44100u);
    CHECK_EQ(vti_rate_choose(48000, 44100, 1, 1), 44100u);
    CHECK_EQ(vti_rate_choose(48000, 48000, 1, 0), 48000u);
    CHECK_EQ(vti_rate_choose(48000, 96000, 1, 1), 44100u); // only the two rates
}
