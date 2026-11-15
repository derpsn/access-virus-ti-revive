// seqevent.h - Linux: one MIDI message from keyport.h's filters as an ALSA sequencer event, for the broker's MIDI ports
// (vtikeyport_linux.c). The kernel's own structs (sound/asequencer.h), no alsa-lib: the broker stays a plain binary
// with libusb linked in. The event goes at once (direct, no queue) to whoever subscribed to the port. A SysEx piece
// (USB-MIDI sends SysEx 3 bytes at a time) becomes a variable-length event with its bytes right behind it: that is
// how the kernel reads a write() on /dev/snd/seq. Plain C, no I/O; the unit test calls it.
#pragma once
#include <sound/asequencer.h>
#include <stdint.h>
#include <string.h>

// `msg`: one channel or system message, or a SysEx piece; `port`: the source port. Returns the bytes to write to
// `out` (0: nothing to send - incomplete, undefined, or no room).
static inline int vti_seq_encode(const uint8_t *msg, int n, int port, uint8_t *out, int cap)
{
    struct snd_seq_event ev;
    if (n < 1 || cap < (int)sizeof ev)
        return 0;
    memset(&ev, 0, sizeof ev);
    ev.flags = SNDRV_SEQ_TIME_STAMP_TICK | SNDRV_SEQ_TIME_MODE_REL | SNDRV_SEQ_EVENT_LENGTH_FIXED;
    ev.queue = SNDRV_SEQ_QUEUE_DIRECT;
    ev.source.port = (unsigned char)port;
    ev.dest.client = SNDRV_SEQ_ADDRESS_SUBSCRIBERS;
    ev.dest.port = SNDRV_SEQ_ADDRESS_UNKNOWN;
    const uint8_t st = msg[0];
    // bytes per status (0 = no such message here): 8x-Ex by the high nibble, Fx by the low one
    static const uint8_t chan_len[7] = {3, 3, 3, 3, 2, 2, 3};
    static const uint8_t sys_len[16] = {0, 2, 3, 2, 0, 0, 1, 0, 1, 0, 1, 1, 1, 0, 1, 1};
    if (st >= 0x80 && st < 0xF0) {
        if (n < chan_len[(st >> 4) - 8])
            return 0;
        const unsigned char ch = st & 0x0F;
        switch (st >> 4) {
        case 0x8: ev.type = SNDRV_SEQ_EVENT_NOTEOFF; break;
        case 0x9: ev.type = SNDRV_SEQ_EVENT_NOTEON; break;
        case 0xA: ev.type = SNDRV_SEQ_EVENT_KEYPRESS; break;
        case 0xB: ev.type = SNDRV_SEQ_EVENT_CONTROLLER; break;
        case 0xC: ev.type = SNDRV_SEQ_EVENT_PGMCHANGE; break;
        case 0xD: ev.type = SNDRV_SEQ_EVENT_CHANPRESS; break;
        default: ev.type = SNDRV_SEQ_EVENT_PITCHBEND; break;
        }
        if (st >> 4 <= 0xA) {
            ev.data.note.channel = ch;
            ev.data.note.note = msg[1];
            ev.data.note.velocity = msg[2];
        } else {
            ev.data.control.channel = ch;
            if (st >> 4 == 0xB) {
                ev.data.control.param = msg[1];
                ev.data.control.value = msg[2];
            } else if (st >> 4 == 0xE) {
                ev.data.control.value = (msg[1] | msg[2] << 7) - 8192;
            } else {
                ev.data.control.value = msg[1];
            }
        }
    } else if (st > 0xF0 && st != 0xF7) {
        const int need = sys_len[st & 0x0F];
        if (!need || n < need)
            return 0;
        switch (st) {
        case 0xF1: ev.type = SNDRV_SEQ_EVENT_QFRAME; ev.data.control.value = msg[1]; break;
        case 0xF2: ev.type = SNDRV_SEQ_EVENT_SONGPOS; ev.data.control.value = msg[1] | msg[2] << 7; break;
        case 0xF3: ev.type = SNDRV_SEQ_EVENT_SONGSEL; ev.data.control.value = msg[1]; break;
        case 0xF6: ev.type = SNDRV_SEQ_EVENT_TUNE_REQUEST; break;
        case 0xF8: ev.type = SNDRV_SEQ_EVENT_CLOCK; break;
        case 0xFA: ev.type = SNDRV_SEQ_EVENT_START; break;
        case 0xFB: ev.type = SNDRV_SEQ_EVENT_CONTINUE; break;
        case 0xFC: ev.type = SNDRV_SEQ_EVENT_STOP; break;
        case 0xFE: ev.type = SNDRV_SEQ_EVENT_SENSING; break;
        default: ev.type = SNDRV_SEQ_EVENT_RESET; break; // 0xFF
        }
    } else { // F0, F7 or data bytes: a piece of a SysEx (vti_dinport_filter passes code index 4-7 pieces as they come)
        if (cap < (int)sizeof ev + n)
            return 0;
        ev.type = SNDRV_SEQ_EVENT_SYSEX;
        ev.flags = (ev.flags & ~SNDRV_SEQ_EVENT_LENGTH_MASK) | SNDRV_SEQ_EVENT_LENGTH_VARIABLE;
        ev.data.ext.len = (unsigned int)n;
        memcpy(out, &ev, sizeof ev);
        memcpy(out + sizeof ev, msg, (size_t)n);
        return (int)sizeof ev + n;
    }
    memcpy(out, &ev, sizeof ev);
    return (int)sizeof ev;
}
