// tirevive-midiports.exe - Windows: the Virus as two MIDI inputs for every MIDI program, "Virus TI Synth" (its keys,
// cable 1) and "Virus TI MIDI" (its DIN MIDI In, cable 0), as the Access driver had them and as TI Revive offers on
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
#include <string>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Midi2.h>
#include <winrt/Windows.Devices.Midi2.Enumeration.h>
#include <winrt/Windows.Devices.Midi2.Transports.Virtual.h>
extern "C" {
#include "midifeed.h"
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
    if (!statics2 || !statics2.IsSystemProvided())
        return "the Windows MIDI Services API found is not Windows' own copy";
    if (!statics.EnsureServiceAvailable())
        return "the Windows MIDI service (midisrv) does not start";
    return "";
}

MidiFunctionBlock source_block(uint8_t number, const wchar_t *name, uint8_t group)
{
    MidiFunctionBlock b;
    b.Number(number);
    b.IsActive(true);
    b.Name(name);
    b.FirstGroup(midi2::MidiGroup(group));
    b.GroupCount(1);                                  // one group: Windows names the port after the block
    b.Direction(MidiFunctionBlockDirection::BlockOutput); // the device sends, programs receive: read-only
    b.RepresentsMidi10Connection(MidiFunctionBlockRepresentsMidi10Connection::YesBandwidthUnrestricted);
    return b;
}

// the stream from the broker: frame by frame to the device, as UMP
struct Feed {
    midi2::MidiEndpointConnection conn{nullptr};
    vti::UmpConv conv[2] = {vti::UmpConv(1), vti::UmpConv(0)}; // port 0 "Virus TI MIDI" = group 2, 1 "Synth" = group 1
    unsigned sent = 0, failed = 0;
};

void on_frame(void *user, int port, const uint8_t *msg, int n)
{
    auto *f = static_cast<Feed *>(user);
    uint32_t w[8];
    const int words = f->conv[port & 1].push(msg, n, w, 8);
    for (int i = 0; i < words;) {
        const int size = (w[i] >> 28) == 3 ? 2 : 1; // type 3 (SysEx) packets are two words, types 1 and 2 one
        if (i + size > words)
            break;
        const auto r = size == 2 ? f->conn.SendSingleMessageWords(0, w[i], w[i + 1]) // 0: send at once
                                 : f->conn.SendSingleMessageWords(0, w[i]);
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

int wmain()
{
    wchar_t tmp[MAX_PATH];
    if (GetTempPathW(MAX_PATH, tmp))
        g_log = _wfopen((std::wstring(tmp) + L"tirevive-midiports.log").c_str(), L"w");
    hlog("tirevive-midiports starting (Windows build %lu)", windows_build());
    try {
        winrt::init_apartment();
        const std::string why = unsupported();
        if (!why.empty()) {
            hlog("no MIDI ports: %s", why.c_str());
            return 3;
        }
        MidiDeclaredEndpointInfo info;
        info.HasStaticFunctionBlocks(true);
        info.Name(L"Virus TI");
        info.ProductInstanceId(L"TIRevive-VirusTI"); // the same at every start, so programs find the ports again
        info.SupportsMidi10Protocol(true);
        info.SupportsMidi20Protocol(false);
        info.SupportsReceivingJitterReductionTimestamps(false);
        info.SupportsSendingJitterReductionTimestamps(false);
        info.SpecificationVersionMajor(1);
        info.SpecificationVersionMinor(1);
        MidiVirtualDeviceCreationConfig config(info.Name(), L"Access Virus TI through TI Revive (read-only)",
                                               L"TI Revive", info);
        config.FunctionBlocks().Append(source_block(0, L"Virus TI Synth", 0));
        config.FunctionBlocks().Append(source_block(1, L"Virus TI MIDI", 1));
        MidiVirtualDevice device = MidiVirtualDeviceManager::CreateVirtualDevice(config);
        if (!device) {
            hlog("no MIDI ports: the virtual device was not created");
            return 4;
        }
        auto session = midi2::MidiSession::Create(L"TI Revive MIDI ports");
        if (!session) {
            hlog("no MIDI ports: no MIDI session");
            return 4;
        }
        Feed feed;
        feed.conn = session.CreateEndpointConnection(device.DeviceEndpointDeviceId());
        if (!feed.conn ||
            feed.conn.AddMessageProcessingPlugin(device) != midi2::MidiMessageProcessingPluginAddResult::Succeeded) {
            hlog("no MIDI ports: the device connection failed");
            return 4;
        }
        feed.conn.Open(); // now programs see "Virus TI Synth" and "Virus TI MIDI"
        hlog("MIDI ports \"Virus TI Synth\" and \"Virus TI MIDI\" are there (read-only)");
        vti_midifeed_reader reader;
        vti_midifeed_init(&reader);
        HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
        uint8_t buf[4096];
        DWORD n = 0;
        while (ReadFile(in, buf, sizeof buf, &n, nullptr) && n > 0) // ends when the broker closes the pipe
            vti_midifeed_read(&reader, buf, (int)n, on_frame, &feed);
        hlog("the broker closed the pipe: %u messages sent, %u failed; closing the ports", feed.sent, feed.failed);
        session.DisconnectEndpointConnection(feed.conn.ConnectionId());
        session.Close();
    } catch (const winrt::hresult_error &e) {
        hlog("no MIDI ports: Windows MIDI Services error 0x%08x: %ls", (unsigned)e.code(), e.message().c_str());
        return 5;
    }
    return 0;
}
