// broker.cpp - see broker.h (POSIX: Unix socket; Windows: the broker's named pipe, overlapped I/O).
#include "broker.h"
extern "C" {
#include "vtiproto.h"
}
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif
#ifdef __APPLE__
#include <crt_externs.h>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <thread>
#define environ (*_NSGetEnviron())
#endif

namespace vti {

Broker::~Broker() { close(); }

#ifdef _WIN32
// \\.\pipe\vtibroker (byte mode), overlapped: the reader thread and writers use the handle at the same time
static bool io_all(intptr_t fd, void *p, size_t n, bool wr)
{
    HANDLE h = (HANDLE)fd;
    OVERLAPPED ov{};
    ov.hEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    bool ok = true;
    for (auto *b = (uint8_t *)p; n && ok;) {
        DWORD done = 0;
        ResetEvent(ov.hEvent);
        BOOL r = wr ? WriteFile(h, b, (DWORD)n, nullptr, &ov) : ReadFile(h, b, (DWORD)n, nullptr, &ov);
        if (!r && GetLastError() != ERROR_IO_PENDING)
            ok = false;
        else if (!GetOverlappedResult(h, &ov, &done, TRUE) || done == 0)
            ok = false;
        b += done;
        n -= done;
    }
    CloseHandle(ov.hEvent);
    return ok;
}

// vtibroker.exe like TI Revive's own clients (vticore_pipe.c): $VTI_BROKER, else InstallDir from
// HKLM\SOFTWARE\TI Revive (written by the MSI), else %ProgramW6432%\TI Revive
static bool broker_path(std::wstring &path)
{
    wchar_t buf[MAX_PATH + 32];
    DWORD n = GetEnvironmentVariableW(L"VTI_BROKER", buf, MAX_PATH);
    if (n && n < MAX_PATH && GetFileAttributesW(buf) != INVALID_FILE_ATTRIBUTES)
        return path = buf, true;
    wchar_t dir[MAX_PATH];
    DWORD sz = sizeof dir;
    if (!RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\TI Revive", L"InstallDir", RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY,
                      nullptr, dir, &sz)) {
        std::wstring d = dir;
        if (!d.empty() && d.back() != L'\\')
            d += L'\\';
        if (GetFileAttributesW((d + L"vtibroker.exe").c_str()) != INVALID_FILE_ATTRIBUTES)
            return path = d + L"vtibroker.exe", true;
    }
    n = GetEnvironmentVariableW(L"ProgramW6432", dir, MAX_PATH);
    if (n && n < MAX_PATH) {
        std::wstring p = std::wstring(dir) + L"\\TI Revive\\vtibroker.exe";
        if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES)
            return path = p, true;
    }
    return false;
}

// the broker is not started at logon: the first client that finds no pipe starts it, detached from the DAW (a DAW
// may run plug-ins in a job object that is killed with it); it exits by itself when idle
static bool start_broker()
{
    std::wstring path;
    if (!broker_path(path))
        return false;
    std::wstring cmd = L"\"" + path + L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    DWORD flags = DETACHED_PROCESS | CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP;
    if (!CreateProcessW(path.c_str(), &cmd[0], nullptr, nullptr, FALSE, flags | CREATE_BREAKAWAY_FROM_JOB, nullptr,
                        nullptr, &si, &pi) &&
        !CreateProcessW(path.c_str(), &cmd[0], nullptr, nullptr, FALSE, flags, nullptr, nullptr, &si, &pi))
        return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

static intptr_t os_connect(const std::string &endpoint, std::string &error)
{
    (void)endpoint; // the broker's named pipe only
    HANDLE h = INVALID_HANDLE_VALUE;
    bool started = false;
    for (int i = 0; i < 40 && h == INVALID_HANDLE_VALUE; i++) { // up to ~4 s for a starting or busy broker
        h = CreateFileA(VTI_PIPE_NAME, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED,
                        nullptr);
        if (h != INVALID_HANDLE_VALUE)
            break;
        DWORD e = GetLastError();
        if (e == ERROR_PIPE_BUSY)
            WaitNamedPipeA(VTI_PIPE_NAME, 100);
        else if (e == ERROR_FILE_NOT_FOUND && !started) {
            if (!(started = start_broker())) {
                error = "vtibroker.exe not found (TI Revive not installed?)";
                return -1;
            }
            Sleep(100);
        } else if (e == ERROR_FILE_NOT_FOUND)
            Sleep(100);
        else
            break;
    }
    if (h == INVALID_HANDLE_VALUE) {
        error = "cannot connect to the TI Revive broker (" VTI_PIPE_NAME ", error " + std::to_string(GetLastError()) + ")";
        return -1;
    }
    return (intptr_t)h;
}

// close in two steps: wake the reader (os_wake), and release the handle only after it has stopped (os_release),
// so it never reads from a handle/fd number that has been reused
static void os_wake(intptr_t fd)
{
    CancelIoEx((HANDLE)fd, nullptr);
}

static void os_release(intptr_t fd)
{
    CloseHandle((HANDLE)fd);
}
#else
static bool io_all(intptr_t fd, void *p, size_t n, bool wr)
{
    for (auto *b = (uint8_t *)p; n;) {
        ssize_t r = wr ? ::send((int)fd, b, n, MSG_NOSIGNAL) : ::recv((int)fd, b, n, 0);
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0)
            return false;
        b += r;
        n -= (size_t)r;
    }
    return true;
}

#ifdef __APPLE__
// macOS has no socket activation: like TI Revive's libVirusUSB, start the installed broker when nobody answers. Started
// from the DAW's process tree it runs in the user's GUI session; no inherited descriptors (it must not keep our sockets
// alive); it exits by itself 30 s after its last client.
static bool start_broker()
{
    const char *path = getenv("TIREVIVE_BROKER");
    if (!path || !*path)
        path = "/Library/Application Support/TI Revive/vtibroker";
    if (access(path, X_OK))
        return false;
    posix_spawnattr_t at;
    posix_spawnattr_init(&at);
    posix_spawnattr_setflags(&at, POSIX_SPAWN_CLOEXEC_DEFAULT | POSIX_SPAWN_SETSID);
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 2, "/tmp/tirevive-vtibroker.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
    pid_t pid;
    char *argv[] = {(char *)path, nullptr};
    int e = posix_spawn(&pid, path, &fa, &at, argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    posix_spawnattr_destroy(&at);
    if (e)
        return false;
    std::thread([pid] { // reap it when it exits (no zombie in the DAW's process tree)
        int st;
        waitpid(pid, &st, 0);
    }).detach();
    return true;
}
#endif

static intptr_t os_connect(const std::string &endpoint, std::string &error)
{
    sockaddr_un sa{};
    sa.sun_family = AF_UNIX;
    if (endpoint.empty())
        snprintf(sa.sun_path, sizeof sa.sun_path, "/tmp/vtibroker-%u.sock", (unsigned)getuid());
    else if (endpoint.size() < sizeof sa.sun_path)
        memcpy(sa.sun_path, endpoint.c_str(), endpoint.size() + 1);
    else {
        error = "socket path too long: " + endpoint;
        return -1;
    }
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    bool ok = fd >= 0 && !::connect(fd, (sockaddr *)&sa, sizeof sa);
#ifdef __APPLE__
    if (fd >= 0 && !ok && endpoint.empty() && start_broker()) // never start the real broker for another endpoint
        for (int i = 0; i < 40 && !ok; i++) { // up to ~4 s for the broker to listen
            usleep(100000);
            ::close(fd);
            fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
            ok = fd >= 0 && !::connect(fd, (sockaddr *)&sa, sizeof sa);
        }
#endif
    if (!ok) {
        error = std::string("cannot connect to the TI Revive broker at ") + sa.sun_path + ": " + strerror(errno);
        if (fd >= 0)
            ::close(fd);
        return -1;
    }
    // the socket lives in /tmp: only a broker of this user is talked to - another user could have made the path
    // first and feed the plug-in what it likes (review 2026-10-19 H4)
    uid_t peer = (uid_t)-1;
#ifdef __APPLE__
    gid_t gid;
    if (getpeereid(fd, &peer, &gid) != 0)
        peer = (uid_t)-1;
#else
    ucred cr{};
    socklen_t cl = sizeof cr;
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cr, &cl) == 0)
        peer = cr.uid;
#endif
    if (peer != getuid()) {
        error = std::string("the TI Revive broker socket ") + sa.sun_path + " belongs to another user";
        ::close(fd);
        return -1;
    }
    return fd;
}

static void os_wake(intptr_t fd)
{
    ::shutdown((int)fd, SHUT_RDWR);
}

static void os_release(intptr_t fd)
{
    ::close((int)fd);
}
#endif

bool Broker::open(std::string &error)
{
    {
        std::lock_guard<std::mutex> g(wr_); // senders on other threads see the handle whole (review 2026-10-19 M7)
        fd_ = os_connect(endpoint_, error);
    }
    if (fd_ < 0)
        return false;
    uint32_t ver = VTI_PROTO_VERSION;
    send(VTIMSG_HELLO, &ver, 4, name_.data(), name_.size()); // older brokers read only the version
    stop_ = false;
    reader_done_ = false;
    reader_ = std::thread([this] {
        read_loop();
        reader_done_ = true;
    });
    for (int i = 0; i < 100 && !present_; i++) // STATE arrives right after connecting
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    return true;
}

void Broker::close()
{
    stop_ = true;
    if (fd_ >= 0 && reader_.joinable())
        while (!reader_done_) { // repeated: on Windows a cancel does not stop a read that starts just after it
            os_wake(fd_);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    if (reader_.joinable())
        reader_.join();
    // under the write lock: a control() or write() of another thread (sync thread, editor) never writes to a released
    // or reused handle (review 2026-10-19 M7); woken first, so a write stuck on a broker that stopped reading fails
    // instead of holding the lock
    if (fd_ >= 0)
        os_wake(fd_);
    std::lock_guard<std::mutex> g(wr_);
    if (fd_ >= 0) {
        os_release(fd_);
        fd_ = -1;
    }
}

bool Broker::send(uint16_t type, const void *a, size_t alen, const void *b, size_t blen)
{
    if (alen + blen > VTIMSG_MAX)
        return false;
    vtimsg h{type, (uint16_t)(alen + blen)};
    std::lock_guard<std::mutex> g(wr_);
    if (fd_ < 0) // checked under the lock that close() takes
        return false;
    return io_all(fd_, &h, sizeof h, true) && (!alen || io_all(fd_, (void *)a, alen, true)) &&
           (!blen || io_all(fd_, (void *)b, blen, true));
}

bool Broker::write(uint8_t ep, const uint8_t *data, size_t len)
{
    for (size_t off = 0; off < len;) { // keep messages below VTIMSG_MAX
        size_t n = std::min<size_t>(len - off, 1024);
        if (!send(VTIMSG_WRITE, &ep, 1, data + off, n))
            return false;
        off += n;
    }
    return true;
}

int Broker::control(uint8_t rt, uint8_t req, uint16_t value, uint16_t index, uint8_t *data, uint16_t len,
                    int timeout_ms)
{
    uint8_t hdr[12];
    uint32_t seq;
    {
        std::lock_guard<std::mutex> g(ctl_);
        seq = ++seq_;
        waiting_.insert(seq); // only replies someone waits for are kept (a late one is dropped)
    }
    memcpy(hdr, &seq, 4);
    hdr[4] = rt, hdr[5] = req, hdr[6] = value & 0xff, hdr[7] = value >> 8, hdr[8] = index & 0xff,
    hdr[9] = index >> 8, hdr[10] = len & 0xff, hdr[11] = len >> 8;
    if (!send(VTIMSG_CONTROL, hdr, 12, (rt & 0x80) ? nullptr : data, (rt & 0x80) ? 0 : len)) {
        std::lock_guard<std::mutex> g(ctl_);
        waiting_.erase(seq);
        return -1;
    }
    std::unique_lock<std::mutex> l(ctl_);
    const bool got = ctl_cv_.wait_for(l, std::chrono::milliseconds(timeout_ms), [&] { return replies_.count(seq) > 0; });
    waiting_.erase(seq);
    if (!got)
        return -1;
    auto rep = std::move(replies_[seq]);
    replies_.erase(seq);
    int32_t r = -1;
    if (rep.size() >= 4)
        memcpy(&r, rep.data(), 4);
    if (r > 0 && (rt & 0x80)) { // never more than the caller's buffer, whatever the broker says (review 2026-10-19)
        r = (int32_t)std::min<size_t>({(size_t)r, (size_t)len, rep.size() - 4}); // <size_t>: windows.h's min macro
        memcpy(data, rep.data() + 4, (size_t)r);
    }
    return r;
}

int Broker::flash_info(std::vector<uint8_t> &out, int timeout_ms)
{
    out.clear();
    // only a broker that announced itself with INFO (0.8.21+) knows the request: an older one would never answer and
    // keep the caller waiting for the whole timeout at every connect. INFO follows the first STATE: wait briefly for it
    for (int i = 0; i < 20 && broker_version().empty(); i++)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (broker_version().empty())
        return -1;
    {
        std::lock_guard<std::mutex> g(ctl_);
        flash_replied_ = false;
    }
    if (!send(VTIMSG_FLASHINFO, nullptr, 0))
        return -1;
    std::unique_lock<std::mutex> l(ctl_);
    if (!ctl_cv_.wait_for(l, std::chrono::milliseconds(timeout_ms), [&] { return flash_replied_; }))
        return -1;
    if (flash_reply_ > 0)
        out = flash_;
    return flash_reply_;
}

int Broker::audio(bool on, int timeout_ms, uint32_t rate)
{
    {
        std::lock_guard<std::mutex> g(ctl_);
        audio_replied_ = false;
    }
    uint32_t v = on ? 1 : 0;
    std::string name; // Windows: the broker signals a named event per block (we poll the ring; it must exist)
#ifdef _WIN32
    static HANDLE ev;
    char nm[64];
    snprintf(nm, sizeof nm, "Local\\vtinative_audio_%lu", (unsigned long)GetCurrentProcessId());
    if (!ev)
        ev = CreateEventA(nullptr, FALSE, FALSE, nm);
    name = nm;
#endif
    std::string tail(name.c_str(), name.size() + 1); // event name with its NUL, then u32 rate (protocol 5)
    for (int i = 0; i < 4; i++)
        tail.push_back((char)(rate >> (8 * i)));
    if (!send(VTIMSG_AUDIO, &v, 4, tail.data(), tail.size())) // u32 on + event name + rate
        return -1;
    std::unique_lock<std::mutex> l(ctl_);
    if (!ctl_cv_.wait_for(l, std::chrono::milliseconds(timeout_ms), [&] { return audio_replied_; }))
        return -1;
    return audio_reply_;
}

bool Broker::observe(ObserveHandler h, std::string &error)
{
    observe_ = std::move(h); // set before the request: messages follow the reply at once
    if (!send(VTIMSG_OBSERVE, nullptr, 0)) {
        error = "broker connection lost";
        return false;
    }
    std::unique_lock<std::mutex> l(ctl_);
    if (!ctl_cv_.wait_for(l, std::chrono::milliseconds(1500), [&] { return observe_reply_ != 0; })) {
        l.unlock();
        close();
        error = "the TI Revive broker is too old for Nerd mode (needs TI Revive 0.8.2 or newer)";
        return false;
    }
    return true;
}

void Broker::read_loop()
{
    std::vector<uint8_t> buf(VTIMSG_MAX);
    while (!stop_) {
        vtimsg h;
        if (!io_all(fd_, &h, sizeof h, false) || h.len > VTIMSG_MAX || !io_all(fd_, buf.data(), h.len, false))
            break;
        switch (h.type) {
        case VTIMSG_RX:
            if (h.len >= 1 && rx_)
                rx_(buf[0], buf.data() + 1, h.len - 1u);
            break;
        case VTIMSG_STATE:
            if (h.len >= 4 && buf[0] == 1 && !present_)
                arrivals_++; // counted before present_ turns true: a caller that sees it present sees the count
            present_ = h.len >= 4 && buf[0] == 1;
            elsewhere_ = h.len >= 4 && buf[0] == 2;
            if (state_)
                state_(present_);
            break;
        case VTIMSG_CONTROL_REPLY:
            if (h.len >= 8) {
                uint32_t seq;
                memcpy(&seq, buf.data(), 4);
                std::lock_guard<std::mutex> g(ctl_);
                if (waiting_.count(seq)) // control() gave up on it: dropped, not kept for good
                    replies_[seq].assign(buf.begin() + 4, buf.begin() + h.len);
                ctl_cv_.notify_all();
            }
            break;
        case VTIMSG_OBSERVE_REPLY:
            if (h.len >= 4) {
                std::lock_guard<std::mutex> g(ctl_);
                memcpy(&observe_reply_, buf.data(), 4);
                ctl_cv_.notify_all();
            }
            break;
        case VTIMSG_OBS_TRAFFIC:
        case VTIMSG_OBS_LOG:
        case VTIMSG_OBS_STATUS:
            if (observe_)
                observe_(h.type, buf.data(), h.len);
            break;
        case VTIMSG_INFO: { // "key=value" fields separated by tabs: keep the version
            std::string t((const char *)buf.data(), h.len), key = "version=";
            for (size_t at = 0; at <= t.size();) {
                size_t end = t.find('\t', at);
                std::string f = t.substr(at, end == std::string::npos ? std::string::npos : end - at);
                if (f.compare(0, key.size(), key) == 0) {
                    std::lock_guard<std::mutex> g(info_mu_);
                    broker_version_ = f.substr(key.size());
                }
                if (end == std::string::npos)
                    break;
                at = end + 1;
            }
            break;
        }
        case VTIMSG_FLASHINFO_REPLY:
            if (h.len >= 4) {
                std::lock_guard<std::mutex> g(ctl_);
                memcpy(&flash_reply_, buf.data(), 4);
                flash_.assign(buf.begin() + 4, buf.begin() + h.len);
                if (flash_reply_ > 0 && (size_t)flash_reply_ != flash_.size())
                    flash_reply_ = -1; // incomplete
                flash_replied_ = true;
                ctl_cv_.notify_all();
            }
            break;
        case VTIMSG_AUDIO_REPLY:
            if (h.len >= 4) {
                std::lock_guard<std::mutex> g(ctl_);
                memcpy(&audio_reply_, buf.data(), 4);
                audio_replied_ = true;
                ctl_cv_.notify_all();
            }
            break;
        default:
            break;
        }
    }
    present_ = false;
    elsewhere_ = false;
}

} // namespace vti
