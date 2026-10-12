// tirbridge.h - protocol between the native (arm64) bridge plug-in and the x86_64 helper that hosts the original
// Access plug-in under Rosetta (findings/design-macos-bridge.md). Shared by both architectures: fixed-width types,
// no pointers, explicit sizes; both sides are little-endian.
#pragma once
#include <stdint.h>

#define TIRB_VERSION 1
#define TIRB_SOCK_FMT "/tmp/tirevive-helper-%u-%d.sock" // uid, DAW pid
#define TIRB_SHM_FMT "/tirb-%d-%u"                       // DAW pid, instance (POSIX shm names max 31 chars)
// Audio handshake: the bridge bumps tirb_shm.req_seq and wakes the helper, the helper bumps done_seq when the block
// is processed; both wait with os_sync_wait_on_address(..., OS_SYNC_WAIT_ON_ADDRESS_SHARED) on the shared counters
// (macOS 14.4+, works across processes and across arm64/x86_64; macOS has no sem_timedwait for a deadline).

// ------------------------------------------------------------------------------------- control channel
// Every message: header + payload. Requests carry a seq; the reply echoes type | TIRB_REPLY and seq.
typedef struct {
    uint32_t type, instance, seq, len; // len = payload bytes
} tirb_msg;

enum {
    TIRB_REPLY = 0x80000000u,
    TIRB_HELLO = 1,        // -> u32 version, i32 daw pid          <- u32 version
    TIRB_CREATE,           // -> (none)                            <- tirb_create_reply (instance in header)
    TIRB_DESTROY,          // -> (none)                            <- (none)
    TIRB_PARAM_INFO,       // -> u32 first, u32 count              <- u32 total, tirb_param[n]
    TIRB_GET_STATE,        // -> (none)                            <- state bytes
    TIRB_SET_STATE,        // -> state bytes                       <- i32 result
    TIRB_SETUP,            // -> tirb_setup                        <- i32 result
    TIRB_ACTIVE,           // -> u32 on                            <- i32 result
    TIRB_GET_PARAM,        // -> u32 id                            <- f64 normalized value
    TIRB_PARAM_STRING,     // -> u32 id, f64 value                 <- utf8 string
    TIRB_EDITOR_OPEN,      // -> i32 x, y (screen, top-left)       <- tirb_editor_reply
    TIRB_EDITOR_CLOSE,     // -> (none)                            <- (none)
    TIRB_EDITOR_EVENT,     // -> tirb_input (no reply)
    TIRB_EDITOR_MOVE,      // -> i32 x, y (no reply)
    TIRB_MIDI_MAPPING,     // -> (none)                            <- u32 n, tirb_midimap[n] (IMidiMapping table)
    TIRB_SET_PARAM,        // -> u32 id, f64 value (controller side, no reply)
    // notifications helper -> bridge (no reply)
    TIRB_N_PARAM_EDIT = 0x100, // tirb_param_edit
    TIRB_N_RESTART,            // i32 flags
    TIRB_N_EDITOR_RESIZE,      // i32 w, h
};

typedef struct {
    uint32_t num_inputs, num_outputs; // audio buses
    uint32_t in_channels[4], out_channels[4];
    char in_names[4][32], out_names[4][32];
    uint32_t event_inputs, event_channels;
    uint32_t param_count, latency;
    char shm_name[32];
} tirb_create_reply;

typedef struct {
    uint32_t id;
    int32_t step_count, flags, unit_id;
    double default_value;
    char title[128], short_title[64], units[32]; // utf8
} tirb_param;

typedef struct {
    int16_t bus, channel, controller, pad; // VST3 CtrlNumber (0-127 CC, 128 aftertouch, 129 pitch bend, ...)
    uint32_t id;
} tirb_midimap;

typedef struct {
    double sample_rate;
    int32_t max_block, process_mode; // VST3 ProcessModes
} tirb_setup;

typedef struct {
    int32_t width, height;
    char pixels_shm[32]; // BGRA premultiplied, width * height * 4, rewritten on every frame
} tirb_editor_reply;

typedef struct {
    int32_t kind; // 1 down, 2 up, 3 drag, 4 move, 5 wheel, 6 key down, 7 key up, 8 enter, 9 exit
    int32_t button, click_count, modifiers;
    float x, y, dx, dy; // view coordinates, origin top-left
    uint32_t key_code;
    char chars[16]; // utf8
} tirb_input;

typedef struct {
    int32_t what; // 0 begin, 1 perform, 2 end
    uint32_t id;
    double value;
} tirb_param_edit;

// ------------------------------------------------------------------------------------- audio shared memory
#define TIRB_MAX_BLOCK 4096
#define TIRB_MAX_EVENTS 512
#define TIRB_MAX_PCHANGES 512
#define TIRB_OUT_CH 6 // 3 stereo buses ("Out 1".."Out 3")

typedef struct {
    int32_t bus, sample_offset;
    int32_t type; // VST3 Event::EventTypes
    int32_t flags;
    uint8_t data[24]; // note on/off/poly pressure fields as in VST3, or short data (sysex chunks are split)
} tirb_event;

typedef struct {
    uint32_t id;
    int32_t sample_offset;
    double value;
} tirb_pchange;

typedef struct {
    uint32_t state, tempo_valid;
    double sample_rate, project_time_music, bar_position_music, cycle_start_music, cycle_end_music, tempo;
    int64_t project_time_samples, system_time, continous_time_samples;
    int32_t time_sig_num, time_sig_den;
} tirb_transport;

typedef struct {
    // written by the bridge before bumping req_seq
    uint32_t req_seq, frames, num_events, num_pchanges, outputs_active; // outputs_active: bit per output bus
    uint32_t pad0;
    tirb_transport transport;
    tirb_event events[TIRB_MAX_EVENTS];
    tirb_pchange pchanges[TIRB_MAX_PCHANGES];
    // written by the helper before bumping done_seq
    uint32_t done_seq, num_out_pchanges;
    tirb_pchange out_pchanges[64];
    float out[TIRB_OUT_CH][TIRB_MAX_BLOCK];
} tirb_shm;
