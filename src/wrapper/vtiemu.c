// vtiemu.c - user-mode emulation of the VirusUSB.sys interface used by Virus Control.dll, on WinUSB.
//
// The Access plugin finds its driver via SetupDi(GUID {8002DBFF-...}) + CreateFileA + DeviceIoControl
// (findings E017, E034-E036). This DLL patches those imports in the Access modules:
//   - SetupDi calls for the Access GUID are redirected to our WinUSB MI_04 interface GUID (src/inf),
//   - CreateFile on that interface returns a virtual handle (an unnamed event object),
//   - DeviceIoControl on virtual handles is emulated (spec.md §11.1, §11.2): raw pipe (open/write/RX ring),
//     vendor control requests (recipient interface -> device, E040) and replayed static answers (E036).
// Everything else (e.g. the USB hub IOCTLs 0x220408/0x220410, E036) goes to the real Windows APIs.
// Log: %TEMP%\vtiemu-<pid>.log
#include "../broker/vticore.h"
#include "replay_table.h"
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <shlwapi.h>
#include <tlhelp32.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static const GUID GUID_ACCESS = {0x8002DBFF, 0xC173, 0x4BFF, {0x80, 0xBE, 0x68, 0x15, 0xD7, 0xE1, 0x30, 0x71}};
static const GUID GUID_MI04 = {0x5B45FAF8, 0x316B, 0x4B94, {0xAA, 0xBF, 0xFF, 0x44, 0x2A, 0x85, 0xC5, 0xE7}};

// ---------------------------------------------------------------------------------------------- logging
static HANDLE g_log = INVALID_HANDLE_VALUE;
static CRITICAL_SECTION g_cs;
static LARGE_INTEGER g_freq, g_t0;

void *vtiaudio_input_region(void);           // vtiaudio.c (M4)
void vtiaudio_output_ring(int start, void *buf);
void vtiaudio_buffer_switch(const uint64_t reg[3]);
void vtiaudio_f5(uint32_t ts14);
void vtiemu_log(const char *fmt, ...);

static void vlog(const char *fmt, ...)
{
    char buf[1024];
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    int n = snprintf(buf, sizeof buf, "%10.6f t%05lu ", (double)(now.QuadPart - g_t0.QuadPart) / g_freq.QuadPart,
                     GetCurrentThreadId());
    va_list ap;
    va_start(ap, fmt);
    n += vsnprintf(buf + n, sizeof buf - n - 2, fmt, ap);
    va_end(ap);
    if (n > (int)sizeof buf - 2)
        n = sizeof buf - 2;
    buf[n++] = '\n';
    DWORD w;
    EnterCriticalSection(&g_cs);
    WriteFile(g_log, buf, n, &w, NULL);
    LeaveCriticalSection(&g_cs);
}

void vtiemu_log(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    vlog("%s", buf);
}

static void vlog_vti(const char *fmt, ...) // vticore diagnostics into our log (they end with \n)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    buf[strcspn(buf, "\n")] = 0;
    vlog("vticore: %s", buf);
}

// ---------------------------------------------------------------------------------------------- device
// RX ring shared with the plugin (spec §11.2, E041): struct ring + HANDLE event at +0x8010.
typedef struct {
    volatile uint32_t wr, rd, used, size;
    uint8_t data[];
} ring;

static vti *g_dev;
static int g_dev_refs;
static ring *volatile g_ring;
static HANDLE g_ring_event;
static CRITICAL_SECTION g_dev_cs;

// Port of FUN_f104a1a0 (ring write of n bytes at wr, no wrap inside).
static void ring_put(ring *r, const uint8_t *p, uint32_t n)
{
    memcpy(r->data + r->wr, p, n);
    r->wr += n;
    if (r->used == 0 || r->rd <= r->wr - n || r->wr <= r->rd) {
        r->used += n;
        if (r->used > r->size) { // overflow: drop the oldest data
            r->rd = r->wr;
            r->used = r->size;
        }
    } else {
        r->rd = r->wr;
        r->used = r->size;
    }
}

// Port of FUN_f104a590 + the RX completion FUN_f1009c20.
static void ring_write(ring *r, const uint8_t *p, uint32_t n)
{
    n &= ~3u;
    if (!n || r->size - r->used < n)
        return;
    uint32_t first = r->size - r->wr;
    if (n < first) {
        ring_put(r, p, n);
    } else {
        ring_put(r, p, first);
        r->wr = 0;
        if (r->rd == r->size)
            r->rd = 0;
        ring_put(r, p + first, n - first);
    }
}

volatile LONG g_sync_reports; // device "Sync Information Event" (global 0x54 = 1, E072), read by vtiaudio.c

static void on_rx(void *user, uint8_t ep, const uint8_t *data, int len)
{
    (void)user;
    if (ep != VTI_EP_MI04_IN) // MI_03 (cable 1) belongs to the Windows MIDI ports, not the plugin pipe
        return;
    for (int i = 0; i + 3 < len; i += 4) // F0 00 20 | 33 01 10 | 73 00 54 | 01 F7: one aligned packet "73 00 54"
        if ((data[i] & 0x0F) == 4 && data[i + 1] == 0x73 && data[i + 2] == 0x00 && data[i + 3] == 0x54)
            InterlockedIncrement(&g_sync_reports);
    ring *r = g_ring;
    if (r) {
        ring_write(r, data, (uint32_t)len);
        SetEvent(g_ring_event);
    }
}

static int dev_acquire(void)
{
    EnterCriticalSection(&g_dev_cs);
    if (!g_dev) {
        g_dev = vti_open(on_rx, NULL);
        vlog("device open -> %p", (void *)g_dev);
    }
    int ok = g_dev != NULL;
    if (ok)
        g_dev_refs++;
    LeaveCriticalSection(&g_dev_cs);
    return ok;
}

// Reconnect transparently if the device/broker connection died (broker restart, unplug + replug).
// Called before every device access; the plugin's RX ring and event stay as they are.
static vti *dev_get(void)
{
    EnterCriticalSection(&g_dev_cs);
    if (g_dev_refs > 0 && (!g_dev || vti_is_dead(g_dev))) {
        static DWORD last_try;
        DWORD now = GetTickCount();
        if (now - last_try > 1000) { // at most one attempt per second
            last_try = now;
            vlog("device connection lost: reconnecting");
            if (g_dev)
                vti_close(g_dev);
            g_dev = vti_open(on_rx, NULL);
            vlog("reconnect -> %p", (void *)g_dev);
        }
    }
    vti *v = g_dev;
    LeaveCriticalSection(&g_dev_cs);
    return v;
}

// For vtiaudio.c (broker audio registration): the current broker connection, NULL if none.
vti *vtiemu_dev(void)
{
    return dev_get();
}

static void dev_release(void)
{
    EnterCriticalSection(&g_dev_cs);
    if (g_dev_refs > 0 && --g_dev_refs == 0) {
        g_ring = NULL;
        if (g_dev)
            vti_close(g_dev);
        g_dev = NULL;
        vlog("device closed");
    }
    LeaveCriticalSection(&g_dev_cs);
}

// ---------------------------------------------------------------------------------------------- virtual handles
static HANDLE g_vh[32];

static int is_virtual(HANDLE h)
{
    for (int i = 0; i < 32; i++)
        if (h && g_vh[i] == h)
            return 1;
    return 0;
}

static HANDLE virtual_open(const char *path)
{
    if (!dev_acquire()) {
        SetLastError(ERROR_FILE_NOT_FOUND);
        return INVALID_HANDLE_VALUE;
    }
    HANDLE h = CreateEventA(NULL, FALSE, FALSE, NULL); // any real kernel handle the plugin can close
    for (int i = 0; i < 32; i++)
        if (!g_vh[i]) {
            g_vh[i] = h;
            break;
        }
    vlog("virtual open \"%s\" -> %p", path, h);
    return h;
}

// ---------------------------------------------------------------------------------------------- IOCTLs
static BOOL reply(void *out, DWORD outsz, LPDWORD ret, const void *data, DWORD len, DWORD info)
{
    if (out && outsz) {
        memset(out, 0, outsz);
        memcpy(out, data, len < outsz ? len : outsz);
    }
    *ret = info <= outsz ? info : outsz;
    return TRUE;
}

static int g_single; // ASIO wrapper copy (vtiasio.c): patch only the one module given to vtiemu_init_module

// ASIO "performance mode" (= buffer size: 1 = 128, 2 = 192, 3 = 256, 4 = 512 samples), property 0xd (get) / 0xe
// (set) of 0x2200d0, chosen in the original ASIO driver's control panel (E086). The kernel driver kept it; here it
// is stored per user so it survives a DAW restart.
#define PERF_KEY "Software\\TI Revive\\VirusUSB\\Parameters" // = the redirected driver service key (hook_RegOpenKeyA)
static uint32_t perf_mode_get(void)
{
    DWORD v = 3, sz = sizeof v;
    if (RegGetValueA(HKEY_CURRENT_USER, PERF_KEY, "PerformanceMode", RRF_RT_REG_DWORD, NULL, &v, &sz) || v < 1 || v > 4)
        v = 3;
    return v;
}
static void perf_mode_set(uint32_t v)
{
    if (v >= 1 && v <= 4)
        RegSetKeyValueA(HKEY_CURRENT_USER, PERF_KEY, "PerformanceMode", REG_DWORD, &v, sizeof v);
}

// The device's own identity. Virus Control picks the model from the product string ("Virus TI" / "VIRUS TI" /
// "Virus TI Snow") and bcdDevice (0x100 = TI or Snow, 0x300 = TI2) - E099. The recorded answers come from a TI2
// Desktop, so they are patched with the connected device's descriptor and strings (a Snow showed up as a TI:
// Snow plug-in "no device", the TI plug-in's sync ran past the Snow's shorter bank list, E115).
static struct {
    int ok;
    uint8_t dd[18];
    char vendor[128], product[128];
} g_id;

static int read_string(vti *dev, uint8_t idx, char *out, int cap)
{
    uint8_t b[255];
    int n = idx ? vti_control(dev, 0x80, 6, (uint16_t)(0x0300 | idx), 0x0409, b, sizeof b) : -1;
    if (n < 2 || b[1] != 3)
        return -1;
    int k = 0;
    for (int i = 2; i + 1 < n && i < b[0] && k < cap - 1; i += 2)
        out[k++] = b[i + 1] ? '?' : (char)b[i]; // UTF-16LE, ASCII only
    out[k] = 0;
    return 0;
}

static int identity(void)
{
    if (g_id.ok)
        return 1;
    vti *dev = dev_get();
    uint8_t dd[18];
    if (!dev || vti_control(dev, 0x80, 6, 0x0100, 0, dd, sizeof dd) != 18 || dd[1] != 1)
        return 0;
    memcpy(g_id.dd, dd, 18);
    if (read_string(dev, dd[14], g_id.vendor, sizeof g_id.vendor))
        strcpy(g_id.vendor, "Access Music");
    if (read_string(dev, dd[15], g_id.product, sizeof g_id.product))
        strcpy(g_id.product, "Virus TI");
    g_id.ok = 1;
    vlog("device identity: %04x:%04x bcdDevice %04x \"%s\" \"%s\"", dd[8] | dd[9] << 8, dd[10] | dd[11] << 8,
         dd[12] | dd[13] << 8, g_id.vendor, g_id.product);
    return 1;
}

static BOOL replay(DWORD code, void *in, void *out, DWORD outsz, LPDWORD ret)
{
    int32_t sel = -1;
    if (code == 0x2200d0 || code == 0x2200d4) // property get/set: first DWORD selects the property
        sel = (int32_t) * (uint32_t *)(in ? in : out);
    if (code == 0x2200d0 && (sel == 0xd || sel == 0xe)) {
        if (sel == 0xe && in)
            perf_mode_set(((uint32_t *)in)[1]);
        uint32_t r[2] = {(uint32_t)sel, perf_mode_get()};
        vlog("  performance mode %s -> %u", sel == 0xe ? "set" : "get", r[1]);
        return reply(out, outsz, ret, r, sizeof r, outsz);
    }
    if ((code == 0x220000 || code == 0x220004) && identity()) { // manufacturer / product string
        const char *str = code == 0x220000 ? g_id.vendor : g_id.product;
        DWORD n = (DWORD)strlen(str) + 1;
        return reply(out, outsz, ret, str, n, n);
    }
    for (size_t i = 0; i < sizeof REPLAY / sizeof *REPLAY; i++)
        if (REPLAY[i].code == code && REPLAY[i].sel == sel) {
            uint8_t buf[512];
            const uint8_t *data = REPLAY[i].data;
            if ((code == 0x22000c || (code == 0x2200d0 && sel == 22)) && REPLAY[i].len <= sizeof buf && identity()) {
                memcpy(buf, data, REPLAY[i].len);
                if (code == 0x22000c) // device block: idVendor, idProduct, bcdDevice first
                    memcpy(buf, g_id.dd + 8, 6);
                else // property 22: the device descriptor without bLength / bDescriptorType after the selector
                    memcpy(buf + 4, g_id.dd + 2, 16);
                data = buf;
            }
            return reply(out, outsz, ret, data, REPLAY[i].len, REPLAY[i].ret);
        }
    if (sel >= 0) { // unknown property: echo the request with value 0, as the driver does for unset ones
        uint32_t echo[2] = {(uint32_t)sel, 0};
        vlog("  property %06lx sel 0x%x not recorded: echo", code, sel);
        return reply(out, outsz, ret, echo, sizeof echo, outsz);
    }
    vlog("  %06lx not recorded: zeros", code);
    return reply(out, outsz, ret, NULL, 0, outsz);
}

// Test switch, set only by scripts/latency-wine.sh (E403): the broker refuses every vendor request (ctrlguard.h, rule 2),
// so the original's flash reads at open (E026, E040) are answered from a table captured earlier from the same Virus,
// "aabbcc <32 hex digits>" per line. Nothing reaches the device; any other vendor request stays refused by the broker.
static struct {
    char addr[7];
    uint8_t data[16];
} g_flash[64];
static int g_flash_n = -1; // -1 not loaded, 0 switch off or empty
static char g_flash_addr[7];

static void flash_load(void)
{
    char path[MAX_PATH], line[128], a[8], d[40];
    g_flash_n = 0;
    if (!GetEnvironmentVariableA("VTI_FLASH_REPLAY", path, sizeof path))
        return;
    FILE *f = fopen(path, "r");
    if (!f)
        return;
    while (g_flash_n < 64 && fgets(line, sizeof line, f))
        if (sscanf(line, "%6s %32s", a, d) == 2 && strlen(a) == 6 && strlen(d) == 32) {
            memcpy(g_flash[g_flash_n].addr, a, 7);
            for (int i = 0; i < 16; i++) {
                unsigned v = 0;
                sscanf(d + 2 * i, "%2x", &v);
                g_flash[g_flash_n].data[i] = (uint8_t)v;
            }
            g_flash_n++;
        }
    fclose(f);
}

// the result of the request, or -2: not for the table
static int flash_replay(uint8_t rt, uint8_t req, uint16_t wi, uint8_t *buf, uint16_t n)
{
    if (g_flash_n < 0)
        flash_load();
    if (!g_flash_n)
        return -2;
    if (!(rt & 0x80) && req == 1 && wi == 0x12 && n == 3) { // the address of the next read
        snprintf(g_flash_addr, sizeof g_flash_addr, "%02x%02x%02x", buf[0], buf[1], buf[2]);
        return 3;
    }
    if ((rt & 0x80) && req == 0x81) {
        if (wi == 1 && n >= 1) { // status: 0x10 = ready (E040)
            buf[0] = 0x10;
            return 1;
        }
        if (wi == 0x30 && n >= 16) {
            memset(buf, 0xff, 16);
            for (int i = 0; i < g_flash_n; i++)
                if (!strcmp(g_flash[i].addr, g_flash_addr))
                    memcpy(buf, g_flash[i].data, 16);
            return 16;
        }
        return -1; // wIndex 0x22 stalls on the real device too (E026)
    }
    return -2;
}

// 0x220008 PGIOCTL_STD_VENDOR_REQUEST: 8-byte setup (bmRequestType, bRequest, wValue, wIndex, wLength) + data.
static BOOL vendor_request(void *in, DWORD insz, void *out, DWORD outsz, LPDWORD ret)
{
    if (insz < 8) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    uint8_t *s = in, buf[4096];
    uint8_t rt = s[0], req = s[1];
    uint16_t wv = s[2] | s[3] << 8, wi = s[4] | s[5] << 8, wl = s[6] | s[7] << 8;
    uint16_t n = (uint16_t)(insz - 8) < wl ? (uint16_t)(insz - 8) : wl;
    if (n > sizeof buf)
        n = sizeof buf;
    // WinUSB rewrites wIndex of interface-recipient requests; the device accepts device recipient (E040).
    uint8_t rt_dev = (rt & ~0x1f) | 0x00;
    if (!(rt & 0x80))
        memcpy(buf, s + 8, n);
    vti *dev = dev_get();
    int r = flash_replay(rt, req, wi, buf, n);
    if (r == -2)
        r = dev ? vti_control(dev, rt_dev, req, wv, wi, buf, n) : -1;
    if (rt & 0x80) {
        memcpy(out, s, 8 < outsz ? 8 : outsz);
        if (r > 0 && outsz > 8)
            memcpy((uint8_t *)out + 8, buf, (DWORD)r < outsz - 8 ? (DWORD)r : outsz - 8);
        *ret = 8 + (r > 0 ? r : 0); // a stalled request returns just the header, like the driver (E036)
    } else {
        *ret = 0;
    }
    return TRUE;
}

static BOOL emulate(HANDLE h, DWORD code, void *in, DWORD insz, void *out, DWORD outsz, LPDWORD ret)
{
    (void)h;
    switch (code) {
    case 0x220008:
        return vendor_request(in, insz, out, outsz, ret);
    case 0x22007c: // wMaxPacketSize of the IN / OUT pipe (EP 0x86 / 0x06)
    case 0x220080: {
        uint32_t mps = 0x20;
        return reply(out, outsz, ret, &mps, 4, 4);
    }
    case 0x220088: // raw pipe OPEN: out = 0x8020-byte RX ring + event handle at +0x8010
        if (!out || outsz != 0x8020) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        {
            ring *r = out;
            r->wr = r->rd = r->used = 0;
            r->size = 0x8000;
            memset(r->data, 0, 0x8000);
            g_ring_event = *(HANDLE *)((uint8_t *)out + 0x8010);
            *(uint32_t *)((uint8_t *)out + 0x8018) = 0;
            g_ring = r;
            vlog("  pipe open ring=%p event=%p", out, g_ring_event);
        }
        *ret = 0;
        return TRUE;
    case 0x22008c: // flush RX ring
        if (g_ring) {
            g_ring->wr = g_ring->rd = g_ring->used = 0;
        }
        *ret = 0;
        return TRUE;
    case 0x220090: // raw pipe CLOSE
        if (g_ring)
            *(uint32_t *)((uint8_t *)g_ring + 0x8018) = 1;
        g_ring = NULL;
        vlog("  pipe close");
        *ret = 0;
        return TRUE;
    case 0x220084: // raw pipe WRITE: USB-MIDI packets in the output buffer (Irp->UserBuffer)
        *ret = 0;
        for (DWORD i = 0; out && i + 3 < outsz; i += 4) { // plugin timestamps "F5 lo hi" (diagnostics, vtiaudio.c)
            const uint8_t *k = (const uint8_t *)out + i;
            if ((k[0] & 0x0F) == 3 && k[1] == 0xF5)
                vtiaudio_f5(k[2] | k[3] << 7);
        }
        vti *dev = dev_get();
        if (out && outsz && (!dev || vti_write(dev, VTI_EP_MI04_OUT, out, (int)outsz))) {
            SetLastError(ERROR_GEN_FAILURE);
            return FALSE;
        }
        return TRUE;
    case 0x2200a8: { // TX ring {used, free}: writes are synchronous, so always empty
        uint32_t st[2] = {0, 0x8000};
        return reply(out, outsz, ret, st, sizeof st, 8);
    }
    case 0x220010: { // map the driver's input AudioSM: {0x18, 1, 1, size, address} (E055, vtiaudio.c)
        void *region = vtiaudio_input_region();
        uint8_t r[24] = {0x18, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0xc0, 0x10, 0x08, 0};
        memcpy(r + 16, &region, 8);
        return reply(out, outsz, ret, r, sizeof r, 24);
    }
    case 0x220030: // plugin output stream start/stop: {u8 start, pad, void *buf} in the output buffer
        if (out && outsz >= 16)
            vtiaudio_output_ring(*(uint8_t *)out, *(void **)((uint8_t *)out + 8));
        *ret = 0;
        return TRUE;
    case 0x2200e4: // buffer-switch registration {routine, thread, event}
        if (out && outsz >= 24)
            vtiaudio_buffer_switch(out);
        *ret = 0;
        return TRUE;
    case 0x2200ac: // flush TX ring
    case 0x2200b0: // per-client stream id
    case 0x2200e8: // notify registration (not used by the plugin's audio path)
    case 0x220024:
    case 0x220064:
    case 0x2200ec:
    case 0x2200f0:
        *ret = 0;
        return TRUE;
    // Status queries of the original ASIO driver (VirusUSB_X64.dll, findings E082): answered with zeros =
    // 0x220068 "no stream active" (0xF04-byte status block), 0x220070 flag, 0x2200b4 "no other client with this
    // stream id", 0x2200bc {status 0 = OK, flag 0}. Zero-filled because the caller reads uninitialised locals.
    case 0x220068:
    case 0x220070:
    case 0x2200b4:
    case 0x2200bc:
        if (out && outsz)
            memset(out, 0, outsz);
        *ret = outsz;
        return TRUE;
    default:
        return replay(code, in, out, outsz, ret);
    }
}

// ---------------------------------------------------------------------------------------------- hooks
static HDEVINFO(WINAPI *real_SetupDiGetClassDevsA)(const GUID *, PCSTR, HWND, DWORD);
static BOOL(WINAPI *real_SetupDiEnumDeviceInterfaces)(HDEVINFO, PSP_DEVINFO_DATA, const GUID *, DWORD,
                                                      PSP_DEVICE_INTERFACE_DATA);
static HANDLE(WINAPI *real_CreateFileA)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
static HANDLE(WINAPI *real_CreateFileW)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
static BOOL(WINAPI *real_DeviceIoControl)(HANDLE, DWORD, LPVOID, DWORD, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);
static BOOL(WINAPI *real_CloseHandle)(HANDLE);
static HMODULE(WINAPI *real_LoadLibraryA)(LPCSTR);
static HMODULE(WINAPI *real_LoadLibraryW)(LPCWSTR);
static HMODULE(WINAPI *real_LoadLibraryExA)(LPCSTR, HANDLE, DWORD);
static HMODULE(WINAPI *real_LoadLibraryExW)(LPCWSTR, HANDLE, DWORD);

static const GUID *map_guid(const GUID *g)
{
    return g && IsEqualGUID(g, &GUID_ACCESS) ? &GUID_MI04 : g;
}

#ifdef VTIEMU_VIRTUAL_DEVICE
// Wine (Linux, findings/design-linux.md): there is no Windows device node for the Virus - the native Linux broker
// owns it. The Access GUID enumerates one virtual interface whose path carries our MI_04 GUID, so CreateFile
// below turns it into a virtual handle as on Windows.
#define FAKE_DEVINFO ((HDEVINFO)(ULONG_PTR)0x7e5a0001)
static const char VIRTUAL_PATH[] = "\\\\?\\usb#vid_133e&pid_0815#tirevive#{5b45faf8-316b-4b94-aabf-ff442a85c5e7}";
static BOOL(WINAPI *real_SetupDiGetDeviceInterfaceDetailA)(HDEVINFO, PSP_DEVICE_INTERFACE_DATA,
                                                           PSP_DEVICE_INTERFACE_DETAIL_DATA_A, DWORD, PDWORD,
                                                           PSP_DEVINFO_DATA);
static BOOL(WINAPI *real_SetupDiDestroyDeviceInfoList)(HDEVINFO);
static BOOL(WINAPI *real_SetupDiOpenDeviceInfoA)(HDEVINFO, PCSTR, HWND, DWORD, PSP_DEVINFO_DATA);

static BOOL WINAPI hook_SetupDiGetDeviceInterfaceDetailA(HDEVINFO di, PSP_DEVICE_INTERFACE_DATA id,
                                                         PSP_DEVICE_INTERFACE_DETAIL_DATA_A det, DWORD sz, PDWORD req,
                                                         PSP_DEVINFO_DATA dd)
{
    if (di != FAKE_DEVINFO) {
        if (!real_SetupDiGetDeviceInterfaceDetailA)
            real_SetupDiGetDeviceInterfaceDetailA =
                (void *)GetProcAddress(GetModuleHandleA("setupapi.dll"), "SetupDiGetDeviceInterfaceDetailA");
        return real_SetupDiGetDeviceInterfaceDetailA(di, id, det, sz, req, dd);
    }
    DWORD need = (DWORD)(offsetof(SP_DEVICE_INTERFACE_DETAIL_DATA_A, DevicePath) + sizeof VIRTUAL_PATH);
    if (req)
        *req = need;
    if (dd && dd->cbSize == sizeof *dd) {
        memset(&dd->ClassGuid, 0, sizeof dd->ClassGuid);
        dd->DevInst = 1;
    }
    vlog("SetupDiGetDeviceInterfaceDetailA (virtual) size %lu", sz);
    if (!det || sz < need) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    memcpy(det->DevicePath, VIRTUAL_PATH, sizeof VIRTUAL_PATH);
    return TRUE;
}

static BOOL WINAPI hook_SetupDiDestroyDeviceInfoList(HDEVINFO di)
{
    if (di == FAKE_DEVINFO)
        return TRUE;
    if (!real_SetupDiDestroyDeviceInfoList)
        real_SetupDiDestroyDeviceInfoList =
            (void *)GetProcAddress(GetModuleHandleA("setupapi.dll"), "SetupDiDestroyDeviceInfoList");
    return real_SetupDiDestroyDeviceInfoList(di);
}

static BOOL WINAPI hook_SetupDiOpenDeviceInfoA(HDEVINFO di, PCSTR inst, HWND w, DWORD fl, PSP_DEVINFO_DATA dd)
{
    vlog("SetupDiOpenDeviceInfoA \"%s\"%s", inst ? inst : "", di == FAKE_DEVINFO ? " (virtual)" : "");
    if (di == FAKE_DEVINFO) {
        if (dd && dd->cbSize == sizeof *dd)
            dd->DevInst = 1;
        return TRUE;
    }
    if (!real_SetupDiOpenDeviceInfoA)
        real_SetupDiOpenDeviceInfoA = (void *)GetProcAddress(GetModuleHandleA("setupapi.dll"), "SetupDiOpenDeviceInfoA");
    return real_SetupDiOpenDeviceInfoA(di, inst, w, fl, dd);
}
#endif

static HDEVINFO WINAPI hook_SetupDiGetClassDevsA(const GUID *g, PCSTR en, HWND w, DWORD fl)
{
#ifdef VTIEMU_VIRTUAL_DEVICE
    if (g && IsEqualGUID(g, &GUID_ACCESS)) {
        vlog("SetupDiGetClassDevsA Access GUID -> virtual device list");
        return FAKE_DEVINFO;
    }
#endif
    if (!real_SetupDiGetClassDevsA)
        real_SetupDiGetClassDevsA = (void *)GetProcAddress(GetModuleHandleA("setupapi.dll"), "SetupDiGetClassDevsA");
    if (g && IsEqualGUID(g, &GUID_ACCESS))
        vlog("SetupDiGetClassDevsA Access GUID -> WinUSB MI_04 GUID");
    return real_SetupDiGetClassDevsA(map_guid(g), en, w, fl);
}

static BOOL WINAPI hook_SetupDiEnumDeviceInterfaces(HDEVINFO di, PSP_DEVINFO_DATA dd, const GUID *g, DWORD i,
                                                    PSP_DEVICE_INTERFACE_DATA id)
{
    if (!real_SetupDiEnumDeviceInterfaces)
        real_SetupDiEnumDeviceInterfaces =
            (void *)GetProcAddress(GetModuleHandleA("setupapi.dll"), "SetupDiEnumDeviceInterfaces");
#ifdef VTIEMU_VIRTUAL_DEVICE
    if (di == FAKE_DEVINFO) {
        if (i > 0 || !id) {
            SetLastError(ERROR_NO_MORE_ITEMS);
            return FALSE;
        }
        id->InterfaceClassGuid = GUID_ACCESS;
        id->Flags = SPINT_ACTIVE;
        id->Reserved = 1;
        return TRUE;
    }
#endif
    return real_SetupDiEnumDeviceInterfaces(di, dd, map_guid(g), i, id);
}

static HANDLE WINAPI hook_CreateFileA(LPCSTR name, DWORD acc, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp,
                                     DWORD fl, HANDLE tmpl)
{
    if (name && StrStrIA(name, "{5b45faf8-316b-4b94-aabf-ff442a85c5e7}"))
        return virtual_open(name);
    return real_CreateFileA(name, acc, share, sa, disp, fl, tmpl);
}

static HANDLE WINAPI hook_CreateFileW(LPCWSTR name, DWORD acc, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp,
                                     DWORD fl, HANDLE tmpl)
{
    if (name && StrStrIW(name, L"{5b45faf8-316b-4b94-aabf-ff442a85c5e7}")) {
        char a[MAX_PATH * 2];
        WideCharToMultiByte(CP_ACP, 0, name, -1, a, sizeof a, NULL, NULL);
        return virtual_open(a);
    }
    return real_CreateFileW(name, acc, share, sa, disp, fl, tmpl);
}

static BOOL WINAPI hook_DeviceIoControl(HANDLE h, DWORD code, LPVOID in, DWORD insz, LPVOID out, DWORD outsz,
                                        LPDWORD ret, LPOVERLAPPED ov)
{
    if (!is_virtual(h))
        return real_DeviceIoControl(h, code, in, insz, out, outsz, ret, ov);
    DWORD got = 0;
    if (!ret)
        ret = &got;
    BOOL ok = emulate(h, code, in, insz, out, outsz, ret);
    if (code != 0x220084 && code != 0x2200a8) // the two high-rate codes are not logged
        vlog("IOCTL %06lx in=%lu out=%lu -> ok=%d ret=%lu", code, insz, outsz, ok, *ret);
    if (ov && ok) { // the plugin calls synchronously (E036); complete overlapped callers immediately
        ov->Internal = 0;
        ov->InternalHigh = *ret;
        if (ov->hEvent)
            SetEvent(ov->hEvent);
    }
    return ok;
}

static BOOL WINAPI hook_CloseHandle(HANDLE h)
{
    if (h && is_virtual(h)) {
        for (int i = 0; i < 32; i++)
            if (g_vh[i] == h)
                g_vh[i] = NULL;
        vlog("virtual close %p", h);
        dev_release();
    }
    return real_CloseHandle(h);
}

// ---- device topology (Control Center, E050) ----
// With the Access driver the {8002DBFF} interface lives on the USB device node itself (parent = hub). Ours
// lives on the MI_04 child of the composite device (parent = composite). Programs that walk to the hub via
// CM_Get_Parent / read the instance ID (Control Center: "no Virus TI found") see the original layout:
// MI_04 node -> reported as the composite device (ID) whose parent is the hub.
static CONFIGRET(WINAPI *real_CM_Get_Parent)(PDEVINST, DEVINST, ULONG);
static CONFIGRET(WINAPI *real_CM_Get_Device_IDA)(DEVINST, PSTR, ULONG, ULONG);

static int is_our_mi04(DEVINST d)
{
    char id[MAX_DEVICE_ID_LEN];
    return real_CM_Get_Device_IDA && real_CM_Get_Device_IDA(d, id, sizeof id, 0) == CR_SUCCESS &&
           StrStrIA(id, "VID_133E&PID_0815&MI_04");
}

static CONFIGRET WINAPI hook_CM_Get_Parent(PDEVINST parent, DEVINST d, ULONG fl)
{
    CONFIGRET r = real_CM_Get_Parent(parent, d, fl);
    if (r == CR_SUCCESS && is_our_mi04(d)) {
        DEVINST composite = *parent;
        r = real_CM_Get_Parent(parent, composite, fl); // skip the composite node: hub, as with VirusUSB.sys
    }
    char id[MAX_DEVICE_ID_LEN] = "";
    if (r == CR_SUCCESS && real_CM_Get_Device_IDA)
        real_CM_Get_Device_IDA(*parent, id, sizeof id, 0);
    vlog("CM_Get_Parent(%lu) -> %lu cr=%lu %s", d, r == CR_SUCCESS ? *parent : 0, r, id);
    return r;
}

static CONFIGRET WINAPI hook_CM_Get_Device_IDA(DEVINST d, PSTR buf, ULONG len, ULONG fl)
{
    DEVINST use = d, parent;
    if (is_our_mi04(d) && real_CM_Get_Parent(&parent, d, 0) == CR_SUCCESS)
        use = parent; // report the composite device's ID (USB\VID_133E&PID_0815\...)
    CONFIGRET r = real_CM_Get_Device_IDA(use, buf, len, fl);
    vlog("CM_Get_Device_IDA(%lu) -> cr=%lu %s", d, r, r == CR_SUCCESS ? buf : "");
    return r;
}

static FARPROC(WINAPI *real_GetProcAddress)(HMODULE, LPCSTR);

static FARPROC WINAPI hook_GetProcAddress(HMODULE m, LPCSTR name)
{
    FARPROC p = real_GetProcAddress(m, name);
    if (!p || (ULONG_PTR)name < 0x10000) // ordinal lookup
        return p;
    if (!strcmp(name, "CM_Get_Parent")) {
        real_CM_Get_Parent = (void *)p;
        if (!real_CM_Get_Device_IDA)
            real_CM_Get_Device_IDA = (void *)real_GetProcAddress(m, "CM_Get_Device_IDA");
        return (FARPROC)(void *)hook_CM_Get_Parent;
    }
    if (!strcmp(name, "CM_Get_Device_IDA")) {
        real_CM_Get_Device_IDA = (void *)p;
        if (!real_CM_Get_Parent)
            real_CM_Get_Parent = (void *)real_GetProcAddress(m, "CM_Get_Parent");
        return (FARPROC)(void *)hook_CM_Get_Device_IDA;
    }
    return p;
}

static void patch_module(HMODULE m, int all);

static void patch_access_modules(void);


static void after_load(HMODULE m)
{
    if (!m || g_single)
        return;
    char path[MAX_PATH];
    GetModuleFileNameA(m, path, sizeof path);
    if (StrStrIA(path, "Virus Control") || StrStrIA(path, "Virus TI")) {
        vlog("loaded %s", path);
        // Virus TI.vst3 loads Virus Control.dll inside its own DllMain, i.e. before this hook could patch the
        // loader (2nd test: plugin said "device not connected"), so look for all Access modules now.
        patch_access_modules();
    }
}

static HMODULE WINAPI hook_LoadLibraryA(LPCSTR n)
{
    HMODULE m = real_LoadLibraryA(n);
    after_load(m);
    return m;
}
static HMODULE WINAPI hook_LoadLibraryW(LPCWSTR n)
{
    HMODULE m = real_LoadLibraryW(n);
    after_load(m);
    return m;
}
static HMODULE WINAPI hook_LoadLibraryExA(LPCSTR n, HANDLE f, DWORD fl)
{
    HMODULE m = real_LoadLibraryExA(n, f, fl);
    after_load(m);
    return m;
}
static HMODULE WINAPI hook_LoadLibraryExW(LPCWSTR n, HANDLE f, DWORD fl)
{
    HMODULE m = real_LoadLibraryExW(n, f, fl);
    after_load(m);
    return m;
}

// The original ASIO driver keeps its settings (PerformanceMode = buffer size) in the old kernel driver's service
// key HKLM\System\CurrentControlSet\Services\VIRUSUSB\..., which does not exist without the Access driver and is
// not writable for a DAW. In the ASIO copy that key is redirected to HKCU\Software\TI Revive\VirusUSB\... (E086).
#define SVC_KEY "System\\CurrentControlSet\\SERVICES\\VIRUSUSB"
static int redirect_key(HKEY root, LPCSTR sub, char *to, size_t cap)
{
    size_t n = strlen(SVC_KEY);
    if (!g_single || root != HKEY_LOCAL_MACHINE || !sub || _strnicmp(sub, SVC_KEY, n))
        return 0;
    snprintf(to, cap, "Software\\TI Revive\\VirusUSB%s", sub + n);
    return 1;
}
// The hooks call the real functions through pointers taken with GetProcAddress (init_common), never by name: a
// call by name goes through this module's own import table, and if that were ever patched the hook would call
// itself (0.4.1: the plug-in hung in IComponent::initialize, E092).
static LSTATUS(WINAPI *real_RegOpenKeyA)(HKEY, LPCSTR, PHKEY);
static LSTATUS(WINAPI *real_RegOpenKeyExA)(HKEY, LPCSTR, DWORD, REGSAM, PHKEY);
static LSTATUS(WINAPI *real_RegCreateKeyA)(HKEY, LPCSTR, PHKEY);
static LSTATUS(WINAPI *real_RegCreateKeyExA)(HKEY, LPCSTR, DWORD, LPSTR, DWORD, REGSAM, const LPSECURITY_ATTRIBUTES,
                                             PHKEY, LPDWORD);

static LSTATUS WINAPI hook_RegOpenKeyA(HKEY root, LPCSTR sub, PHKEY out)
{
    char to[512];
    if (redirect_key(root, sub, to, sizeof to))
        return real_RegCreateKeyA(HKEY_CURRENT_USER, to, out); // create: the panel opens before it writes
    return real_RegOpenKeyA(root, sub, out);
}
static LSTATUS WINAPI hook_RegOpenKeyExA(HKEY root, LPCSTR sub, DWORD opt, REGSAM sam, PHKEY out)
{
    char to[512];
    if (redirect_key(root, sub, to, sizeof to))
        return real_RegCreateKeyExA(HKEY_CURRENT_USER, to, 0, NULL, 0, sam, NULL, out, NULL);
    return real_RegOpenKeyExA(root, sub, opt, sam, out);
}
static LSTATUS WINAPI hook_RegCreateKeyA(HKEY root, LPCSTR sub, PHKEY out)
{
    char to[512];
    if (redirect_key(root, sub, to, sizeof to))
        return real_RegCreateKeyA(HKEY_CURRENT_USER, to, out);
    return real_RegCreateKeyA(root, sub, out);
}

static const struct {
    const char *dll, *fn;
    void *hook;
    int loader; // 1 = also patched in non-Access modules (to see Access modules being loaded)
} HOOKS[] = {
    {"setupapi.dll", "SetupDiGetClassDevsA", hook_SetupDiGetClassDevsA, 0},
    {"setupapi.dll", "SetupDiEnumDeviceInterfaces", hook_SetupDiEnumDeviceInterfaces, 0},
#ifdef VTIEMU_VIRTUAL_DEVICE
    {"setupapi.dll", "SetupDiGetDeviceInterfaceDetailA", hook_SetupDiGetDeviceInterfaceDetailA, 0},
    {"setupapi.dll", "SetupDiDestroyDeviceInfoList", hook_SetupDiDestroyDeviceInfoList, 0},
    {"setupapi.dll", "SetupDiOpenDeviceInfoA", hook_SetupDiOpenDeviceInfoA, 0},
#endif
    {"kernel32.dll", "CreateFileA", hook_CreateFileA, 0},
    {"kernel32.dll", "CreateFileW", hook_CreateFileW, 0},
    {"kernel32.dll", "DeviceIoControl", hook_DeviceIoControl, 0},
    {"kernel32.dll", "CloseHandle", hook_CloseHandle, 0},
    {"kernel32.dll", "LoadLibraryA", hook_LoadLibraryA, 1},
    {"kernel32.dll", "LoadLibraryW", hook_LoadLibraryW, 1},
    {"kernel32.dll", "LoadLibraryExA", hook_LoadLibraryExA, 1},
    {"kernel32.dll", "LoadLibraryExW", hook_LoadLibraryExW, 1},
    {"kernel32.dll", "GetProcAddress", hook_GetProcAddress, 0},
    {"advapi32.dll", "RegOpenKeyA", hook_RegOpenKeyA, 0},
    {"advapi32.dll", "RegOpenKeyExA", hook_RegOpenKeyExA, 0},
    {"advapi32.dll", "RegCreateKeyA", hook_RegCreateKeyA, 0},
};

static int dll_matches(const char *imp, const char *want)
{
    if (!_stricmp(imp, want))
        return 1;
    // kernel32 functions are often imported through API sets (api-ms-win-core-*.dll)
    return !_stricmp(want, "kernel32.dll") && !_strnicmp(imp, "api-ms-win-core-", 16);
}

// The module this emulation is linked into (wrapper VST3, ASIO wrapper, vtiemu*.dll): never patched, its own
// calls must reach the real functions (E092).
static HMODULE self_module(void)
{
    static HMODULE self;
    if (!self)
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCWSTR)(void *)self_module, &self);
    return self;
}

// Replace the IAT slots of module m that import one of HOOKS by name (all = 0: only the loader hooks).
static void patch_module(HMODULE m, int all)
{
    if (m == self_module())
        return;
    BYTE *base = (BYTE *)m;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress)
        return;
    int patched = 0;
    for (IMAGE_IMPORT_DESCRIPTOR *d = (void *)(base + dir.VirtualAddress); d->Name; d++) {
        const char *dll = (const char *)(base + d->Name);
        if (!d->OriginalFirstThunk)
            continue;
        IMAGE_THUNK_DATA *names = (void *)(base + d->OriginalFirstThunk), *slots = (void *)(base + d->FirstThunk);
        for (; names->u1.AddressOfData; names++, slots++) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal))
                continue;
            const char *fn = (const char *)((IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData))->Name;
            for (size_t i = 0; i < sizeof HOOKS / sizeof *HOOKS; i++) {
                if ((!all && !HOOKS[i].loader) || !dll_matches(dll, HOOKS[i].dll) || strcmp(fn, HOOKS[i].fn) ||
                    slots->u1.Function == (ULONG_PTR)HOOKS[i].hook)
                    continue;
                DWORD old;
                VirtualProtect(&slots->u1.Function, sizeof(ULONG_PTR), PAGE_READWRITE, &old);
                slots->u1.Function = (ULONG_PTR)HOOKS[i].hook;
                VirtualProtect(&slots->u1.Function, sizeof(ULONG_PTR), old, &old);
                patched++;
            }
        }
    }
    if (patched && all) {
        char path[MAX_PATH];
        GetModuleFileNameA(m, path, sizeof path);
        vlog("patched %d imports in %s", patched, path);
    }
}

// Fully patch every Access module currently loaded (idempotent).
static void patch_access_modules(void)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snap == INVALID_HANDLE_VALUE)
        return;
    MODULEENTRY32W me = {.dwSize = sizeof me};
    for (BOOL ok = Module32FirstW(snap, &me); ok; ok = Module32NextW(snap, &me))
        if (StrStrIW(me.szModule, L"Virus TI") || StrStrIW(me.szModule, L"Virus Control"))
            patch_module(me.hModule, 1);
    CloseHandle(snap);
}

// Patch the loader imports of every module already in the process (so the host's load of Virus TI.vst3 is
// seen), and fully patch Access modules that are already loaded.
static void patch_process(HMODULE self)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    if (snap == INVALID_HANDLE_VALUE)
        return;
    MODULEENTRY32W me = {.dwSize = sizeof me};
    wchar_t windir[MAX_PATH];
    UINT wl = GetWindowsDirectoryW(windir, MAX_PATH);
    for (BOOL ok = Module32FirstW(snap, &me); ok; ok = Module32NextW(snap, &me)) {
        // Never patch system modules: kernel32's LoadLibrary* are stubs that call through their own IAT, so
        // hooking them there makes our hook call itself (first test crashed with a stack overflow).
        if (me.hModule == self || !_wcsnicmp(me.szExePath, windir, wl))
            continue;
        int access = StrStrIW(me.szModule, L"Virus TI") || StrStrIW(me.szModule, L"Virus Control");
        patch_module(me.hModule, access);
    }
    CloseHandle(snap);
}

// Set up logging and the hooks. self = this module (never patched). Called from DllMain when injected
// (vtiemu.dll) or from the wrapper VST3 before it loads the original plugin (vtiwrap.c).
#ifndef VTIEMU_LOG_PREFIX
#define VTIEMU_LOG_PREFIX "vtiemu" // the ASIO wrapper uses its own log (two emulation copies in one DAW process)
#endif

static void init_common(void)
{
    static LONG once;
    if (InterlockedExchange(&once, 1))
        return;
    {
        InitializeCriticalSection(&g_cs);
        InitializeCriticalSection(&g_dev_cs);
        QueryPerformanceFrequency(&g_freq);
        QueryPerformanceCounter(&g_t0);
        char path[MAX_PATH + 32], tmp[MAX_PATH];
        GetTempPathA(sizeof tmp, tmp);
        snprintf(path, sizeof path, "%s" VTIEMU_LOG_PREFIX "-%lu.log", tmp, GetCurrentProcessId());
        g_log = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, 0, NULL);
        // Real functions from kernelbase (kernel32 exports are forwarders/stubs on Win10+), fall back to kernel32.
        HMODULE k32 = GetModuleHandleA("kernelbase.dll");
        if (!k32)
            k32 = GetModuleHandleA("kernel32.dll");
        real_CreateFileA = (void *)GetProcAddress(k32, "CreateFileA");
        real_CreateFileW = (void *)GetProcAddress(k32, "CreateFileW");
        real_DeviceIoControl = (void *)GetProcAddress(k32, "DeviceIoControl");
        real_CloseHandle = (void *)GetProcAddress(k32, "CloseHandle");
        real_LoadLibraryA = (void *)GetProcAddress(k32, "LoadLibraryA");
        real_LoadLibraryW = (void *)GetProcAddress(k32, "LoadLibraryW");
        real_LoadLibraryExA = (void *)GetProcAddress(k32, "LoadLibraryExA");
        real_LoadLibraryExW = (void *)GetProcAddress(k32, "LoadLibraryExW");
        real_GetProcAddress = (void *)GetProcAddress(k32, "GetProcAddress");
        HMODULE adv = LoadLibraryA("advapi32.dll");
        real_RegOpenKeyA = (void *)GetProcAddress(adv, "RegOpenKeyA");
        real_RegOpenKeyExA = (void *)GetProcAddress(adv, "RegOpenKeyExA");
        real_RegCreateKeyA = (void *)GetProcAddress(adv, "RegCreateKeyA");
        real_RegCreateKeyExA = (void *)GetProcAddress(adv, "RegCreateKeyExA");
        char exe[MAX_PATH];
        GetModuleFileNameA(NULL, exe, sizeof exe);
        vlog("vtiemu attached to pid %lu %s", GetCurrentProcessId(), exe);
        vti_log = vlog_vti;
    }
}

void vtiemu_init(HMODULE self)
{
    static LONG once;
    if (InterlockedExchange(&once, 1))
        return;
    init_common();
    patch_process(self);
}

// ASIO wrapper: log + real function pointers without patching anything (vtiemu_log is usable afterwards).
void vtiemu_init_log(void)
{
    g_single = 1;
    init_common();
}

// ASIO wrapper: emulate the driver for exactly one module (the original VirusUSB_X64.dll), nothing else in the
// process (the plug-in's own emulation copy handles Virus TI.vst3 / Virus Control.dll).
void vtiemu_init_module(HMODULE target)
{
    g_single = 1;
    init_common();
    patch_module(target, 1);
}

// Fully patch Access modules loaded by the caller (the wrapper calls this right after loading the plugin).
void vtiemu_patch_access(void)
{
    patch_access_modules();
}

#ifndef VTIEMU_NO_DLLMAIN
BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID unused)
{
    (void)unused;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        vtiemu_init(inst);
    }
    return TRUE;
}
#endif
