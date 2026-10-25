// library.cpp - see library.h.
#include "library.h"
#include "model/remotedevice.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace vti {

namespace fs = std::filesystem;

bool load_library_file(const std::string &path, PatchLibrary &lib, const std::vector<uint8_t> &page_b)
{
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return false;
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    lib.path = path;
    lib.dumps.clear();
    lib.skipped = 0;
    std::vector<std::vector<uint8_t>> found; // in file order
    // SysEx single dumps anywhere in the file (SMF sysex events or raw .syx): 00 20 33 01 <dev> 10 ... F7
    for (size_t i = 0; i + 8 < d.size(); i++) {
        if (!(d[i] == 0x00 && d[i + 1] == 0x20 && d[i + 2] == 0x33 && d[i + 3] == 0x01 && d[i + 5] == 0x10))
            continue;
        auto end = std::find(d.begin() + i, d.end(), 0xF7);
        if (end == d.end())
            break;
        std::vector<uint8_t> m = {0xF0};
        m.insert(m.end(), d.begin() + i, end + 1);
        // other sizes are skipped like the original's decoder does (E356); a version upgrade_single does not know
        // (e.g. 127) stays out too: the original would list it, but what such a dump does on the Virus is unknown
        if (m.size() == 524) {
            found.push_back(m);
        } else if (m.size() == 267 && page_b.size() == 256) { // Virus C: page A only -> upgrade like the original
            m.resize(524, 0);
            m[523] = 0xF7;
            if (RemoteDevice::upgrade_single(m, page_b))
                found.push_back(m);
            else
                lib.skipped++;
        } else {
            lib.skipped++;
        }
        i = end - d.begin();
    }
    // the original shows each patch at its program byte (byte 8), gaps stay empty (E298); when the bytes repeat
    // (other tools' exports, several banks in one file) the file order, as before
    std::vector<bool> used(128, false);
    bool by_program = found.size() <= 128;
    for (auto &m : found) {
        if (!by_program)
            break;
        if (m[8] >= 128 || used[m[8]])
            by_program = false;
        else
            used[m[8]] = true;
    }
    if (!by_program) {
        lib.dumps = std::move(found);
        return true;
    }
    for (auto &m : found) {
        if (m[8] >= lib.dumps.size())
            lib.dumps.resize(m[8] + 1);
        lib.dumps[m[8]] = std::move(m);
    }
    return true;
}

std::vector<PatchLibrary> load_libraries(const std::string &dir, const std::vector<uint8_t> &page_b)
{
    std::vector<PatchLibrary> out;
    std::error_code ec;
    // like the original: also the libraries in subfolders (e.g. "Virus TI2 Factory Backup/Rom/Virus TI2 Rom A")
    for (auto &e : fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec)) {
        if (!e.is_regular_file(ec))
            continue;
        std::string ext = e.path().extension().string();
        for (auto &ch : ext)
            ch = (char)tolower(ch);
        if (ext != ".mid" && ext != ".syx")
            continue;
        PatchLibrary lib;
        if (!load_library_file(e.path().string(), lib, page_b))
            continue;
        lib.name = (e.path().parent_path().lexically_relative(dir) / e.path().stem()).generic_string();
        if (lib.name.rfind("./", 0) == 0)
            lib.name = lib.name.substr(2);
        out.push_back(std::move(lib));
    }
    std::sort(out.begin(), out.end(), [](const PatchLibrary &a, const PatchLibrary &b) { return a.name < b.name; });
    return out;
}

namespace {
std::string lower_ext(const fs::path &p)
{
    std::string ext = p.extension().string();
    for (auto &ch : ext)
        ch = (char)tolower(ch);
    return ext;
}

// the same patches in the same slots (the slot layout is the user's); the bank/program bytes and the checksums may
// differ (save_library sets them)
bool same_patches(const PatchLibrary &a, const PatchLibrary &b)
{
    static const std::vector<uint8_t> gap;
    const size_t n = std::max(a.dumps.size(), b.dumps.size());
    for (size_t i = 0; i < n; i++) {
        const auto &x = i < a.dumps.size() ? a.dumps[i] : gap, &y = i < b.dumps.size() ? b.dumps[i] : gap;
        if (x.size() != y.size())
            return false;
        for (size_t k = 0; k < x.size(); k++)
            if (k != 7 && k != 8 && k != 265 && k != 522 && x[k] != y[k])
                return false;
    }
    return true;
}
} // namespace

bool importable_library(const std::string &path)
{
    const fs::path p(path);
    const std::string ext = lower_ext(p);
    return !p.stem().empty() && (ext == ".mid" || ext == ".midi" || ext == ".syx");
}

LibraryImport import_library(const std::string &src, const std::string &dir, const std::vector<uint8_t> &page_b)
{
    LibraryImport r;
    const fs::path from(src);
    const std::string file = from.filename().string();
    if (dir.empty()) { // no documents folder known: a relative path would land in the host's working folder
        r.error = "No Personal Libraries folder";
        return r;
    }
    PatchLibrary lib;
    if (!importable_library(src) || !load_library_file(src, lib, page_b)) {
        r.error = "Cannot read " + file;
        return r;
    }
    r.skipped = lib.skipped;
    r.patches = (int)std::count_if(lib.dumps.begin(), lib.dumps.end(), [](const std::vector<uint8_t> &d) { return !d.empty(); });
    if (r.patches == 0) {
        r.error = "No Virus patches in " + file;
        return r;
    }
    std::error_code ec;
    // already in the folder (a drop from Personal Libraries itself): listed under its own name
    const fs::path base = fs::weakly_canonical(dir, ec), canon = fs::weakly_canonical(from, ec);
    const fs::path rel = canon.lexically_relative(base);
    if (!rel.empty() && *rel.begin() != "..") {
        r.existed = true;
        r.path = src;
        r.name = (rel.parent_path() / rel.stem()).generic_string();
        return r;
    }
    // a .syx becomes a .mid unless it holds more than one bank (save_library writes 128 positions)
    const bool as_mid = lower_ext(from) != ".syx" || lib.dumps.size() <= 128;
    const std::string ext = as_mid ? ".mid" : ".syx", stem = from.stem().string();
    for (int n = 1; n < 1000; n++) {
        const std::string name = n == 1 ? stem : stem + " " + std::to_string(n);
        const fs::path mid = fs::path(dir) / (name + ".mid"), syx = fs::path(dir) / (name + ".syx");
        bool taken = false;
        for (const fs::path &p : {mid, syx}) { // both would be listed as `name`
            if (!fs::exists(p, ec))
                continue;
            taken = true;
            PatchLibrary there;
            if (load_library_file(p.string(), there, page_b) && same_patches(there, lib)) {
                r.existed = true;
                r.path = p.string();
                r.name = name;
                return r;
            }
        }
        if (taken)
            continue;
        const fs::path to = ext == ".mid" ? mid : syx;
        fs::create_directories(dir, ec);
        std::string err;
        if (as_mid && lower_ext(from) == ".syx") {
            PatchLibrary out = lib;
            out.path = to.string();
            if (!save_library(out, err)) {
                r.error = err;
                return r;
            }
        } else if (!fs::copy_file(from, to, ec)) {
            r.error = "Cannot copy " + file + ": " + ec.message();
            return r;
        }
        r.path = to.string();
        r.name = name;
        return r;
    }
    r.error = "No free name for " + file;
    return r;
}

std::string library_file_name(const std::string &chosen)
{
    auto ends = [&](const std::string &e) {
        return chosen.size() >= e.size() && chosen.compare(chosen.size() - e.size(), e.size(), e) == 0;
    };
    if (ends(".midi"))
        return chosen.substr(0, chosen.size() - 1);
    return ends(".mid") ? chosen : chosen + ".mid";
}

bool write_empty_library(const std::string &path, std::string &error)
{
    // byte for byte the original's empty file (Virus Control 0x180065900 writes it): the end of track at delta 120
    static const uint8_t empty[] = {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0x01, 0xE0,
                                    'M', 'T', 'r', 'k', 0, 0, 0, 4, 0x78, 0xFF, 0x2F, 0x00};
    std::ofstream o(path, std::ios::binary | std::ios::trunc);
    o.write((const char *)empty, sizeof empty);
    o.close();
    if (!o) {
        error = "cannot write " + path;
        return false;
    }
    return true;
}

bool save_library(const PatchLibrary &lib, std::string &error)
{
    std::vector<uint8_t> trk;
    auto varlen = [&](uint32_t v) {
        uint8_t b[5];
        int n = 0;
        b[n++] = v & 0x7F;
        while (v >>= 7)
            b[n++] = (uint8_t)(0x80 | (v & 0x7F));
        while (n)
            trk.push_back(b[--n]);
    };
    for (size_t i = 0; i < lib.dumps.size() && i < 128; i++) {
        std::vector<uint8_t> m = lib.dumps[i];
        if (m.size() != 524) // a gap: no patch at this position (the original leaves it out, E298)
            continue;
        m[7] = 1, m[8] = (uint8_t)i;
        unsigned a = 0;
        for (int k = 5; k < 265; k++)
            a += m[k];
        m[265] = a & 0x7F;
        for (int k = 265; k < 522; k++)
            a += m[k];
        m[522] = a & 0x7F;
        varlen(120);
        trk.push_back(0xF0);
        varlen((uint32_t)m.size() - 1);
        trk.insert(trk.end(), m.begin() + 1, m.end());
    }
    varlen(120);
    trk.insert(trk.end(), {0xFF, 0x2F, 0x00});
    std::vector<uint8_t> f = {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0, 0x60, 'M', 'T', 'r', 'k'};
    for (int s = 24; s >= 0; s -= 8)
        f.push_back((uint8_t)(trk.size() >> s));
    f.insert(f.end(), trk.begin(), trk.end());
    std::string tmp = lib.path + ".tmp";
    {
        std::ofstream o(tmp, std::ios::binary);
        o.write((const char *)f.data(), (std::streamsize)f.size());
        if (!o) {
            error = "cannot write " + tmp;
            return false;
        }
    }
    std::error_code ec;
    fs::rename(tmp, lib.path, ec);
    if (ec)
        error = ec.message();
    return !ec;
}

std::string xdg_documents_dir(const std::string &home, const std::string &user_dirs)
{
    std::istringstream in(user_dirs);
    std::string line;
    while (std::getline(in, line)) {
        size_t k = line.find_first_not_of(" \t");
        if (k == std::string::npos || line.compare(k, 19, "XDG_DOCUMENTS_DIR=\"") != 0)
            continue;
        size_t b = k + 19, e = line.find('"', b);
        if (e == std::string::npos)
            return {};
        std::string v = line.substr(b, e - b);
        if (v.rfind("$HOME", 0) == 0)
            v = home + v.substr(5);
        while (v.size() > 1 && v.back() == '/')
            v.pop_back();
        if (v.empty() || v[0] != '/' || v == home)
            return {}; // XDG: "$HOME/" means "no documents folder"
        return v;
    }
    return {};
}

std::string library_dir()
{
    const std::string tail = "/Access Music/Virus TI/Patches";
    std::vector<std::string> c;
#ifdef _WIN32
    if (const char *up = getenv("USERPROFILE"))
        c.push_back(std::string(up) + "/Documents" + tail);
#else
    if (const char *home = getenv("HOME")) {
        std::string h = home, docs = h + "/Documents" + tail;
        const char *cfg = getenv("XDG_CONFIG_HOME");
        std::ifstream f((cfg && *cfg ? std::string(cfg) : h + "/.config") + "/user-dirs.dirs");
        std::string xdg;
        if (f) {
            std::stringstream t;
            t << f.rdbuf();
            xdg = xdg_documents_dir(h, t.str());
        }
        std::error_code ec;
        if (!fs::is_directory(docs, ec) && !xdg.empty())
            c.push_back(xdg + tail);
        c.push_back(docs);
        if (const char *user = getenv("USER")) // an existing Access installation under Wine (Linux)
            c.push_back(h + "/.wine/drive_c/users/" + user + "/Documents" + tail);
    }
#endif
    std::error_code ec;
    for (auto &d : c)
        if (fs::is_directory(d, ec))
            return d;
    return c.empty() ? std::string() : c.front();
}

std::vector<LibraryRoot> library_roots_for(const std::string &personal, const std::string &user,
                                           const std::string &shared)
{
    const std::string tail = "/Access Music/Virus TI/Patches";
    std::vector<LibraryRoot> r;
    auto add = [&r](const char *menu, const std::string &dir) {
        if (dir.empty() || std::any_of(r.begin(), r.end(), [&](const LibraryRoot &x) { return x.dir == dir; }))
            return;
        r.push_back({menu, dir});
    };
    add("Personal Libraries", personal);
    add("User Libraries", user.empty() ? user : user + tail);
    add("Public Libraries", shared.empty() ? shared : shared + tail);
    return r;
}

std::vector<LibraryRoot> library_roots()
{
    std::string user, shared;
#ifdef _WIN32
    if (const char *a = getenv("APPDATA"))
        user = a;
    if (const char *p = getenv("PROGRAMDATA"))
        shared = p;
#elif defined(__APPLE__)
    if (const char *home = getenv("HOME"))
        user = std::string(home) + "/Library/Application Support";
    shared = "/Library/Application Support";
#endif
    return library_roots_for(library_dir(), user, shared);
}

} // namespace vti
