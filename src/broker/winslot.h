// winslot.h - Windows: the device path of one interface (MI_00, MI_03 or MI_04) of the Virus of vti_slot when several
// are connected (#45, devslot.h). Each interface is its own device under the Virus' composite USB device, so the key is
// the parent's location path (the USB port, like libusb's bus and port path; the parent's instance ID would carry the
// serial number). The same key for MI_00, MI_03 and MI_04 keeps a broker's audio and MIDI on one Virus.
#pragma once
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <stdlib.h>
#include <string.h>
#include "devslot.h"

#define WINSLOT_MAX (VTI_SLOTS_MAX * 4)

// the parent's first location path as ASCII ("PCIROOT(0)#PCI(1400)#USBROOT(0)#USB(2)"); "" when Windows has none
static void winslot_key(DEVINST inst, char *key, size_t cap)
{
    key[0] = 0;
    DEVINST parent;
    wchar_t w[512];
    ULONG len = sizeof w;
    if (CM_Get_Parent(&parent, inst, 0) != CR_SUCCESS ||
        CM_Get_DevNode_Registry_PropertyW(parent, CM_DRP_LOCATION_PATHS, NULL, w, &len, 0) != CR_SUCCESS)
        return;
    size_t i = 0;
    for (; i + 1 < cap && w[i]; i++)
        key[i] = (char)(w[i] < 128 ? w[i] : '?');
    key[i] = 0;
}

// the slot-th interface of GUID g in key order: its device path (malloc'd, the caller frees it), NULL when there is none
static wchar_t *winslot_path(const GUID *g, int slot)
{
    HDEVINFO di = SetupDiGetClassDevsW(g, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (di == INVALID_HANDLE_VALUE)
        return NULL;
    char keys[WINSLOT_MAX][160];
    const char *kp[WINSLOT_MAX];
    wchar_t *paths[WINSLOT_MAX];
    int n = 0;
    SP_DEVICE_INTERFACE_DATA id = {.cbSize = sizeof id};
    for (DWORD i = 0; n < WINSLOT_MAX && SetupDiEnumDeviceInterfaces(di, NULL, g, i, &id); i++) {
        DWORD need = 0;
        SetupDiGetDeviceInterfaceDetailW(di, &id, NULL, 0, &need, NULL);
        SP_DEVICE_INTERFACE_DETAIL_DATA_W *d = need >= sizeof *d ? malloc(need) : NULL;
        SP_DEVINFO_DATA dev = {.cbSize = sizeof dev};
        if (!d)
            continue;
        d->cbSize = sizeof *d;
        if (!SetupDiGetDeviceInterfaceDetailW(di, &id, d, need, NULL, &dev)) {
            free(d);
            continue;
        }
        winslot_key(dev.DevInst, keys[n], sizeof keys[n]);
        kp[n] = keys[n];
        paths[n] = _wcsdup(d->DevicePath);
        free(d);
        if (paths[n])
            n++;
    }
    SetupDiDestroyDeviceInfoList(di);
    const int pick = vti_slot_pick(kp, n, slot);
    for (int i = 0; i < n; i++)
        if (i != pick)
            free(paths[i]);
    return pick >= 0 ? paths[pick] : NULL;
}
