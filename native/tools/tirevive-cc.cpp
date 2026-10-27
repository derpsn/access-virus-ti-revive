// tirevive-cc - TI Revive Control Center, command line (src/cc/controlcenter.h).
//   tirevive-cc info [--raw]                  USB identity of the Virus, its identification blocks (OS version; --raw:
//                                             the bytes), RAM banks compared with the plug-in's cache
//   tirevive-cc backup [--out DIR] [--banks 1234]
//                                             RAM banks -> "RAM A.mid".. (library format; default <Documents>/
//                                             Access Music/Virus TI/Backups/<date time>)
//   tirevive-cc restore A|B|C|D FILE [--yes]  RAM bank from a library (e.g. a backup's "RAM B.mid"): only the
//                                             programs that differ, single dumps into RAM like the plug-in's browser,
//                                             read back; asks first unless --yes (never flash, E313)
//   tirevive-cc list FILE                     patch names in a library (.mid or .syx), no Virus needed
//   tirevive-cc export FILE OUT.syx|OUT.mid   convert a library, no Virus needed
//   tirevive-cc logs [--out FILE]             plug-in + broker logs in one file for a bug report (anonymized)
//   tirevive-cc debug on|off                  the plug-in's log (diag.on; takes effect when the plug-in starts)
//   tirevive-cc option [NAME on|off]          A/B options against the original (nerd mode's Options menu)
//   tirevive-cc watch [--seconds N]           Nerd mode in the terminal: broker status, log and the traffic, decoded
//                                             (a read-only observer: works while the plug-in uses the Virus)
// Firmware/OS update, Burn To Flash, factory reset and renaming the Virus are not supported, on purpose: they write
// the Virus' flash (AGENTS.md rule 2). restore writes RAM only.
#include "../src/access/accessfiles.h"
#include "../src/access/library.h"
#include "../src/cc/controlcenter.h"
#include "../src/cc/decode.h"
#include "../src/cc/diag.h"
#include "../src/model/remotedevice.h"
#include "../src/model/sysinfo.h"
#include "vtiproto.h"
#include <cctype>
#include <cstdio>
#include <algorithm>
#include <chrono>
#include <mutex>
#include <thread>
#include <cstring>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

static int usage()
{
    fprintf(stderr, "usage: tirevive-cc info\n"
                    "       tirevive-cc backup [--out DIR] [--banks 1234]\n"
                    "       tirevive-cc restore A|B|C|D FILE.mid|FILE.syx [--yes]\n"
                    "       tirevive-cc list FILE.mid|FILE.syx\n"
                    "       tirevive-cc export FILE.mid|FILE.syx OUT.syx|OUT.mid\n"
                    "       tirevive-cc logs [--out FILE]\n"
                    "       tirevive-cc debug on|off\n"
                    "       tirevive-cc option [NAME on|off]\n"
                    "       tirevive-cc watch [--seconds N]\n"
                    "Only restore writes to the Virus (RAM, never flash). Close the TI Revive plug-in (or your DAW)\n"
                    "first: the Virus serves one program at a time.\n");
    return 2;
}

static bool connect(vti::Broker &broker)
{
    std::string err;
    broker.set_name("tirevive-cc");
    if (!broker.open(err) || !broker.device_present()) {
        fprintf(stderr, "tirevive-cc: %s\n", err.empty() ? "no Virus (not connected, or another program owns it)"
                                                         : err.c_str());
        return false;
    }
    return true;
}

static int info(bool raw)
{
    vti::Broker broker;
    if (!connect(broker))
        return 1;
    std::string err;
    vti::cc::DeviceInfo di;
    if (!vti::cc::read_device_info(broker, di, err)) {
        fprintf(stderr, "tirevive-cc: %s\n", err.c_str());
        return 1;
    }
    printf("USB device   %04x:%04x, bcdDevice %x.%02x\n", di.vendor, di.product, di.bcd >> 8, di.bcd & 0xFF);
    printf("Manufacturer %s\nProduct      %s\n", di.manufacturer.c_str(), di.product_name.c_str());
    // the identification blocks from the Virus' flash, read by the broker with the original driver's fixed read-only
    // sequence (flashinfo.h, E026/E040)
    std::vector<uint8_t> fl;
    int n = broker.flash_info(fl);
    if (n <= 0)
        printf("Flash info   no answer (broker older than 0.8.21, or another program's plug-in has the Virus)\n");
    else
        printf("Virus OS     %s\n", vti::tios_from_flash(fl).empty() ? "(not found)" : vti::tios_from_flash(fl).c_str());
    for (int i = 0; raw && i < n; i += 16) {
        printf("  %06x ", (unsigned)(i < 96 ? 0xFF7FA0 + i : 0x800000 + i - 96));
        for (int k = 0; k < 16; k++)
            printf(" %02x", fl[i + k]);
        printf("  |");
        for (int k = 0; k < 16; k++)
            putchar(fl[i + k] >= 32 && fl[i + k] < 127 ? fl[i + k] : '.');
        printf("|\n");
    }
    vti::MemorySync sync(broker);
    std::string cache = vti::cc::cache_dir();
    for (int bank = 1; bank <= 4; bank++) {
        std::string name = std::string("RAM ") + (char)('A' + bank - 1);
        auto fp = sync.bank_info(bank);
        if (fp.size() != 128) {
            printf("%s        no answer\n", name.c_str());
            continue;
        }
        vti::Bank b;
        int current = 0;
        if (vti::load_bank_file(cache + "/" + name, b))
            for (int p = 0; p < 128; p++)
                current += vti::RemoteDevice::single_fingerprint(b[p]) == fp[p];
        printf("%s        128 programs, %d up to date in the cache", name.c_str(), current);
        if (current)
            printf(" (1: %s)", vti::cc::patch_name(b[0]).c_str());
        printf("\n");
    }
    printf("Cache        %s\n", cache.c_str());
    return 0;
}

static int backup(int argc, char **argv)
{
    std::string out, banks = "1234";
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--out") && i + 1 < argc)
            out = argv[++i];
        else if (!strcmp(argv[i], "--banks") && i + 1 < argc)
            banks = argv[++i];
        else
            return usage();
    }
    if (out.empty())
        out = vti::cc::default_backup_dir();
    vti::Broker broker;
    if (!connect(broker))
        return 1;
    printf("Backup of the RAM banks to %s\n(unchanged programs come from the cache, the others take about 1 s each)\n",
           out.c_str());
    std::string err;
    int fetched = 0;
    bool ok = vti::cc::backup_ram(broker, banks, out, [&](int bank, int p, bool f, const vti::Dump &d) {
        fetched += f;
        if (f)
            printf("  RAM %c %3d %s\n", 'A' + bank - 1, p + 1, vti::cc::patch_name(d).c_str());
        else if (p == 127)
            printf("  RAM %c checked\n", 'A' + bank - 1);
        fflush(stdout);
    }, err);
    if (!ok) {
        fprintf(stderr, "tirevive-cc: %s\n", err.c_str());
        return 1;
    }
    printf("Done: %d programs read from the Virus, libraries in %s\n", fetched, out.c_str());
    return 0;
}

static bool load(const char *path, vti::PatchLibrary &lib);

static int restore(int argc, char **argv)
{
    if (argc < 4)
        return usage();
    const char b = (char)toupper((unsigned char)argv[2][0]);
    bool yes = argc == 5 && !strcmp(argv[4], "--yes");
    if (strlen(argv[2]) != 1 || b < 'A' || b > 'D' || (argc == 5 && !yes) || argc > 5)
        return usage();
    vti::PatchLibrary lib;
    if (!load(argv[3], lib))
        return 1;
    if (!yes) {
        printf("Overwrite RAM %c with %s? The programs that differ are replaced (RAM only). [y/N] ", b, argv[3]);
        fflush(stdout);
        char line[16] = {};
        if (!fgets(line, sizeof line, stdin) || (line[0] != 'y' && line[0] != 'Y')) {
            printf("Nothing written.\n");
            return 1;
        }
    }
    vti::Broker broker;
    if (!connect(broker))
        return 1;
    std::string err;
    int written = 0;
    bool ok = vti::cc::restore_ram(broker, b - 'A' + 1, lib.dumps, [&](int done, int total) {
        printf("  RAM %c: %d of %d written\n", b, done, total);
        fflush(stdout);
    }, written, err);
    if (!ok) {
        fprintf(stderr, "tirevive-cc: %s (%d written)\n", err.c_str(), written);
        return 1;
    }
    printf("Done: %d programs written to RAM %c and read back%s\n", written, b,
           written ? "" : " (all programs were the same already)");
    return 0;
}

static bool load(const char *path, vti::PatchLibrary &lib)
{
    // with the Access files, old Virus C dumps are upgraded like in the plug-in; without them they are skipped
    std::string err;
    auto files = vti::AccessFiles::open("", err);
    if (!vti::load_library_file(path, lib, files ? files->default_page_b() : std::vector<uint8_t>())) {
        fprintf(stderr, "tirevive-cc: cannot read %s\n", path);
        return false;
    }
    if (lib.dumps.empty()) {
        fprintf(stderr, "tirevive-cc: no Virus TI single dumps in %s\n", path);
        return false;
    }
    return true;
}

static int list(const char *path)
{
    vti::PatchLibrary lib;
    if (!load(path, lib))
        return 1;
    for (size_t i = 0; i < lib.dumps.size(); i++)
        if (!lib.dumps[i].empty()) // a gap: no patch at this position (E298)
            printf("%3zu %s\n", i + 1, vti::cc::patch_name(lib.dumps[i]).c_str());
    return 0;
}

static int export_(const char *in, const char *out)
{
    vti::PatchLibrary lib;
    if (!load(in, lib))
        return 1;
    std::string ext = fs::path(out).extension().string(), err;
    for (auto &c : ext)
        c = (char)tolower(c);
    bool ok;
    if (ext == ".syx") {
        ok = vti::cc::write_syx(lib.dumps, out, err);
    } else if (ext == ".mid") {
        if (lib.dumps.size() > 128)
            fprintf(stderr, "tirevive-cc: a library holds 128 patches, the other %zu are left out\n",
                    lib.dumps.size() - 128);
        lib.path = out;
        ok = vti::save_library(lib, err);
    } else
        return usage();
    if (!ok) {
        fprintf(stderr, "tirevive-cc: %s\n", err.c_str());
        return 1;
    }
    size_t n = 0;
    for (size_t i = 0; i < lib.dumps.size() && (ext != ".mid" || i < 128); i++)
        n += !lib.dumps[i].empty();
    printf("%zu patches -> %s\n", n, out);
    return 0;
}

#ifndef TIREVIVE_VERSION
#define TIREVIVE_VERSION "0.0.0"
#endif

static int logs(int argc, char **argv)
{
    std::string out = "TI-Revive-logs.txt", err;
    if (argc == 4 && !strcmp(argv[2], "--out"))
        out = argv[3];
    else if (argc != 2)
        return usage();
    if (!vti::cc::collect_logs(out, TIREVIVE_VERSION, err)) {
        fprintf(stderr, "tirevive-cc: %s\n", err.c_str());
        return 1;
    }
    printf("Logs written to %s (home folder and user name replaced). Check it before you attach it.\n", out.c_str());
    if (!vti::cc::plugin_log_enabled())
        printf("The plug-in log is off: \"tirevive-cc debug on\", restart the plug-in, reproduce, collect again.\n");
    return 0;
}

static int debug(const char *arg)
{
    bool on = !strcmp(arg, "on");
    if (!on && strcmp(arg, "off"))
        return usage();
    std::string err;
    if (!vti::cc::set_plugin_log(on, err)) {
        fprintf(stderr, "tirevive-cc: %s\n", err.c_str());
        return 1;
    }
    printf("Plug-in log %s (%s); takes effect when the plug-in starts.\n", on ? "on" : "off",
           vti::cc::plugin_log_path().c_str());
    return 0;
}

// without arguments: the options and their state; with NAME on|off: switch one
static int option(int argc, char **argv)
{
    if (argc == 2) {
        for (auto &o : vti::cc::plugin_options())
            printf("%-18s %-3s  %s\n", o.name, vti::cc::plugin_option(o.name) ? "on" : "off", o.label);
        printf("Options take effect when the plug-in starts.\n");
        return 0;
    }
    if (argc != 4 || (strcmp(argv[3], "on") && strcmp(argv[3], "off")))
        return usage();
    std::string err;
    if (!vti::cc::set_plugin_option(argv[2], !strcmp(argv[3], "on"), err)) {
        fprintf(stderr, "tirevive-cc: %s\n", err.c_str());
        return 1;
    }
    printf("%s %s; takes effect when the plug-in starts.\n", argv[2], argv[3]);
    return 0;
}

static int watch(int argc, char **argv)
{
    int secs = 0;
    if (argc == 4 && !strcmp(argv[2], "--seconds"))
        secs = atoi(argv[3]);
    else if (argc != 2)
        return usage();
    // parameter names from the device model when the Access files are there
    std::string err;
    auto files = vti::AccessFiles::open("", err);
    vti::RemoteDevice model;
    bool have_model = files && model.load(files->get("remotedevice.xml"), err);
    vti::cc::TrafficDecoder dec(have_model ? &model : nullptr);
    vti::Broker broker;
    broker.set_name("tirevive-cc watch");
    std::mutex mu;
    if (!broker.open(err)) {
        fprintf(stderr, "tirevive-cc: %s\n", err.c_str());
        return 1;
    }
    bool ok = broker.observe([&](uint16_t type, const uint8_t *d, size_t n) {
        std::lock_guard<std::mutex> l(mu);
        if (type == VTIMSG_OBS_LOG) {
            printf("log     %.*s\n", (int)n, (const char *)d);
        } else if (type == VTIMSG_OBS_STATUS) {
            std::string s((const char *)d, n);
            for (auto &c : s)
                c = c == '\t' ? ' ' : c;
            printf("status  %s\n", s.c_str());
        } else if (type == VTIMSG_OBS_TRAFFIC) {
            dec.feed(d, n, [](vti::cc::TrafficLine &&t) {
                static const char *dir[] = {"-> Virus", "<- Virus", "USB ctl "};
                printf("%4u.%03u %s %s\n", t.ms / 1000, t.ms % 1000, dir[t.dir], t.text.c_str());
            });
        }
        fflush(stdout);
    }, err);
    if (!ok) {
        fprintf(stderr, "tirevive-cc: %s\n", err.c_str());
        return 1;
    }
    printf("Watching (read-only; %s). Ctrl+C ends.\n", have_model ? "parameter names from the Access files"
                                                                  : "no Access files: parameters as numbers");
    for (int i = 0; secs == 0 || i < secs * 10; i++)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    broker.close();
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2)
        return usage();
    std::string cmd = argv[1];
    if (cmd == "info" && (argc == 2 || (argc == 3 && !strcmp(argv[2], "--raw"))))
        return info(argc == 3);
    if (cmd == "backup")
        return backup(argc, argv);
    if (cmd == "restore")
        return restore(argc, argv);
    if (cmd == "list" && argc == 3)
        return list(argv[2]);
    if (cmd == "export" && argc == 4)
        return export_(argv[2], argv[3]);
    if (cmd == "watch")
        return watch(argc, argv);
    if (cmd == "logs")
        return logs(argc, argv);
    if (cmd == "debug" && argc == 3)
        return debug(argv[2]);
    if (cmd == "option")
        return option(argc, argv);
    return usage();
}
