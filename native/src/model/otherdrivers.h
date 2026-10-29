// otherdrivers.h - other drivers for the Virus TI that take its USB interfaces, so TI Revive finds no Virus: AURA
// Plugins' USB MIDI driver (AURA Virus HE, issue #17, E366) and the old Access drivers of a full Access install (E134).
// macOS installs them as plug-ins in /Library/Audio/MIDI Drivers and /Library/Audio/Plug-Ins/HAL; whichever claims
// the Virus first after it appears on USB wins. The "No Virus TI can be found" page names them (our own addition).
// Header only: the plug-in and the tests use it (the folders are parameters, so the tests use their own).
#pragma once
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

namespace vti {

// the makers of the other Virus drivers found in these folders, each once, AURA first
inline std::vector<std::string> other_virus_drivers(const std::string &midi_drivers, const std::string &hal)
{
    namespace fs = std::filesystem;
    bool aura = false, access = false;
    auto scan = [&](const std::string &dir) {
        std::error_code ec;
        if (dir.empty() || !fs::is_directory(dir, ec))
            return;
        for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
            std::string n = it->path().filename().string();
            std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            if (n.find("aura") != std::string::npos && n.find("virus") != std::string::npos)
                aura = true;
            else if (n.rfind("virus ti midi driver", 0) == 0 || n.rfind("de.access-music.virus_ti", 0) == 0)
                access = true;
        }
    };
    scan(midi_drivers);
    scan(hal);
    std::vector<std::string> out;
    if (aura)
        out.push_back("AURA Plugins");
    if (access)
        out.push_back("Access");
    return out;
}

// two lines for the page (each at most 58 characters, its text width); none without a driver found
inline std::vector<std::string> other_driver_hint(const std::vector<std::string> &found)
{
    if (found.empty())
        return {};
    std::string who = found[0];
    for (size_t i = 1; i < found.size(); i++)
        who += ", " + found[i];
    return {"Another Virus driver is installed (" + who + "):", "only one can use the Virus. See the README."};
}

} // namespace vti
