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

// where the user number comes from: the libusb package's LaunchDaemon names it in VTI_OWNER_UID (launchd sets no
// SUDO_UID); the Terminal test runs through sudo. The first one set wins; vti_owner_uid checks it either way.
static inline const char *vti_owner_env(const char *owner_uid, const char *sudo_uid)
{
    return owner_uid && *owner_uid ? owner_uid : sudo_uid;
}

// may the plug-in talk to a broker socket whose peer is `peer`? Its own user's, or root's (the sudo broker)
static inline int vti_peer_trusted(unsigned peer, unsigned me) { return peer == me || peer == 0; }

// may a client with user `peer` use the broker that serves `owner`? The same rule from the broker's side: as a root
// service it must not serve another user of the Mac (the socket's owner and mode are the first guard)
static inline int vti_client_allowed(unsigned peer, unsigned owner) { return peer == owner || peer == 0; }
