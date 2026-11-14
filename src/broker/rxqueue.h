// rxqueue.h - between the libusb callbacks and the reader thread of one MIDI endpoint (vticore.c, Linux and macOS).
//
// Why: the original's Mac driver keeps 8 reads pending on each endpoint and resubmits each as it completes (E521 (1));
// we had one blocking read whose thread also wrote to every client socket, so a slow client left EP 0x85 unasked, and
// the Virus holds a key until the host asks (E517 (5)). Now the callback only queues and resubmits; it may run on any
// thread that handles libusb events (the Linux audio thread too), so it must never block. The reader thread takes the
// records in order and keeps the error rules of vtireaderr.h. Nothing is dropped: when the queue could not take what
// the pending reads might bring, a transfer is parked and the reader resubmits it once it has made room.
// Plain C, no locking here (the caller holds its mutex), no I/O; the unit test calls it.
#pragma once
#include <stdint.h>
#include <string.h>

#define VTI_RX_MAX 64 // the endpoints' wMaxPacketSize is 64 at most (lsusb -v)

typedef struct {
    uint8_t slot;            // which transfer it came from, for the resubmit after an error
    int8_t err;              // 0 = data, else the libusb error code (vti_xfer_err)
    uint8_t len;
    uint8_t data[VTI_RX_MAX];
} vti_rx_rec;

typedef struct {
    vti_rx_rec *r;
    unsigned cap, head, count;
} vti_rxq;

static inline void vti_rxq_init(vti_rxq *q, vti_rx_rec *recs, unsigned cap)
{
    q->r = recs;
    q->cap = cap;
    q->head = q->count = 0;
}

// 0, or -1 when full (never overwrites)
static inline int vti_rxq_push(vti_rxq *q, int slot, int err, const uint8_t *data, int len)
{
    if (q->count >= q->cap)
        return -1;
    vti_rx_rec *e = &q->r[(q->head + q->count) % q->cap];
    e->slot = (uint8_t)slot;
    e->err = (int8_t)err;
    e->len = (uint8_t)(len < 0 ? 0 : len > VTI_RX_MAX ? VTI_RX_MAX : len);
    if (e->len)
        memcpy(e->data, data, e->len);
    q->count++;
    return 0;
}

static inline int vti_rxq_pop(vti_rxq *q, vti_rx_rec *out)
{
    if (!q->count)
        return -1;
    *out = q->r[q->head];
    q->head = (q->head + 1) % q->cap;
    q->count--;
    return 0;
}

// a resubmit is safe while every pending read could still complete into the queue
static inline int vti_rxq_may_resubmit(const vti_rxq *q, unsigned pending) { return q->count + pending <= q->cap; }

static inline void vti_park(unsigned *parked, int slot) { *parked |= 1u << slot; }

static inline int vti_unpark(unsigned *parked) // the lowest parked slot, or -1
{
    for (int i = 0; i < 32; i++)
        if (*parked & (1u << i)) {
            *parked &= ~(1u << i);
            return i;
        }
    return -1;
}

// After the reader gave up on the device its other transfers stay submitted until the close (0.25-0.5 s): what they
// bring still goes to the clients (S138: a key in that window was lost), further errors are neither counted nor resent.
static inline int vti_rx_hand_on_after_giveup(int err) { return err == 0; }

// What a completed transfer does (rx_done). While the close runs (`closing`: transfers cancelled, readers still
// draining) data still goes on but nothing is resubmitted (S139: a dump across a recovery reopen lost 8 packets).
// While the reader clears a halt (`quiet`: the endpoint's transfers cancelled first, the kernel warns about a clear on
// an endpoint with transfers queued, S142) what comes back is parked for the resubmit after the clear; data still on.
enum { VTI_RX_QUEUE_RESUBMIT, VTI_RX_ERROR, VTI_RX_QUEUE, VTI_RX_DROP, VTI_RX_QUEUE_PARK, VTI_RX_PARK };
static inline int vti_rx_done_action(int closing, int quiet, int err)
{
    if (closing)
        return err ? VTI_RX_DROP : VTI_RX_QUEUE;
    if (quiet)
        return err ? VTI_RX_PARK : VTI_RX_QUEUE_PARK;
    return err ? VTI_RX_ERROR : VTI_RX_QUEUE_RESUBMIT;
}

// After an error only one transfer probes the endpoint, the others wait parked until data comes back (S143: every
// round put all 8 out into a halt that stayed, 375 stalled transfers in one give-up run).
static inline int vti_rx_probe_now(int inflight) { return inflight == 0; }
static inline int vti_rx_unpark_after(int err) { return err == 0; }

// libusb_transfer_status -> the libusb error code the reader's rules use (plain numbers, testable without libusb)
static inline int vti_xfer_err(int status)
{
    switch (status) {
    case 0: return 0;  // LIBUSB_TRANSFER_COMPLETED
    case 2: return -7; // TIMED_OUT -> LIBUSB_ERROR_TIMEOUT
    case 4: return -9; // STALL -> LIBUSB_ERROR_PIPE
    case 5: return -4; // NO_DEVICE -> LIBUSB_ERROR_NO_DEVICE
    case 6: return -8; // OVERFLOW -> LIBUSB_ERROR_OVERFLOW
    default: return -1; // ERROR, and CANCELLED outside a close -> LIBUSB_ERROR_IO
    }
}
