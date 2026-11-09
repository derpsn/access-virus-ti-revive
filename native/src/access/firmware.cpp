// firmware.cpp - see firmware.h.
#include "firmware.h"
#include "model/utf8.h"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>

namespace vti {

namespace fs = std::filesystem;

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

// chunks of an IFF FORM: (tag, data, length)
struct Chunk {
    std::string tag;
    const uint8_t *data;
    size_t len;
};
static std::vector<Chunk> chunks(const uint8_t *d, size_t n)
{
    std::vector<Chunk> out;
    if (n < 8 || memcmp(d, "FORM", 4))
        return out;
    size_t end = std::min(n, (size_t)be32(d + 4) + 8);
    for (size_t p = 8; p + 8 <= end;) {
        size_t l = be32(d + p + 4);
        if (p + 8 + l > end)
            break;
        out.push_back({std::string((const char *)d + p, 4), d + p + 8, l});
        p += 8 + l;
    }
    return out;
}

std::vector<std::vector<std::vector<uint8_t>>> load_rom_banks(const std::string &path, VirusModel model,
                                                               std::string &error)
{
    std::vector<std::vector<std::vector<uint8_t>>> banks;
    std::ifstream f(u8path(path), std::ios::binary);
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    auto top = chunks(d.data(), d.size());
    if (top.empty() || top[0].tag != "TABL") {
        error = path + ": not a Virus firmware package";
        return banks;
    }
    // TABL: the names of the following chunks, NUL separated (the first name has a 1-byte prefix)
    std::vector<std::string> names;
    std::string cur;
    for (size_t i = 0; i < top[0].len; i++) {
        char ch = (char)top[0].data[i];
        if (ch) {
            cur += ch;
        } else if (!cur.empty()) {
            names.push_back(cur);
            cur.clear();
        }
    }
    const char *want = model == VirusModel::TI2 ? "vti_2.bin" : model == VirusModel::Snow ? "vti_snow.bin" : "vti.bin";
    const Chunk *img = nullptr;
    for (size_t i = 0; i < names.size() && i + 1 < top.size(); i++)
        if (names[i].size() >= strlen(want) && names[i].compare(names[i].size() - strlen(want), strlen(want), want) == 0)
            img = &top[i + 1];
    if (!img) {
        error = std::string("firmware package without ") + want;
        return banks;
    }
    std::map<std::string, std::vector<uint8_t>> sectors;
    for (auto &c : chunks(img->data, img->len)) {
        std::vector<uint8_t> s;
        if (c.len == 0x8c02 || c.len == 0x8c00) {
            s.reserve(0x8000);
            for (size_t r = 0; r < 512 && 70 * r + 70 <= c.len; r++) {
                s.insert(s.end(), c.data + 70 * r + 3, c.data + 70 * r + 35);
                s.insert(s.end(), c.data + 70 * r + 38, c.data + 70 * r + 70);
            }
        } else if (c.len == 0x8000) {
            s.assign(c.data, c.data + c.len);
        }
        if (s.size() == 0x8000)
            sectors[c.tag] = std::move(s);
    }
    for (int b = 0; b < 26; b++) {
        char t0[8], t1[8];
        snprintf(t0, sizeof t0, "P%03d", 64 + 2 * b);
        snprintf(t1, sizeof t1, "P%03d", 65 + 2 * b);
        if (!sectors.count(t0) || !sectors.count(t1))
            break;
        std::vector<std::vector<uint8_t>> bank;
        for (int p = 0; p < 128; p++) {
            const uint8_t *src = sectors[p < 64 ? t0 : t1].data() + (p % 64) * 512;
            std::vector<uint8_t> m = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x10, (uint8_t)(5 + b), (uint8_t)p};
            m.insert(m.end(), src, src + 256);    // page A
            m.push_back(0);                       // page A checksum
            m.insert(m.end(), src + 256, src + 512); // page B
            m.push_back(0);
            m.push_back(0xF7);
            unsigned a = 0;
            for (int i = 5; i < 265; i++)
                a += m[i];
            m[265] = a & 0x7F;
            for (int i = 265; i < 522; i++)
                a += m[i];
            m[522] = a & 0x7F;
            bank.push_back(std::move(m));
        }
        banks.push_back(std::move(bank));
    }
    if (banks.empty())
        error = std::string(want) + ": no ROM sectors";
    return banks;
}

std::string firmware_path(const std::string &vc)
{
    std::error_code ec;
    fs::path p = u8path(vc);
    for (int up = 0; up < 4 && !p.empty(); up++, p = p.parent_path()) // DLL folder, or the bundle's ancestors
        if (fs::is_regular_file(p / "firmware.bin", ec))
            return u8str(p / "firmware.bin");
    return {};
}

} // namespace vti
