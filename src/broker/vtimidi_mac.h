// vtimidi_mac.h - macOS: the broker stands in for Access' "Virus TI MIDI Driver.plugin" towards Virus Control
// (findings E100): shared slot table "VirusTICommMem", per-device rings MDW (plug-in -> device) and MDR
// (device -> plug-in) and the transport message port MDP. The words in the rings are the raw USB-MIDI packets of
// the plug-in channel (MI_04, EP 0x06 / 0x86), the same stream vtiemu forwards on Windows.
#pragma once
#include <stdint.h>

// Key that pairs the slot with the device: Virus Control compares slot +4 with VC_DeviceInfo +0x10, which our
// libVirusUSB.dylib reports (src/mac/libvirususb/VirusClient.cpp). The original used the USB locationID.
#define VTI_MAC_DEVKEY 0x00100000u

// Create the slot memory, rings and message port and start the pump thread. `write` sends packets to the
// device's plug-in channel (called from the pump thread). Returns 0 on success.
int vtimidi_start(int (*write)(const uint8_t *data, int len));

// Packets received from the plug-in channel (EP 0x86): appended to the MDR ring.
void vtimidi_rx(const uint8_t *data, int len);
