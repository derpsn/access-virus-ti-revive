// state.cpp - the plug-in's project state records and the meta parameter value mapping (see state.h)
#include "model/state.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace vti {

// ---- state container ("MIDI" + u32 BE length, records of u32 BE length + message) ----------------------------------

std::vector<std::vector<uint8_t>> parse_container(const std::vector<uint8_t> &d)
{
    std::vector<std::vector<uint8_t>> out;
    if (d.size() < 8 || memcmp(d.data(), "MIDI", 4))
        return out;
    size_t end = std::min(d.size(), 8 + ((size_t)d[4] << 24 | d[5] << 16 | d[6] << 8 | d[7]));
    for (size_t pos = 8; pos + 4 <= end;) {
        size_t n = (size_t)d[pos] << 24 | d[pos + 1] << 16 | d[pos + 2] << 8 | d[pos + 3];
        pos += 4;
        if (pos + n > end)
            break;
        out.emplace_back(d.begin() + pos, d.begin() + pos + n);
        pos += n;
    }
    return out;
}

std::vector<uint8_t> build_container(const std::vector<std::vector<uint8_t>> &recs)
{
    std::vector<uint8_t> d = {'M', 'I', 'D', 'I', 0, 0, 0, 0};
    auto be32 = [&](size_t v) {
        for (int s = 24; s >= 0; s -= 8)
            d.push_back((uint8_t)(v >> s));
    };
    for (auto &r : recs) {
        be32(r.size());
        d.insert(d.end(), r.begin(), r.end());
    }
    size_t n = d.size() - 8;
    for (int i = 0; i < 4; i++)
        d[4 + i] = (uint8_t)(n >> (24 - 8 * i));
    return d;
}

bool is_system_clock_global(const std::vector<uint8_t> &m)
{
    static const uint8_t head[7] = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x73};
    return m.size() == 11 && !memcmp(m.data(), head, sizeof head) && m[8] >= 0x0F && m[8] <= 0x11 && m[10] == 0xF7;
}

int drop_system_clock_globals(std::vector<std::vector<uint8_t>> &recs)
{
    const size_t before = recs.size();
    recs.erase(std::remove_if(recs.begin(), recs.end(), is_system_clock_global), recs.end());
    return (int)(before - recs.size());
}

int upgrade_state_dumps(std::vector<std::vector<uint8_t>> &recs)
{
    int n = 0;
    for (auto &r : recs)
        if (r.size() == 524 && r[0] == 0xF0 && r[6] == 0x10 && r[7] == 0x00 && (int8_t)r[9] < 12 &&
            RemoteDevice::upgrade_single(r))
            n++;
    return n;
}

bool is_part_dump(const std::vector<uint8_t> &m, int part)
{
    return m.size() == 524 && m[0] == 0xF0 && m[6] == 0x10 && m[7] == 0x00 && m[8] == part;
}

// editor-only per-part settings (LFO link buttons, ...) have no Virus message: they are kept in the state as
// F0 7D 'V' 'T' 'N' 01 <part> <path ASCII> 00 <value> F7 (7D = non-commercial ID: the original and the Virus ignore it)
std::vector<uint8_t> editor_record(const std::string &path, int part, int value)
{
    std::vector<uint8_t> m = {0xF0, 0x7D, 'V', 'T', 'N', 0x01, (uint8_t)(part & 0x7F)};
    for (char ch : path)
        m.push_back((uint8_t)ch & 0x7F);
    m.push_back(0);
    m.push_back((uint8_t)(value & 0x7F));
    m.push_back(0xF7);
    return m;
}

bool parse_editor_record(const std::vector<uint8_t> &m, std::string &path, int &part, int &value)
{
    if (m.size() < 11 || m[0] != 0xF0 || m[1] != 0x7D || m[2] != 'V' || m[3] != 'T' || m[4] != 'N' || m[5] != 0x01)
        return false;
    part = m[6];
    size_t i = 7;
    path.clear();
    while (i < m.size() && m[i] != 0)
        path += (char)m[i++];
    if (i + 2 >= m.size())
        return false;
    value = m[i + 1];
    return true;
}

int meta_to_client(const MetaClient &c, int mv)
{
    for (auto &m : c.maps)
        if (m.second == mv)
            return m.first;
    return (int)std::lround(c.a * mv + c.b);
}

bool client_to_meta(const MetaClient &c, int cv, int &mv)
{
    for (auto &m : c.maps)
        if (m.first == cv)
            return mv = m.second, true;
    if (!c.maps.empty() || c.a == 0)
        return false;
    mv = (int)std::lround((cv - c.b) / c.a);
    return true;
}

} // namespace vti
