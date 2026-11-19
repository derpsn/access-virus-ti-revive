// updatecheck.cpp - see updatecheck.h
#include "updatecheck.h"
#include "diag.h"
#include "model/utf8.h"
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX // std::min below, not the windows.h macro
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif

namespace vti::cc {

namespace {
const char *kApiUrl = "https://api.github.com/repos/derpsn/ti-revive/releases/latest";

// "x.y.z" or "x.y.z-rcN": the three numbers and the rc number (0 = final). False for anything else.
bool parse_version(const std::string &v, long n[3], long &rc)
{
    size_t i = 0;
    for (int k = 0; k < 3; k++) {
        if (i >= v.size() || v[i] < '0' || v[i] > '9')
            return false;
        long x = 0;
        while (i < v.size() && v[i] >= '0' && v[i] <= '9' && x < 100000)
            x = x * 10 + (v[i++] - '0');
        n[k] = x;
        if (k < 2) {
            if (i >= v.size() || v[i] != '.')
                return false;
            i++;
        }
    }
    rc = 0;
    if (i == v.size())
        return true;
    if (v.compare(i, 3, "-rc") != 0 || i + 3 >= v.size())
        return false;
    for (i += 3; i < v.size(); i++) {
        if (v[i] < '0' || v[i] > '9' || rc > 100000)
            return false;
        rc = rc * 10 + (v[i] - '0');
    }
    return rc > 0;
}

bool final_version(const std::string &v)
{
    long n[3], rc;
    return parse_version(v, n, rc) && rc == 0;
}

// the value of "key": "..." (a string) or "key": true/false in a flat JSON object; no full parser needed for the
// three fields we read, and nothing from the answer is ever run or shown unchecked
bool json_field(const std::string &json, const char *key, std::string &value)
{
    const std::string k = std::string("\"") + key + "\"";
    size_t p = json.find(k);
    if (p == std::string::npos)
        return false;
    p = json.find_first_not_of(" \t\r\n", p + k.size());
    if (p == std::string::npos || json[p] != ':')
        return false;
    p = json.find_first_not_of(" \t\r\n", p + 1);
    if (p == std::string::npos)
        return false;
    if (json[p] == '"') {
        const size_t e = json.find('"', p + 1);
        if (e == std::string::npos)
            return false;
        value = json.substr(p + 1, e - p - 1);
        return true;
    }
    const size_t e = json.find_first_of(",} \t\r\n", p);
    value = json.substr(p, e == std::string::npos ? std::string::npos : e - p);
    return true;
}

std::string url_encode(const std::string &s)
{
    static const char *hex = "0123456789ABCDEF";
    std::string o;
    for (unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.' || c == '~') {
            o += (char)c;
        } else {
            o += '%';
            o += hex[c >> 4];
            o += hex[c & 15];
        }
    }
    return o;
}
} // namespace

bool latest_from_release_json(const std::string &json, std::string &version)
{
    std::string tag, flag;
    if (!json_field(json, "tag_name", tag) || tag.size() < 2 || tag[0] != 'v')
        return false;
    if (json_field(json, "prerelease", flag) && flag == "true")
        return false;
    if (json_field(json, "draft", flag) && flag == "true")
        return false;
    const std::string v = tag.substr(1);
    if (!final_version(v))
        return false;
    version = v;
    return true;
}

bool version_newer(const std::string &a, const std::string &b)
{
    long na[3], nb[3], ra, rb;
    if (!parse_version(a, na, ra))
        return false;
    if (!parse_version(b, nb, rb))
        return true;
    for (int k = 0; k < 3; k++)
        if (na[k] != nb[k])
            return na[k] > nb[k];
    if (ra == rb)
        return false;
    return ra == 0 || (rb != 0 && ra > rb); // the final after its rcs
}

std::string format_update_state(const UpdateState &s)
{
    std::ostringstream o;
    o << "checked " << s.checked << "\n";
    if (!s.latest.empty())
        o << "latest " << s.latest << "\n";
    if (!s.seen.empty())
        o << "seen " << s.seen << "\n";
    return o.str();
}

bool parse_update_state(const std::string &text, UpdateState &s)
{
    s = UpdateState();
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const size_t sp = line.find(' ');
        if (sp == std::string::npos)
            continue;
        const std::string key = line.substr(0, sp), val = line.substr(sp + 1);
        if (key == "checked") {
            char *end = nullptr;
            const long long t = strtoll(val.c_str(), &end, 10);
            if (end && *end == 0 && t >= 0)
                s.checked = t;
        } else if (key == "latest" && final_version(val)) {
            s.latest = val;
        } else if (key == "seen" && final_version(val)) {
            s.seen = val;
        }
    }
    return true;
}

bool check_due(long long now, const UpdateState &s)
{
    return s.checked <= 0 || now < s.checked || now - s.checked >= 86400;
}

std::string update_available(const std::string &current, const UpdateState &s)
{
    return !s.latest.empty() && version_newer(s.latest, current) ? s.latest : std::string();
}

bool show_update_note(const std::string &current, const UpdateState &s)
{
    const std::string v = update_available(current, s);
    return !v.empty() && v != s.seen;
}

std::string bug_report_url(const std::string &version, const std::string &os)
{
    return "https://github.com/derpsn/ti-revive/issues/new?template=bug_report.yml&versions=" +
           url_encode("TI Revive " + version + ", " + os);
}

const char *os_family()
{
#if defined(_WIN32)
    return "Windows";
#elif defined(__APPLE__)
    return "macOS";
#else
    return "Linux";
#endif
}

std::string update_state_path() { return u8str(u8path(plugin_dir()) / "update.txt"); }

UpdateState read_update_state()
{
    UpdateState s;
    std::ifstream f(u8path(update_state_path()), std::ios::binary);
    if (f) {
        std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (text.size() < 4096)
            parse_update_state(text, s);
    }
    return s;
}

bool write_update_state(const UpdateState &s)
{
    std::error_code ec;
    std::filesystem::create_directories(u8path(plugin_dir()), ec);
    const std::filesystem::path f = u8path(update_state_path()), tmp = u8path(update_state_path() + ".tmp");
    {
        std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
        if (!o)
            return false;
        o << format_update_state(s);
        if (!o)
            return false;
    }
    std::filesystem::rename(tmp, f, ec); // whole or not at all: the plug-in may read it at the same moment
    return !ec;
}

namespace {
// Runs a program (no shell) and returns its standard output, at most `cap` bytes. `ok` = it ran and exited with 0.
#ifdef _WIN32
bool run_capture(const std::wstring &exe, std::wstring cmdline, std::string &out, size_t cap)
{
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0))
        return false;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = wr;
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};
    const BOOL started = CreateProcessW(exe.c_str(), &cmdline[0], nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                        nullptr, &si, &pi);
    CloseHandle(wr);
    if (!started) {
        CloseHandle(rd);
        return false;
    }
    char buf[4096];
    DWORD n = 0;
    while (ReadFile(rd, buf, sizeof buf, &n, nullptr) && n > 0)
        if (out.size() < cap)
            out.append(buf, std::min<size_t>(n, cap - out.size()));
    CloseHandle(rd);
    WaitForSingleObject(pi.hProcess, 30000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return code == 0;
}
#else
bool run_capture(const std::vector<std::string> &args, std::string &out, size_t cap, bool &missing)
{
    int fd[2];
    if (pipe(fd) != 0)
        return false;
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, fd[1], 1);
    posix_spawn_file_actions_addclose(&fa, fd[0]);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    std::vector<char *> argv;
    for (auto &a : args)
        argv.push_back(const_cast<char *>(a.c_str()));
    argv.push_back(nullptr);
    pid_t pid = 0;
    const int e = posix_spawnp(&pid, argv[0], &fa, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    close(fd[1]);
    if (e != 0) {
        close(fd[0]);
        missing = e == ENOENT;
        return false;
    }
    char buf[4096];
    ssize_t n;
    while ((n = read(fd[0], buf, sizeof buf)) > 0 || (n < 0 && errno == EINTR))
        if (n > 0 && out.size() < cap)
            out.append(buf, std::min<size_t>((size_t)n, cap - out.size()));
    close(fd[0]);
    int st = 0;
    while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {
    }
    if (WIFEXITED(st) && WEXITSTATUS(st) == 127) // posix_spawnp on glibc reports a missing program this way
        missing = true;
    return WIFEXITED(st) && WEXITSTATUS(st) == 0;
}
#endif
} // namespace

bool run_update_check(UpdateState &s, std::string &error)
{
    std::string json;
    const size_t cap = 256 * 1024; // the answer is a few KB; never more than this into memory
    bool ok = false;
#ifdef _WIN32
    // curl.exe is part of Windows 10 1803 and later and of Windows 11
    wchar_t sys[MAX_PATH];
    const UINT n = GetSystemDirectoryW(sys, MAX_PATH);
    const std::wstring curl = std::wstring(sys, n) + L"\\curl.exe";
    ok = n > 0 && run_capture(curl, L"curl.exe -fsSL --max-time 20 -H \"Accept: application/vnd.github+json\" " +
                                        widen(kApiUrl), json, cap);
    if (!ok)
        error = GetFileAttributesW(curl.c_str()) == INVALID_FILE_ATTRIBUTES ? "curl.exe not found" : "no answer";
#else
    bool missing = false;
    ok = run_capture({"curl", "-fsSL", "--max-time", "20", "-H", "Accept: application/vnd.github+json", kApiUrl}, json,
                     cap, missing);
#ifndef __APPLE__ // macOS always has curl; some Linux desktops have only wget
    if (!ok && missing) {
        json.clear();
        missing = false;
        ok = run_capture({"wget", "-q", "-T", "20", "-O", "-", "--header=Accept: application/vnd.github+json", kApiUrl},
                         json, cap, missing);
        if (!ok && missing)
            error = "neither curl nor wget found";
    }
#endif
    if (!ok && error.empty())
        error = "no answer";
#endif
    if (!ok)
        return false;
    std::string v;
    if (!latest_from_release_json(json, v)) {
        error = "an answer without a release";
        return false;
    }
    s.checked = (long long)time(nullptr);
    s.latest = v;
    return true;
}

void start_update_check_if_due(bool now)
{
    if (!plugin_option("update-check") || (!now && !check_due((long long)time(nullptr), read_update_state())))
        return;
    std::thread([] {
        UpdateState s = read_update_state();
        std::string err;
        if (run_update_check(s, err))
            write_update_state(s);
        // no answer (offline, no curl): nothing written, so the next start of the Control Center tries again
    }).detach();
}

} // namespace vti::cc
