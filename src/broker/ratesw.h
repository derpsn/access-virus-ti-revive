// ratesw.h - one sample rate switch of the Virus' USB audio, in the order the Access driver uses (E323 (3),
// findings/design-rate.md step 3): the stream stopped and every transfer back, alternate setting 0 on the two
// streaming interfaces, ~250 ms, alternate setting 1, SET_CUR to EP 0x82 and 0x01 twice, then the stream (IN, then
// OUT). Never a SET_CUR on a running stream. The Virus' USB chip starts its own 250 ms countdown at the SET_CUR and
// then tells the DSP, which re-locks its clock ("Audio clock changed" on the display) - so a switch is not free:
// at most one per VTI_RATE_DEBOUNCE_MS (rule 2), and a refused SET_CUR goes back to the old rate once, no retries
// back and forth. Deliberate deviation: no SET_CONFIGURATION (libusb/WinUSB cannot re-issue it on claimed
// interfaces). Plain C over a small table of operations, so the Linux (libusb) and the Windows (WinUSB) stream share
// it and the unit test (native/tests/test_ratesw.cpp) runs it against a fake.
#pragma once
#include <stdint.h>

#define VTI_RATE_DEBOUNCE_MS 1000
#define VTI_RATE_SETTLE_MS 250 // between alt 0 and alt 1, like the driver (~240 ms in the capture, E323)

typedef struct {
    void *u;
    void (*stop)(void *u);                              // cancel every transfer and wait until all are back
    int (*alt)(void *u, int iface, int alt);            // 0 = done
    void (*sleep_ms)(void *u, int ms);
    int (*set_cur)(void *u, uint8_t ep, uint32_t rate); // UAC1 SAMPLING_FREQ_CONTROL; 0 = accepted
    int (*start)(void *u, uint32_t rate);               // the stream at `rate`: IN, then OUT; 0 = running
} vti_rate_ops;

// the Virus runs at 44.1 or 48 kHz; anything else is 44.1 (the plug-in maps host rates, native/src/device/virusrate.h)
static inline uint32_t vti_rate_valid(uint32_t rate) { return rate == 48000 ? 48000 : 44100; }

// SET_CUR on EP 0x82, 0x01, 0x82, 0x01 (the driver's order); every one is tried, 0 when all four were accepted
static inline int vti_rate_set_cur(const vti_rate_ops *o, uint32_t rate)
{
    static const uint8_t eps[4] = {0x82, 0x01, 0x82, 0x01};
    int bad = 0;
    for (int i = 0; i < 4; i++)
        bad |= o->set_cur(o->u, eps[i], rate) != 0;
    return bad ? -1 : 0;
}

// The stream is running at `from`; afterwards it runs at `to` (0), at `from` again because the Virus refused `to`
// (-1), or not at all because alternate setting 1 or the restart failed (-2).
static inline int vti_rate_switch(const vti_rate_ops *o, uint32_t from, uint32_t to)
{
    if (from == to)
        return 0;
    o->stop(o->u);
    o->alt(o->u, 1, 0);
    o->alt(o->u, 2, 0);
    o->sleep_ms(o->u, VTI_RATE_SETTLE_MS);
    if (o->alt(o->u, 1, 1) || o->alt(o->u, 2, 1))
        return -2;
    if (vti_rate_set_cur(o, to) == 0)
        return o->start(o->u, to) ? -2 : 0;
    vti_rate_set_cur(o, from);
    return o->start(o->u, from) ? -2 : -1;
}

// at most one switch per VTI_RATE_DEBOUNCE_MS; times are a 32-bit ms clock (the broker's uptime)
typedef struct {
    int switched;     // a switch happened
    uint32_t last_ms; // when
} vti_rate_guard;

static inline int vti_rate_may_switch(const vti_rate_guard *g, uint32_t now_ms)
{
    return !g->switched || (uint32_t)(now_ms - g->last_ms) >= VTI_RATE_DEBOUNCE_MS;
}

static inline void vti_rate_switched(vti_rate_guard *g, uint32_t now_ms)
{
    g->switched = 1;
    g->last_ms = now_ms;
}

// The rate the stream runs at after a client asked for `wanted` while it runs (or last ran) at `cur`: the owner's
// wish when a switch is allowed now; otherwise it stays - a sharing client never switches the owner's Virus, and a
// second request within the debounce waits for the next one (the client reads the real rate from the shared ring).
// `wanted` 0 = no wish (a client that only listens): the rate stays.
static inline uint32_t vti_rate_choose(uint32_t cur, uint32_t wanted, int owner, int may_switch)
{
    return wanted && owner && may_switch ? vti_rate_valid(wanted) : cur;
}

// what a client asked for, `cur` when it had no wish (for the log)
static inline uint32_t vti_rate_wanted(uint32_t wanted, uint32_t cur) { return wanted ? vti_rate_valid(wanted) : cur; }

// The wanted rate in a VTIMSG_AUDIO payload (protocol 5): u32 on, the NUL-terminated event name, u32 rate. Older
// clients send no rate: 44100 (what they expect); 0 = no wish (keep the rate that is set); a rate other than
// 44100/48000 is 44100.
static inline uint32_t vti_audio_req_rate(const uint8_t *p, int len)
{
    for (int i = 4; i < len; i++)
        if (p[i] == 0) {
            if (len - (i + 1) < 4)
                return 44100;
            const uint8_t *r = p + i + 1;
            const uint32_t v = (uint32_t)r[0] | (uint32_t)r[1] << 8 | (uint32_t)r[2] << 16 | (uint32_t)r[3] << 24;
            return v ? vti_rate_valid(v) : 0;
        }
    return 44100;
}

// Recovery when the Virus leaves the stream's rate while streaming (E175/E241: packets of the other rate after a wake
// or a replug). Its USB chip ignores a SET_CUR with the rate it has stored, and the host CPU can have put the DSPs to
// the other rate without the chip (E331) - so asking for the same rate again changes nothing. A real switch away and,
// one debounce later, back makes the chip announce each change, and its announcements reach the DSPs. Three tries
// per stream, then the caller says once that the Virus needs a power cycle.
#define VTI_RELOCK_TRIES 3
enum { VTI_RELOCK_NONE, VTI_RELOCK_AWAY, VTI_RELOCK_BACK, VTI_RELOCK_GIVE_UP };

typedef struct {
    int away;           // switched away, the way back is due at at_ms + VTI_RATE_DEBOUNCE_MS
    uint32_t back_rate; // the stream's rate before
    uint32_t at_ms;
    int tries;
} vti_relock;

static inline void vti_relock_reset(vti_relock *r)
{
    r->away = 0;
    r->tries = 0;
}

// `lost`: a second of the other rate's packets was just reported; `rate`: the stream's rate now. For AWAY and BACK,
// *to is the rate to switch the stream to.
static inline int vti_relock_step(vti_relock *r, int lost, uint32_t rate, uint32_t now_ms, uint32_t *to)
{
    if (r->away) { // the other rate's packets are expected now
        if ((uint32_t)(now_ms - r->at_ms) < VTI_RATE_DEBOUNCE_MS)
            return VTI_RELOCK_NONE;
        r->away = 0;
        if (rate == r->back_rate) // the Virus refused the switch away: nowhere to go back from
            return VTI_RELOCK_NONE;
        *to = r->back_rate;
        return VTI_RELOCK_BACK;
    }
    if (!lost)
        return VTI_RELOCK_NONE;
    if (r->tries < VTI_RELOCK_TRIES) {
        r->tries++;
        r->away = 1;
        r->back_rate = rate;
        r->at_ms = now_ms;
        *to = rate == 48000 ? 44100 : 48000;
        return VTI_RELOCK_AWAY;
    }
    return r->tries++ == VTI_RELOCK_TRIES ? VTI_RELOCK_GIVE_UP : VTI_RELOCK_NONE;
}
