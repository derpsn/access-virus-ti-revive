// PanelFollow: what the plug-in does with changes made on the Virus' own panel (E152) - the 150 ms preset debounce
// per part, the soft knob value reports, the controllers restored after a patch load
#include "check.h"
#include "access/accessfiles.h"
#include "model/panelfollow.h"
#include "model/remotedevice.h"
#include <memory>
using namespace vti;
using Clock = PanelFollow::Clock;
using std::chrono::milliseconds;

TEST(panel_preset_paths)
{
    CHECK(PanelFollow::is_preset_path("Multi Part Parameter/Bank"));
    CHECK(PanelFollow::is_preset_path("Multi Part Parameter/Patch"));
    CHECK(!PanelFollow::is_preset_path("Multi Part Parameter/Part Volume"));
    CHECK(!PanelFollow::is_preset_path("Multi Part Parameter/Bank "));
    PanelFollow f;
    const auto t = Clock::now();
    CHECK(!f.preset_report("Filter 1/Resonance", 0, t)); // not a preset report: nothing marked
    CHECK(!f.preset_report("Multi Part Parameter/Patch", 16, t)); // out of range
    CHECK(!f.preset_report("Multi Part Parameter/Patch", -1, t));
    CHECK(f.presets_due(t + milliseconds(1000)) == 0);
}

TEST(panel_preset_debounce)
{
    PanelFollow f;
    const auto t = Clock::now();
    CHECK(f.preset_report("Multi Part Parameter/Bank", 0, t));
    CHECK(f.presets_due(t) == 0);
    CHECK(f.presets_due(t + milliseconds(149)) == 0);
    // the Patch report follows the Bank report: the timer starts again from the last one
    CHECK(f.preset_report("Multi Part Parameter/Patch", 0, t + milliseconds(100)));
    CHECK(f.presets_due(t + milliseconds(200)) == 0);
    CHECK(f.presets_due(t + milliseconds(250)) == 1u);
    CHECK(f.presets_due(t + milliseconds(500)) == 0); // forgotten once handed out
}

TEST(panel_preset_turning_through)
{
    // turning the Virus' value knob through ten presets in 50 ms steps loads only the last one, once
    PanelFollow f;
    const auto t = Clock::now();
    int loads = 0;
    for (int i = 0; i < 10; i++) {
        f.preset_report("Multi Part Parameter/Patch", 0, t + milliseconds(50 * i));
        if (f.presets_due(t + milliseconds(50 * i + 1)))
            loads++;
    }
    CHECK(loads == 0);
    CHECK(f.presets_due(t + milliseconds(450 + 150)) == 1u);
}

TEST(panel_preset_several_parts)
{
    PanelFollow f;
    const auto t = Clock::now();
    f.preset_report("Multi Part Parameter/Patch", 0, t);
    f.preset_report("Multi Part Parameter/Patch", 3, t + milliseconds(10));
    f.preset_report("Multi Part Parameter/Bank", 15, t + milliseconds(20));
    CHECK(f.presets_due(t + milliseconds(200)) == ((1u << 0) | (1u << 3) | (1u << 15)));
}

TEST(panel_softknob_reports)
{
    CHECK(PanelFollow::softknob_report("Global Soft Knob Configuration/Value Softknob0") == 1);
    CHECK(PanelFollow::softknob_report("Global Soft Knob Configuration/Value Softknob1") == 2);
    CHECK(PanelFollow::softknob_report("Global Soft Knob Configuration/Value Softknob2") == 3);
    CHECK(PanelFollow::softknob_report("Global Soft Knob Configuration/Value Softknob3") == 0);
    CHECK(PanelFollow::softknob_report("Global Soft Knob Configuration/Value Softknob") == 0);
    CHECK(PanelFollow::softknob_report("Global Soft Knob Configuration/Value Softknob10") == 0);
    CHECK(PanelFollow::softknob_report("Global Soft Knob Configuration/Destination 1") == 0);
    CHECK(PanelFollow::softknob_report("Softknobs/Softknob 1") == 0);
}

TEST(panel_restored_controllers_in_model)
{
    CHECK(PanelFollow::restored_controllers().size() == 13);
    std::string err;
    auto files = AccessFiles::open("", err);
    if (!files)
        SKIP("no Access installation");
    RemoteDevice dev;
    CHECK(dev.load(files->get("remotedevice.xml"), err));
    for (auto &p : PanelFollow::restored_controllers()) {
        if (!dev.find(p))
            std::fprintf(stderr, "  missing: %s\n", p.c_str());
        CHECK(dev.find(p) != nullptr);
        CHECK(!dev.encode(p, 64, 0).empty()); // a controller the plug-in can send
    }
    // the reports the plug-in reacts to exist in the description with these names
    CHECK(dev.find("Multi Part Parameter/Bank") && dev.find("Multi Part Parameter/Patch"));
    CHECK(dev.find("Global Soft Knob Configuration/Value Softknob0") &&
          dev.find("Global Soft Knob Configuration/Value Softknob2"));
    CHECK(dev.find("Multi Parameters/Panel Part"));
}

// one report from the Virus that decodes to several parameters (CC 40 = "Filter 1 Cutoff" while the vocoder is off,
// "Vocoder Carrier Center Frequency" while it is on): only those whose condition holds now - the original adds only
// the visible one in Ableton Live's Configure mode (round 0.8.29 W3: both were added); none holds: all, nothing lost
TEST(panel_reports_by_condition)
{
    using C = RemoteDevice::Change;
    std::vector<C> two = {{"Filter 1/Cutoff", 0, 90}, {"Vocoder/Carrier Center Frequency", 0, 90}};
    auto vocoder_off = [](const std::string &p, int) { return p == "Filter 1/Cutoff"; };
    auto r = PanelFollow::reports_by_condition(two, vocoder_off);
    CHECK(r.size() == 1 && r[0].path == "Filter 1/Cutoff");
    auto vocoder_on = [](const std::string &p, int) { return p == "Vocoder/Carrier Center Frequency"; };
    r = PanelFollow::reports_by_condition(two, vocoder_on);
    CHECK(r.size() == 1 && r[0].path == "Vocoder/Carrier Center Frequency");
    r = PanelFollow::reports_by_condition(two, [](const std::string &, int) { return false; });
    CHECK(r.size() == 2);
    std::vector<C> one = {{"Filter 1/Cutoff", 0, 90}}; // a single parameter is kept even if its condition is false
    CHECK(PanelFollow::reports_by_condition(one, [](const std::string &, int) { return false; }).size() == 1);
}

// a part selected on the Virus (Multi edit mode) reports Panel Part as 72 pp 1d vv with pp = the part selected BEFORE
// and vv = the new one (E342: 00->01, 01->02, ... 04->03). Panel Part is not per part: the report is kept as a global
// (-1), where the editor reads and sets it - stored under pp the editor never saw it (TI Revive did not follow, E342)
TEST(panel_reports_globals_have_no_part)
{
    using C = RemoteDevice::Change;
    std::vector<C> r = {{"Multi Parameters/Panel Part", 3, 4}, {"Multi Part Parameter/Output Select Control2", 2, 9}};
    auto global = [](const std::string &p) { return p == "Multi Parameters/Panel Part"; };
    PanelFollow::globals_without_part(r, global);
    CHECK(r[0].part == -1 && r[0].value == 4); // the new part, not the old one
    CHECK(r[1].part == 2 && r[1].value == 9);  // per-part parameters keep their part
}

