// controlcenter.cpp - see controlcenter.h.
#include "controlcenter.h"
#include "../access/library.h"
#include "../device/usbmidi.h"
#include "../model/bankdrop.h"
#include <cctype>
#include <chrono>
#include <thread>
#include <ctime>
#include <filesystem>
#include <fstream>

namespace vti::cc {

namespace fs = std::filesystem;

// a string descriptor (UTF-16LE) as ASCII; '?' for anything else
static std::string string_descriptor(Broker &broker, uint8_t index)
{
    uint8_t b[255] = {};
    int n = index ? broker.control(0x80, 6, (uint16_t)(0x0300 | index), 0x0409, b, sizeof b) : -1;
    std::string s;
    for (int i = 2; i + 1 < n && i + 1 < b[0]; i += 2)
        s += (b[i + 1] == 0 && b[i] >= 32 && b[i] < 127) ? (char)b[i] : '?';
    return s;
}

bool read_device_info(Broker &broker, DeviceInfo &info, std::string &error)
{
    uint8_t d[18] = {};
    if (broker.control(0x80, 6, 0x0100, 0, d, sizeof d) != (int)sizeof d) {
        error = "device descriptor not readable";
        return false;
    }
    info.vendor = (uint16_t)(d[8] | d[9] << 8);
    info.product = (uint16_t)(d[10] | d[11] << 8);
    info.bcd = (uint16_t)(d[12] | d[13] << 8);
    info.manufacturer = string_descriptor(broker, d[14]);
    info.product_name = string_descriptor(broker, d[15]);
    return true;
}

std::string patch_name(const Dump &d)
{
    if (d.size() < 259)
        return {};
    std::string s(d.begin() + 249, d.begin() + 259);
    while (!s.empty() && s.back() == ' ')
        s.pop_back();
    return s;
}

std::string cache_dir()
{
    // same folders as the plug-in (plugin/source/virusplugin.cpp own_cache_dir)
#ifdef _WIN32
    const char *base = getenv("LOCALAPPDATA");
    return base ? std::string(base) + "/virus-ti-native/cache" : std::string();
#elif defined(__APPLE__)
    const char *home = getenv("HOME");
    return home ? std::string(home) + "/Library/Application Support/virus-ti-native/cache" : std::string();
#else
    const char *home = getenv("HOME");
    return home ? std::string(home) + "/.local/share/virus-ti-native/cache" : std::string();
#endif
}

std::string default_backup_dir()
{
    // next to the "Patches" folder, not inside it: the plug-in lists everything below Patches as libraries
    std::string lib = library_dir();
    fs::path base = lib.empty() ? fs::path("Virus TI Backups") : fs::path(lib).parent_path() / "Backups";
    char stamp[32];
    time_t t = time(nullptr);
    strftime(stamp, sizeof stamp, "%Y-%m-%d %H%M", localtime(&t));
    return (base / stamp).string();
}

bool backup_ram(Broker &broker, const std::string &banks, const std::string &out_dir,
                const std::function<void(int, int, bool, const Dump &)> &progress, std::string &error,
                const std::atomic<bool> *stop)
{
    std::error_code ec;
    fs::create_directories(out_dir, ec);
    if (ec) {
        error = "cannot create " + out_dir + ": " + ec.message();
        if (ec == std::errc::permission_denied) // e.g. Documents/Access Music made by the Access installer as root (E182)
            error += ". Choose another folder, or give this one back to your user (macOS: run \"Set up Access Virus "
                     "TI files.command\" again, it fixes folders the Access installer created as administrator).";
        return false;
    }
    std::string cache = cache_dir();
    if (!cache.empty())
        fs::create_directories(cache, ec);
    MemorySync sync(broker);
    for (char c : banks) {
        int bank = c - '0';
        if (bank < 1 || bank > 4)
            continue;
        std::string name = std::string("RAM ") + (char)('A' + bank - 1);
        Bank b;
        if (!cache.empty())
            load_bank_file(cache + "/" + name, b);
        int n = sync.sync_bank(bank, b, [&](int p, bool fetched) {
            if (progress)
                progress(bank, p, fetched, b[p]);
        }, stop);
        if (!cache.empty())
            save_bank_file(cache + "/" + name, b); // also what was fetched before a failure
        if (n == -2) {
            error = "cancelled";
            return false;
        }
        if (n < 0) {
            error = name + ": the Virus did not answer";
            return false;
        }
        PatchLibrary lib;
        lib.name = name;
        lib.path = (fs::path(out_dir) / (name + ".mid")).string();
        lib.dumps.assign(b.begin(), b.end());
        for (auto &d : lib.dumps)
            if (d.size() != 524) {
                error = name + ": incomplete";
                return false;
            }
        if (!save_library(lib, error))
            return false;
    }
    return true;
}

// the sound and name of two single dumps: all but the header (bank, program) and the two checksums
static bool same_patch(const Dump &a, const Dump &b)
{
    return a.size() == 524 && b.size() == 524 && std::equal(a.begin() + 9, a.begin() + 265, b.begin() + 9) &&
           std::equal(a.begin() + 266, a.begin() + 522, b.begin() + 266);
}

std::vector<RestoreWrite> restore_plan(int bank, const std::vector<Dump> &backup, const Bank &current)
{
    std::vector<RestoreWrite> w;
    if (bank < 1 || bank > 4)
        return w;
    for (size_t p = 0; p < backup.size() && p < 128; p++) {
        if (!BankDrop::is_single(backup[p]) || same_patch(backup[p], current[p]))
            continue;
        Dump d = backup[p];
        BankDrop::address(d, bank - 1, (int)p);
        w.push_back({(int)p, std::move(d)});
    }
    return w;
}

int bank_from_name(const std::string &stem)
{
    std::string s = stem;
    for (auto &c : s)
        c = (char)std::toupper((unsigned char)c);
    for (size_t i = s.find("RAM "); i != std::string::npos; i = s.find("RAM ", i + 1)) {
        char b = i + 4 < s.size() ? s[i + 4] : 0;
        bool word = i + 5 >= s.size() || !std::isalnum((unsigned char)s[i + 5]);
        if ((i == 0 || !std::isalnum((unsigned char)s[i - 1])) && b >= 'A' && b <= 'D' && word)
            return b - 'A' + 1;
    }
    return 0;
}

std::vector<std::vector<uint8_t>> dump_pieces(const Dump &d)
{
    auto pk = usbmidi_pack(d, 3);
    std::vector<std::vector<uint8_t>> out;
    for (size_t off = 0; off < pk.size(); off += 64)
        out.emplace_back(pk.begin() + off, pk.begin() + std::min(pk.size(), off + 64));
    return out;
}

bool restore_ram(Broker &broker, int bank, const std::vector<Dump> &backup,
                 const std::function<void(int, int)> &progress, int &written, std::string &error,
                 const std::atomic<bool> *stop)
{
    written = 0;
    if (bank < 1 || bank > 4) {
        error = "only RAM banks A-D can be restored";
        return false;
    }
    const std::string name = std::string("RAM ") + (char)('A' + bank - 1);
    std::string cache = cache_dir();
    std::error_code ec;
    if (!cache.empty())
        fs::create_directories(cache, ec);
    Bank b;
    if (!cache.empty())
        load_bank_file(cache + "/" + name, b);
    MemorySync sync(broker);
    // the bank as the Virus has it now: only what differs is written
    int n = sync.sync_bank(bank, b, nullptr, stop);
    if (!cache.empty())
        save_bank_file(cache + "/" + name, b);
    if (n == -2) {
        error = "cancelled";
        return false;
    }
    if (n < 0) {
        error = name + ": the Virus did not answer";
        return false;
    }
    auto plan = restore_plan(bank, backup, b);
    for (size_t i = 0; i < plan.size(); i++) {
        if (stop && stop->load()) {
            error = "cancelled";
            break;
        }
        bool ok = true;
        for (auto &piece : dump_pieces(plan[i].dump)) {
            ok = ok && broker.write(0x06, piece.data(), piece.size());
            std::this_thread::sleep_for(std::chrono::milliseconds(10)); // the plug-in's pace (dumppacer.h)
        }
        if (!ok) {
            error = name + ": writing to the Virus failed";
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        // read back: the Virus has the patch, the cache gets what it says
        Dump r = sync.single(bank, plan[i].slot);
        if (!same_patch(r, plan[i].dump)) {
            error = name + " " + std::to_string(plan[i].slot) + ": the Virus has another patch after the restore";
            break;
        }
        b[(size_t)plan[i].slot] = r;
        written++;
        if (progress)
            progress((int)i + 1, (int)plan.size());
    }
    if (!cache.empty())
        save_bank_file(cache + "/" + name, b);
    return written == (int)plan.size() && error.empty();
}

bool write_syx(const std::vector<Dump> &dumps, const std::string &path, std::string &error)
{
    std::string tmp = path + ".tmp";
    {
        std::ofstream o(tmp, std::ios::binary);
        for (auto &d : dumps)
            o.write((const char *)d.data(), (std::streamsize)d.size());
        if (!o) {
            error = "cannot write " + tmp;
            return false;
        }
    }
    std::error_code ec;
    fs::rename(tmp, path, ec);
    if (ec)
        error = ec.message();
    return !ec;
}

} // namespace vti::cc
