// vticore.h - user-mode access to the Access Virus TI over libusb (WinUSB backend on Windows).
// Protocol: findings/spec.md (handshake §3, USB-MIDI framing §4/§5, SysEx §6).
#pragma once
#include <stddef.h>
#include <stdint.h>

#define VTI_VID 0x133e
#define VTI_PID 0x0815
#define VTI_EP_MI03_OUT 0x05 // handshake, identity request
#define VTI_EP_MI03_IN 0x85  // handshake reply, hardware CCs
#define VTI_EP_MI04_OUT 0x06 // plugin channel: host -> device (cables 2, 3, 5)
#define VTI_EP_MI04_IN 0x86  // plugin channel: device -> host (cables 2, 3)

typedef struct vti vti;

// Diagnostics sink (default: stderr). Embedders without a console (vtiemu inside a DAW) redirect it.
extern void (*vti_log)(const char *fmt, ...);

// Called from a reader thread for every completed IN transfer (raw bytes = USB-MIDI 4-byte packets).
typedef void (*vti_rx_cb)(void *user, uint8_t ep, const uint8_t *data, int len);

// Open the first Virus TI, claim MI_03 and MI_04 (detaching kernel drivers), send the handshake and
// start the IN readers. Returns NULL on failure (message on stderr).
vti *vti_open(vti_rx_cb cb, void *user);
void vti_close(vti *v);

// 1 once a reader hit a fatal error (device removed, handle invalid): close and reopen.
int vti_is_dead(vti *v);
// macOS: let the system re-enumerate the Virus (a software replug after a sleep, E472); at most once a minute;
// 0 = done, -2 = skipped by the gap, other < 0 = libusb error or not supported (Linux, Windows)
int vti_reenumerate(vti *v);

// libusb backend only (vticore.c): the context and handle, for the Linux broker's audio streaming
struct libusb_context;
struct libusb_device_handle;
void vti_libusb(vti *v, struct libusb_context **ctx, struct libusb_device_handle **h);

// Everything sent to the Virus through this file, whoever asked: a client, the broker's own handshake and note-offs, a
// control request of any origin (a, alen then b, blen make the record; kind 0 = write, 2 = control: setup[8], i32 result,
// data). Nerd mode and the capture see it. Implemented by the libusb and the WinUSB backend; set before vti_open.
typedef void (*vti_tap_cb)(uint8_t kind, uint8_t ep, const void *a, int alen, const void *b, int blen);
void vti_set_tap(vti_tap_cb cb);

// Interrupt OUT of raw bytes, split into wMaxPacketSize chunks. Returns 0 on success.
int vti_write(vti *v, uint8_t ep, const uint8_t *data, int len);

// Control transfer on the default pipe (8-byte setup + data). Returns bytes transferred or -1.
int vti_control(vti *v, uint8_t bmRequestType, uint8_t bRequest, uint16_t wValue, uint16_t wIndex, uint8_t *data,
                uint16_t len);

// Broker audio (vticore_pipe.c only, protocol 2): register (on = 1) the named auto-reset event `event_name`
// to be signalled for every block appended to the shared input ring (vtiaudioshm.h), or unregister (on = 0).
// Returns 1 = streaming, 0 = no WinUSB audio function, -1 = not supported (older broker, direct backends).
int vti_audio(vti *v, int on, const char *event_name);

// Pack a complete SysEx message (F0 ... F7) into USB-MIDI packets for `cable`. Returns bytes written to
// out (a multiple of 4), or -1 if out is too small.
int vti_pack_sysex(uint8_t cable, const uint8_t *msg, int len, uint8_t *out, int outsz);

// Feed USB-MIDI packets; calls `done` for each complete SysEx per cable and `ev` for other events.
typedef struct {
    uint8_t buf[16][1024];
    int len[16];
} vti_unpacker;
typedef void (*vti_sysex_cb)(void *user, uint8_t cable, const uint8_t *msg, int len);
typedef void (*vti_event_cb)(void *user, uint8_t cable, const uint8_t *ev, int len);
void vti_unpack(vti_unpacker *u, const uint8_t *data, int len, vti_sysex_cb done, vti_event_cb ev, void *user);

// Hanging-note protection (brokers): does a plug-in channel write (EP 0x06, USB-MIDI packets) contain a note-on, and
// the packets that silence everything (sustain off + All Notes Off on all 16 channels of the cables the plug-in uses)
int vti_has_note_on(const uint8_t *data, int len);
int vti_pack_notes_off(uint8_t *out, int outsz); // returns the number of bytes (128 per cable) or -1
