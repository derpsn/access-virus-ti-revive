// Patch libraries and the Control Center's export (src/access/library, src/cc/controlcenter) on generated single
// dumps: .mid and .syx round trips keep the patch data; save_library numbers the patches and recomputes the checksums.
#include "access/library.h"
#include "cc/controlcenter.h"
#include "check.h"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>

using namespace vti;
namespace fs = std::filesystem;

namespace {
Dump single(int n)
{
    Dump m(524);
    for (size_t i = 0; i < m.size(); i++)
        m[i] = (uint8_t)((i * 7 + n * 13) & 0x7F);
    const uint8_t head[] = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x10, 0x02, 0x55};
    memcpy(m.data(), head, sizeof head);
    char name[11];
    snprintf(name, sizeof name, "Test %-5d", n);
    memcpy(m.data() + 249, name, 10);
    m[523] = 0xF7;
    return m;
}

// everything except the bank/program bytes and the two checksums
bool same_patch(const Dump &a, const Dump &b)
{
    if (a.size() != 524 || b.size() != 524)
        return false;
    for (size_t i = 0; i < 524; i++)
        if (i != 7 && i != 8 && i != 265 && i != 522 && a[i] != b[i])
            return false;
    return true;
}
} // namespace

TEST(library_round_trip)
{
    fs::path dir = fs::temp_directory_path() / "vtitests-library";
    fs::remove_all(dir);
    fs::create_directories(dir);
    PatchLibrary lib;
    for (int i = 0; i < 5; i++)
        lib.dumps.push_back(single(i));
    lib.path = (dir / "a.mid").string();
    std::string err;
    CHECK(save_library(lib, err));
    CHECK(cc::write_syx(lib.dumps, (dir / "a.syx").string(), err));

    PatchLibrary mid, syx;
    CHECK(load_library_file((dir / "a.mid").string(), mid));
    CHECK(load_library_file((dir / "a.syx").string(), syx));
    CHECK(mid.dumps.size() == 5 && syx.dumps.size() == 5);
    for (size_t i = 0; i < mid.dumps.size() && i < syx.dumps.size(); i++) {
        CHECK(same_patch(mid.dumps[i], lib.dumps[i]));
        CHECK(syx.dumps[i] == lib.dumps[i]); // .syx keeps the dumps as they are
        CHECK(mid.dumps[i][7] == 1 && mid.dumps[i][8] == i); // library: bank 1, program = position
    }
    CHECK(cc::patch_name(mid.dumps[3]) == "Test 3");
    CHECK(load_libraries(dir.string()).size() == 2);
    fs::remove_all(dir);
}

TEST(library_edge_cases)
{
    fs::path dir = fs::temp_directory_path() / "vtitests-library-edge";
    fs::remove_all(dir);
    fs::create_directories(dir / "sub");
    auto write = [](const fs::path &p, const Dump &d) {
        std::ofstream o(p, std::ios::binary);
        o.write((const char *)d.data(), (std::streamsize)d.size());
    };
    // a Virus C dump (page A only, 267 bytes, version 0) between a good one and a cut-off one
    Dump c(267, 0);
    const uint8_t head[] = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x10, 0x01, 0x05};
    memcpy(c.data(), head, sizeof head);
    c[266] = 0xF7;
    Dump file = single(1);
    file.insert(file.end(), c.begin(), c.end());
    Dump cut = single(2);
    cut.pop_back(); // no F7: the scan ends here
    file.insert(file.end(), cut.begin(), cut.end());
    write(dir / "sub" / "Mixed.SYX", file);
    write(dir / "notes.txt", single(3));

    auto patches = [](const PatchLibrary &l) {
        return std::count_if(l.dumps.begin(), l.dumps.end(), [](const Dump &d) { return !d.empty(); });
    };
    PatchLibrary plain, upgraded; // each patch at its program byte (E298): single() has 0x55, the Virus C dump 5
    CHECK(load_library_file((dir / "sub" / "Mixed.SYX").string(), plain));
    CHECK(patches(plain) == 1 && plain.dumps.size() == 0x56); // without page B a Virus C dump cannot be upgraded
    CHECK(load_library_file((dir / "sub" / "Mixed.SYX").string(), upgraded, std::vector<uint8_t>(256, 0)));
    CHECK(patches(upgraded) == 2);
    if (upgraded.dumps.size() > 5)
        CHECK(upgraded.dumps[5].size() == 524 && upgraded.dumps[5][9] != 0 && upgraded.dumps[5][523] == 0xF7);
    PatchLibrary none;
    CHECK(!load_library_file((dir / "missing.mid").string(), none));

    auto libs = load_libraries(dir.string()); // subfolders, any case of the extension, other files skipped
    CHECK(libs.size() == 1 && libs[0].name == "sub/Mixed");

    PatchLibrary out;
    out.dumps.push_back(single(4));
    out.dumps.push_back(Dump(10, 0)); // not a single dump: left out
    out.path = (dir / "no-such-folder" / "x.mid").string();
    std::string err;
    CHECK(!save_library(out, err) && !err.empty());
    out.path = (dir / "x.mid").string();
    PatchLibrary back;
    CHECK(save_library(out, err) && load_library_file(out.path, back) && back.dumps.size() == 1);
    fs::remove_all(dir);
}

#ifndef _WIN32
#include <unistd.h>
// Linux: the documents folder from the XDG user dirs (localized, e.g. ~/Dokumente), an existing ~/Documents library
// folder first (installs before this change put the factory libraries there)
TEST(library_dir_xdg_documents)
{
    CHECK(vti::xdg_documents_dir("/h", "XDG_DOCUMENTS_DIR=\"$HOME/Dokumente\"\n") == "/h/Dokumente");
    CHECK(vti::xdg_documents_dir("/h", "# comment\nXDG_MUSIC_DIR=\"$HOME/Musik\"\n  XDG_DOCUMENTS_DIR=\"/data/docs\"\n") ==
          "/data/docs");
    CHECK(vti::xdg_documents_dir("/h", "XDG_MUSIC_DIR=\"$HOME/Musik\"\n").empty());
    CHECK(vti::xdg_documents_dir("/h", "XDG_DOCUMENTS_DIR=\"$HOME/\"\n").empty()); // = the home itself: unusable
    CHECK(vti::xdg_documents_dir("/h", "XDG_DOCUMENTS_DIR=\"relative\"\n").empty());

    struct Env {
        std::string k, old;
        bool had;
        Env(const char *key, const std::string &v) : k(key), had(getenv(key) != nullptr)
        {
            if (had)
                old = getenv(key);
            setenv(key, v.c_str(), 1);
        }
        ~Env() { had ? setenv(k.c_str(), old.c_str(), 1) : unsetenv(k.c_str()); }
    };
    fs::path dir = fs::temp_directory_path() / ("vtitests-libdir-" + std::to_string(getpid()));
    fs::remove_all(dir);
    fs::create_directories(dir / "cfg");
    Env home("HOME", dir.string()), cfg("XDG_CONFIG_HOME", (dir / "cfg").string());
    const std::string tail = "/Access Music/Virus TI/Patches";
    CHECK(vti::library_dir() == dir.string() + "/Documents" + tail); // no user-dirs.dirs: ~/Documents
    std::ofstream(dir / "cfg/user-dirs.dirs") << "XDG_DOCUMENTS_DIR=\"$HOME/Dokumente\"\n";
    CHECK(vti::library_dir() == dir.string() + "/Dokumente" + tail);
    fs::create_directories(dir.string() + "/Documents" + tail); // an earlier install's folder wins
    CHECK(vti::library_dir() == dir.string() + "/Documents" + tail);
    auto roots = vti::library_roots(); // Linux: only the personal one (no Access installer puts libraries elsewhere)
    CHECK(roots.size() == 1 && roots[0].menu == "Personal Libraries" && roots[0].dir == vti::library_dir());
    fs::remove_all(dir);
}
#endif

// the original's three roots (E307), in its order: Personal (Documents), User (%APPDATA%, ~/Library/Application
// Support), Public (C:\ProgramData, /Library/Application Support), each + "/Access Music/Virus TI/Patches"; a missing
// base or a folder already listed is left out
TEST(library_roots_three)
{
    const std::string tail = "/Access Music/Virus TI/Patches";
    auto r = vti::library_roots_for("/d" + tail, "/u", "/p");
    CHECK(r.size() == 3);
    if (r.size() == 3) {
        CHECK(r[0].menu == "Personal Libraries" && r[0].dir == "/d" + tail);
        CHECK(r[1].menu == "User Libraries" && r[1].dir == "/u" + tail);
        CHECK(r[2].menu == "Public Libraries" && r[2].dir == "/p" + tail);
    }
    r = vti::library_roots_for("/d" + tail, "", "/d");
    CHECK(r.size() == 1 && r[0].menu == "Personal Libraries");
}

// "Create Empty Library" (E217): the name from the save dialog gets ".mid" like in the original (".midi" -> ".mid",
// anything else + ".mid"); the file is the original's empty library (a standard MIDI file with one empty track, 26
// bytes), written over an existing file (the dialog asked); it loads as a library without patches
TEST(library_create_empty)
{
    CHECK(library_file_name("/a/New Library.mid") == "/a/New Library.mid");
    CHECK(library_file_name("/a/x.midi") == "/a/x.mid");
    CHECK(library_file_name("/a/x") == "/a/x.mid");
    CHECK(library_file_name("/a/x.syx") == "/a/x.syx.mid");
    fs::path dir = fs::temp_directory_path() / "vtitests-library-empty";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const std::string path = (dir / "New Library.mid").string();
    std::string err;
    std::ofstream(path) << "old";
    CHECK(write_empty_library(path, err));
    const Dump want = {0x4D, 0x54, 0x68, 0x64, 0, 0, 0, 6, 0, 0, 0, 1, 0x01, 0xE0,
                       0x4D, 0x54, 0x72, 0x6B, 0, 0, 0, 4, 0x78, 0xFF, 0x2F, 0x00};
    Dump got;
    {
        std::ifstream f(path, std::ios::binary); // closed before remove_all: Windows cannot delete an open file
        got.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    }
    CHECK(got == want);
    PatchLibrary lib;
    CHECK(load_library_file(path, lib) && lib.dumps.empty());
    CHECK(load_libraries(dir.string()).size() == 1);
    CHECK(!write_empty_library((dir / "no-such-folder" / "x.mid").string(), err) && !err.empty());
    fs::remove_all(dir);
}

// a library position is the dump's program byte, as in the original (E298): gaps stay empty (a deleted patch, a copy
// from RAM A 27 shown at 27); the writer keeps each patch at its position and leaves the gaps out of the file
TEST(library_position_is_program_byte)
{
    fs::path dir = fs::temp_directory_path() / "vtitests-library-pos";
    fs::remove_all(dir);
    fs::create_directories(dir);
    PatchLibrary lib;
    lib.dumps.resize(28);
    lib.dumps[0] = single(0), lib.dumps[1] = single(1), lib.dumps[27] = single(27);
    lib.path = (dir / "gaps.mid").string();
    std::string err;
    CHECK(save_library(lib, err));
    PatchLibrary back;
    CHECK(load_library_file(lib.path, back));
    CHECK(back.dumps.size() == 28);
    if (back.dumps.size() == 28) {
        CHECK(same_patch(back.dumps[0], single(0)) && same_patch(back.dumps[1], single(1)));
        CHECK(same_patch(back.dumps[27], single(27)) && back.dumps[27][8] == 27);
        for (int p = 2; p < 27; p++)
            CHECK(back.dumps[p].empty());
    }
    CHECK(fs::file_size(lib.path) == 18 + 4 + 3 * (1 + 1 + 2 + 523) + 4); // three patches, no gap entries

    // repeated program bytes (other tools' exports, several banks in one .syx): the file order, as before
    Dump a = single(5), b = single(6), c = single(7);
    a[8] = 0, b[8] = 0, c[8] = 5;
    CHECK(cc::write_syx({a, b, c}, (dir / "dup.syx").string(), err));
    PatchLibrary dup;
    CHECK(load_library_file((dir / "dup.syx").string(), dup));
    CHECK(dup.dumps.size() == 3 && dup.dumps[0] == a && dup.dumps[1] == b && dup.dumps[2] == c);
    fs::remove_all(dir);
}

// the original's decoder (CPatchLibrarySMFDecoder 0x18006b360, E356) keeps only single dumps of exactly 267 or 524
// bytes and skips the rest without a word; ours counts what it leaves out (damaged dumps, Virus C without page B,
// versions it cannot convert) so load_banks can name the library in diag.log
TEST(library_counts_skipped_dumps)
{
    fs::path dir = fs::temp_directory_path() / "vtitests-library-skip";
    fs::remove_all(dir);
    fs::create_directories(dir);
    Dump good = single(0), cut = single(1), virus_c = single(2);
    cut.erase(cut.begin() + 100, cut.begin() + 164); // a dump 64 bytes short, like "Incubation V3 ... .mid.mid"
    virus_c.resize(267);
    virus_c[266] = 0xF7;
    std::string err;
    CHECK(cc::write_syx({good, cut, virus_c}, (dir / "mixed.syx").string(), err));
    PatchLibrary lib;
    CHECK(load_library_file((dir / "mixed.syx").string(), lib)); // no page B: the Virus C dump cannot be upgraded
    CHECK(std::count_if(lib.dumps.begin(), lib.dumps.end(), [](const Dump &d) { return !d.empty(); }) == 1);
    CHECK(lib.skipped == 2);
    PatchLibrary clean;
    CHECK(cc::write_syx({good}, (dir / "clean.syx").string(), err));
    CHECK(load_library_file((dir / "clean.syx").string(), clean) && clean.skipped == 0);
    CHECK(load_library_file((dir / "mixed.syx").string(), lib) && lib.skipped == 2); // a reload starts from 0
    fs::remove_all(dir);
}

// a library file dropped on the browser (TI Revive's own addition, E357; the original refuses file drops, E355):
// .mid/.midi copied byte for byte, .syx written as the original's .mid; nothing is overwritten, the same patches
// already there are only shown, a file without Virus patches is not copied
TEST(library_import)
{
    fs::path dir = fs::temp_directory_path() / "vtitests-library-import";
    fs::remove_all(dir);
    fs::create_directories(dir / "Patches");
    fs::create_directories(dir / "Downloads");
    const std::string patches = (dir / "Patches").string(), dl = (dir / "Downloads").string();
    CHECK(importable_library("a/Bank.mid") && importable_library("Bank.MIDI") && importable_library("x.SyX"));
    CHECK(!importable_library("notes.txt") && !importable_library("mid") && !importable_library("a.mid.txt"));

    PatchLibrary src;
    src.dumps = {single(0), single(1)};
    src.path = dl + "/Bank.mid";
    std::string err;
    CHECK(save_library(src, err));
    auto r = import_library(src.path, patches);
    CHECK(r.error.empty() && !r.existed && r.patches == 2 && r.skipped == 0 && r.name == "Bank");
    CHECK(r.path == (fs::path(patches) / "Bank.mid").string());
    auto bytes = [](const std::string &p) {
        std::ifstream f(p, std::ios::binary);
        return Dump((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    };
    CHECK(bytes(r.path) == bytes(src.path)); // a .mid is copied as it is

    auto again = import_library(src.path, patches); // the same file again: shown, not copied
    CHECK(again.existed && again.path == r.path && again.name == "Bank");
    CHECK(!fs::exists(fs::path(patches) / "Bank 2.mid"));
    auto inside = import_library(r.path, patches); // a file from the Personal Libraries folder itself
    CHECK(inside.existed && inside.path == r.path);

    PatchLibrary other; // another bank of the same name: next to it, never over it
    other.dumps = {single(5)};
    fs::create_directories(dir / "Downloads" / "b");
    other.path = dl + "/b/Bank.mid";
    CHECK(save_library(other, err));
    auto second = import_library(other.path, patches);
    CHECK(second.error.empty() && !second.existed && second.name == "Bank 2" && second.patches == 1);
    CHECK(bytes(r.path) == bytes(src.path));

    CHECK(cc::write_syx({single(3), single(4)}, dl + "/Sys.syx", err)); // .syx -> the original's .mid
    auto syx = import_library(dl + "/Sys.syx", patches);
    CHECK(syx.error.empty() && syx.name == "Sys" && syx.path == (fs::path(patches) / "Sys.mid").string());
    PatchLibrary back;
    CHECK(load_library_file(syx.path, back));
    CHECK(std::count_if(back.dumps.begin(), back.dumps.end(), [](const Dump &d) { return !d.empty(); }) == 2);
    CHECK(import_library(dl + "/Sys.syx", patches).existed); // the same patches as the converted file

    std::ofstream(dl + "/empty.mid") << "MThd but no Virus";
    auto none = import_library(dl + "/empty.mid", patches);
    CHECK(!none.error.empty() && none.path.empty() && !fs::exists(fs::path(patches) / "empty.mid"));
    CHECK(!import_library(dl + "/missing.mid", patches).error.empty());

    Dump cut = single(6); // damaged patches: imported, counted
    cut.resize(300);
    cut[299] = 0xF7;
    CHECK(cc::write_syx({single(7), cut}, dl + "/Part.syx", err));
    auto part = import_library(dl + "/Part.syx", patches);
    CHECK(part.error.empty() && part.patches == 1 && part.skipped == 1);
    fs::remove_all(dir);
}

// review of E357: the same patches in other slots are another library (the slot layout is the user's); without a
// Personal Libraries folder (no HOME/USERPROFILE) nothing is written (not into the host's working folder)
TEST(library_import_slots_and_no_folder)
{
    fs::path dir = fs::temp_directory_path() / "vtitests-library-import2";
    fs::remove_all(dir);
    fs::create_directories(dir / "Patches");
    const std::string patches = (dir / "Patches").string();
    std::string err;
    PatchLibrary a, b;
    a.dumps = {single(0), single(1)};
    b.dumps.resize(10);
    b.dumps[5] = single(0), b.dumps[9] = single(1);
    a.path = (dir / "Bank.mid").string();
    fs::create_directories(dir / "other");
    b.path = (dir / "other" / "Bank.mid").string();
    CHECK(save_library(a, err) && save_library(b, err));
    CHECK(!import_library(a.path, patches).existed);
    auto moved = import_library(b.path, patches);
    CHECK(!moved.existed && moved.name == "Bank 2");
    auto none = import_library(a.path, "");
    CHECK(!none.error.empty() && none.path.empty());
    fs::remove_all(dir);
}
