// panelfollow.cpp - see panelfollow.h
#include "model/panelfollow.h"

namespace vti {

bool PanelFollow::is_preset_path(const std::string &path)
{
    return path == "Multi Part Parameter/Bank" || path == "Multi Part Parameter/Patch";
}

bool PanelFollow::preset_report(const std::string &path, int part, Clock::time_point now)
{
    if (!is_preset_path(path) || part < 0 || part > 15)
        return false;
    std::lock_guard<std::mutex> lk(mu_);
    parts_ |= 1u << part;
    due_ = now + PRESET_QUIET; // one timer for all parts, from the last report (the Virus reports Bank, then Patch)
    return true;
}

unsigned PanelFollow::presets_due(Clock::time_point now)
{
    std::lock_guard<std::mutex> lk(mu_);
    if (!parts_ || now < due_)
        return 0;
    unsigned p = parts_;
    parts_ = 0;
    return p;
}

int PanelFollow::softknob_report(const std::string &path)
{
    static const std::string prefix = "Global Soft Knob Configuration/Value Softknob";
    if (path.size() != prefix.size() + 1 || path.compare(0, prefix.size(), prefix) != 0)
        return 0;
    const char n = path.back();
    return n >= '0' && n <= '2' ? n - '0' + 1 : 0;
}

const std::vector<std::string> &PanelFollow::restored_controllers()
{
    static const std::vector<std::string> c = {
        "Internal MIDI Standard Parameters/01 Modulation Wheel",
        "Internal MIDI Standard Parameters/02 Breath Controller",
        "Internal MIDI Standard Parameters/03 Controller 03",
        "Internal MIDI Standard Parameters/04 Foot Controller",
        "Internal MIDI Standard Parameters/06 Data Slider",
        "Internal MIDI Standard Parameters/08 Balance",
        "Internal MIDI Standard Parameters/09 Controller 09",
        "Internal MIDI Standard Parameters/11 Expression",
        "Internal MIDI Standard Parameters/12 Controller 12",
        "Internal MIDI Standard Parameters/13 Controller 13",
        "Internal MIDI Standard Parameters/14 Controller 14",
        "Internal MIDI Standard Parameters/15 Controller 15",
        "Internal MIDI Standard Parameters/16 Controller 16",
    };
    return c;
}

bool PanelFollow::host_controller_kept(const std::string &path)
{
    static const std::string prefix = "Internal MIDI Standard Parameters/";
    return path.size() > prefix.size() && path.compare(0, prefix.size(), prefix) == 0;
}

} // namespace vti
