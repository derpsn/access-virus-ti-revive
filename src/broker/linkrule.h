// linkrule.h - when the broker links and unlinks the Virus (global 0x52, the raw handshake on EP 0x05; E392, E524).
//
// The original's MIDI driver sends the raw 4E 73 52 01 when the first MIDI stream opens and 4E 73 52 00 when the last
// one closes (vtimidi.sys 0x13518 and its callers; the Mac driver at device start and release). While linked, the Virus
// sends its keys and its MIDI In only to USB and keeps Local off (E392, E517). TI Revive linked at open and never
// unlinked, so after the DAW closed a Keyboard or Polar stayed silent on its own keys until a power cycle (E520 (3)).
// A user = a client that is not an observer (a plug-in or the Control Center). Plain C, no I/O; the unit test calls it.
#pragma once
#include <stdint.h>

static inline void vti_link_msg(uint8_t m[4], int on)
{
    m[0] = 0x4E;
    m[1] = 0x73;
    m[2] = 0x52;
    m[3] = on ? 0x01 : 0x00;
}

// after a user left: unlink when it was the last one (the original ~0.3 s after the plug-in's close sequence)
static inline int vti_link_off_now(int linked, int users_left) { return linked && users_left == 0; }

// before a user's message goes to the Virus: link again when the last user had unlinked it
static inline int vti_link_on_now(int linked) { return !linked; }
