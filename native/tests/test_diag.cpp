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
    // on by default since 0.8.41 (user 2026-11-10: reports without the log kept coming); diag.off switches it off
    CHECK(plugin_log_enabled());
    std::string err;
    CHECK(set_plugin_log(false, err) && !plugin_log_enabled()); // creates the folder too
    CHECK(fs::exists(h.dir / ".local/share/virus-ti-native/diag.off"));
    CHECK(set_plugin_log(false, err)); // again: harmless
    CHECK(set_plugin_log(true, err) && plugin_log_enabled());
    CHECK(!fs::exists(h.dir / ".local/share/virus-ti-native/diag.off"));
    CHECK(set_plugin_log(true, err)); // already on
    // an old diag.on (the switch before 0.8.41) changes nothing, and switching off removes it
    std::ofstream(h.dir / ".local/share/virus-ti-native/diag.on").put('\n');
    CHECK(plugin_log_enabled());
    CHECK(set_plugin_log(false, err) && !plugin_log_enabled());
    CHECK(!fs::exists(h.dir / ".local/share/virus-ti-native/diag.on"));
    CHECK(set_plugin_log(true, err));
    CHECK(broker_log_location().find("journalctl") != std::string::npos);
    (void)broker_log_tail(5); // the user journal: read-only, may be empty (CI)
}

// the Control Center's options (nerd mode): flag files in the plug-in's folder, read when a plug-in instance starts
TEST(diag_plugin_options)
{
    Home h("vtester");
    const auto &opts = plugin_options();
    CHECK(opts.size() == 6);
    // plugin-log (0.8.41): the plug-in's log for bug reports, ON by default; diag.off switches it off, so the plug-in's
    // own Options page can do it (the user is told on What's New and the welcome page)
    CHECK(std::string(opts[5].name) == "plugin-log" && std::string(opts[5].file) == "diag.off");
    CHECK(opts[5].default_on && plugin_option("plugin-log") == plugin_log_enabled());
    // low-latency (E496, 0.8.41): the experimental lower lead, OFF by default; low-latency.on switches it on
    CHECK(std::string(opts[4].name) == "low-latency" && std::string(opts[4].file) == "low-latency.on");
    CHECK(!opts[4].default_on);
    CHECK(!plugin_option("low-latency"));
    // the Control Center's Options page (0.8.41) shows a title and a one-line explanation per option
    for (auto &o : opts)
        CHECK(o.title && *o.title && o.hint && *o.hint);
    // slip-restart (E488, 0.8.41): on by default, the Control Center switch writes slip-restart.off
    CHECK(std::string(opts[3].name) == "slip-restart" && std::string(opts[3].file) == "slip-restart.off");
    CHECK(opts[3].default_on);
    CHECK(plugin_option("slip-restart"));
    CHECK(std::string(opts[0].name) == "outdelay-changes" && std::string(opts[0].file) == "outdelay-changes.on");
    CHECK(std::string(opts[1].name) == "timing-classic" && std::string(opts[1].file) == "timing-classic.on");
    // live-notes is on by default since 0.8.39 (the original's VST3 does it always, E400/E402; user 2026-11-08): its
    // file switches it OFF
    CHECK(std::string(opts[2].name) == "live-notes" && std::string(opts[2].file) == "live-notes.off");
    CHECK(opts[2].default_on && !opts[0].default_on && !opts[1].default_on);
    CHECK(plugin_option("live-notes")); // on without any file
    std::string e0;
    CHECK(set_plugin_option("live-notes", false, e0) && !plugin_option("live-notes"));
    CHECK(fs::exists(h.dir / ".local/share/virus-ti-native/live-notes.off"));
    CHECK(set_plugin_option("live-notes", true, e0) && plugin_option("live-notes"));
    CHECK(!fs::exists(h.dir / ".local/share/virus-ti-native/live-notes.off"));
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

// the log is on for everyone since 0.8.41, so it must not grow without end: over the limit diag.log becomes diag.log.1
// (an older .1 is replaced), at most about two limits on disk
TEST(diag_rotate_log)
{
    Home h("vtester");
    fs::path log = h.dir / "diag.log", old = h.dir / "diag.log.1";
    CHECK(!rotate_log(log.string(), 100)); // missing: nothing to do
    std::ofstream(log) << std::string(100, 'a');
    CHECK(!rotate_log(log.string(), 100) && fs::exists(log)); // at the limit: kept
    std::ofstream(log, std::ios::app) << "b";
    CHECK(rotate_log(log.string(), 100) && !fs::exists(log) && fs::file_size(old) == 101);
    std::ofstream(log) << std::string(150, 'c');
    CHECK(rotate_log(log.string(), 100) && fs::file_size(old) == 150); // the older .1 is replaced
    CHECK(kPluginLogLimit >= 1000000 && kPluginLogLimit <= 8000000);
}

TEST(diag_collect_logs)
{
    Home h("vtester");
    std::string err;
    CHECK(set_plugin_log(true, err));
    fs::create_directories(plugin_dir()); // "on" writes no file since 0.8.41; the plug-in makes its folder itself
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
    // right after a rotation diag.log is short: the end of diag.log.1 comes first, so the report keeps its context
    {
        std::ofstream(plugin_log_path() + ".1") << "older line\n";
        std::ofstream(plugin_log_path()) << "newer line\n";
        CHECK(collect_logs(out.string(), "0.0.1", err));
        std::ifstream g(out);
        std::string r((std::istreambuf_iterator<char>(g)), std::istreambuf_iterator<char>());
        const size_t a = r.find("older line"), b = r.find("newer line");
        CHECK(a != std::string::npos && b != std::string::npos && a < b);
    }
    // switched off: the header says how to switch it on again
    CHECK(set_plugin_log(false, err) && collect_logs(out.string(), "0.0.1", err));
    {
        std::ifstream g(out);
        std::string r((std::istreambuf_iterator<char>(g)), std::istreambuf_iterator<char>());
        CHECK(r.find("Plug-in log OFF (switch it on on the Options page") != std::string::npos);
    }
    // a folder as the target: cannot be written
    err.clear();
    CHECK(!collect_logs(h.dir.string(), "0.0.1", err) && !err.empty());
}

// a flag file that cannot be written or removed (here: a folder with something in it sits at its name) is reported,
// so the Control Center does not show a switch as changed when it is not
TEST(diag_flag_file_errors)
{
    Home h("vtester");
    const fs::path f = fs::path(plugin_dir()) / "outdelay-changes.on";
    fs::create_directories(f);
    std::ofstream(f / "inside").put('\n');
    std::string err;
    CHECK(!set_plugin_option("outdelay-changes", true, err) && !err.empty());
    err.clear();
    CHECK(!set_plugin_option("outdelay-changes", false, err) && !err.empty());
    CHECK(fs::exists(f / "inside"));
}

// the boundaries of anonymize(): a home folder of "/" (e.g. a service account) is not replaced, or every slash in the
// report would become "~"; a user name of 3 characters is replaced (only 1-2 are too short)
TEST(diag_anonymize_boundaries)
{
    Home h("bob");
    CHECK(anonymize("owner bob") == "owner <user>");
    setenv("HOME", "/", 1);
    CHECK(anonymize("/usr/lib/x") == "/usr/lib/x");
    setenv("HOME", h.dir.string().c_str(), 1);
}

#ifndef __APPLE__
// Linux reads the broker's log from the user journal: each line arrives with its line end (LF, or CR LF), which goes,
// and nothing else of the line; the last line may come without one. A stand-in journalctl in PATH prints known lines.
TEST(diag_broker_log_tail_line_ends)
{
    Home h("vtester");
    const fs::path bin = h.dir / "bin";
    fs::create_directories(bin);
    {
        std::ofstream o(bin / "journalctl");
        o << "#!/bin/sh\nprintf 'one\\r\\ntwo\\n\\nlast'\n";
    }
    fs::permissions(bin / "journalctl", fs::perms::owner_all);
    const char *p = getenv("PATH");
    const std::string old_path = p ? p : "";
    setenv("PATH", bin.string().c_str(), 1);
    const std::vector<std::string> lines = broker_log_tail(10);
    p ? setenv("PATH", old_path.c_str(), 1) : unsetenv("PATH");
    CHECK((lines == std::vector<std::string>{"one", "two", "", "last"}));
}
#endif
#endif
