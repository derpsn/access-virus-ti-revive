// sendtimeout.h - how long the POSIX broker waits for one send to a client (audit 2026-11-17 #1). The broker sends
// RX packets, audio ticks and states while it holds the client-list lock; a client that stops reading (a DAW frozen
// while it writes a crash report, tirevive-cc suspended with Ctrl+Z) filled its socket buffer and blocked that send
// for good: the MIDI reader, the Linux ticks, every other client and the reconnect waited with it. With a timeout
// the send fails, the broker shuts that client's socket and its own thread tears it down (the Windows broker drops a
// client whose queue overflows). 0 = block again (observers have their own writer thread). Header only, C.
#pragma once
#include <sys/socket.h>
#include <sys/time.h>

#define VTI_SEND_TIMEOUT_MS 250 // longer than any normal pause of a reading client, short against an audio stall

static inline int vti_send_timeout(int fd, int ms)
{
    struct timeval tv = {ms / 1000, (ms % 1000) * 1000};
    return setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
}
