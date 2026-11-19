#include "model/sysinfo.h"

namespace vti {

static std::string or_dash(const std::string &s, const char *prefix = "")
{
    return s.empty() ? "-" : prefix + s;
}

std::string system_info_text(const std::string &param, const SystemInfo &info)
{
    if (param == "VirusControl/Access Website") // without the scheme: fits the field
        return "derpsn.github.io/ti-revive";
    static const std::string base = "Internal System Info/";
    if (param.compare(0, base.size(), base) != 0)
        return {};
    const std::string name = param.substr(base.size());
    if (name == "Virus TI Control Version String")
        return or_dash(info.plugin, "TI Revive ");
    if (name == "Audio Driver Version String" || name == "Audio Library Version String" ||
        name == "Audio Host Interface Version String" || name == "MIDI Driver Version String")
        return or_dash(info.broker, "TI Revive ");
    if (name == "TIOS Version String")
        return or_dash(info.tios);
    return {};
}

// the original (FUN_1800ac7c0) reads a 0x50-byte record from 9 bytes into the 0x800000 area and takes the name at its
// offset 0x25 up to a zero byte inside the record: USB address 0x80002E, block 6 of ours starts at 0x800000
std::string tios_from_flash(const std::vector<uint8_t> &blocks)
{
    const size_t base = 6 * 16, first = base + 0x09 + 0x25, end = base + 0x09 + 0x50;
    if (blocks.size() < end)
        return {};
    std::string name;
    for (size_t i = first; i < end; i++) {
        uint8_t c = blocks[i];
        if (c == 0)
            return name;
        if (c < 32 || c > 126)
            return {};
        name += (char)c;
    }
    return {}; // no end inside the record
}

} // namespace vti
