// firstmute.h - simulator S098 (= S078's click at plug-in start): on a running device (a kept broker, no USB reset)
// the first iso IN transfer of a new stream carries stale memory of the USB chip, two odd samples next to
// descriptor-like bytes; a port reset clears it, a stream restart does not. The broker mutes that one transfer and
// keeps its frame count, so the timeline does not move. Header only: the broker and the tests use it.
#pragma once

typedef struct {
    int pending; // the next completed transfer is the stream's first
} vti_firstmute;

static inline void vti_firstmute_start(vti_firstmute *m) { m->pending = 1; }

// 1 for the first completed transfer of the stream (mute it), 0 afterwards
static inline int vti_firstmute_take(vti_firstmute *m)
{
    const int first = m->pending;
    m->pending = 0;
    return first;
}
