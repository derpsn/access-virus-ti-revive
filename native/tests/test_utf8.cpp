// Unicode file names (#35): TI Revive keeps paths as UTF-8 strings on every platform and converts to wide paths only
// at the Windows edge (src/model/utf8). Before, a library file whose name the Windows ANSI code page cannot represent
// made fs::path::string() throw std::system_error in initialize() and crashed the host. On Linux and macOS the file
// name tests pass either way (their paths are UTF-8 already); the Windows CI runs them where the crash was.
#include "access/library.h"
#include "check.h"
#include "model/remotetemplates.h"
#include "model/utf8.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

using namespace vti;
namespace fs = std::filesystem;

namespace {
const std::string kJapanese = "\xE3\x83\x86\xE3\x82\xB9\xE3\x83\x88"; // テスト: in no Western code page
const std::string kDecomposed = "Bjo\xCC\x88rn";                       // o + U+0308, as names copied from a Mac
const std::string kComposed = "Bj\xC3\xB6rn";                          // U+00F6
const std::string kEmoji = "Bass \xF0\x9F\x8E\xB9";                     // U+1F3B9, outside the BMP (a surrogate pair)

// a fresh folder below the temp folder, as UTF-8 (the temp folder itself can hold the user name)
std::string fresh_dir(const char *name)
{
    const fs::path p = fs::temp_directory_path() / name;
    std::error_code ec;
    fs::remove_all(p, ec);
    fs::create_directories(p, ec);
    return u8str(p);
}

void remove_dir(const std::string &dir)
{
    std::error_code ec;
    fs::remove_all(u8path(dir), ec);
}

bool touch(const std::string &path)
{
    FILE *f = u8fopen(path, "wb");
    if (!f)
        return false;
    fclose(f);
    return true;
}

std::vector<uint8_t> single(int n)
{
    std::vector<uint8_t> m(524);
    for (size_t i = 0; i < m.size(); i++)
        m[i] = (uint8_t)((i * 7 + n * 13) & 0x7F);
    const uint8_t head[] = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x10, 0x02, 0x55};
    memcpy(m.data(), head, sizeof head);
    m[523] = 0xF7;
    return m;
}
} // namespace

TEST(utf8_path_round_trip)
{
    for (const std::string &s : {kJapanese, kDecomposed, kComposed, kEmoji, std::string("plain ASCII.mid")}) {
        CHECK(u8str(u8path(s)) == s);
        CHECK(u8generic(u8path("a/" + s)) == "a/" + s);
        CHECK(u8str(u8path(s + ".mid").stem()) == s);
    }
    CHECK(u8str(fs::path()).empty() && u8path("").empty());
}

TEST(utf8_files_with_any_name)
{
    const std::string dir = fresh_dir("vtitests-utf8-files");
    std::vector<std::string> made;
    for (const std::string &s : {kJapanese, kDecomposed, kComposed, kEmoji}) {
        const std::string p = dir + "/" + s + ".txt";
        CHECK(touch(p));
        CHECK(fs::exists(u8path(p)));
        made.push_back(s + ".txt");
    }
    std::vector<std::string> seen;
    std::error_code ec;
    for (fs::directory_iterator it(u8path(dir), ec), end; !ec && it != end; it.increment(ec))
        seen.push_back(u8str(it->path().filename()));
    std::sort(made.begin(), made.end());
    std::sort(seen.begin(), seen.end());
    CHECK(seen == made);
    remove_dir(dir);
}

// the crash of #35: such a name in the Patches folder, read by load_banks() in initialize()
TEST(utf8_library_names)
{
    const std::string dir = fresh_dir("vtitests-utf8-library");
    std::string err;
    for (const std::string &s : {kJapanese, kDecomposed}) {
        PatchLibrary lib;
        lib.dumps = {single(1), single(2)};
        lib.path = dir + "/" + s + ".mid";
        CHECK(save_library(lib, err));
        PatchLibrary back;
        CHECK(load_library_file(lib.path, back));
        CHECK(back.dumps.size() == 2);
    }
    CHECK(touch(dir + "/" + kEmoji + ".txt")); // not a library, but its name is read too
    std::error_code ec;
    fs::create_directories(u8path(dir + "/" + kComposed), ec);
    PatchLibrary sub;
    sub.dumps = {single(3)};
    sub.path = dir + "/" + kComposed + "/" + kComposed + ".mid";
    CHECK(save_library(sub, err));

    auto libs = load_libraries(dir);
    std::vector<std::string> names;
    for (auto &l : libs)
        names.push_back(l.name);
    std::vector<std::string> want = {kJapanese, kDecomposed, kComposed + "/" + kComposed};
    std::sort(names.begin(), names.end());
    std::sort(want.begin(), want.end());
    CHECK(names == want);
    for (auto &l : libs)
        CHECK(!l.dumps.empty());
    remove_dir(dir);
}

TEST(utf8_library_import)
{
    const std::string src = fresh_dir("vtitests-utf8-import-src"), dst = fresh_dir("vtitests-utf8-import-dst");
    PatchLibrary lib;
    lib.dumps = {single(4)};
    lib.path = src + "/" + kJapanese + ".mid";
    std::string err;
    CHECK(save_library(lib, err));
    auto r = import_library(lib.path, dst);
    CHECK(r.error.empty() && r.patches == 1 && r.name == kJapanese);
    CHECK(r.path == u8str(u8path(dst) / u8path(kJapanese + ".mid")));
    CHECK(fs::exists(u8path(r.path)));
    auto again = import_library(lib.path, dst); // the same again: found by its UTF-8 name, not copied
    CHECK(again.existed && again.path == r.path);
    remove_dir(src);
    remove_dir(dst);
}

TEST(utf8_remote_template_names)
{
    const std::string dir = fresh_dir("vtitests-utf8-templates");
    CHECK(touch(dir + "/" + kJapanese + ".vrt"));
    CHECK(touch(dir + "/" + kEmoji + ".txt"));
    auto files = remote_template_files(dir);
    CHECK(files.size() == 1 && !files.empty() && files[0] == kJapanese + ".vrt");
    remove_dir(dir);
}

TEST(utf8_environment)
{
    const std::string value = "C:/Users/" + kJapanese + "/" + kComposed;
#ifdef _WIN32
    CHECK(_wputenv_s(L"VTI_UTF8_TEST", widen(value).c_str()) == 0);
#else
    CHECK(setenv("VTI_UTF8_TEST", value.c_str(), 1) == 0);
#endif
    CHECK(u8env("VTI_UTF8_TEST") == value);
    CHECK(u8env("VTI_UTF8_TEST_NOT_SET_ANYWHERE").empty());
}

// the editor's bitmap font has ASCII 32..127 only: one glyph per character, accents folded, the rest '?'
TEST(utf8_ascii_fold_for_the_bitmap_font)
{
    CHECK(ascii_fold("Plain Name 12") == "Plain Name 12");
    CHECK(ascii_fold(kComposed) == "Bjorn");
    CHECK(ascii_fold(kDecomposed) == "Bjorn"); // the combining mark takes no glyph
    CHECK(ascii_fold(kJapanese) == "???");
    CHECK(ascii_fold(kEmoji) == "Bass ?");
    CHECK(ascii_fold("Caf\xC3\xA9 \xC3\x91 \xC3\x85") == "Cafe N A");
    CHECK(ascii_fold("Bj\xF6rn") == "Bj?rn"); // a stray Latin-1 byte (not UTF-8): one '?'
    CHECK(ascii_fold("ab\x01" "c") == "ab?c");
}
