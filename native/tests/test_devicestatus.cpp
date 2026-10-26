// The editor's device status values (the original's status routine, 0x180071190, and the layout's SystemStatus/Error
// pages): which page the editor shows for a missing Virus, a Virus held by another program, and a connected Virus
#include "check.h"
#include "model/devicestatus.h"
#include "model/otherdrivers.h"
#include <filesystem>
#include <chrono>
#include <fstream>

using namespace vti;

TEST(devicestatus_missing_virus)
{
    // no Virus: the "No Virus TI can be found" page (SystemStatus/Error 3) and all three communication flags set
    DeviceStatus s = device_status(false, false);
    CHECK(s.system_error == 3);
    CHECK(s.usb_comm == 1 && s.midi_comm == 1 && s.audio_comm == 1);
}

TEST(devicestatus_connected_and_elsewhere)
{
    DeviceStatus ok = device_status(true, false);
    CHECK(ok.system_error == 0 && ok.usb_comm == 0 && ok.midi_comm == 0 && ok.audio_comm == 0);
    // another program's plug-in has the Virus: "Virus unavailable" (VDM not Master, 2), even while it is absent here
    CHECK(device_status(true, true).system_error == 2);
    CHECK(device_status(false, true).system_error == 2);
    CHECK(device_status(false, true).usb_comm == 0); // not a communication error: the other program talks to it
}

TEST(devicestatus_values_by_path)
{
    // the editor reads them by parameter path
    DeviceStatus s = device_status(false, false);
    CHECK(device_status_value(s, "SystemStatus/Error") == 3);
    CHECK(device_status_value(s, "Internal System Info/USB Communication") == 1);
    CHECK(device_status_value(s, "Internal System Info/MIDI Communication") == 1);
    CHECK(device_status_value(s, "Internal System Info/Audio Communication") == 1);
    CHECK(device_status_value(s, "Filter 1/Cutoff") == -1); // not a status value
}

TEST(devicestatus_change_for_a_waiting_instance)
{
    // macOS user test M3 (0.8.19): a duplicated track's instance waits for the first one without a broker connection
    // (presence unknown), and its editor showed the normal page instead of "Virus unavailable"
    StatusChange c;
    CHECK(!c.update(-1, false)); // not known yet: nothing to show
    CHECK(c.update(-1, true));   // another instance or program has the Virus: "Virus unavailable" right away
    CHECK(!c.update(-1, true));  // reported once
    CHECK(!c.update(1, true));   // presence does not matter while another one has it
    CHECK(c.update(1, false));   // taken over: the normal page
    CHECK(c.update(0, false));   // switched off: "No Virus TI can be found"
    CHECK(!c.update(0, false));
}

// E134, E366: another driver for the Virus (AURA's USB MIDI driver, the old Access drivers) takes the interfaces and
// TI Revive finds no Virus. The "No Virus TI can be found" page names it (macOS folders, given here)
TEST(other_virus_drivers_found_by_name)
{
    namespace fs = std::filesystem;
    const fs::path t = fs::temp_directory_path() / ("vti-drivers-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::remove_all(t);
    fs::create_directories(t / "midi" / "AURA Plugins Virus TI USB Midi.plugin");
    fs::create_directories(t / "midi" / "Some Other Synth.plugin");
    fs::create_directories(t / "hal");
    std::vector<std::string> found = other_virus_drivers((t / "midi").string(), (t / "hal").string());
    CHECK(found.size() == 1 && found[0] == "AURA Plugins");
    fs::create_directories(t / "midi" / "Virus TI MIDI Driver.plugin");
    fs::create_directories(t / "hal" / "de.access-music.virus_ti.plugin");
    found = other_virus_drivers((t / "midi").string(), (t / "hal").string());
    CHECK(found.size() == 2 && found[0] == "AURA Plugins" && found[1] == "Access"); // Access once
    CHECK(other_virus_drivers((t / "missing").string(), "").empty());
    // the page's two lines (at most 58 characters each, the width of the page's text)
    auto lines = other_driver_hint({"AURA Plugins"});
    CHECK(lines.size() == 2 && lines[0].find("AURA Plugins") != std::string::npos);
    CHECK(lines[0].size() <= 58 && lines[1].size() <= 58);
    CHECK(other_driver_hint({}).empty());
    fs::remove_all(t);
}
