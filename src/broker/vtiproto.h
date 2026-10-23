// vtiproto.h - framing between vtibroker.exe and its clients over \\.\pipe\vtibroker (byte-mode named pipe).
// Every message: vtimsg header + `len` payload bytes. All integers little-endian.
//
// client -> broker                          broker -> client
//   WRITE    {ep, data}                       RX       {ep, data}          (unsolicited, every IN transfer)
//   CONTROL  {seq, setup[8], data(OUT)}       CONTROL_REPLY {seq, result, data(IN)}
//                                             STATE    {connected}         (on connect and on change)
//   AUDIO    {on, event name, rate}           AUDIO_REPLY {result}         (protocol 2, vtiaudioshm.h; rate: 5)
//                                             AUDIO_TICK {in_frames}       (protocol 3, Linux: per received block)
//   OBSERVE  {}                               OBSERVE_REPLY {version}      (protocol 4: read-only observer)
//                                             INFO {"version=..."}         (after HELLO, brokerinfo.h)
//                                             OBS_TRAFFIC / OBS_LOG / OBS_STATUS (to observers only)
//
// Observers (protocol 4, the Control Center's Nerd mode) never own the Virus: their WRITE, CONTROL and AUDIO are
// refused. They get a copy of the traffic the broker passes on, its log lines and a status line once a second,
// through a bounded queue per observer: when an observer falls behind, only its own copy is dropped (counted).
// HELLO may carry the client's name after the version (shown in the status); older clients send only the version.
#pragma once
#include <stdint.h>

#define VTI_PIPE_NAME "\\\\.\\pipe\\vtibroker"
#define VTI_PROTO_VERSION 5
#define VTI_TCP_PORT 47615 // Linux broker, 127.0.0.1 (Wine clients); actual port in ~/.config/tirevive/broker.token

enum {
    VTIMSG_HELLO = 1,         // client -> broker: u32 version
    VTIMSG_WRITE = 2,         // client -> broker: u8 ep, data...
    VTIMSG_CONTROL = 3,       // client -> broker: u32 seq, u8 setup[8], OUT data...
    VTIMSG_AUDIO = 4,         // client -> broker: u32 on (1 = start, 0 = stop), char event_name[] (on = 1), then
                              // (protocol 5) u32 the wanted Virus rate, 44100 or 48000 (ratesw.h; missing = 44100)
    VTIMSG_AUTH = 5,          // client -> broker, first message on the Linux TCP listener: char token[64] (hex)
    VTIMSG_OBSERVE = 6,       // client -> broker: become a read-only observer (protocol 4)
    VTIMSG_FLASHINFO = 7,     // client -> broker: the Virus' identification blocks (flashinfo.h; owner only, read once
                              // per device open, then cached)
    VTIMSG_RX = 0x81,         // broker -> client: u8 ep, data...
    VTIMSG_CONTROL_REPLY = 0x83, // broker -> client: u32 seq, i32 result (bytes or -1), IN data...
    VTIMSG_STATE = 0x84,      // broker -> client: u32 connected (1 = device open; 2 = open, but another program's
                              // plug-in has it - wait, E166; older clients read 2 as open)
    VTIMSG_AUDIO_REPLY = 0x85, // broker -> client: i32 result (1 = streaming into VTI_AUDIO_SHM_NAME, 0 = no WinUSB
                               // audio function, -1 = error)
    VTIMSG_AUDIO_TICK = 0x86, // broker -> audio client (Linux, no shared events across Wine): i64 in_frames
    VTIMSG_OBSERVE_REPLY = 0x87, // broker -> observer: u32 protocol version (a client without it: broker too old)
    VTIMSG_OBS_TRAFFIC = 0x88, // broker -> observer: u8 kind (0 host->device, 1 device->host, 2 control request),
                               // u8 ep, u32 ms since the broker started, payload (kind 2: setup[8], i32 result, data)
    VTIMSG_OBS_LOG = 0x89,     // broker -> observer: one log line (text, no newline)
    VTIMSG_OBS_STATUS = 0x8A,  // broker -> observer, once a second: "key=value" fields separated by tabs
    VTIMSG_FLASHINFO_REPLY = 0x8C, // broker -> client: i32 result (bytes or -1), the blocks (flashinfo.h)
    VTIMSG_INFO = 0x8B,        // broker -> client after its HELLO: "key=value" fields separated by tabs ("version=0.8.21",
                               // brokerinfo.h); older clients ignore it
};

#pragma pack(push, 1)
typedef struct {
    uint16_t type;
    uint16_t len; // payload bytes
} vtimsg;
#pragma pack(pop)

#define VTIMSG_MAX 4096
