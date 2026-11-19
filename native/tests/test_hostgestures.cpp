// Knob moves on the Virus as host edits, like the original (E304, issue #10): the first change of a parameter begins
// an edit, each tick performs the latest value (coalesced), 250 ms after the last change the edit ends; the value the
// host hands back in that time is an echo and not sent to the Virus again
#include "check.h"
#include "model/hostgestures.h"
#include <string>
#include <vector>
using namespace vti;

namespace {
struct Log {
    std::vector<std::string> calls;
    void tick(HostGestures &g, int64_t now)
    {
        g.tick(
            now, [&](uint32_t id) { calls.push_back("begin " + std::to_string(id)); },
            [&](uint32_t id, double v) { calls.push_back("perform " + std::to_string(id) + " " + std::to_string((int)(v * 127 + 0.5))); },
            [&](uint32_t id) { calls.push_back("end " + std::to_string(id)); });
    }
};
} // namespace

// a knob turned: begin + perform on the first tick, the latest value per tick, end 250 ms after the last change
TEST(hostgestures_begin_perform_end)
{
    HostGestures g;
    Log l;
    g.device_change(7, 10 / 127.0, 1000);
    g.device_change(7, 12 / 127.0, 1010);
    l.tick(g, 1020);
    CHECK((l.calls == std::vector<std::string>{"begin 7", "perform 7 12"}));
    l.calls.clear();
    l.tick(g, 1050); // nothing new: nothing performed, still within 250 ms
    CHECK(l.calls.empty());
    g.device_change(7, 15 / 127.0, 1100);
    l.tick(g, 1120);
    CHECK((l.calls == std::vector<std::string>{"perform 7 15"}));
    l.calls.clear();
    l.tick(g, 1340); // 240 ms after the last change: still open
    CHECK(l.calls.empty());
    l.tick(g, 1360); // 260 ms: ended
    CHECK((l.calls == std::vector<std::string>{"end 7"}));
    l.calls.clear();
    l.tick(g, 2000);
    CHECK(l.calls.empty());
    CHECK(!g.active(7));
}

// two knobs at once: each its own edit, in the order they moved
TEST(hostgestures_two_parameters)
{
    HostGestures g;
    Log l;
    g.device_change(3, 1 / 127.0, 0);
    g.device_change(9, 2 / 127.0, 5);
    l.tick(g, 10);
    CHECK((l.calls == std::vector<std::string>{"begin 3", "perform 3 1", "begin 9", "perform 9 2"}));
    l.calls.clear();
    l.tick(g, 300);
    CHECK((l.calls == std::vector<std::string>{"end 3", "end 9"}));
}

// the host hands the performed value back to process(): while the edit is open and up to 500 ms after its end the
// same value is an echo (not sent to the Virus); another value (recorded automation, the user moved it) is not
TEST(hostgestures_echo)
{
    HostGestures g;
    Log l;
    g.device_change(4, 64 / 127.0, 0);
    l.tick(g, 10);
    CHECK(g.echo(4, 64 / 127.0, 20));
    CHECK(!g.echo(4, 65 / 127.0, 20));
    CHECK(!g.echo(5, 64 / 127.0, 20)); // another parameter
    l.tick(g, 300);                     // ended at ~250
    CHECK(g.echo(4, 64 / 127.0, 700));
    CHECK(!g.echo(4, 64 / 127.0, 900)); // long after: a real change to the same value
}

// closing the editor (no more ticks): open edits end at once, nothing stays queued
TEST(hostgestures_flush)
{
    HostGestures g;
    Log l;
    g.device_change(2, 5 / 127.0, 0);
    l.tick(g, 1);
    g.device_change(6, 9 / 127.0, 2);
    l.calls.clear();
    g.flush([&](uint32_t id) { l.calls.push_back("end " + std::to_string(id)); });
    CHECK((l.calls == std::vector<std::string>{"end 2"})); // 6 never began: nothing to end
    l.calls.clear();
    l.tick(g, 3);
    CHECK(l.calls.empty());
}

// audit 2026-11-17 #5: the AU wrapper stores the value as a Float32 and hands that back; for 108 of the 128 knob
// values the rounding moved it by 1e-9 or more, so a knob turned on the Virus went back to it. The same 7-bit step is
// the echo; the next step is not
TEST(hostgestures_echo_survives_float_rounding)
{
    for (int k = 0; k < 128; k++) {
        HostGestures g;
        Log l;
        g.device_change(4, k / 127.0, 0);
        l.tick(g, 10);
        CHECK(g.echo(4, (double)(float)(k / 127.0), 20));
        if (k < 127)
            CHECK(!g.echo(4, (double)(float)((k + 1) / 127.0), 20));
    }
}
