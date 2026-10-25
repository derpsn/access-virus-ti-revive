// accessfiles.cpp - see accessfiles.h. The PE resource reader handles the standard three-level resource tree
// (type / name / language) of a PE32+ or PE32 image; only named types "XML" and "PNG" are collected.
#include "accessfiles.h"
#include <cstdio>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace vti {

static std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

static bool read_file(const fs::path &p, std::vector<uint8_t> &out)
{
    std::ifstream f(p, std::ios::binary);
    if (!f)
        return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

template <class T> static bool rd(const std::vector<uint8_t> &d, size_t off, T &v)
{
    if (off + sizeof(T) > d.size())
        return false;
    std::memcpy(&v, d.data() + off, sizeof(T));
    return true;
}

bool AccessFiles::load_pe(const std::string &dll, std::string &error, const char *required)
{
    std::vector<uint8_t> d;
    if (!read_file(dll, d)) {
        error = "cannot read " + dll;
        return false;
    }
    uint32_t pe = 0;
    uint16_t nsec = 0, optsz = 0, magic = 0;
    if (d.size() < 0x40 || d[0] != 'M' || d[1] != 'Z' || !rd(d, 0x3c, pe) || !rd(d, pe + 6, nsec) ||
        !rd(d, pe + 20, optsz) || !rd(d, pe + 24, magic)) {
        error = dll + " is not a PE image";
        return false;
    }
    pe32_ = magic == 0x10b;
    size_t dd = pe + 24 + (magic == 0x20b ? 112 : 96); // data directories
    uint32_t rsrc_rva = 0;
    rd(d, dd + 2 * 8, rsrc_rva);
    struct Sec {
        uint32_t va, vs, ra, rs;
    };
    std::vector<Sec> secs;
    for (uint16_t i = 0; i < nsec; i++) {
        size_t o = pe + 24 + optsz + 40u * i;
        Sec s{};
        rd(d, o + 12, s.va), rd(d, o + 8, s.vs), rd(d, o + 20, s.ra), rd(d, o + 16, s.rs);
        secs.push_back(s);
    }
    auto r2o = [&](uint32_t rva) -> size_t {
        for (auto &s : secs)
            if (rva >= s.va && rva < s.va + std::max(s.vs, s.rs))
                return rva - s.va + s.ra;
        return 0;
    };
    size_t base = r2o(rsrc_rva);
    if (!rsrc_rva || !base) {
        error = dll + " has no resources";
        return false;
    }
    auto dir_name = [&](uint32_t nm) -> std::string {
        if (!(nm & 0x80000000u))
            return std::to_string(nm);
        size_t so = base + (nm & 0x7fffffffu);
        uint16_t len = 0;
        rd(d, so, len);
        std::string s;
        for (uint16_t k = 0; k < len; k++) {
            uint16_t c = 0;
            rd(d, so + 2 + 2u * k, c);
            s += c < 128 ? (char)c : '?';
        }
        return s;
    };
    auto entries = [&](size_t dir) {
        std::vector<std::pair<uint32_t, uint32_t>> v;
        uint16_t named = 0, ids = 0;
        rd(d, dir + 12, named), rd(d, dir + 14, ids);
        for (int i = 0; i < named + ids; i++) {
            uint32_t nm = 0, ent = 0;
            rd(d, dir + 16 + 8u * i, nm), rd(d, dir + 20 + 8u * i, ent);
            v.push_back({nm, ent});
        }
        return v;
    };
    for (auto [tname, tent] : entries(base)) {
        std::string type = dir_name(tname);
        if ((type != "XML" && type != "PNG") || !(tent & 0x80000000u))
            continue;
        for (auto [nname, nent] : entries(base + (tent & 0x7fffffffu))) {
            if (!(nent & 0x80000000u))
                continue;
            auto langs = entries(base + (nent & 0x7fffffffu));
            if (langs.empty() || (langs[0].second & 0x80000000u))
                continue;
            uint32_t drva = 0, dsz = 0;
            rd(d, base + langs[0].second, drva), rd(d, base + langs[0].second + 4, dsz);
            size_t off = r2o(drva);
            if (!off || off + dsz > d.size())
                continue;
            std::string name = lower(dir_name(nname));
            files_[name].assign(d.begin() + (long)off, d.begin() + (long)(off + dsz));
        }
    }
    if (!files_.count(required)) {
        error = dll + " does not contain the Virus TI resources";
        return false;
    }
    source_ = dll;
    return true;
}

bool AccessFiles::load_dir(const std::string &dir, std::string &error, const char *required)
{
    std::error_code ec;
    for (auto &e : fs::directory_iterator(dir, ec)) {
        std::string ext = lower(e.path().extension().string());
        if (ext != ".xml" && ext != ".png")
            continue;
        read_file(e.path(), files_[lower(e.path().filename().string())]);
    }
    if (!files_.count(required)) {
        error = dir + " does not contain the Virus TI resources";
        return false;
    }
    source_ = dir;
    return true;
}

std::vector<uint8_t> AccessFiles::find_dump(uint8_t cmd, size_t size, size_t name_at, const char *name,
                                            std::vector<uint8_t> *binary, size_t *offset) const
{
    std::vector<std::string> bins = {source_};                                       // Windows: the DLL itself
    bins.push_back((fs::path(source_) / ".." / "MacOS" / "Virus Control").string()); // Mac: bundle binary
    const uint8_t hdr[] = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, cmd};
    size_t nl = strlen(name);
    for (auto &b : bins) {
        std::error_code ec;
        if (!fs::is_regular_file(b, ec))
            continue;
        std::vector<uint8_t> d;
        read_file(b, d);
        for (size_t i = 0; i + size <= d.size(); i++)
            if (d[i] == 0xF0 && !memcmp(&d[i], hdr, sizeof hdr) && d[i + size - 1] == 0xF7 &&
                !memcmp(&d[i + name_at], name, nl)) {
                if (offset)
                    *offset = i;
                std::vector<uint8_t> out(d.begin() + i, d.begin() + i + size);
                if (binary)
                    *binary = std::move(d);
                return out;
            }
    }
    return {};
}

std::vector<float> find_osc_waves(const std::vector<uint8_t> &bin)
{
    const size_t n = size_t{64} * 64, bytes = n * 4;
    auto at = [&](size_t off) {
        float f;
        memcpy(&f, &bin[off], 4);
        return f;
    };
    // finite from the bits: the Mac and Windows builds use fast-math (VST3 SDK), where std::isfinite may be folded
    // to true and a NaN row passed (reproduced with g++ -ffast-math, 2026-10-20)
    auto finite_at = [&](size_t off) {
        uint32_t b;
        memcpy(&b, &bin[off], 4);
        return (b >> 23 & 0xFF) != 0xFF;
    };
    const double two_pi = 2 * 3.14159265358979323846;
    for (size_t i = 0; i + bytes <= bin.size(); i++) {
        // row 0 starts 0, sin(2 pi / 64), ...: cheap pre-check on the first two values
        if (bin[i] || bin[i + 1] || bin[i + 2] || bin[i + 3] || std::fabs(at(i + 4) - 0.0980171f) > 1e-4f)
            continue;
        bool ok = true;
        for (size_t k = 0; k < 64 && ok; k++) // the shipped sine is rounded (~1e-4)
            ok = finite_at(i + 4 * k) && std::fabs(at(i + 4 * k) - std::sin(two_pi * (double)k / 64)) < 1e-3;
        for (size_t k = 64; k < n && ok; k++)
            ok = finite_at(i + 4 * k) && std::fabs(at(i + 4 * k)) <= 1.0001f;
        if (!ok)
            continue;
        std::vector<float> t(n);
        memcpy(t.data(), &bin[i], bytes);
        return t;
    }
    return {};
}

namespace {
uint32_t le32(const std::vector<uint8_t> &b, size_t at)
{
    return at + 4 <= b.size() ? (uint32_t)b[at] | b[at + 1] << 8 | b[at + 2] << 16 | (uint32_t)b[at + 3] << 24 : 0;
}
uint64_t le64(const std::vector<uint8_t> &b, size_t at)
{
    return (uint64_t)le32(b, at) | (uint64_t)le32(b, at + 4) << 32;
}
uint32_t be32(const std::vector<uint8_t> &b, size_t at)
{
    return at + 4 <= b.size() ? (uint32_t)b[at] << 24 | b[at + 1] << 16 | b[at + 2] << 8 | b[at + 3] : 0;
}

// an address in the binary -> its file offset (SIZE_MAX if outside every mapped range)
struct AddressRange {
    uint64_t addr, size;
    size_t file;
};
using AddressMap = std::vector<AddressRange>;

size_t to_file(const AddressMap &m, uint64_t a)
{
    for (auto &r : m)
        if (a >= r.addr && a - r.addr < r.size)
            return r.file + (size_t)(a - r.addr);
    return SIZE_MAX;
}

// a 64-bit PE (PE32+; the 32-bit DLL is not supported, E345): absolute addresses = image base + RVA
bool pe_map(const std::vector<uint8_t> &b, AddressMap &m)
{
    if (b.size() < 0x40 || b[0] != 'M' || b[1] != 'Z')
        return false;
    size_t pe = le32(b, 0x3C);
    if (pe + 24 > b.size() || memcmp(&b[pe], "PE\0\0", 4) != 0)
        return false;
    size_t nsec = b[pe + 6] | b[pe + 7] << 8, optsz = b[pe + 20] | b[pe + 21] << 8, opt = pe + 24;
    const int magic = opt + 32 <= b.size() ? b[opt] | b[opt + 1] << 8 : 0;
    if (magic != 0x20B) // PE32+
        return false;
    uint64_t base = le64(b, opt + 24);
    for (size_t i = 0, sec = opt + optsz; i < nsec && sec + 40 <= b.size(); i++, sec += 40)
        m.push_back({base + le32(b, sec + 12), le32(b, sec + 16), le32(b, sec + 20)});
    return true;
}

// a thin 64-bit Mach-O at `at` (a slice of a universal binary or the whole file): its LC_SEGMENT_64 commands
bool macho64_map(const std::vector<uint8_t> &b, size_t at, AddressMap &m)
{
    if (le32(b, at) != 0xFEEDFACF)
        return false;
    size_t ncmds = le32(b, at + 16), cmd = at + 32;
    for (size_t i = 0; i < ncmds && cmd + 8 <= b.size(); i++) {
        uint32_t kind = le32(b, cmd), size = le32(b, cmd + 4);
        if (kind == 0x19 && cmd + 72 <= b.size()) // LC_SEGMENT_64: vmaddr, vmsize, fileoff, filesize
            m.push_back({le64(b, cmd + 24), std::min(le64(b, cmd + 32), le64(b, cmd + 48)),
                         at + (size_t)le64(b, cmd + 40)});
        if (size < 8)
            break;
        cmd += size;
    }
    return true;
}

std::vector<AddressMap> address_maps(const std::vector<uint8_t> &b)
{
    std::vector<AddressMap> maps;
    AddressMap m;
    if (pe_map(b, m) || macho64_map(b, 0, m))
        maps.push_back(m);
    else if (be32(b, 0) == 0xCAFEBABE) // universal: each 64-bit slice
        for (size_t i = 0, n = be32(b, 4); i < n && i < 16; i++) {
            AddressMap s;
            if (macho64_map(b, be32(b, 8 + 20 * i + 8), s))
                maps.push_back(s);
        }
    return maps;
}

std::string c_string(const std::vector<uint8_t> &b, size_t at, size_t max)
{
    std::string s;
    for (; at < b.size() && b[at] && s.size() <= max; at++)
        s += (char)b[at];
    return at < b.size() && !b[at] ? s : std::string();
}

bool menu_name(const std::string &s)
{
    if (s.size() < 3 || s.size() > 100)
        return false;
    bool letter = false;
    for (char c : s) {
        if (c < 32 || c > 126)
            return false;
        letter |= isalpha((unsigned char)c) != 0;
    }
    return letter;
}

// "B0 05 FF": 1..20 two-digit upper-case hex bytes, single spaces between them
bool midi_bytes_text(const std::string &s)
{
    if (s.size() < 2 || s.size() > 59 || s.size() % 3 != 2)
        return false;
    for (size_t i = 0; i < s.size(); i++)
        if (i % 3 == 2 ? s[i] != ' ' : !isxdigit((unsigned char)s[i]) || islower((unsigned char)s[i]))
            return false;
    return true;
}
} // namespace

std::vector<KnobTemplate> find_knob_templates(const std::vector<uint8_t> &bin)
{
    // the first run of at least 8 entries; in the Windows DLL it ends right before template 0's name record, in the
    // Mac binary it lies elsewhere, so the shape alone decides (E197)
    for (auto &m : address_maps(bin)) {
        const size_t p = 8, pair = 2 * p;
        auto entry = [&](size_t at, KnobTemplate &k) {
            size_t n = to_file(m, le64(bin, at)), d = to_file(m, le64(bin, at + p));
            if (n == SIZE_MAX || d == SIZE_MAX)
                return false;
            k = {c_string(bin, n, 100), c_string(bin, d, 59)};
            return menu_name(k.name) && midi_bytes_text(k.midi);
        };
        for (size_t at = 0; at + pair <= bin.size(); at += p) {
            std::vector<KnobTemplate> run;
            KnobTemplate k;
            for (size_t e = at; e + pair <= bin.size() && entry(e, k); e += pair)
                run.push_back(k);
            if (run.size() >= 8)
                return run;
            if (!run.empty()) // a shorter run: go on after it
                at += pair * run.size() - p;
        }
    }
    return {};
}

std::vector<KnobTemplate> AccessFiles::knob_templates() const
{
    std::vector<std::string> bins = {source_};                                       // Windows: the DLL itself
    bins.push_back((fs::path(source_) / ".." / "MacOS" / "Virus Control").string()); // Mac: bundle binary
    for (auto &b : bins) {
        std::error_code ec;
        if (!fs::is_regular_file(b, ec))
            continue;
        std::vector<uint8_t> d;
        read_file(b, d);
        auto t = find_knob_templates(d);
        if (!t.empty())
            return t;
    }
    return {};
}

std::vector<float> AccessFiles::osc_waves() const
{
    std::vector<std::string> bins = {source_};                                       // Windows: the DLL itself
    bins.push_back((fs::path(source_) / ".." / "MacOS" / "Virus Control").string()); // Mac: bundle binary
    for (auto &b : bins) {
        std::error_code ec;
        if (!fs::is_regular_file(b, ec))
            continue;
        std::vector<uint8_t> d;
        read_file(b, d);
        auto t = find_osc_waves(d);
        if (!t.empty())
            return t;
    }
    return {};
}

std::vector<uint8_t> AccessFiles::init_single() const { return find_dump(0x10, 524, 249, " -Init-"); }

std::vector<uint8_t> AccessFiles::default_page_b() const
{
    std::vector<uint8_t> bin;
    size_t off = 0;
    if (find_dump(0x10, 524, 249, " -Init-", &bin, &off).empty())
        return {};
    // where the original's upgrader copies page B from, relative to the init single: the 64-bit Windows DLL 0x400
    // before it (E345), the Mac binary 0x2a0 after it, in both slices of the universal file (E346, FUN_000e5c00)
    const uint32_t magic = le32(bin, 0);
    size_t at;
    if (bin.size() >= 2 && bin[0] == 'M' && bin[1] == 'Z' && off >= 0x400)
        at = off - 0x400;
    else if (magic == 0xFEEDFACF || magic == 0xBEBAFECA) // thin 64-bit / universal Mach-O
        at = off + 0x2a0;
    else
        return {};
    if (at + 0x100 > bin.size())
        return {};
    std::vector<uint8_t> t(bin.begin() + at, bin.begin() + at + 0x100);
    for (auto b : t)
        if (b > 0x7F)
            return {};
    return t;
}

std::vector<uint8_t> AccessFiles::init_multi() const { return find_dump(0x11, 267, 13, "MultiCtrl!"); }

#if defined(_WIN32)
#define FIX_32BIT "run \"Set up Access Virus TI files\" from the Start menu (TI Revive)"
#else
#define FIX_32BIT "run TI Revive's install.sh again"
#endif

std::unique_ptr<AccessFiles> AccessFiles::open(const std::string &hint, std::string &error)
{
    std::vector<std::string> cands;
    if (!hint.empty())
        cands.push_back(hint);
    if (const char *e = std::getenv("VTI_ACCESS_DIR"))
        cands.push_back(e);
    const char *home = std::getenv("HOME");
    const std::string common = "Access Music/Virus TI/Common";
#if defined(_WIN32)
    for (const char *pf : {"ProgramW6432", "ProgramFiles"})
        if (const char *p = std::getenv(pf))
            cands.push_back(std::string(p) + "/" + common);
#elif defined(__APPLE__)
    cands.push_back("/Library/Application Support/" + common + "/Virus Control.bundle");
#else
    // TI Revive's Linux installer writes the folder it installed the Access software to (any Wine prefix): a DAW
    // started from the desktop does not see WINEPREFIX
    std::string conf = std::getenv("XDG_CONFIG_HOME") ? std::getenv("XDG_CONFIG_HOME") : (home ? std::string(home) + "/.config" : "");
    if (!conf.empty())
        if (FILE *f = std::fopen((conf + "/tirevive/access-dir").c_str(), "r")) {
            char line[1024] = {};
            if (std::fgets(line, sizeof line, f)) {
                std::string d(line);
                while (!d.empty() && (d.back() == '\n' || d.back() == '\r'))
                    d.pop_back();
                if (!d.empty())
                    cands.push_back(d);
            }
            std::fclose(f);
        }
    std::string prefix = std::getenv("WINEPREFIX") ? std::getenv("WINEPREFIX") : (home ? std::string(home) + "/.wine" : "");
    if (!prefix.empty())
        cands.push_back(prefix + "/drive_c/Program Files/" + common);
#endif
    std::string last = "no Access Virus TI installation found", x86;
    for (auto c : cands) {
        fs::path p(c);
        std::error_code ec;
        if (fs::is_directory(p / "Contents" / "Resources", ec))
            p = p / "Contents" / "Resources";
        else if (fs::is_directory(p, ec) && fs::exists(p / "Virus Control.dll", ec))
            p = p / "Virus Control.dll";
        else if (fs::is_directory(p, ec) && fs::exists(p / "Virus Control.bundle", ec))
            p = p / "Virus Control.bundle" / "Contents" / "Resources";
        std::unique_ptr<AccessFiles> a(new AccessFiles);
        bool ok = fs::is_directory(p, ec) ? a->load_dir(p.string(), last) : a->load_pe(p.string(), last);
        if (ok && a->pe32_) { // only the 64-bit DLL: the 32-bit one's tables lie elsewhere (E345); keep the reason
            if (x86.empty())
                x86 = p.string() + " is the 32-bit Virus Control.dll; TI Revive needs the 64-bit one from the "
                                   "\"Virus TI Software Suite 5.1.7.00 64-bit.msi\": " FIX_32BIT;
            continue;
        }
        if (ok)
            return a;
    }
    error = x86.empty() ? last : x86;
    return nullptr;
}

std::unique_ptr<AccessFiles> AccessFiles::open_control_center(std::string &error)
{
    const char *exe = "Virus Control Center.exe";
    std::vector<fs::path> cands;
    if (const char *e = std::getenv("VTI_ACCESS_CC"))
        cands.push_back(e);
    std::string err;
    if (auto plugin = open("", err)) { // the Control Center sits next to the plug-in in the installer's Common folder
        fs::path src(plugin->source());
        cands.push_back(src.parent_path() / exe);
    }
#if defined(_WIN32)
    for (const char *pf : {"ProgramW6432", "ProgramFiles"})
        if (const char *p = std::getenv(pf))
            cands.push_back(fs::path(p) / "Access Music/Virus TI/Common" / exe);
#elif defined(__APPLE__)
    // TI Revive's "Set up Access Virus TI files" copies its images and layout there; else an Access installation
    cands.push_back("/Library/Application Support/Access Music/Virus TI/Common/Virus Control Center Resources");
    cands.push_back("/Applications/Access Music/Virus TI/Virus Control Center.app/Contents/Resources");
#else
    const char *home = std::getenv("HOME");
    std::string prefix = std::getenv("WINEPREFIX") ? std::getenv("WINEPREFIX") : (home ? std::string(home) + "/.wine" : "");
    if (!prefix.empty())
        cands.push_back(fs::path(prefix) / "drive_c/Program Files/Access Music/Virus TI/Common" / exe);
#endif
    std::string last = "Access' Virus Control Center not found (it comes with the Virus TI Software Suite installer)";
    for (auto &p : cands) {
        std::error_code ec;
        if (!fs::exists(p, ec))
            continue;
        std::unique_ptr<AccessFiles> a(new AccessFiles);
        bool ok = fs::is_directory(p, ec) ? a->load_dir(p.string(), last, "engine.xml")
                                          : a->load_pe(p.string(), last, "engine.xml");
        if (ok && a->files_.count("editor.xml"))
            return a;
    }
    error = last;
    return nullptr;
}

DeviceFiles device_files(DeviceType type)
{
    if (type == DeviceType::Snow)
        return {"remotedevicePR.xml", "editordevice.xml", "editorPR.xml", "automationPR.xml", 4};
    return {"remotedevice.xml", "editordevice.xml", "editor.xml", "automation.xml", 16};
}

DeviceFiles AccessFiles::files_for(DeviceType type) const
{
    DeviceFiles f = device_files(type);
    for (const char *n : {f.remotedevice, f.editordevice, f.editor, f.automation})
        if (get(n).empty())
            return device_files(DeviceType::TI);
    return f;
}

const std::vector<uint8_t> &AccessFiles::get(const std::string &name) const
{
    static const std::vector<uint8_t> none;
    auto it = files_.find(lower(name));
    return it == files_.end() ? none : it->second;
}

std::vector<std::string> AccessFiles::names() const
{
    std::vector<std::string> v;
    for (auto &kv : files_)
        v.push_back(kv.first);
    return v;
}

} // namespace vti
