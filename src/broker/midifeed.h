// midifeed.h - Windows: what the broker writes to its MIDI port helper (tirevive-midiports.exe, src/midiports-win) on
// the helper's standard input: the messages keyport.h's filters pass, one frame each. A frame is a marker byte
// (0xA0 | port: 0 "Virus TI MIDI", 1 "Virus TI Synth", as on Linux), the length (1-3: one USB-MIDI packet's bytes)
// and the bytes. Virus to DAW only: the helper reads, it never writes back. Plain C, no I/O; the unit test calls it.
#pragma once
#include <stdint.h>

#define VTI_MIDIFEED_MARK 0xA0

// One frame into `out` (cap bytes). Returns its length, 0 when the message or the room does not fit.
static inline int vti_midifeed_frame(int port, const uint8_t *msg, int n, uint8_t *out, int cap)
{
    if (port < 0 || port > 1 || n < 1 || n > 3 || cap < n + 2)
        return 0;
    out[0] = (uint8_t)(VTI_MIDIFEED_MARK | port);
    out[1] = (uint8_t)n;
    for (int i = 0; i < n; i++)
        out[2 + i] = msg[i];
    return n + 2;
}

typedef void (*vti_midifeed_cb)(void *user, int port, const uint8_t *msg, int n);

typedef struct {
    int state; // 0 = looking for a marker, 1 = the length next, 2 = bytes
    int port, len, have;
    uint8_t msg[3];
} vti_midifeed_reader;

static inline void vti_midifeed_init(vti_midifeed_reader *r)
{
    r->state = 0;
    r->port = r->len = r->have = 0;
}

// Any piece of the stream (a pipe delivers what it has). A bad length drops the frame and the reader looks for the
// next marker.
static inline void vti_midifeed_read(vti_midifeed_reader *r, const uint8_t *data, int len, vti_midifeed_cb cb,
                                     void *user)
{
    for (int i = 0; i < len; i++) {
        const uint8_t b = data[i];
        if (r->state == 0) {
            if ((b & 0xFE) == VTI_MIDIFEED_MARK) {
                r->port = b & 1;
                r->state = 1;
            }
        } else if (r->state == 1) {
            if (b >= 1 && b <= 3) {
                r->len = b;
                r->have = 0;
                r->state = 2;
            } else {
                r->state = 0;
            }
        } else {
            r->msg[r->have++] = b;
            if (r->have == r->len) {
                cb(user, r->port, r->msg, r->len);
                r->state = 0;
            }
        }
    }
}
