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
