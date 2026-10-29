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
    chosen_bank_[part] = -1; // the Virus' own choice wins over one made in the plug-in before
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

std::vector<std::vector<uint8_t>> PanelFollow::program_select(int part, int bank, int program)
{
    if (part < 0 || part > 15 || bank < 0 || bank > 127 || program < 0 || program > 127)
        return {};
    const uint8_t ch = (uint8_t)part;
    return {{(uint8_t)(0xB0 | ch), 0x00, 0x00}, {(uint8_t)(0xB0 | ch), 0x20, (uint8_t)bank}, {(uint8_t)(0xC0 | ch), (uint8_t)program}};
}

bool PanelFollow::preset_chosen(int part, int bank, int patch, Clock::time_point now)
{
    if (part < 0 || part > 15)
        return false;
    std::lock_guard<std::mutex> lk(mu_);
    chosen_bank_[part] = bank;
    chosen_patch_[part] = patch;
    parts_ |= 1u << part;
    due_ = now + PRESET_QUIET;
    return true;
}

bool PanelFollow::chosen(int part, int &bank, int &patch)
{
    if (part < 0 || part > 15)
        return false;
    std::lock_guard<std::mutex> lk(mu_);
    if (chosen_bank_[part] < 0)
        return false;
    bank = chosen_bank_[part], patch = chosen_patch_[part];
    return true;
}

void PanelFollow::forget_chosen(int part)
{
    if (part < 0 || part > 15)
        return;
    std::lock_guard<std::mutex> lk(mu_);
    chosen_bank_[part] = -1;
}

} // namespace vti
