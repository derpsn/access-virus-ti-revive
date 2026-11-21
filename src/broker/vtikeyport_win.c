// vtikeyport_win.c - see vtikeyport.h. Windows: the ports come from Windows MIDI Services (Windows 11 25H2 and later,
// with the API in Windows itself), which needs C++/WinRT; the broker is plain C. So a small helper,
// tirevive-midiports.exe (src/midiports-win), creates the virtual device, and the broker feeds it the messages on its
// standard input (midifeed.h). The installer puts the helper next to the broker only on a Windows that supports it
// (user 2026-11-15: "if not its not installed"); without it there are no ports, as before. The helper ends when the
// broker closes the pipe (its exit, a crash too), so the ports live as long as the broker, as on macOS and Linux.
#include "vtikeyport.h"
#include "midifeed.h"
#include "vticore.h"
#include "devslot.h"
#include <windows.h>
#include <stdio.h>
#include <wchar.h>

static HANDLE g_pipe = INVALID_HANDLE_VALUE; // the helper's standard input
static volatile LONG g_sent[2], g_failed[2];  // [0] "TI Revive MIDI" (cable 0), [1] "TI Revive Synth" (cable 1)
static char g_name[2][32] = {"TI Revive MIDI", "TI Revive Synth"}; // Virus N of several: "TI Revive N MIDI" (#45)

void vti_keyport_start(void)
{
    vti_slot_port_name(g_name[0], sizeof g_name[0], "MIDI", vti_slot);
    vti_slot_port_name(g_name[1], sizeof g_name[1], "Synth", vti_slot);
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return;
    wchar_t *slash = wcsrchr(path, L'\\');
    if (!slash || (size_t)(slash - path) + 25 >= MAX_PATH)
        return;
    wcscpy(slash + 1, L"tirevive-midiports.exe");
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
        vti_log("MIDI ports: not installed (they need Windows 11 25H2 or later with Windows MIDI Services)\n");
        return;
    }
    SECURITY_ATTRIBUTES sa = {sizeof sa, NULL, TRUE};
    HANDLE rd = NULL, wr = NULL;
    if (!CreatePipe(&rd, &wr, &sa, 64 * 1024)) {
        vti_log("MIDI ports: no pipe (%lu)\n", GetLastError());
        return;
    }
    SetHandleInformation(wr, HANDLE_FLAG_INHERIT, 0); // only the read end goes to the helper
    // never block the USB reader thread: a stuck helper costs messages, not the Virus' input
    DWORD mode = PIPE_NOWAIT;
    SetNamedPipeHandleState(wr, &mode, NULL, NULL);
    HANDLE nul = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);
    STARTUPINFOW si;
    ZeroMemory(&si, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = rd;
    si.hStdOutput = nul;
    si.hStdError = nul;
    PROCESS_INFORMATION pi;
    wchar_t cmd[MAX_PATH + 16];
    if (vti_slot > 1) // the helper names its ports and log for Virus N
        swprintf(cmd, MAX_PATH + 16, L"\"%ls\" -slot %d", path, vti_slot);
    else
        swprintf(cmd, MAX_PATH + 16, L"\"%ls\"", path);
    const BOOL ok = CreateProcessW(path, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    const DWORD err = GetLastError();
    CloseHandle(rd);
    if (nul != INVALID_HANDLE_VALUE)
        CloseHandle(nul);
    if (!ok) {
        CloseHandle(wr);
        vti_log("MIDI ports: helper not started (%lu)\n", err);
        return;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    g_pipe = wr;
    char sfx[4];
    vti_slot_suffix(sfx, vti_slot);
    vti_log("MIDI ports: helper started (\"%s\", \"%s\", read-only; its log: %%TEMP%%\\tirevive-midiports%s.log)\n",
            g_name[0], g_name[1], sfx);
}

// the pipe is non-blocking (PIPE_NOWAIT): WriteFile may take part of a frame or nothing; -1 on an error
static int pipe_write(void *user, const uint8_t *b, int n)
{
    (void)user;
    DWORD w = 0;
    return WriteFile(g_pipe, b, (DWORD)n, &w, NULL) ? (int)w : -1;
}

static vti_midifeed_out g_out; // a frame's tail the pipe did not take yet (USB reader thread only)

static void send_to(int port, const uint8_t *msg, int n)
{
    if (g_pipe == INVALID_HANDLE_VALUE)
        return;
    uint8_t buf[8];
    const int len = vti_midifeed_frame(port, msg, n, buf, (int)sizeof buf);
    if (len <= 0)
        return;
    if (vti_midifeed_send(&g_out, buf, len, pipe_write, NULL)) { // whole frames only (audit #26)
        if (InterlockedIncrement(&g_sent[port]) == 1)
            vti_log("MIDI port \"%s\": first message handed to the helper\n", g_name[port]);
    } else if (InterlockedIncrement(&g_failed[port]) == 1) { // once: this runs per message on the USB reader thread
        vti_log("MIDI port \"%s\": message not handed to the helper (%lu)\n", g_name[port], GetLastError());
    }
}

void vti_keyport_send(const uint8_t *msg, int n) { send_to(1, msg, n); }
void vti_dinport_send(const uint8_t *msg, int n) { send_to(0, msg, n); }

void vti_keyport_log_counts(void)
{
    if (g_pipe == INVALID_HANDLE_VALUE)
        return;
    for (int k = 0; k < 2; k++)
        vti_log("MIDI port \"%s\": %ld messages handed to the helper, %ld failed\n", g_name[k], g_sent[k],
                g_failed[k]);
}
