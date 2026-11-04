// trafficring.h - the broker's pre-roll: the newest traffic records, kept from the moment the broker runs, so a capture
// that connects later still gets the handshake and the Virus' reply to it (E392: that reply reports Local). Holds whole
// records, drops the oldest and counts them. Plain C, no locking (the caller holds its lock); the unit test calls it.
#pragma once
#include <stdint.h>
#include <string.h>

#define VTI_TRAFRING_HDR 6        // kind, ep, u32 ms: the head of a VTIMSG_OBS_TRAFFIC message
#define VTI_TRAFRING_MAXBODY 8192 // a longer record is refused, not cut

typedef struct {
    uint8_t *mem;
    size_t cap, head, len; // bytes, the oldest record at head
    uint64_t lost;         // records dropped or refused
} vti_trafring;

typedef void (*vti_trafring_cb)(void *user, const uint8_t *hdr, const uint8_t *body, int n);

static inline void vti_trafring_init(vti_trafring *r, uint8_t *mem, size_t cap)
{
    r->mem = mem, r->cap = cap, r->head = r->len = 0, r->lost = 0;
}

static inline void vti_trafring_read_(const vti_trafring *r, size_t at, void *out, size_t n)
{
    size_t pos = (r->head + at) % r->cap, first = n < r->cap - pos ? n : r->cap - pos;
    memcpy(out, r->mem + pos, first);
    memcpy((uint8_t *)out + first, r->mem, n - first);
}

static inline void vti_trafring_write_(vti_trafring *r, const void *p, size_t n)
{
    size_t pos = (r->head + r->len) % r->cap, first = n < r->cap - pos ? n : r->cap - pos;
    memcpy(r->mem + pos, p, first);
    memcpy(r->mem, (const uint8_t *)p + first, n - first);
    r->len += n;
}

static inline void vti_trafring_push(vti_trafring *r, const uint8_t *hdr, const void *a, int alen, const void *b, int blen)
{
    const size_t body = (size_t)(alen > 0 ? alen : 0) + (size_t)(blen > 0 ? blen : 0), total = 2 + VTI_TRAFRING_HDR + body;
    if (body > VTI_TRAFRING_MAXBODY || total > r->cap) {
        r->lost++;
        return;
    }
    while (r->cap - r->len < total) { // room by dropping whole records, the oldest first
        uint16_t old;
        vti_trafring_read_(r, 0, &old, 2);
        r->head = (r->head + 2u + old) % r->cap;
        r->len -= 2u + old;
        r->lost++;
    }
    const uint16_t size = (uint16_t)(VTI_TRAFRING_HDR + body);
    vti_trafring_write_(r, &size, 2);
    vti_trafring_write_(r, hdr, VTI_TRAFRING_HDR);
    if (alen > 0)
        vti_trafring_write_(r, a, (size_t)alen);
    if (blen > 0)
        vti_trafring_write_(r, b, (size_t)blen);
}

static inline void vti_trafring_each(const vti_trafring *r, vti_trafring_cb cb, void *user)
{
    uint8_t rec[VTI_TRAFRING_HDR + VTI_TRAFRING_MAXBODY];
    for (size_t at = 0; at < r->len;) {
        uint16_t size;
        vti_trafring_read_(r, at, &size, 2);
        vti_trafring_read_(r, at + 2, rec, size);
        cb(user, rec, rec + VTI_TRAFRING_HDR, size - VTI_TRAFRING_HDR);
        at += 2u + size;
    }
}
