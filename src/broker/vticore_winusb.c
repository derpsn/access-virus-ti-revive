// vticore_winusb.c - Windows backend of vticore.h on the inbox WinUSB driver (src/inf/vti_winusb.inf binds
// WinUSB to MI_03 and MI_04, each with its own device interface GUID). Packing helpers are shared with the
// libusb backend via vticore_midi.c.
#include "vticore.h"
#include <windows.h>
#include <setupapi.h>
#include <winusb.h>
#include "writeretry.h"
#include "vtireaderr.h"
#include "linkrule.h" // link at open, unlink at close like the original driver (E524)
#include <stdio.h>
#include <stdlib.h>

static const GUID GUID_MI03 = {0xCF293C20, 0xF175, 0x42AB, {0xB2, 0xBE, 0xA0, 0x2F, 0xEC, 0x94, 0x09, 0x37}};
static const GUID GUID_MI04 = {0x5B45FAF8, 0x316B, 0x4B94, {0xAA, 0xBF, 0xFF, 0x44, 0x2A, 0x85, 0xC5, 0xE7}};
static const uint8_t HANDSHAKE[4] = {0x4e, 0x73, 0x52, 0x01}; // spec §3, sent raw on EP 0x05

typedef struct {
    HANDLE file;
    WINUSB_INTERFACE_HANDLE usb;
} iface;

struct vti {
    iface mi[2]; // [0] = MI_03, [1] = MI_04
    vti_rx_cb cb;
    void *user;
    volatile LONG stop, dead;
    HANDLE rd[2];
    int linked; // the last link message sent (E524)
};

static int open_iface(const GUID *g, iface *out)
{
    HDEVINFO di = SetupDiGetClassDevsW(g, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (di == INVALID_HANDLE_VALUE)
        return -1;
    SP_DEVICE_INTERFACE_DATA id = {.cbSize = sizeof id};
    int ok = -1;
    if (SetupDiEnumDeviceInterfaces(di, NULL, g, 0, &id)) {
        DWORD need = 0;
        SetupDiGetDeviceInterfaceDetailW(di, &id, NULL, 0, &need, NULL);
        SP_DEVICE_INTERFACE_DETAIL_DATA_W *d = malloc(need);
        d->cbSize = sizeof *d;
        if (SetupDiGetDeviceInterfaceDetailW(di, &id, d, need, NULL, NULL)) {
            out->file = CreateFileW(d->DevicePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, NULL);
            if (out->file != INVALID_HANDLE_VALUE && WinUsb_Initialize(out->file, &out->usb)) {
                ok = 0;
                // Writes must never block forever (a stuck device hung the DAW, E042): 1 s on both OUT pipes.
                ULONG t = 1000;
                WinUsb_SetPipePolicy(out->usb, VTI_EP_MI03_OUT, PIPE_TRANSFER_TIMEOUT, sizeof t, &t);
                WinUsb_SetPipePolicy(out->usb, VTI_EP_MI04_OUT, PIPE_TRANSFER_TIMEOUT, sizeof t, &t);
                // Clear halts left by aborted transfers of a previous client (reads failed with error 31).
                static const uint8_t eps[] = {VTI_EP_MI03_OUT, VTI_EP_MI03_IN, VTI_EP_MI04_OUT, VTI_EP_MI04_IN};
                for (size_t i = 0; i < sizeof eps; i++)
                    WinUsb_ResetPipe(out->usb, eps[i]);
            }
            else
                vti_log("vti: open %ls failed: %lu\n", d->DevicePath, GetLastError());
        }
        free(d);
    }
    SetupDiDestroyDeviceInfoList(di);
    return ok;
}

static iface *iface_for(vti *v, uint8_t ep)
{
    return &v->mi[(ep & 0x0f) == 0x06];
}

typedef struct {
    vti *v;
    uint8_t ep;
} rd_arg;

static DWORD WINAPI reader(LPVOID p)
{
    rd_arg a = *(rd_arg *)p;
    free(p);
    iface *f = iface_for(a.v, a.ep);
    ULONG timeout = 200; // ms, so the stop flag is noticed
    WinUsb_SetPipePolicy(f->usb, a.ep, PIPE_TRANSFER_TIMEOUT, sizeof timeout, &timeout);
    uint8_t buf[64];
    int fails = 0;
    while (!a.v->stop) {
        ULONG got = 0;
        if (WinUsb_ReadPipe(f->usb, a.ep, buf, sizeof buf, &got, NULL) && got) {
            fails = 0;
            a.v->cb(a.v->user, a.ep, buf, (int)got);
        }
        else {
            DWORD e = GetLastError();
            if (e != ERROR_SEM_TIMEOUT && e != 0) {
                vti_log("vti: read ep %02x: error %lu\n", a.ep, e);
                if (e == ERROR_GEN_FAILURE)
                    fails++;
                if (vti_read_error_dead(e, fails)) { // removed (also 433, vtireaderr.h), or stalled beyond recovery
                    a.v->dead = 1;
                    break;
                }
                if (e == ERROR_GEN_FAILURE) // stalled pipe: clear the halt and keep reading
                    WinUsb_ResetPipe(f->usb, a.ep);
                Sleep(10);
            }
        }
    }
    return 0;
}

vti *vti_open(vti_rx_cb cb, void *user)
{
    vti *v = calloc(1, sizeof *v);
    v->cb = cb;
    v->user = user;
    if (open_iface(&GUID_MI03, &v->mi[0]) || open_iface(&GUID_MI04, &v->mi[1])) {
        vti_log("vti: Virus TI WinUSB interfaces not found (is vti_winusb.inf installed?)\n");
        vti_close(v);
        return NULL;
    }
    // Readers first: if nobody drains EP 0x85/0x86, the device stops accepting OUT data, including the
    // handshake (E042).
    uint8_t eps[2] = {VTI_EP_MI03_IN, VTI_EP_MI04_IN};
    for (int i = 0; i < 2; i++) {
        rd_arg *a = malloc(sizeof *a);
        a->v = v;
        a->ep = eps[i];
        v->rd[i] = CreateThread(NULL, 0, reader, a, 0, NULL);
    }
    if (vti_write(v, VTI_EP_MI03_OUT, HANDSHAKE, sizeof HANDSHAKE)) {
        vti_close(v);
        return NULL;
    }
    v->linked = 1;
    return v;
}

void vti_close(vti *v)
{
    if (!v)
        return;
    if (v->linked && !v->dead) // the original's vtimidi.sys unlinks when its last stream closes (E524)
        vti_link(v, 0);
    v->stop = 1;
    for (int i = 0; i < 2; i++)
        if (v->rd[i]) {
            WaitForSingleObject(v->rd[i], 2000);
            CloseHandle(v->rd[i]);
        }
    for (int i = 0; i < 2; i++) {
        if (v->mi[i].usb)
            WinUsb_Free(v->mi[i].usb);
        if (v->mi[i].file && v->mi[i].file != INVALID_HANDLE_VALUE)
            CloseHandle(v->mi[i].file);
    }
    free(v);
}

struct write_ctx {
    iface *f;
    uint8_t ep;
    DWORD err; // last error that was not the pipe timeout
};

static int write_packet(void *ctx, const uint8_t *data, int n, int *done)
{
    struct write_ctx *c = ctx;
    ULONG got = 0;
    // WinUSB splits into wMaxPacketSize packets itself; on the pipe timeout (1 s, set at open) `got` holds what
    // the Virus took before it, the retry continues from there
    BOOL ok = WinUsb_WritePipe(c->f->usb, c->ep, (PUCHAR)data, (ULONG)n, &got, NULL);
    *done = (int)got;
    if (ok)
        return 0;
    DWORD e = GetLastError();
    if (e == ERROR_SEM_TIMEOUT)
        return VTI_XFER_TIMEOUT;
    c->err = e;
    return -1;
}

static vti_tap_cb g_tap;
void vti_set_tap(vti_tap_cb cb) { g_tap = cb; }

// a control request as the tap's record: setup[8], i32 result, then the data that went or came
static void tap_control(uint8_t rt, uint8_t req, uint16_t val, uint16_t idx, uint16_t len, int32_t r, const uint8_t *data)
{
    if (!g_tap)
        return;
    uint8_t rec[12] = {rt, req, (uint8_t)val, (uint8_t)(val >> 8), (uint8_t)idx, (uint8_t)(idx >> 8), (uint8_t)len, (uint8_t)(len >> 8)};
    memcpy(rec + 8, &r, 4);
    g_tap(2, 0, rec, 12, data, (rt & 0x80) ? (r > 0 ? r : 0) : len);
}

int vti_write(vti *v, uint8_t ep, const uint8_t *data, int len)
{
    if (g_tap) // before the transfer: the order on the wire
        g_tap(0, ep, data, len, NULL, 0);
    struct write_ctx c = {iface_for(v, ep), ep, 0};
    int timeouts = 0;
    // a timed-out packet was NAKed, never taken (the link is lossless, E239): try again, up to 5 s in a row
    int r = vti_write_chunks(&c, write_packet, data, len, len > 0 ? len : 1, 5, &timeouts);
    if (timeouts)
        vti_log("vti: write ep %02x: %d timeout(s), %s\n", ep, timeouts, r ? "gave up" : "then accepted");
    if (r && c.err)
        vti_log("vti: write ep %02x: error %lu\n", ep, c.err);
    return r;
}

int vti_control(vti *v, uint8_t bmRequestType, uint8_t bRequest, uint16_t wValue, uint16_t wIndex, uint8_t *data,
                uint16_t len)
{
    WINUSB_SETUP_PACKET sp = {bmRequestType, bRequest, wValue, wIndex, len};
    ULONG done = 0;
    // Issued through the MI_04 handle; WinUSB may rewrite wIndex for interface-recipient requests (E040).
    if (!WinUsb_ControlTransfer(v->mi[1].usb, sp, data, len, &done, NULL)) {
        vti_log("vti: control %02x/%02x wIndex %04x: error %lu\n", bmRequestType, bRequest, wIndex,
                GetLastError());
        tap_control(bmRequestType, bRequest, wValue, wIndex, len, -1, data);
        return -1;
    }
    tap_control(bmRequestType, bRequest, wValue, wIndex, len, (int32_t)done, data);
    return (int)done;
}

int vti_audio(vti *v, int on, const char *event_name) // broker audio exists only behind vtibroker (vticore_pipe.c)
{
    (void)v;
    (void)on;
    (void)event_name;
    return -1;
}

int vti_is_dead(vti *v)
{
    return v->dead != 0;
}

int vti_link(vti *v, int on)
{
    if (!v || v->dead)
        return -1;
    uint8_t m[4];
    vti_link_msg(m, on);
    int r = vti_write(v, VTI_EP_MI03_OUT, m, sizeof m);
    if (!r)
        v->linked = on;
    vti_log("vti: link %s (4E 73 52 %02X) -> %d\n", on ? "on" : "off", m[3], r);
    return r;
}

int vti_linked(vti *v) { return v && v->linked; }
