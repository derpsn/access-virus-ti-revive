// stallfill.h - how much silence the USB audio on libusb must add so the frame counter stays exact (E078, E439).
// Only frames really lost count. Wall clock against delivered frames alone is not enough: the two clocks drift apart by
// tens of ppm, which at a fixed limit added 30 ms that stayed every 22 minutes (E439), and a thread held up while
// transfers stay queued loses nothing, those transfers then complete at once. So the difference is read only when
// completions run at their normal pace again, against a base that follows slow drift. Header-only, tested without hardware.
#pragma once
#include <stdint.h>

typedef struct {
    double t0, last, prev_dt, base;
    double np; // ms per transfer
} vti_stallfill;

#define VTI_STALLFILL_INIT(np_ms) {0, 0, (double)(np_ms), 0, (double)(np_ms)}

// now: monotonic ms at this completion; delivered_ms: ms delivered before it. Returns the ms of silence to add first.
static inline uint64_t vti_stallfill_step(vti_stallfill *s, double now, uint64_t delivered_ms)
{
    if (!s->t0) {
        s->t0 = now - s->np; // this transfer covered the last np ms
        s->last = now;
        return 0;
    }
    const double dt = now - s->last;
    s->last = now;
    const int calm = dt >= s->np / 2 && dt <= s->np * 2 && s->prev_dt >= s->np / 2;
    s->prev_dt = dt;
    if (!calm) // a gap or a burst of queued transfers: wait until they have drained
        return 0;
    const double excess = (now - s->t0) - ((double)delivered_ms + s->np) - s->base;
    if (excess > 3 * s->np)
        return (uint64_t)excess;
    s->base += excess / 256; // slow drift, either way
    return 0;
}
