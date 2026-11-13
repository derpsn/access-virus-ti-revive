// owneruid.h - whose broker this is (libusb-audio branch, E511). On macOS the broker needs root to take the Virus'
// audio from Apple's driver; started with sudo it still serves the user who started it: its socket and audio ring
// are named after and owned by that user (SUDO_UID), so the plug-in finds them as before. The plug-in, for its part,
// trusts a broker socket of its own user or of root, nobody else (another user could have made the path in /tmp
// first, review 2026-10-19 H4). Header only, C: the broker and the plug-in's tests use it.
#pragma once
#include <stdlib.h>

// the user the broker serves: SUDO_UID when running as root through sudo (a positive number, nothing else), else uid
static inline unsigned vti_owner_uid(unsigned euid, unsigned uid, const char *sudo_uid)
{
    if (euid != 0 || !sudo_uid || !*sudo_uid)
        return uid;
    char *end = NULL; // long long: a 32-bit long (Windows) saturates instead of failing the range check
    long long v = strtoll(sudo_uid, &end, 10);
    if (*end || v <= 0 || v > 0x7fffffffLL)
        return uid;
    return (unsigned)v;
}

// may the plug-in talk to a broker socket whose peer is `peer`? Its own user's, or root's (the sudo broker)
static inline int vti_peer_trusted(unsigned peer, unsigned me) { return peer == me || peer == 0; }
