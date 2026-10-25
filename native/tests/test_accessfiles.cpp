// AccessFiles on fake installations in a temporary folder (no Access content): a resource folder, a macOS-style
// bundle with its binary (init single / multi dumps found by their headers), a minimal PE image with an XML resource
// tree built here, the search order of open() and open_control_center(), and the error paths. Runs in CI.
#ifndef _WIN32
#include "check.h"
#include "access/accessfiles.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <unistd.h>
using namespace vti;
namespace fs = std::filesystem;
using Bytes = std::vector<uint8_t>;

namespace {
// sets environment variables for one test and puts the old values back (the other tests find the real files)
struct Env {
    std::vector<std::pair<std::string, std::pair<bool, std::string>>> saved;
    void set(const char *k, const std::string &v)
    {
        remember(k);
        setenv(k, v.c_str(), 1);
    }
    void unset(const char *k)
    {
        remember(k);
        unsetenv(k);
    }
    void remember(const char *k)
    {
        for (auto &s : saved)
            if (s.first == k)
                return;
        const char *o = getenv(k);
        saved.push_back({k, {o != nullptr, o ? o : ""}});
    }
    ~Env()
    {
        for (auto &s : saved)
            s.second.first ? setenv(s.first.c_str(), s.second.second.c_str(), 1) : unsetenv(s.first.c_str());
    }
};

struct TempDir {
    fs::path dir;
    explicit TempDir(const char *tag)
    {
        dir = fs::temp_directory_path() / (std::string("vtitests-") + tag + "-" + std::to_string(getpid()));
        fs::remove_all(dir);
        fs::create_directories(dir);
    }
    ~TempDir()
    {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};

void write(const fs::path &p, const Bytes &b)
{
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary).write((const char *)b.data(), (std::streamsize)b.size());
}
void write(const fs::path &p, const std::string &s) { write(p, Bytes(s.begin(), s.end())); }

const std::string REMOTE = "<Remotedevice device=\"Fake\"/>";

// a dump the way find_dump looks for it: F0 00 20 33 01 10 <cmd> ... name at name_at ... F7
Bytes fake_dump(uint8_t cmd, size_t size, size_t name_at, const char *name)
{
    Bytes d(size, 0x01);
    const uint8_t hdr[] = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, cmd};
    memcpy(d.data(), hdr, sizeof hdr);
    memcpy(d.data() + name_at, name, strlen(name));
    d.back() = 0xF7;
    return d;
}

// a minimal PE32+ image (`pe32`: a PE32 one, like the 32-bit Virus Control.dll): one section holding a resource tree
// type -> name -> language -> data, for the named resources (type "XML" or "PNG"), plus an id-typed resource
// (skipped like icons)
Bytes fake_pe(const std::vector<std::pair<std::string, std::string>> &xml, bool with_resources = true,
              bool pe32 = false)
{
    const uint32_t pe = 0x40, optsz = pe32 ? 224 : 240, dirs = pe32 ? 96 : 112, sec_raw = 0x400, sec_va = 0x1000;
    Bytes rs; // resource section
    auto put16 = [](Bytes &b, size_t o, uint16_t v) { b[o] = v & 0xFF, b[o + 1] = v >> 8; };
    auto put32 = [](Bytes &b, size_t o, uint32_t v) {
        for (int i = 0; i < 4; i++)
            b[o + i] = (v >> (8 * i)) & 0xFF;
    };
    auto grow = [&](size_t n) {
        size_t o = rs.size();
        rs.resize(o + n, 0);
        return o;
    };
    auto dir = [&](int named, int ids) {
        size_t o = grow(16 + 8u * (named + ids));
        put16(rs, o + 12, (uint16_t)named), put16(rs, o + 14, (uint16_t)ids);
        return o;
    };
    auto str = [&](const std::string &s) {
        size_t o = grow(2 + 2 * s.size());
        put16(rs, o, (uint16_t)s.size());
        for (size_t i = 0; i < s.size(); i++)
            put16(rs, o + 2 + 2 * i, (uint8_t)s[i]);
        return o;
    };
    size_t root = dir(1, 1);
    size_t type_xml = dir((int)xml.size(), 0);
    put32(rs, root + 16, 0x80000000u | (uint32_t)str("XML"));
    put32(rs, root + 20, 0x80000000u | (uint32_t)type_xml);
    put32(rs, root + 24, 3);                                   // id type (an icon): skipped
    put32(rs, root + 28, 0x80000000u | (uint32_t)dir(0, 0));   // an empty directory
    for (size_t i = 0; i < xml.size(); i++) {
        size_t lang = dir(0, 1);
        put32(rs, type_xml + 16 + 8 * i, 0x80000000u | (uint32_t)str(xml[i].first));
        put32(rs, type_xml + 20 + 8 * i, 0x80000000u | (uint32_t)lang);
        size_t entry = grow(16);
        put32(rs, lang + 16, 1033), put32(rs, lang + 20, (uint32_t)entry); // a data entry (no high bit)
        size_t data = grow(xml[i].second.size());
        memcpy(rs.data() + data, xml[i].second.data(), xml[i].second.size());
        put32(rs, entry, sec_va + (uint32_t)data), put32(rs, entry + 4, (uint32_t)xml[i].second.size());
    }
    Bytes d(sec_raw + rs.size(), 0);
    d[0] = 'M', d[1] = 'Z';
    put32(d, 0x3c, pe);
    memcpy(d.data() + pe, "PE\0\0", 4);
    put16(d, pe + 4, pe32 ? 0x14c : 0x8664); // machine: x86 / x64
    put16(d, pe + 6, 1);              // one section
    put16(d, pe + 20, optsz);
    put16(d, pe + 24, pe32 ? 0x10b : 0x20b); // PE32 / PE32+
    if (with_resources) {
        put32(d, pe + 24 + dirs + 16, sec_va); // data directory 2: resources
        put32(d, pe + 24 + dirs + 20, (uint32_t)rs.size());
    }
    size_t sh = pe + 24 + optsz;
    put32(d, sh + 8, (uint32_t)rs.size()), put32(d, sh + 12, sec_va);
    put32(d, sh + 16, (uint32_t)rs.size()), put32(d, sh + 20, sec_raw);
    memcpy(d.data() + sec_raw, rs.data(), rs.size());
    return d;
}

void no_installation(Env &env, const fs::path &home)
{
    env.set("HOME", home.string());
    env.unset("VTI_ACCESS_DIR"), env.unset("VTI_ACCESS_CC"), env.unset("WINEPREFIX"), env.unset("XDG_CONFIG_HOME");
}
} // namespace

TEST(accessfiles_resource_folder)
{
    TempDir t("res");
    write(t.dir / "RemoteDevice.XML", REMOTE); // names are found case-insensitively
    write(t.dir / "knob.png", Bytes{0x89, 'P', 'N', 'G'});
    write(t.dir / "notes.txt", std::string("ignored"));
    Env env;
    no_installation(env, t.dir / "home");
    std::string err;
    auto a = AccessFiles::open(t.dir.string(), err);
    CHECK(a != nullptr);
    if (!a)
        return;
    CHECK(a->get("remotedevice.xml").size() == REMOTE.size() && a->get("REMOTEDEVICE.xml").size() == REMOTE.size());
    CHECK(a->get("knob.png").size() == 4 && a->get("notes.txt").empty() && a->get("missing.xml").empty());
    CHECK(a->names().size() == 2 && a->source() == t.dir.string());
    fs::create_directories(t.dir / "empty");
    CHECK(!AccessFiles::open((t.dir / "empty").string(), err) && !err.empty());
}

// E361: the original's device types use their own files - the Snow its 4-part editor (editorPR.xml), its device
// description and automation list; the editor's device file is shared. Without the Snow's files (an installation
// that lacks them) the Snow plug-in uses the TI's, so it still opens.
TEST(accessfiles_device_files)
{
    DeviceFiles ti = device_files(DeviceType::TI), snow = device_files(DeviceType::Snow);
    CHECK(std::string(ti.editor) == "editor.xml" && std::string(ti.remotedevice) == "remotedevice.xml" &&
          std::string(ti.automation) == "automation.xml" && ti.parts == 16);
    CHECK(std::string(snow.editor) == "editorPR.xml" && std::string(snow.remotedevice) == "remotedevicePR.xml" &&
          std::string(snow.automation) == "automationPR.xml" && snow.parts == 4);
    CHECK(std::string(snow.editordevice) == "editordevice.xml" && std::string(ti.editordevice) == "editordevice.xml");

    TempDir t("types");
    write(t.dir / "remotedevice.xml", REMOTE);
    Env env;
    no_installation(env, t.dir / "home");
    std::string err;
    auto a = AccessFiles::open(t.dir.string(), err);
    CHECK(a != nullptr);
    if (!a)
        return;
    CHECK(std::string(a->files_for(DeviceType::Snow).editor) == "editor.xml"); // no Snow files: the TI's
    CHECK(a->files_for(DeviceType::Snow).parts == 16);
    for (const char *n : {"remotedevicePR.xml", "editorPR.xml", "automationPR.xml", "editordevice.xml"})
        write(t.dir / n, std::string("<x/>"));
    a = AccessFiles::open(t.dir.string(), err);
    CHECK(a && std::string(a->files_for(DeviceType::Snow).editor) == "editorPR.xml" &&
          a->files_for(DeviceType::Snow).parts == 4);
    CHECK(a && std::string(a->files_for(DeviceType::TI).editor) == "editor.xml");
}

TEST(accessfiles_bundle_dumps)
{
    TempDir t("bundle");
    fs::path bundle = t.dir / "Virus Control.bundle";
    write(bundle / "Contents" / "Resources" / "remotedevice.xml", REMOTE);
    // the bundle binary (a Mach-O): 7-bit data where the Windows DLL keeps page B (init - 0x400, a decoy here), the
    // init single, the default page B at init + 0x2a0 (E346), an init multi
    Bytes bin = {0xCF, 0xFA, 0xED, 0xFE};
    bin.insert(bin.end(), 0x500, 0x05);
    Bytes single = fake_dump(0x10, 524, 249, " -Init-"), multi = fake_dump(0x11, 267, 13, "MultiCtrl!");
    bin.insert(bin.end(), single.begin(), single.end());
    bin.insert(bin.end(), 0x2a0 - single.size(), 0x00);
    bin.insert(bin.end(), 0x100, 0x06);
    bin.insert(bin.end(), 7, 0x00);
    bin.insert(bin.end(), multi.begin(), multi.end());
    write(bundle / "Contents" / "MacOS" / "Virus Control", bin);
    Env env;
    no_installation(env, t.dir);
    env.set("VTI_ACCESS_DIR", bundle.string()); // the bundle itself: open() goes to Contents/Resources
    std::string err;
    auto a = AccessFiles::open("", err);
    CHECK(a != nullptr);
    if (!a)
        return;
    CHECK(a->init_single() == single);
    CHECK(a->init_multi() == multi);
    CHECK(a->default_page_b() == Bytes(0x100, 0x06));
    // a folder holding the bundle is found too
    auto b = AccessFiles::open(t.dir.string(), err);
    CHECK(b && b->init_single() == single);
}

TEST(accessfiles_no_dumps)
{
    TempDir t("nodump");
    write(t.dir / "remotedevice.xml", REMOTE);
    Env env;
    no_installation(env, t.dir / "home");
    std::string err;
    auto a = AccessFiles::open(t.dir.string(), err);
    CHECK(a && a->init_single().empty() && a->init_multi().empty() && a->default_page_b().empty());
}

TEST(accessfiles_pe_image)
{
    TempDir t("pe");
    fs::path dll = t.dir / "Virus Control.dll";
    write(dll, fake_pe({{"REMOTEDEVICE.XML", REMOTE}, {"EDITOR.XML", "<Editor/>"}}));
    Env env;
    no_installation(env, t.dir);
    std::string err;
    auto a = AccessFiles::open(dll.string(), err); // the DLL itself
    CHECK(a != nullptr);
    if (!a)
        return;
    CHECK(std::string(a->get("remotedevice.xml").begin(), a->get("remotedevice.xml").end()) == REMOTE);
    CHECK(a->get("editor.xml").size() == 9 && a->names().size() == 2);
    // open() with the folder that holds the DLL (the Windows / Wine layout)
    auto b = AccessFiles::open(t.dir.string(), err);
    CHECK(b && b->source() == dll.string());
    // and through WINEPREFIX
    fs::path wine = t.dir / "prefix";
    write(wine / "drive_c/Program Files/Access Music/Virus TI/Common/Virus Control.dll", fake_pe({{"REMOTEDEVICE.XML", REMOTE}}));
    env.set("WINEPREFIX", wine.string());
    auto c = AccessFiles::open("", err);
    CHECK(c && c->get("remotedevice.xml").size() == REMOTE.size());
}

TEST(accessfiles_pe_page_b)
{
    // the 64-bit Windows DLL keeps the default page B 0x400 before the init single (E345), 7-bit data after it
    TempDir t("pepageb");
    fs::path dll = t.dir / "Virus Control.dll";
    Bytes d = fake_pe({{"REMOTEDEVICE.XML", REMOTE}}), single = fake_dump(0x10, 524, 249, " -Init-");
    d.insert(d.end(), 0x100, 0x07);
    d.insert(d.end(), 0x300, 0x00);
    d.insert(d.end(), single.begin(), single.end());
    d.insert(d.end(), 0x400, 0x05);
    write(dll, d);
    Env env;
    no_installation(env, t.dir / "home");
    std::string err;
    auto a = AccessFiles::open(dll.string(), err);
    CHECK(a && a->default_page_b() == Bytes(0x100, 0x07));
}

TEST(accessfiles_pe32_refused)
{
    // only the 64-bit Virus Control.dll counts (E345): an extracted 64-bit installer can leave the 32-bit one in
    // Common, whose tables (page B for Virus C patches) lie elsewhere
    TempDir t("pe32");
    fs::path x86 = t.dir / "x86" / "Virus Control.dll", x64 = t.dir / "x64" / "Virus Control.dll";
    write(x86, fake_pe({{"REMOTEDEVICE.XML", REMOTE}}, true, true));
    write(x64, fake_pe({{"REMOTEDEVICE.XML", REMOTE}}));
    Env env;
    no_installation(env, t.dir / "home");
    std::string err;
    CHECK(!AccessFiles::open(x86.string(), err));
    // the reason survives the standard places tried after the hint
    CHECK(err.find("32-bit") != std::string::npos && err.find(x86.string()) != std::string::npos);
    // a 64-bit one later in the search order is taken
    env.set("VTI_ACCESS_DIR", x64.parent_path().string());
    auto a = AccessFiles::open(x86.parent_path().string(), err);
    CHECK(a && a->source() == x64.string());
}

TEST(accessfiles_pe32_control_center)
{
    // the Control Center's resources come from Virus Control Center.exe, which may be a 32-bit program: still read
    TempDir t("pe32cc");
    fs::path exe = t.dir / "Virus Control Center.exe";
    write(exe, fake_pe({{"ENGINE.XML", "<Engine/>"}, {"EDITOR.XML", "<Editor/>"}}, true, true));
    Env env;
    no_installation(env, t.dir / "home");
    env.set("VTI_ACCESS_CC", exe.string());
    std::string err;
    auto a = AccessFiles::open_control_center(err);
    CHECK(a && a->get("engine.xml").size() == 9);
}

TEST(accessfiles_pe_errors)
{
    TempDir t("pebad");
    Env env;
    no_installation(env, t.dir / "home");
    std::string err;
    // open() goes on to the standard places after the hint and reports the last error, so only the failure and a
    // message are checked; the PE parser's error paths run all the same
    auto fails = [&](const fs::path &p, const char *) {
        err.clear();
        return !AccessFiles::open(p.string(), err) && !err.empty();
    };
    write(t.dir / "text.dll", std::string(100, 'x'));
    CHECK(fails(t.dir / "text.dll", "not a PE"));
    write(t.dir / "nores.dll", fake_pe({{"REMOTEDEVICE.XML", REMOTE}}, false));
    CHECK(fails(t.dir / "nores.dll", "no resources"));
    write(t.dir / "other.dll", fake_pe({{"SOMETHING.XML", "<x/>"}}));
    CHECK(fails(t.dir / "other.dll", "does not contain"));
    CHECK(fails(t.dir / "missing.dll", "cannot read"));
}

TEST(accessfiles_open_nothing)
{
    TempDir t("none");
    Env env;
    no_installation(env, t.dir);
    std::string err;
    CHECK(!AccessFiles::open("", err) && !err.empty());
    err.clear();
    CHECK(!AccessFiles::open_control_center(err) && !err.empty());
    // the installer's note of where it put the Access software (XDG config), pointing to a fake folder
    write(t.dir / "access" / "remotedevice.xml", REMOTE);
    write(t.dir / ".config" / "tirevive" / "access-dir", (t.dir / "access").string() + "\n");
    auto a = AccessFiles::open("", err);
    CHECK(a && a->source() == (t.dir / "access").string());
}

TEST(accessfiles_control_center)
{
    TempDir t("cc");
    Env env;
    no_installation(env, t.dir);
    std::string err;
    // VTI_ACCESS_CC: a folder with the Control Center's resources (it needs engine.xml and editor.xml)
    write(t.dir / "cc" / "engine.xml", std::string("<Engine/>"));
    env.set("VTI_ACCESS_CC", (t.dir / "cc").string());
    CHECK(!AccessFiles::open_control_center(err)); // editor.xml missing
    write(t.dir / "cc" / "editor.xml", std::string("<Editor/>"));
    auto a = AccessFiles::open_control_center(err);
    CHECK(a && a->get("engine.xml").size() == 9);
    // next to the plug-in's DLL in the installer's Common folder (an exe with both resources)
    env.unset("VTI_ACCESS_CC");
    fs::path common = t.dir / "prefix/drive_c/Program Files/Access Music/Virus TI/Common";
    write(common / "Virus Control.dll", fake_pe({{"REMOTEDEVICE.XML", REMOTE}}));
    write(common / "Virus Control Center.exe", fake_pe({{"ENGINE.XML", "<Engine/>"}, {"EDITOR.XML", "<Editor/>"}}));
    env.set("WINEPREFIX", (t.dir / "prefix").string());
    auto b = AccessFiles::open_control_center(err);
    CHECK(b && b->source() == (common / "Virus Control Center.exe").string());
}

// the classic oscillator's 64 waves (64 x 64 floats in the plug-in binary, row 0 = one sine period): found by
// content, so the Windows DLL and the macOS bundle binary both work; anything implausible is rejected
static std::vector<uint8_t> waves_blob(size_t junk_before, float row1_value)
{
    std::vector<uint8_t> b(junk_before, 0x5A);
    for (int r = 0; r < 64; r++)
        for (int i = 0; i < 64; i++) {
            float v = r == 0 ? (float)std::sin(2 * 3.14159265358979 * i / 64) : (r == 1 ? row1_value : 0.25f * (r % 4));
            uint8_t f[4];
            memcpy(f, &v, 4);
            b.insert(b.end(), f, f + 4);
        }
    b.insert(b.end(), 100, 0xA5);
    return b;
}

TEST(accessfiles_osc_waves_by_content)
{
    auto w = vti::find_osc_waves(waves_blob(4096, 0.5f));
    CHECK(w.size() == 64 * 64);
    if (w.size() == 64 * 64) {
        CHECK(w[0] == 0.0f && std::fabs(w[16] - 1.0f) < 1e-6f); // row 0: sine, peak at index 16
        CHECK(w[64] == 0.5f && w[64 * 5] == 0.25f);
    }
    CHECK(vti::find_osc_waves(waves_blob(4098, 0.5f)).size() == 64 * 64); // not 4-byte aligned in the file
    CHECK(vti::find_osc_waves(waves_blob(4096, 5.0f)).empty());            // a row out of +-1: not the table
    CHECK(vti::find_osc_waves(waves_blob(4096, std::nanf(""))).empty());    // NaN: also under fast-math (Mac, Windows)
    CHECK(vti::find_osc_waves(waves_blob(4096, -INFINITY)).empty());
    auto nan0 = waves_blob(4096, 0.5f);
    float qnan = std::nanf("");
    memcpy(&nan0[4096 + 4 * 8], &qnan, 4); // a NaN in the sine row: also checked from the bits
    CHECK(vti::find_osc_waves(nan0).empty());
    CHECK(vti::find_osc_waves(std::vector<uint8_t>(20000, 0)).empty());     // zeros are no sine
    auto cut = waves_blob(16, 0.5f);
    cut.resize(16 + 64 * 64 * 4 - 8); // truncated
    CHECK(vti::find_osc_waves(cut).empty());
}

// macOS: the resources are in Contents/Resources, the table in the bundle binary Contents/MacOS/Virus Control
TEST(accessfiles_osc_waves_mac_bundle)
{
    TempDir t("bundlewaves");
    fs::path bundle = t.dir / "Virus Control.bundle";
    write(bundle / "Contents" / "Resources" / "remotedevice.xml", REMOTE);
    write(bundle / "Contents" / "MacOS" / "Virus Control", waves_blob(0x1234, 0.5f));
    Env env;
    no_installation(env, t.dir);
    env.set("VTI_ACCESS_DIR", bundle.string());
    std::string err;
    auto a = AccessFiles::open("", err);
    CHECK(a != nullptr);
    if (!a)
        return;
    auto w = a->osc_waves();
    CHECK(w.size() == 64 * 64 && w[64] == 0.5f);
    // a bundle without the table: no waves (the display falls back to the sine)
    write(bundle / "Contents" / "MacOS" / "Virus Control", Bytes(0x2000, 0x5A));
    CHECK(AccessFiles::open("", err)->osc_waves().empty());
}

namespace {
void put64(std::vector<uint8_t> &b, size_t at, uint64_t v)
{
    for (int i = 0; i < 8; i++)
        b[at + i] = (uint8_t)(v >> (8 * i));
}
void put32(std::vector<uint8_t> &b, size_t at, uint32_t v)
{
    for (int i = 0; i < 4; i++)
        b[at + i] = (uint8_t)(v >> (8 * i));
}
// a knob template table like the original's (E197): (name pointer, MIDI string pointer) pairs right before a remote
// template name record; `base` + file offset = the address the pointers use. Made-up entries, nothing from Access.
void put_table(std::vector<uint8_t> &b, size_t table, size_t strings, uint64_t base, int n, int ptr = 8)
{
    auto put = [&](size_t at, uint64_t v) { ptr == 8 ? put64(b, at, v) : put32(b, at, (uint32_t)v); };
    size_t s = strings;
    for (int i = 0; i < n; i++) {
        std::string name = "Group " + std::to_string(i / 4) + "/Entry " + std::to_string(i);
        std::string midi = i % 2 ? "B0 0" + std::to_string(i % 10) + " FF" : "F0 7F FF F7";
        memcpy(&b[s], name.c_str(), name.size() + 1);
        put(table + 2 * ptr * i, base + s);
        s += name.size() + 1;
        memcpy(&b[s], midi.c_str(), midi.size() + 1);
        put(table + 2 * ptr * i + ptr, base + s);
        s += midi.size() + 1;
    }
    const uint8_t rec[] = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x13, 0x00, 0x20, 'I', 'n', 'i', 't'};
    memcpy(&b[table + 2 * ptr * n], rec, sizeof rec);
}
// a minimal PE32+ file: one section mapping RVA 0x1000 to file offset 0x400, image base 0x180000000
std::vector<uint8_t> pe_with_table(int n)
{
    std::vector<uint8_t> b(0x4000, 0);
    b[0] = 'M', b[1] = 'Z';
    put32(b, 0x3C, 0x80);
    memcpy(&b[0x80], "PE\0\0", 4);
    b[0x84] = 0x64, b[0x85] = 0x86;                 // x64
    b[0x86] = 1;                                     // one section
    b[0x94] = 0xF0;                                  // size of the optional header
    b[0x98] = 0x0B, b[0x99] = 0x02;                  // PE32+
    put64(b, 0x98 + 24, 0x180000000ull);             // image base
    size_t sec = 0x98 + 0xF0;
    memcpy(&b[sec], ".rdata\0\0", 8);
    put32(b, sec + 8, 0x3C00);  // virtual size
    put32(b, sec + 12, 0x1000); // RVA
    put32(b, sec + 16, 0x3C00); // raw size
    put32(b, sec + 20, 0x400);  // raw offset
    // file offset f <-> address 0x180000000 + 0x1000 + (f - 0x400)
    put_table(b, 0x800, 0x2000, 0x180000000ull + 0x1000 - 0x400, n);
    return b;
}
// a minimal PE32 file (the 32-bit Virus Control.dll): the same section, image base 0x10000000, 4-byte pointers
std::vector<uint8_t> pe32_with_table(int n)
{
    std::vector<uint8_t> b(0x4000, 0);
    b[0] = 'M', b[1] = 'Z';
    put32(b, 0x3C, 0x80);
    memcpy(&b[0x80], "PE\0\0", 4);
    b[0x84] = 0x4C, b[0x85] = 0x01;      // i386
    b[0x86] = 1;                          // one section
    b[0x94] = 0xE0;                       // size of the optional header
    b[0x98] = 0x0B, b[0x99] = 0x01;       // PE32
    put32(b, 0x98 + 28, 0x10000000u);     // image base
    size_t sec = 0x98 + 0xE0;
    memcpy(&b[sec], ".rdata\0\0", 8);
    put32(b, sec + 8, 0x3C00);
    put32(b, sec + 12, 0x1000);
    put32(b, sec + 16, 0x3C00);
    put32(b, sec + 20, 0x400);
    put_table(b, 0x800, 0x2000, 0x10000000u + 0x1000 - 0x400, n, 4);
    return b;
}
} // namespace

// the REMOTE page's knob templates, found in the plug-in binary by shape (E197): a run of (name, MIDI string) pointer
// pairs; names may use "/" for submenus
TEST(accessfiles_knob_templates_pe)
{
    auto t = find_knob_templates(pe_with_table(35));
    CHECK(t.size() == 35);
    CHECK(t[0].name == "Group 0/Entry 0" && t[0].midi == "F0 7F FF F7");
    CHECK(t[34].name == "Group 8/Entry 34" && t[1].midi == "B0 01 FF");
    CHECK(find_knob_templates(pe_with_table(7)).empty()); // too short to be the table
    auto cut = pe_with_table(35);
    put64(cut, 0x800 + 16 * 30 + 8, 0x170000000ull); // entry 30's MIDI pointer outside the image: the run ends there
    CHECK(find_knob_templates(cut).size() == 30);
    auto odd = pe_with_table(35);
    memcpy(&odd[0x2000 + 16], "b0 05 ff", 9); // not the shape of a MIDI string ("Group 0/Entry 0" is 16 bytes)
    CHECK(find_knob_templates(odd).size() == 34 && find_knob_templates(odd)[0].name == "Group 0/Entry 1");
    CHECK(find_knob_templates(std::vector<uint8_t>(100, 0)).empty());
}

// the 32-bit Windows DLL is not supported (E345: open() refuses it): its table of 4-byte pointers is not read
TEST(accessfiles_knob_templates_pe32)
{
    CHECK(find_knob_templates(pe32_with_table(35)).empty());
}

// a thin 64-bit Mach-O (the Mac bundle's binary): addresses through its LC_SEGMENT_64 commands
TEST(accessfiles_knob_templates_macho)
{
    std::vector<uint8_t> b(0x4000, 0);
    put32(b, 0, 0xFEEDFACF);
    put32(b, 4, 0x01000007); // x86_64
    put32(b, 16, 1);         // one load command
    put32(b, 20, 72);
    put32(b, 32, 0x19); // LC_SEGMENT_64
    put32(b, 36, 72);
    memcpy(&b[40], "__DATA", 6);
    put64(b, 56, 0x100000000ull); // vmaddr
    put64(b, 64, 0x4000);         // vmsize
    put64(b, 72, 0);              // fileoff
    put64(b, 80, 0x4000);         // filesize
    put_table(b, 0x800, 0x2000, 0x100000000ull, 35);
    auto t = find_knob_templates(b);
    CHECK(t.size() == 35 && t[5].name == "Group 1/Entry 5" && t[5].midi == "B0 05 FF");
}

#endif
