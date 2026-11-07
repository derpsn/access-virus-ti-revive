// keyport.h - what the broker's MIDI port passes from the Virus to the DAW (#29). The original's plug-in never carried the
// Virus keys: the Access driver's "Virus TI Synth" port did, and the DAW track sent them back through the plug-in (Setup
// Guide, E391); with Local off (E392) that is the only way a key sounds. Keys, wheels, pressure and pedals leave the
// Virus on EP 0x85 cable 1 (E392 3, global 0x3B). Channel messages only: SysEx on that cable is the Virus talking to the
// host (bank data outside a session, the handshake reply). Plain C for the broker, no I/O; the unit test calls it.
#pragma once
#include <stdint.h>

typedef void (*vti_keyport_cb)(void *user, const uint8_t *msg, int n);

// `packets`: one IN transfer, 4-byte USB-MIDI packets
static inline void vti_keyport_filter(uint8_t ep, const uint8_t *packets, int len, vti_keyport_cb cb, void *user)
{
    if (ep != 0x85)
        return;
    for (int i = 0; i + 4 <= len; i += 4) {
        const uint8_t *p = packets + i;
        if (p[0] >> 4 != 1) // cable 1, also skips padding
            continue;
        const int cin = p[0] & 0x0F, st = p[1];
        if (cin < 0x8 || cin > 0xE || (st & 0xF0) >> 4 != cin) // CIN 8..E carry the status' high nibble: else garbled
            continue;
        const int n = cin == 0xC || cin == 0xD ? 2 : 3;
        if (p[2] & 0x80 || (n == 3 && p[3] & 0x80))
            continue;
        cb(user, p + 1, n);
    }
}
