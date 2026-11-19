// keyporthelper.h - macOS, the libusb package (#37, E544): its broker runs as root from launchd (a LaunchDaemon), outside
// the user's login session, and CoreMIDI shows a virtual source only in the session of the process that made it: the
// ports "TI Revive Synth" / "TI Revive MIDI" never reached Logic. So a root broker that serves a user starts a helper in
// that user's session (launchctl asuser UID vtibroker -midiports UID); the helper gives up root, makes the sources and
// gets the messages on its standard input in midifeed.h frames, as the Windows helper. A broker of the user itself (the
// normal package) makes the sources as before. Read-only as before: the helper has no destination (rule 2).
// Plain C, no I/O: the unit test calls it.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VTI_KEYPORT_HELPER_ARGS 7
#define VTI_KEYPORT_RESTART_MS 10000

// root, serving a user: the ports must be made in that user's session
static inline int vti_keyport_via_helper(unsigned euid, unsigned owner) { return euid == 0 && owner != 0; }

// the helper's command line into argv (NULL-terminated); `uid` is room for the user number. 0 when it cannot be built.
static inline int vti_keyport_helper_argv(const char **argv, char *uid, size_t uid_cap, const char *self, unsigned owner)
{
    char num[16]; // the number first: snprintf straight into a short `uid` trips -Wformat-truncation
    const int len = snprintf(num, sizeof num, "%u", owner);
    if (!self || !*self || owner == 0 || len <= 0 || (size_t)len >= uid_cap)
        return 0;
    memcpy(uid, num, (size_t)len + 1);
    argv[0] = "/bin/launchctl";
    argv[1] = "asuser";
    argv[2] = uid;
    argv[3] = self;
    argv[4] = "-midiports";
    argv[5] = uid;
    argv[6] = NULL;
    return 1;
}

// start (again) now? `last_ms` < 0: never started. A helper that ended is started at the next message, not more often
// than every 10 s (the USB reader thread pays for it)
static inline int vti_keyport_restart_due(int64_t now_ms, int64_t last_ms)
{
    return last_ms < 0 || now_ms - last_ms >= VTI_KEYPORT_RESTART_MS;
}

// the helper's argument: a positive user number, else 0 (the helper then exits: it never makes ports as root)
static inline unsigned vti_keyport_helper_uid(const char *s)
{
    if (!s || !*s)
        return 0;
    char *end = NULL;
    const long long v = strtoll(s, &end, 10);
    return *end || v <= 0 || v > 0x7fffffffLL ? 0 : (unsigned)v;
}

// midifeed.h's port (0 "TI Revive MIDI", 1 "TI Revive Synth") to vtikeyport_mac.c's source index ([0] Synth, [1] MIDI)
static inline int vti_keyport_mac_source(int feed_port) { return feed_port == 1 ? 0 : feed_port == 0 ? 1 : -1; }
