// ctrlguard.h - which USB control requests a broker client may send to the Virus (AGENTS.md rule 2). Our clients only
// read standard information (GET_DESCRIPTOR for the model, E115). Vendor requests reach the Virus' flash protocol
// (E026/E040; the write/erase requests of an OS update or "Burn To Flash" are not known, E169), class requests the
// audio function, standard host-to-device requests its configuration - none of them may pass a broker. Plain C, no
// platform headers: the Windows and the POSIX broker use it, the unit test calls it directly.
#pragma once
#include <stdint.h>

// 1 = pass it to the Virus, 0 = refuse it (the client gets -1)
static inline int vti_control_allowed(const uint8_t setup[8])
{
    uint8_t rt = setup[0], req = setup[1];
    if (rt != 0x80 && rt != 0x81 && rt != 0x82) // device-to-host, standard, to device / interface / endpoint
        return 0;
    return req == 0 /* GET_STATUS */ || req == 6 /* GET_DESCRIPTOR */ || req == 8 /* GET_CONFIGURATION */ ||
           req == 10 /* GET_INTERFACE */;
}

// may a client's VTIMSG_WRITE go to endpoint `ep`? Only the plug-in channel EP 0x06 (MI04 OUT): every client writes
// there and nowhere else. EP 0x05 is the USB chip's command endpoint (the one known command is the driver's handshake,
// E030, which the broker sends itself); an IN endpoint would make libusb run an IN transfer for the client (audit
// 2026-11-17 #2)
static inline int vti_write_allowed(uint8_t ep) { return ep == 0x06; }
