// tirevive-midiports.exe - Windows: the Virus as two MIDI inputs for every MIDI program, "TI Revive Synth" (its keys,
// cable 1) and "TI Revive MIDI" (its DIN MIDI In, cable 0), as the Access driver had them and as TI Revive offers on
// macOS and Linux (src/broker/vtikeyport.h). Windows MIDI Services (Windows 11 25H2 and later) lets an app create a
// virtual MIDI device without a driver; this helper does that and sends what the broker writes to its standard input
// (src/broker/midifeed.h), as UMP (umpconv.h). Read-only (user 2026-11-15): both function blocks are outputs of the
// device, so no program can send anything to the Virus through them (rule 2). It ends when the broker closes the
// pipe, and with it the ports. Only Windows' own copy of the API is used, never one shipped by us (user 2026-11-15):
// without it, on an older Windows or in legacy MIDI mode, it logs why and exits; the installer does not even put it
// there then (src/installer). Built with MSVC and C++/WinRT (native/CMakeLists.txt, target tirevive-midiports).
// Follows Microsoft's samples simple-app-to-app-midi and detect-midi-services (microsoft/MIDI, read 2026-11-15).
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Midi2.h>
#include <winrt/Windows.Devices.Midi2.Enumeration.h>
#include <winrt/Windows.Devices.Midi2.Transports.Virtual.h>
extern "C" {
#include "midifeed.h"
#include "devslot.h" // "-slot N": the ports of Virus N of several (#45)
}
#include "umpconv.h"

namespace midi2 = winrt::Windows::Devices::Midi2;
using namespace winrt::Windows::Devices::Midi2::Enumeration;
using namespace winrt::Windows::Devices::Midi2::Transports::Virtual;

namespace {

FILE *g_log = nullptr; // %TEMP%\tirevive-midiports.log, replaced at each start

void hlog(const char *fmt, ...)
{
    if (!g_log)
        return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(g_log, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

DWORD windows_build()
{
    using RtlGetVersionProc = LONG(WINAPI *)(PRTL_OSVERSIONINFOW);
    auto get = reinterpret_cast<RtlGetVersionProc>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
    RTL_OSVERSIONINFOW info{sizeof info};
    return get && get(&info) == 0 ? info.dwBuildNumber : 0;
}

bool legacy_midi_mode() // the user (or an app) switched Windows back to the old MIDI stack
{
    DWORD v = 0, size = sizeof v;
    return RegGetValueW(HKEY_LOCAL_MACHINE, LR"(SOFTWARE\Microsoft\Windows NT\CurrentVersion\Drivers32)",
                        L"UseLegacyMidi", RRF_RT_REG_DWORD, nullptr, &v, &size) == ERROR_SUCCESS &&
           v == 1;
}

// Why this Windows cannot have the ports, or "" when it can (and the MIDI service runs)
std::string unsupported()
{
    const DWORD build = windows_build();
    if (build < 26200) // 25H2; 24H2 (26100) never gets the API (microsoft/MIDI release notes)
        return "Windows build " + std::to_string(build) + " (needs Windows 11 25H2, build 26200, or later)";
    if (legacy_midi_mode())
        return "Windows is in legacy MIDI mode (UseLegacyMidi = 1)";
    winrt::hresult_error error{};
    auto statics = winrt::try_get_activation_factory<midi2::MidiApi, midi2::IMidiApiStatics>(error);
    if (!statics)
        return "this Windows has no Windows MIDI Services API yet (it comes with a Windows update)";
    auto statics2 = statics.try_as<midi2::IMidiApiStatics2>();
    if (!statics2 || !statics2.IsSystemProvided()) {
        // TEST ONLY (user 2026-11-15: test in the VM before the API is in Windows): with VTI_MIDI2_ALLOW_SDK=1 a copy
        // of the API next to the helper (from microsoft/MIDI's Preview 11 package) is taken too. Releases never put
        // one there, and the installer installs the helper only where Windows has its own.
        const char *allow = getenv("VTI_MIDI2_ALLOW_SDK");
        if (!(allow && !strcmp(allow, "1")))
            return "the Windows MIDI Services API found is not Windows' own copy";
        hlog("TEST: VTI_MIDI2_ALLOW_SDK=1, taking a copy of the API that is not Windows' own");
    }
    if (!statics.EnsureServiceAvailable())
        return "the Windows MIDI service (midisrv) does not start";
    return "";
}

// One port = one virtual device with one output block on group 1. On our 26H2 VM (2026-11-15) Windows named the
// MIDI 1.0 ports of ONE device with two blocks "Virus TI" and "Virus TI Gr 2" (device name, then group number), not
// after the blocks; a device's first port carries the device's name, so each port gets a device of its own.
MidiVirtualDeviceCreationConfig port_config(const wchar_t *name, const wchar_t *instance)
{
    MidiDeclaredEndpointInfo info;
    info.HasStaticFunctionBlocks(true);
    info.Name(name);
    info.ProductInstanceId(instance); // the same at every start, so programs find the port again
    info.SupportsMidi10Protocol(true);
    info.SupportsMidi20Protocol(false);
    info.SupportsReceivingJitterReductionTimestamps(false);
    info.SupportsSendingJitterReductionTimestamps(false);
    info.SpecificationVersionMajor(1);
    info.SpecificationVersionMinor(1);
    MidiVirtualDeviceCreationConfig config(info.Name(), L"Access Virus TI through TI Revive (read-only)", L"TI Revive",
                                           info);
    MidiFunctionBlock b;
    b.Number(0);
    b.IsActive(true);
    b.Name(name);
    b.FirstGroup(midi2::MidiGroup(static_cast<uint8_t>(0)));
    b.GroupCount(1);
    b.Direction(MidiFunctionBlockDirection::BlockOutput); // the device sends, programs receive: read-only
    b.RepresentsMidi10Connection(MidiFunctionBlockRepresentsMidi10Connection::YesBandwidthUnrestricted);
    config.FunctionBlocks().Append(b);
    return config;
}

// Creates the device and opens its connection. Right after EnsureServiceAvailable() started the service, creation
// failed twice on the VM (null after about 10 s) and worked with the service already running: so a few tries.
midi2::MidiEndpointConnection open_port(midi2::MidiSession const &session, const wchar_t *name, const wchar_t *instance,
                                        MidiVirtualDevice &device)
{
    for (int attempt = 1; attempt <= 10; attempt++) {
        device = MidiVirtualDeviceManager::CreateVirtualDevice(port_config(name, instance));
        if (device) {
            auto conn = session.CreateEndpointConnection(device.DeviceEndpointDeviceId());
            if (conn && conn.AddMessageProcessingPlugin(device) == midi2::MidiMessageProcessingPluginAddResult::Succeeded) {
                conn.Open();
                if (attempt > 1)
                    hlog("\"%ls\": created at attempt %d", name, attempt);
                return conn;
            }
        }
        Sleep(1000);
    }
    hlog("no MIDI port \"%ls\": the virtual device was not created (10 attempts)", name);
    return nullptr;
}

// the stream from the broker: frame by frame to its port's device, as UMP
struct Feed {
    midi2::MidiEndpointConnection conn[2] = {nullptr, nullptr}; // [0] "TI Revive MIDI", [1] "TI Revive Synth"
    vti::UmpConv conv[2] = {vti::UmpConv(0), vti::UmpConv(0)};  // group 1 on each device
    unsigned sent = 0, failed = 0;
};

void on_frame(void *user, int port, const uint8_t *msg, int n)
{
    auto *f = static_cast<Feed *>(user);
    auto &conn = f->conn[port & 1];
    if (!conn)
        return;
    uint32_t w[8];
    const int words = f->conv[port & 1].push(msg, n, w, 8);
    for (int i = 0; i < words;) {
        const int size = (w[i] >> 28) == 3 ? 2 : 1; // type 3 (SysEx) packets are two words, types 1 and 2 one
        if (i + size > words)
            break;
        const auto r = size == 2 ? conn.SendSingleMessageWords(0, w[i], w[i + 1]) // 0: send at once
                                 : conn.SendSingleMessageWords(0, w[i]);
        if (midi2::MidiEndpointConnection::SendMessageSucceeded(r)) {
            if (f->sent++ == 0)
                hlog("first message sent to Windows");
        } else if (f->failed++ == 0) {
            hlog("a message was not sent (result 0x%x)", (unsigned)r);
        }
        i += size;
    }
}

} // namespace

int wmain(int argc, wchar_t **argv)
{
    int slot = 1; // the broker passes "-slot N" for Virus N > 1
    if (argc == 3 && !wcscmp(argv[1], L"-slot") && argv[2][0] && !argv[2][1]) {
        const char s[2] = {(char)argv[2][0], 0};
        const int p = vti_slot_parse(s);
        slot = p ? p : 1;
    }
    char sfx[4], synth_name[32], din_name[32];
    vti_slot_suffix(sfx, slot);
    vti_slot_port_name(synth_name, sizeof synth_name, "Synth", slot);
    vti_slot_port_name(din_name, sizeof din_name, "MIDI", slot);
    const auto wide = [](const char *a) { return std::wstring(a, a + strlen(a)); }; // ASCII names
    wchar_t tmp[MAX_PATH];
    if (GetTempPathW(MAX_PATH, tmp))
        g_log = _wfopen((std::wstring(tmp) + L"tirevive-midiports" + wide(sfx) + L".log").c_str(), L"w");
    hlog("tirevive-midiports starting (Windows build %lu)", windows_build());
    try {
        winrt::init_apartment();
        const std::string why = unsupported();
        if (!why.empty()) {
            hlog("no MIDI ports: %s", why.c_str());
            return 3;
        }
        auto session = midi2::MidiSession::Create(L"TI Revive MIDI ports");
        if (!session) {
            hlog("no MIDI ports: no MIDI session");
            return 4;
        }
        Feed feed;
        MidiVirtualDevice synth{nullptr}, din{nullptr};
        // Virus 1 keeps the IDs programs remember; Virus N gets "-N"
        feed.conn[1] = open_port(session, wide(synth_name).c_str(), (L"TIRevive-VirusTI-Synth" + wide(sfx)).c_str(), synth);
        feed.conn[0] = open_port(session, wide(din_name).c_str(), (L"TIRevive-VirusTI-MIDI" + wide(sfx)).c_str(), din);
        if (!feed.conn[0] && !feed.conn[1])
            return 4;
        hlog("MIDI ports there (read-only):%s%s%s%s%s", feed.conn[1] ? " \"" : "", feed.conn[1] ? synth_name : "",
             feed.conn[1] ? "\"" : "", feed.conn[0] ? " \"" : "", feed.conn[0] ? din_name : "");
        vti_midifeed_reader reader;
        vti_midifeed_init(&reader);
        HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
        uint8_t buf[4096];
        DWORD n = 0;
        while (ReadFile(in, buf, sizeof buf, &n, nullptr) && n > 0) // ends when the broker closes the pipe
            vti_midifeed_read(&reader, buf, (int)n, on_frame, &feed);
        hlog("the broker closed the pipe: %u messages sent, %u failed; closing the ports", feed.sent, feed.failed);
        for (auto &c : feed.conn)
            if (c)
                session.DisconnectEndpointConnection(c.ConnectionId());
        session.Close();
    } catch (const winrt::hresult_error &e) {
        hlog("no MIDI ports: Windows MIDI Services error 0x%08x: %ls", (unsigned)e.code(), e.message().c_str());
        return 5;
    }
    return 0;
}
