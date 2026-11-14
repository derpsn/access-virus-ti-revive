// diag.cpp - see diag.h.
#include "diag.h"
#include "controlcenter.h"
#include "model/utf8.h"
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <vector>

namespace vti::cc {

namespace fs = std::filesystem;

std::string plugin_dir()
{
    std::string c = cache_dir(); // <plug-in folder>/cache
    return c.empty() ? c : u8str(u8path(c).parent_path());
}

std::string plugin_log_path() { return u8str(u8path(plugin_dir()) / "diag.log"); }

static bool flag(const char *file)
{
    std::error_code ec;
    return fs::exists(u8path(plugin_dir()) / file, ec);
}

static bool set_flag(const char *file, bool on, std::string &error)
{
    fs::path f = u8path(plugin_dir()) / file;
    std::error_code ec;
    if (on) {
        fs::create_directories(f.parent_path(), ec);
        std::ofstream o(f);
        if (!o) {
            error = "cannot create " + u8str(f);
            return false;
        }
    } else if (fs::exists(f, ec) && !fs::remove(f, ec)) {
        error = "cannot remove " + u8str(f) + ": " + ec.message();
        return false;
    }
    return true;
}

bool plugin_log_enabled() { return !flag("diag.off"); }

bool set_plugin_log(bool on, std::string &error)
{
    // the switch of 0.8.40 and older goes when the log is switched off, so no file suggests it is still on
    return set_flag("diag.off", !on, error) && (on || set_flag("diag.on", false, error));
}

bool rotate_log(const std::string &path, unsigned long long limit)
{
    std::error_code ec;
    const fs::path f = u8path(path);
    const auto size = fs::file_size(f, ec);
    if (ec || size <= limit)
        return false;
    fs::path old = f;
    old += ".1";
    fs::rename(f, old, ec); // replaces an older .1 (POSIX and Windows' MoveFileEx semantics in std::filesystem)
    return !ec;
}

const std::vector<PluginOption> &plugin_options()
{
    static const std::vector<PluginOption> opts = {
        {"outdelay-changes", "outdelay-changes.on", "Output delays only on change (not at every Play/Stop)", false,
         "Output delays only on change",
         "Sends the parts' output delays only when they change, not at every Play and Stop (a test against the original)."},
        {"timing-classic", "timing-classic.on", "Timing as before 0.8.35 (lead 1024, no safety net)", false,
         "Timing as before 0.8.35", "Lead 1024 and no safety net, as before 0.8.35 (a test; notes can come late)."},
        // on by default since 0.8.39, as the original's VST3 always does it (E400, E402); the file switches it off
        {"live-notes", "live-notes.off", "Notes played live skip the lead, like the original (#30)", true, "Live notes",
         "Notes you play live skip the lead, like the original: about 22 ms instead of 76 ms at 48 kHz (#30)."},
        // on by default since 0.8.41: restart the Virus' audio once when it arrives byte-slipped (slipwatch.h, E488)
        // macOS too since 0.8.41-rc6 (#37: 9 of 22 starts slipped on a Catalina Mac, E510)
        {"slip-restart", "slip-restart.off", "Restart the Virus' audio when it arrives scrambled", true,
         "Restart scrambled audio",
         "When the Virus' audio arrives scrambled (crackle, notes off time), restart it. On a Mac this helps when the "
         "DAW does not use the Virus as its own audio device."},
        // off by default (0.8.41, #30): the lead per host block measured exact on the TI2 (stamprule.h, E496)
        {"low-latency", "low-latency.on", "Lower latency, experimental (#30)", false, "Lower latency (experimental)",
         "About 36-48 ms instead of 76-87 ms at buffers up to 256 (55-59 ms at 512, unchanged above). It leaves "
         "the DAW only about 20 ms: if notes come late, switch it off."},
        // on by default since 0.8.41 (user 2026-11-10): the plug-in's log for bug reports; diag.off switches it off
        {"plugin-log", "diag.off", "Plug-in log for bug reports (stays on this computer)", true, "Plug-in log",
         "Writes what the plug-in does to diag.log on this computer, for bug reports. Nothing is sent anywhere."},
        // off by default (0.8.41, E519): played notes a little later, so the Virus always meets their stamp
        {"live-later", "live-later.on", "Played notes a little later, for an even rhythm (E519)", false,
         "Even rhythm for played notes",
         "Notes you play live go about 3 ms later, so the Virus plays them exactly in your rhythm. Off: they are "
         "stamped like the original's, and one can come 1-3 ms late now and then."},
    };
    return opts;
}

static const PluginOption *find_option(const std::string &name)
{
    for (auto &o : plugin_options())
        if (name == o.name)
            return &o;
    return nullptr;
}

bool plugin_option(const std::string &name)
{
    const PluginOption *o = find_option(name);
    return o && (o->default_on ? !flag(o->file) : flag(o->file));
}

bool set_plugin_option(const std::string &name, bool on, std::string &error)
{
    const PluginOption *o = find_option(name);
    if (!o) {
        error = "unknown option " + name;
        return false;
    }
    return set_flag(o->file, o->default_on ? !on : on, error);
}

#if defined(_WIN32) || defined(__APPLE__) // Linux: the journal
static std::string broker_log_file()
{
#ifdef _WIN32
    const std::string t = u8env("TEMP");
    return t.empty() ? std::string() : t + "\\vtibroker.log";
#else
    return "/tmp/tirevive-vtibroker.log";
#endif
}
#endif

std::string broker_log_location()
{
#if defined(_WIN32) || defined(__APPLE__)
    return broker_log_file();
#else
    return "journalctl --user -u tirevive-broker";
#endif
}

std::vector<std::string> tail_file(const std::string &path, size_t n)
{
    std::ifstream f(u8path(path), std::ios::binary);
    if (!f)
        return {};
    // only the end of a long log: diag.log can grow to many MB
    f.seekg(0, std::ios::end);
    std::streamoff size = f.tellg(), from = std::max<std::streamoff>(0, size - (std::streamoff)(n * 400 + 4096));
    f.seekg(from);
    std::deque<std::string> lines;
    std::string l;
    if (from > 0)
        std::getline(f, l); // a partial first line
    while (std::getline(f, l)) {
        if (!l.empty() && l.back() == '\r')
            l.pop_back();
        lines.push_back(l);
        if (lines.size() > n)
            lines.pop_front();
    }
    return {lines.begin(), lines.end()};
}

std::vector<std::string> broker_log_tail(size_t n)
{
#if defined(_WIN32) || defined(__APPLE__)
    return tail_file(broker_log_file(), n);
#else
    std::string cmd = "journalctl --user -u tirevive-broker -o cat --no-pager -n " + std::to_string(n) + " 2>/dev/null";
    std::vector<std::string> lines;
    if (FILE *p = popen(cmd.c_str(), "r")) {
        char buf[1024];
        while (fgets(buf, sizeof buf, p)) {
            std::string l(buf);
            while (!l.empty() && (l.back() == '\n' || l.back() == '\r'))
                l.pop_back();
            lines.push_back(l);
        }
        pclose(p);
    }
    return lines;
#endif
}

static void replace_all(std::string &s, const std::string &from, const std::string &to)
{
    if (from.empty())
        return;
    for (size_t i = s.find(from); i != std::string::npos; i = s.find(from, i + to.size()))
        s.replace(i, from.size(), to);
}

std::string anonymize(const std::string &text)
{
    std::string s = text;
    std::vector<std::string> homes, users;
#ifdef _WIN32
    // both spellings: TI Revive's own lines are UTF-8, the broker (C) writes the ANSI code page
    homes = {u8env("USERPROFILE"), getenv("USERPROFILE") ? getenv("USERPROFILE") : ""};
    users = {u8env("USERNAME"), getenv("USERNAME") ? getenv("USERNAME") : ""};
#else
    homes = {u8env("HOME")};
    users = {u8env("USER")};
#endif
    for (const std::string &home : homes) {
        if (home.size() <= 1)
            continue;
        replace_all(s, home, "~");
        std::string alt = home; // the same path with the other kind of slashes
        for (auto &c : alt)
            c = c == '\\' ? '/' : c == '/' ? '\\' : c;
        replace_all(s, alt, "~");
    }
    for (const std::string &user : users)
        if (user.size() >= 3) // a very short name would also hit ordinary words
            replace_all(s, user, "<user>");
    return s;
}

bool collect_logs(const std::string &out_path, const std::string &version, std::string &error)
{
    std::string t = "TI Revive logs\n==============\n";
    char stamp[32];
    time_t now = time(nullptr);
    strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", localtime(&now));
#if defined(_WIN32)
    const char *os = "Windows";
#elif defined(__APPLE__)
    const char *os = "macOS";
#else
    const char *os = "Linux";
#endif
    t += std::string("Collected ") + stamp + " by TI Revive Control Center " + version + " on " + os + "\n";
    t += std::string("Plug-in log ") +
         (plugin_log_enabled() ? "ON" : "OFF (switch it on on the Options page, then reproduce)") + "\n\n";
    t += "--- plug-in diag.log (last 2000 lines) ---\n";
    std::vector<std::string> lines = tail_file(plugin_log_path(), 2000);
    if (lines.size() < 2000) { // just rotated: the end of the older file first
        std::vector<std::string> older = tail_file(plugin_log_path() + ".1", 2000 - lines.size());
        lines.insert(lines.begin(), older.begin(), older.end());
    }
    for (auto &l : lines)
        t += l + "\n";
    t += "\n--- broker log (" + broker_log_location() + ", last 1000 lines) ---\n";
    for (auto &l : broker_log_tail(1000))
        t += l + "\n";
    t = anonymize(t);
    std::ofstream o(u8path(out_path), std::ios::binary);
    o << t;
    if (!o) {
        error = "cannot write " + out_path;
        return false;
    }
    return true;
}

} // namespace vti::cc
