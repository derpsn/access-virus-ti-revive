// diag.cpp - see diag.h.
#include "diag.h"
#include "controlcenter.h"
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>

namespace vti::cc {

namespace fs = std::filesystem;

std::string plugin_dir()
{
    std::string c = cache_dir(); // <plug-in folder>/cache
    return c.empty() ? c : fs::path(c).parent_path().string();
}

std::string plugin_log_path() { return (fs::path(plugin_dir()) / "diag.log").string(); }

static bool flag(const char *file)
{
    std::error_code ec;
    return fs::exists(fs::path(plugin_dir()) / file, ec);
}

static bool set_flag(const char *file, bool on, std::string &error)
{
    fs::path f = fs::path(plugin_dir()) / file;
    std::error_code ec;
    if (on) {
        fs::create_directories(f.parent_path(), ec);
        std::ofstream o(f);
        if (!o) {
            error = "cannot create " + f.string();
            return false;
        }
    } else if (fs::exists(f, ec) && !fs::remove(f, ec)) {
        error = "cannot remove " + f.string() + ": " + ec.message();
        return false;
    }
    return true;
}

bool plugin_log_enabled() { return flag("diag.on"); }

bool set_plugin_log(bool on, std::string &error) { return set_flag("diag.on", on, error); }

const std::vector<PluginOption> &plugin_options()
{
    static const std::vector<PluginOption> opts = {
        {"outdelay-changes", "outdelay-changes.on", "Output delays only on change (not at every Play/Stop)"},
        {"timing-classic", "timing-classic.on", "Timing as before 0.8.35 (lead 1024, no safety net)"},
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
    return o && flag(o->file);
}

bool set_plugin_option(const std::string &name, bool on, std::string &error)
{
    const PluginOption *o = find_option(name);
    if (!o) {
        error = "unknown option " + name;
        return false;
    }
    return set_flag(o->file, on, error);
}

#if defined(_WIN32) || defined(__APPLE__) // Linux: the journal
static std::string broker_log_file()
{
#ifdef _WIN32
    const char *t = getenv("TEMP");
    return t ? std::string(t) + "\\vtibroker.log" : std::string();
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
    std::ifstream f(path, std::ios::binary);
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
    std::string s = text, home, user;
#ifdef _WIN32
    if (const char *h = getenv("USERPROFILE"))
        home = h;
    if (const char *u = getenv("USERNAME"))
        user = u;
#else
    if (const char *h = getenv("HOME"))
        home = h;
    if (const char *u = getenv("USER"))
        user = u;
#endif
    if (home.size() > 1) {
        replace_all(s, home, "~");
        std::string alt = home; // the same path with the other kind of slashes
        for (auto &c : alt)
            c = c == '\\' ? '/' : c == '/' ? '\\' : c;
        replace_all(s, alt, "~");
    }
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
    t += std::string("Plug-in log ") + (plugin_log_enabled() ? "ON" : "OFF (create diag.on, then reproduce)") + "\n\n";
    t += "--- plug-in diag.log (last 2000 lines) ---\n";
    for (auto &l : tail_file(plugin_log_path(), 2000))
        t += l + "\n";
    t += "\n--- broker log (" + broker_log_location() + ", last 1000 lines) ---\n";
    for (auto &l : broker_log_tail(1000))
        t += l + "\n";
    t = anonymize(t);
    std::ofstream o(out_path, std::ios::binary);
    o << t;
    if (!o) {
        error = "cannot write " + out_path;
        return false;
    }
    return true;
}

} // namespace vti::cc
