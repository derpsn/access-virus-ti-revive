// devslot.h - several Viruses at once (#45): one broker per Virus, numbered 1..VTI_SLOTS_MAX. Broker N takes the N-th
// Virus in the order of a stable key per device (libusb: bus and port path; Windows: the parent device instance), so
// the same cable in the same port gives the same number after a restart; the original's plug-in kept one owner
// instance per Virus as well (E553). Slot 1 keeps today's names; slot N appends "-N" to its pipe, socket, audio
// memory and log. Plain C, no I/O; the unit test calls it.
#pragma once
#include <string.h>

#define VTI_SLOTS_MAX 4

// the "-slot N" argument: 1..VTI_SLOTS_MAX, 0 when missing or bad
static inline int vti_slot_parse(const char *s)
{
    if (!s || s[0] < '1' || s[0] > '0' + VTI_SLOTS_MAX || s[1])
        return 0;
    return s[0] - '0';
}

// the index in keys[] of the slot-th device in key order (equal keys: list order), -1 when there are fewer devices
static inline int vti_slot_pick(const char *const *keys, int n, int slot)
{
    for (int i = 0; i < n; i++) {
        int before = 0;
        for (int j = 0; j < n; j++) {
            const int c = strcmp(keys[j], keys[i]);
            before += c < 0 || (c == 0 && j < i);
        }
        if (before == slot - 1)
            return i;
    }
    return -1;
}

// "" for slot 1, "-N" otherwise
static inline void vti_slot_suffix(char out[4], int slot)
{
    out[0] = 0;
    if (slot > 1) {
        out[0] = '-';
        out[1] = (char)('0' + slot);
        out[2] = 0;
    }
}

// a MIDI port or client name: "Virus TI <what>" for Virus 1 (the Access driver's names, which DAWs remember), "Virus TI
// N <what>" for Virus N; `what` "" gives "Virus TI" / "Virus TI N"
#include <stdio.h>
static inline void vti_slot_port_name(char *out, size_t cap, const char *what, int slot)
{
    char num[3] = "";
    if (slot > 1 && slot <= VTI_SLOTS_MAX) {
        num[0] = ' ';
        num[1] = (char)('0' + slot);
        num[2] = 0;
    }
    snprintf(out, cap, "Virus TI%s%s%s", num, *what ? " " : "", what);
}

// the CoreMIDI unique ID DAWs remember a port by: "VTI<cable>" for Virus 1 (unchanged), "VT<N><cable>" for Virus N
static inline int vti_slot_port_id(int cable, int slot)
{
    return slot > 1 ? (0x5654 << 16) | (('0' + slot) << 8) | ('0' + cable) : (0x565449 << 8) | ('0' + cable);
}
