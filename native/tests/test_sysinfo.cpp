// The PATCH UTILITY page's INFO box ("Internal System Info/... Version String"): the original shows its own version
// (built in, FUN_180078060), the Access driver versions and the Virus' OS read from its flash (FUN_1800ac7c0). TI Revive
// shows its plug-in version, the broker's version in the four driver rows (the broker replaces the Access drivers) and
// the OS version once it is known; "-" while something is unknown.
#include "check.h"
#include "model/sysinfo.h"
#include <cstring>
#include <vector>
using namespace vti;

TEST(sysinfo_version_texts)
{
    SystemInfo s;
    s.plugin = "0.8.21";
    s.broker = "0.8.21";
    s.tios = "5.1.7.00";
    CHECK(system_info_text("Internal System Info/Virus TI Control Version String", s) == "TI Revive 0.8.21");
    for (const char *p : {"Internal System Info/Audio Driver Version String", "Internal System Info/Audio Library Version String",
                          "Internal System Info/Audio Host Interface Version String",
                          "Internal System Info/MIDI Driver Version String"})
        CHECK(system_info_text(p, s) == "TI Revive 0.8.21");
    CHECK(system_info_text("Internal System Info/TIOS Version String", s) == "5.1.7.00");
    CHECK(system_info_text("Osc/Shape", s).empty()); // not ours: the caller falls back to the value text
}

// the OS name in the identification blocks (flashinfo.h order: FF7FA0..FF7FF0, then 800000..800050): the original
// reads a 0x50-byte record from 9 bytes into the 0x800000 area and takes the name at its offset 0x25 up to a zero byte
// (FUN_1800ac7c0), i.e. USB address 0x80002E - E026 found "1.7.00" at the start of block 0x800030
TEST(sysinfo_tios_from_flash)
{
    std::vector<uint8_t> f(12 * 16, 0xFF);
    const char *os = "5.1.7.00";
    memcpy(&f[96 + 0x2E], os, strlen(os) + 1);
    CHECK(tios_from_flash(f) == "5.1.7.00");
    f[96 + 0x2E + 8] = 'x'; // no end within the record: like the original's "no TIOS"
    for (size_t i = 96 + 0x2E + 8; i < f.size(); i++)
        f[i] = 'x';
    CHECK(tios_from_flash(f).empty());
    CHECK(tios_from_flash(std::vector<uint8_t>(40, 0)).empty()); // too short
    std::vector<uint8_t> junk(12 * 16, 0);
    junk[96 + 0x2E] = 0x01; // not printable
    CHECK(tios_from_flash(junk).empty());
}

// the INFO box's link (URLButton "VirusControl/Access Website"): the original points to access-music.de; ours to the
// TI Revive website, shown without the scheme so it fits the 260 px field (43 characters at 6 px)
TEST(sysinfo_website_link)
{
    SystemInfo s;
    CHECK(system_info_text("VirusControl/Access Website", s) == "derpsn.github.io/ti-revive");
    CHECK(system_info_text("VirusControl/Access Website", s).size() * 6 <= 260);
    CHECK(std::string(website_url()) == "https://derpsn.github.io/ti-revive/");
}

TEST(sysinfo_unknown_shows_dash)
{
    SystemInfo s;
    s.plugin = "0.8.21"; // a broker without the version message, no OS read yet
    CHECK(system_info_text("Internal System Info/Audio Driver Version String", s) == "-");
    CHECK(system_info_text("Internal System Info/TIOS Version String", s) == "-");
    CHECK(system_info_text("Internal System Info/Virus TI Control Version String", s) == "TI Revive 0.8.21");
}
