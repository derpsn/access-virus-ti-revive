// Nerd mode's diagnostics (src/cc/diag) below a temporary HOME - never the user's real plug-in folder: the diag.on
// switch, the end of long logs, anonymizing (home folder, user name) and the bug-report file. POSIX (sets HOME).
#ifndef _WIN32
#include "check.h"
#include "cc/diag.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <unistd.h>
using namespace vti::cc;
namespace fs = std::filesystem;

namespace {
struct Home {
    std::string old_home, old_user;
    bool had_home = false, had_user = false;
    fs::path dir;
    explicit Home(const char *user)
    {
        if (const char *h = getenv("HOME"))
            old_home = h, had_home = true;
        if (const char *u = getenv("USER"))
            old_user = u, had_user = true;
        dir = fs::temp_directory_path() / ("vtitests-diag-" + std::to_string(getpid()));
        fs::remove_all(dir);
        fs::create_directories(dir);
        setenv("HOME", dir.string().c_str(), 1);
        setenv("USER", user, 1);
    }
    ~Home()
    {
        had_home ? setenv("HOME", old_home.c_str(), 1) : unsetenv("HOME");
        had_user ? setenv("USER", old_user.c_str(), 1) : unsetenv("USER");
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};
} // namespace

TEST(diag_switch_and_paths)
{
    Home h("vtester");
    CHECK(plugin_dir() == (h.dir / ".local/share/virus-ti-native").string());
    CHECK(plugin_log_path() == (h.dir / ".local/share/virus-ti-native/diag.log").string());
    CHECK(!plugin_log_enabled());
    std::string err;
    CHECK(set_plugin_log(true, err) && plugin_log_enabled()); // creates the folder too
    CHECK(set_plugin_log(true, err));                         // again: harmless
    CHECK(set_plugin_log(false, err) && !plugin_log_enabled());
    CHECK(set_plugin_log(false, err)); // already off
    CHECK(broker_log_location().find("journalctl") != std::string::npos);
    (void)broker_log_tail(5); // the user journal: read-only, may be empty (CI)
}

// the Control Center's options (nerd mode): flag files in the plug-in's folder, read when a plug-in instance starts
TEST(diag_plugin_options)
{
    Home h("vtester");
    const auto &opts = plugin_options();
    CHECK(opts.size() == 2);
    CHECK(std::string(opts[0].name) == "outdelay-changes" && std::string(opts[0].file) == "outdelay-changes.on");
    CHECK(std::string(opts[1].name) == "timing-classic" && std::string(opts[1].file) == "timing-classic.on");
    CHECK(!plugin_option("outdelay-changes"));
    std::string err;
    CHECK(set_plugin_option("outdelay-changes", true, err) && plugin_option("outdelay-changes"));
    CHECK(fs::exists(h.dir / ".local/share/virus-ti-native/outdelay-changes.on"));
    CHECK(!plugin_option("timing-classic")); // independent
    CHECK(set_plugin_option("outdelay-changes", false, err) && !plugin_option("outdelay-changes"));
    CHECK(!set_plugin_option("no-such-option", true, err) && !err.empty());
    CHECK(!plugin_option("no-such-option"));
}

TEST(diag_tail_file)
{
    Home h("vtester");
    CHECK(tail_file((h.dir / "missing.log").string(), 10).empty());
    fs::path small = h.dir / "small.log";
    std::ofstream(small) << "one\r\ntwo\nthree\n";
    auto t = tail_file(small.string(), 2);
    CHECK(t.size() == 2 && t[0] == "two" && t[1] == "three"); // the last n, CR removed
    CHECK(tail_file(small.string(), 10).size() == 3 && tail_file(small.string(), 10)[0] == "one");
    // a long log: only its end is read, the partial first line of that window is dropped
    fs::path big = h.dir / "big.log";
    {
        std::ofstream o(big);
        for (int i = 0; i < 20000; i++)
            o << "line " << i << " " << std::string(50, 'x') << "\n";
    }
    auto b = tail_file(big.string(), 3);
    CHECK(b.size() == 3 && b[2].rfind("line 19999 ", 0) == 0 && b[0].rfind("line 19997 ", 0) == 0);
}

TEST(diag_anonymize)
{
    Home h("vtester");
    const std::string home = h.dir.string();
    std::string alt = home; // the same path with backslashes (Windows-style paths in logs)
    for (auto &c : alt)
        c = c == '/' ? '\\' : c;
    std::string in = "cache " + home + "/cache, wine " + alt + "\\drive_c, owner vtester, other text";
    std::string out = anonymize(in);
    CHECK(out.find(home) == std::string::npos && out.find(alt) == std::string::npos);
    CHECK(out.find("~/cache") != std::string::npos && out.find("~\\drive_c") != std::string::npos);
    CHECK(out.find("owner <user>") != std::string::npos && out.find("other text") != std::string::npos);
    {
        Home shortname("ab"); // a very short user name is left alone (it would hit ordinary words)
        CHECK(anonymize("about ab").find("<user>") == std::string::npos);
    }
}

TEST(diag_collect_logs)
{
    Home h("vtester");
    std::string err;
    CHECK(set_plugin_log(true, err));
    {
        std::ofstream o(plugin_log_path());
        o << "Virus TI native: cache " << h.dir.string() << "/x\n" << "Virus ready after 3.8 s\n";
    }
    fs::path out = h.dir / "report.txt";
    CHECK(collect_logs(out.string(), "0.0.1", err));
    std::ifstream f(out);
    std::string all((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    CHECK(all.find("TI Revive logs") == 0 && all.find("Control Center 0.0.1 on Linux") != std::string::npos);
    CHECK(all.find("Plug-in log ON") != std::string::npos && all.find("Virus ready after 3.8 s") != std::string::npos);
    CHECK(all.find(h.dir.string()) == std::string::npos && all.find("cache ~/x") != std::string::npos);
    CHECK(all.find("--- broker log") != std::string::npos);
    // a folder as the target: cannot be written
    err.clear();
    CHECK(!collect_logs(h.dir.string(), "0.0.1", err) && !err.empty());
}
#endif
