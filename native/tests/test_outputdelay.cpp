// The output delay of parts on the Virus' analog outputs (device/outputdelay.h, E344): the original tells the Virus to
// hold back the audio of analog parts by its latency, so they sound together with the USB parts; USB parts get 0.
// Hardware-free.
#include "check.h"
#include "../src/device/outputdelay.h"
#include "access/accessfiles.h"
#include "model/remotedevice.h"
#include <string>

using vti::OutputDelay;

namespace {
int usb_all[16] = {10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10};
}

TEST(outputdelay_analog_is_below_9)
{
    CHECK(vti::output_is_analog(0) && vti::output_is_analog(1) && vti::output_is_analog(4) && vti::output_is_analog(8));
    CHECK(!vti::output_is_analog(9) && !vti::output_is_analog(10) && !vti::output_is_analog(13)); // USB1/USB2 (E343)
    CHECK(!vti::output_is_analog(-1));
}

TEST(outputdelay_message_like_the_original)
{
    // captures/ref-outdelay-1: part 1 on Out1 -> F0 00 20 33 01 10 6F 00 7F 1A F7 (26 ms)
    auto m = OutputDelay::message(0, 26);
    const uint8_t want[11] = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x6F, 0x00, 0x7F, 0x1A, 0xF7};
    CHECK(m.size() == 11);
    for (int i = 0; i < 11; i++)
        CHECK(m[i] == want[i]);
    CHECK(OutputDelay::message(15, 0)[7] == 0x0F);
    CHECK(OutputDelay::message(3, 500)[9] == 127); // a data byte: at most 127 ms
    CHECK(OutputDelay::message(3, -5)[9] == 0);
}

TEST(outputdelay_first_time_all_parts)
{
    OutputDelay d;
    int parts[16], ms[16];
    int out[16];
    for (int i = 0; i < 16; i++)
        out[i] = usb_all[i];
    out[0] = 1; // part 1 on Out1 L+R
    int n = d.due(out, 40, false, parts, ms);
    CHECK(n == 16); // nothing sent yet: every part
    CHECK(parts[0] == 0 && ms[0] == 40);
    for (int i = 1; i < 16; i++)
        CHECK(ms[i] == 0);
    CHECK(d.due(out, 40, false, parts, ms) == 0); // unchanged: nothing
}

TEST(outputdelay_only_changed_parts)
{
    OutputDelay d;
    int parts[16], ms[16];
    int out[16];
    for (int i = 0; i < 16; i++)
        out[i] = usb_all[i];
    d.due(out, 40, false, parts, ms);
    out[2] = 4; // part 3 to Out2
    CHECK(d.due(out, 40, false, parts, ms) == 1);
    CHECK(parts[0] == 2 && ms[0] == 40);
    out[2] = 13; // back to USB2
    CHECK(d.due(out, 40, false, parts, ms) == 1);
    CHECK(parts[0] == 2 && ms[0] == 0);
    out[5] = 7; // part 6 to Out3, then the latency changes: the analog part follows, USB parts stay at 0
    d.due(out, 40, false, parts, ms);
    CHECK(d.due(out, 84, false, parts, ms) == 1);
    CHECK(parts[0] == 5 && ms[0] == 84);
}

TEST(outputdelay_all_parts_at_play_stop_and_after_reset)
{
    OutputDelay d;
    int parts[16], ms[16];
    int out[16];
    for (int i = 0; i < 16; i++)
        out[i] = usb_all[i];
    out[0] = 1;
    d.due(out, 26, false, parts, ms);
    CHECK(d.due(out, 26, true, parts, ms) == 16); // Play / Stop: the original sends all 16 (ref-outdelay-1)
    CHECK(ms[0] == 26 && ms[1] == 0);
    d.reset(); // a new connection: the Virus' values are unknown again
    CHECK(d.due(out, 26, false, parts, ms) == 16);
}

// the part's Output Select is found through its message (72 pp 29), not by name: the TI calls it "Output Select
// Control2/3", the Snow "pr Output Select Control2/3" - a fixed Snow name read nothing on a TI (every part looked
// analog, the USB parts were delayed too; user, REAPER, 2026-10-24)
TEST(outputdelay_select_found_by_message_in_both_models)
{
    std::string err;
    auto files = vti::AccessFiles::open("", err);
    if (!files)
        SKIP("no Access installation");
    auto r = OutputDelay::select_report(2);
    CHECK(r[6] == 0x72 && r[7] == 2 && r[8] == 0x29);
    for (const char *name : {"remotedevice.xml", "remotedevicePR.xml"}) {
        auto xml = files->get(name);
        if (xml.empty())
            continue; // the Snow description only where the installer has it
        vti::RemoteDevice dev;
        CHECK(dev.load(xml, err));
        auto changes = dev.decode(std::vector<uint8_t>(r.begin(), r.end()));
        CHECK(changes.size() == 2); // the forms with and without the third USB output
        for (auto &c : changes) {
            const bool form = c.path.size() > 15 && (c.path.compare(c.path.size() - 15, 15, "Select Control2") == 0 ||
                                                     c.path.compare(c.path.size() - 15, 15, "Select Control3") == 0);
            if (!form)
                std::fprintf(stderr, "  %s: %s\n", name, c.path.c_str());
            CHECK(form && c.part == 2);
        }
    }
}
