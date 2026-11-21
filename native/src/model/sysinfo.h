// sysinfo.h - the texts of the PATCH UTILITY page's INFO box ("Internal System Info/... Version String"). The original
// shows its built-in version, the Access driver versions and the Virus' OS (E189 comparison); TI Revive shows its
// plug-in version, the broker's version in the four driver rows (the broker replaces those drivers) and the Virus' OS
// once read. Unknown values show "-".
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace vti {

struct SystemInfo {
    std::string plugin; // this plug-in's version ("0.8.21")
    std::string broker; // the broker's version, empty if it did not tell (older broker, not connected)
    std::string tios;   // the Virus' OS version, empty until read
};

// the TI Revive website: the INFO box's link opens it (the original's link points to access-music.de)
inline const char *website_url() { return "https://derpsn.github.io/ti-revive/"; }

// the text for one of the INFO box's parameters (versions, the website link), or "" for any other parameter
std::string system_info_text(const std::string &param, const SystemInfo &info);

// the Virus' OS name ("5.1.7.00") from its identification blocks (Broker::flash_info, flashinfo.h order); "" when the
// record holds no printable, terminated name (the original's "no TIOS")
std::string tios_from_flash(const std::vector<uint8_t> &blocks);

} // namespace vti
