// idlepace.h - pacing the reads on the Virus TI Snow's bulk EP 0x85 (vticore.c, Linux and macOS), and the idle
// packet filter both readers share (vti_drop_idle, also vticore_winusb.c).
//
// Why: the Snow's bulk EP 0x85 answers every read at once, ~3300 times a second, with FF FE 00 00 (cable 15, Active
// Sensing; nothing in the plug-in reads it, E387). With 8 reads kept pending (rxqueue.h) every one of them was a libusb
// transfer, a queue record and a reader wake-up: a third of a CPU core on an M4 Pro (#42, E547). The original Access
// drivers read just as fast on Windows and macOS (E548), so the rule here is our own:
//   - a read that brought only whole idle packets rests its transfer instead of resubmitting it;
//   - the reader lets one rested transfer out per VTI_PACE_NS, so an idle Snow is asked about 1000 times a second;
//   - a read with anything else in it hands the data on as before and wakes every rested transfer at once, so a SysEx
//     dump or a knob turn runs at full speed. A message that starts in a quiet moment waits at most VTI_PACE_NS for
//     the next read (the Virus holds it until it is asked, E517 (5)).
//   - the option "snow-idle-slow" (off by default, #42, user 2026-11-17): after VTI_PACE_QUIET_NS without real data
//     the paced reads come every VTI_PACE_SLOW_NS instead; the first message after a pause may then wait up to that
//     long, real data brings back the 1 ms pace at once. The broker reads the option from the user's TI Revive folder.
// The TI/TI2 interrupt endpoints send nothing when idle and never get here.
// Plain C, no locking (the caller holds its endpoint mutex), no I/O, no clock of its own; the unit test calls it.
#pragma once
#include <stdint.h>
#include <string.h>

#define VTI_PACE_NS 1000000ull       // 1 ms between paced reads while idle
#define VTI_PACE_SLOW_NS 4000000ull  // the option: 4 ms once the Snow has been quiet ...
#define VTI_PACE_QUIET_NS 1000000000ull // ... for a second
#define VTI_PACE_OPTION "snow-idle-slow.on"

// 1 when b holds n > 0 bytes that are all whole FF FE 00 00 packets
static inline int vti_idle_only(const uint8_t *b, int n)
{
    static const uint8_t idle[4] = {0xff, 0xfe, 0x00, 0x00};
    if (!b || n <= 0 || n % 4)
        return 0;
    for (int i = 0; i < n; i += 4)
        if (memcmp(b + i, idle, 4))
            return 0;
    return 1;
}

// Drops every whole FF FE 00 00 packet from b (n bytes), keeps the rest in order, a cut packet at the end too.
// Returns the bytes kept; the packets dropped are (n - kept) / 4. Both readers (vticore.c, vticore_winusb.c) call it,
// only on a bulk EP 0x85 (the Snow, E387): ~2000 idle packets a second would reach every client otherwise.
static inline int vti_drop_idle(uint8_t *b, int n)
{
    static const uint8_t idle[4] = {0xff, 0xfe, 0x00, 0x00};
    int w = 0, r = 0;
    for (; r + 4 <= n; r += 4)
        if (memcmp(b + r, idle, 4)) {
            memmove(b + w, b + r, 4);
            w += 4;
        }
    for (; r < n; r++)
        b[w++] = b[r];
    return w;
}

typedef struct {
    uint32_t rested;      // one bit per transfer slot (0..31)
    uint64_t next_ns;     // monotonic time the next rested transfer may go out
    int slow;             // the option snow-idle-slow (set by the broker)
    int have_data;        // last_data_ns is known
    uint64_t last_data_ns; // the last real data; without any, the first paced read starts the quiet time
} vti_pace;

static inline void vti_pace_init(vti_pace *p)
{
    p->rested = 0;
    p->next_ns = 0;
    p->slow = 0;
    p->have_data = 0;
    p->last_data_ns = 0;
}

// the callback: real data came at now_ns; the quiet time starts again and the next paced read may go at once
static inline void vti_pace_data(vti_pace *p, uint64_t now_ns)
{
    p->have_data = 1;
    p->last_data_ns = now_ns;
    if (p->next_ns > now_ns)
        p->next_ns = now_ns;
}

// the callback: an idle-only read, keep this transfer back
static inline void vti_pace_rest(vti_pace *p, int slot)
{
    if (slot < 0 || slot > 31) // never a shift by 32 or more
        return;
    p->rested |= 1u << slot;
}

// the callback: real data came, every rested transfer goes out now (the bits returned)
static inline uint32_t vti_pace_wake_all(vti_pace *p)
{
    const uint32_t m = p->rested;
    p->rested = 0;
    return m;
}

// the reader: the slot to resubmit now, or -1 (nothing rested, or the last one went out less than VTI_PACE_NS ago)
static inline int vti_pace_due(vti_pace *p, uint64_t now_ns)
{
    if (!p->rested || now_ns < p->next_ns)
        return -1;
    int s = 0;
    while (!(p->rested & (1u << s)))
        s++;
    p->rested &= ~(1u << s);
    if (!p->have_data) { // no data seen yet: quiet from now on
        p->have_data = 1;
        p->last_data_ns = now_ns;
    }
    const int quiet = p->slow && now_ns - p->last_data_ns >= VTI_PACE_QUIET_NS;
    p->next_ns = now_ns + (quiet ? VTI_PACE_SLOW_NS : VTI_PACE_NS);
    return s;
}

// the reader: how long it may wait for records before the next paced resubmit, at most cap
static inline uint64_t vti_pace_wait(const vti_pace *p, uint64_t now_ns, uint64_t cap)
{
    if (!p->rested)
        return cap;
    if (now_ns >= p->next_ns)
        return 0;
    const uint64_t d = p->next_ns - now_ns;
    return d < cap ? d : cap;
}

// the broker: where the option's file is for the user it runs as (home = $HOME; mac = 1 on macOS), the plug-in's
// folder (native/src/cc/diag.h plugin_dir); 0, or -1 without a home or when the path does not fit
static inline int vti_pace_option_path(char *buf, size_t size, const char *home, int mac)
{
    if (!buf || !size || !home || !*home)
        return -1;
    const char *mid = mac ? "/Library/Application Support/virus-ti-native/" : "/.local/share/virus-ti-native/";
    const size_t a = strlen(home), b = strlen(mid), c = strlen(VTI_PACE_OPTION);
    if (a + b + c + 1 > size) // never a cut path
        return -1;
    memcpy(buf, home, a);
    memcpy(buf + a, mid, b);
    memcpy(buf + a + b, VTI_PACE_OPTION, c + 1);
    return 0;
}
