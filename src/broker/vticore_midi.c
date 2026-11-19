// vticore_midi.c - USB-MIDI SysEx packing shared by the libusb and WinUSB backends (see vticore.h)
#include "vticore.h"
#include <stdarg.h>
#include <stdio.h>

static void log_stderr(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}
void (*vti_log)(const char *fmt, ...) = log_stderr;
int vti_slot = 1;

int vti_pack_sysex(uint8_t cable, const uint8_t *msg, int len, uint8_t *out, int outsz)
{
    int o = 0;
    for (int i = 0; i < len; i += 3) {
        if (o + 4 > outsz)
            return -1;
        int rest = len - i;
        uint8_t cin = rest > 3 ? 0x4 : rest == 3 ? 0x7 : rest == 2 ? 0x6 : 0x5; // continue / end with 3,2,1
        out[o] = (uint8_t)(cable << 4 | cin);
        for (int k = 0; k < 3; k++)
            out[o + 1 + k] = k < rest && (cin == 0x4 || k < cin - 4) ? msg[i + k] : 0;
        o += 4;
    }
    return o;
}

void vti_unpack(vti_unpacker *u, const uint8_t *data, int len, vti_sysex_cb done, vti_event_cb ev, void *user)
{
    static const int cin_len[16] = {0, 0, 2, 3, 3, 1, 2, 3, 3, 3, 3, 3, 2, 2, 3, 1};
    for (int i = 0; i + 4 <= len; i += 4) {
        uint8_t cable = data[i] >> 4, cin = data[i] & 0xf;
        if (!data[i])
            continue;
        const uint8_t *e = data + i + 1;
        int n = cin_len[cin];
        if (cin >= 0x4 && cin <= 0x7) { // SysEx start/continue (4) or end (5, 6, 7)
            for (int k = 0; k < n && u->len[cable] < (int)sizeof u->buf[0]; k++)
                u->buf[cable][u->len[cable]++] = e[k];
            if (cin != 0x4) {
                if (done)
                    done(user, cable, u->buf[cable], u->len[cable]);
                u->len[cable] = 0;
            }
        } else if (ev) {
            ev(user, cable, e, n);
        }
    }
}

int vti_has_note_on(const uint8_t *data, int len)
{
    for (int i = 0; i + 3 < len; i += 4)
        if ((data[i] & 0x0F) == 0x9 && (data[i + 1] & 0xF0) == 0x90 && data[i + 3]) // CIN 9 with velocity > 0
            return 1;
    return 0;
}

int vti_pack_notes_off(uint8_t *out, int outsz)
{
    const uint8_t cable = 2; // the plug-in's notes (EP 0x06 cable 2, E056)
    int o = 0;
    for (int ch = 0; ch < 16; ch++) {
        static const uint8_t cc[2] = {64, 123}; // sustain pedal off, All Notes Off
        for (int k = 0; k < 2; k++) {
            if (o + 4 > outsz)
                return -1;
            out[o++] = (uint8_t)(cable << 4 | 0xB);
            out[o++] = (uint8_t)(0xB0 | ch);
            out[o++] = cc[k];
            out[o++] = 0;
        }
    }
    return o;
}
