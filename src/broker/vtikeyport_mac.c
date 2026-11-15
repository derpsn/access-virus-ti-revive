// vtikeyport_mac.c - see vtikeyport.h. A broker of the user (the normal package) makes the CoreMIDI sources itself; the
// libusb package's root LaunchDaemon cannot (its sources are not in the user's session, #37, E544), so it starts a
// helper in the user's session and feeds it the messages (keyporthelper.h, midifeed.h).
#include "vtikeyport.h"
#include "keyporthelper.h"
#include "midifeed.h"
#include "owneruid.h"
#include "vticore.h"
#include <CoreFoundation/CoreFoundation.h>
#include <CoreMIDI/CoreMIDI.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <limits.h>
#include <mach-o/dyld.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <pwd.h>
#include <spawn.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

// DAWs remember a port by this number, also when it comes back after the broker was gone (it exits 30 s after its last
// client): it must stay the same. "VTI1" for the keys, "VTI0" for MIDI In (the cable numbers).
#define KEYPORT_UNIQUE_ID 0x56544931
#define DINPORT_UNIQUE_ID 0x56544930

// [0] "Virus TI Synth" (cable 1), [1] "Virus TI MIDI" (cable 0)
static _Atomic(MIDIEndpointRef) g_source[2];
static atomic_uint g_sent[2], g_failed[2]; // for the log: did the messages get as far as macOS (#29)
static const char *const g_name[2] = {"Virus TI Synth", "Virus TI MIDI"};

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations" // MIDI 1.0 calls: the port speaks MIDI 1.0, macOS 10.x and up

static MIDIEndpointRef make_source(MIDIClientRef client, int k, CFStringRef name, SInt32 id)
{
    MIDIEndpointRef src = 0;
    OSStatus st = MIDISourceCreate(client, name, &src);
    if (st != noErr) {
        vti_log("MIDI port \"%s\": no source (%d)\n", g_name[k], (int)st);
        return 0;
    }
    st = MIDIObjectSetIntegerProperty(src, kMIDIPropertyUniqueID, id);
    if (st != noErr) // another process has it (a second broker): the port still works, a DAW may not remember it
        vti_log("MIDI port \"%s\": unique ID not set (%d)\n", g_name[k], (int)st);
    return src;
}

// both sources; 0 when CoreMIDI gave none
static int make_sources(void)
{
    MIDIClientRef client = 0;
    OSStatus st = MIDIClientCreate(CFSTR("TI Revive"), NULL, NULL, &client);
    if (st != noErr) {
        vti_log("MIDI port: no CoreMIDI client (%d)\n", (int)st);
        return 0;
    }
    // both, as the original's Mac driver had them (E521 (4)); one failing leaves the other
    MIDIEndpointRef keys = make_source(client, 0, CFSTR("Virus TI Synth"), KEYPORT_UNIQUE_ID);
    if (keys) {
        atomic_store(&g_source[0], keys);
        vti_log("MIDI port \"Virus TI Synth\" is there (Virus keys to the DAW, read-only)\n");
    }
    MIDIEndpointRef din = make_source(client, 1, CFSTR("Virus TI MIDI"), DINPORT_UNIQUE_ID);
    if (din) {
        atomic_store(&g_source[1], din);
        vti_log("MIDI port \"Virus TI MIDI\" is there (the Virus' MIDI In to the DAW, read-only)\n");
    }
    return keys || din;
}

// MIDIClientCreate can wait for the MIDI server: not on the broker's start-up path
static void *start_thread(void *arg)
{
    (void)arg;
    make_sources();
    return NULL;
}

// --- the root broker's side: the helper in the user's session (keyporthelper.h) ---
static int g_via_helper;            // set once in vti_keyport_start, before the USB reader runs
static unsigned g_owner;            // the user the helper runs for
static char g_self[PATH_MAX];       // this program, the helper's binary
static int g_helper_fd = -1;        // the helper's standard input; only the USB reader thread writes and restarts
static pid_t g_helper_pid = -1;
static int64_t g_helper_started = -1; // ms, CLOCK_MONOTONIC

static int64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void helper_start(void)
{
    g_helper_started = now_ms();
    if (g_helper_pid > 0) { // the one that ended: reaped, not left as a zombie
        waitpid(g_helper_pid, NULL, WNOHANG);
        g_helper_pid = -1;
    }
    char uid[16];
    const char *argv[VTI_KEYPORT_HELPER_ARGS];
    if (!vti_keyport_helper_argv(argv, uid, sizeof uid, g_self, g_owner))
        return;
    int p[2];
    if (pipe(p)) {
        vti_log("MIDI ports: no pipe for the helper (%s)\n", strerror(errno));
        return;
    }
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, p[0], 0);
    posix_spawn_file_actions_addclose(&fa, p[0]);
    posix_spawn_file_actions_addclose(&fa, p[1]);
    pid_t pid = -1;
    const int err = posix_spawn(&pid, argv[0], &fa, NULL, (char *const *)argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    close(p[0]);
    if (err) {
        close(p[1]);
        vti_log("MIDI ports: helper not started (%s)\n", strerror(err));
        return;
    }
    // never block the USB reader thread: a stuck helper costs messages, not the Virus' input
    fcntl(p[1], F_SETFL, fcntl(p[1], F_GETFL) | O_NONBLOCK);
    fcntl(p[1], F_SETFD, FD_CLOEXEC);
    g_helper_fd = p[1];
    g_helper_pid = pid;
    vti_log("MIDI ports: helper started in the session of user %u (pid %d)\n", g_owner, (int)pid);
}

void vti_keyport_start(void)
{
    g_owner = vti_owner_uid((unsigned)geteuid(), (unsigned)getuid(),
                            vti_owner_env(getenv("VTI_OWNER_UID"), getenv("SUDO_UID")));
    if (vti_keyport_via_helper((unsigned)geteuid(), g_owner)) {
        char raw[PATH_MAX];
        uint32_t rsize = sizeof raw;
        if (_NSGetExecutablePath(raw, &rsize) != 0 || !realpath(raw, g_self)) {
            vti_log("MIDI ports: own path unknown, no helper\n");
            return;
        }
        g_via_helper = 1;
        helper_start();
        return;
    }
    pthread_t t;
    if (pthread_create(&t, NULL, start_thread, NULL) == 0)
        pthread_detach(t);
    else
        vti_log("MIDI port: thread not started\n");
}

static void helper_send(int k, const uint8_t *msg, int n)
{
    if (g_helper_fd < 0) {
        if (!vti_keyport_restart_due(now_ms(), g_helper_started))
            return;
        helper_start();
        if (g_helper_fd < 0)
            return;
    }
    uint8_t frame[8];
    const int len = vti_midifeed_frame(k == 0 ? 1 : 0, msg, n, frame, sizeof frame); // [0] Synth = feed port 1
    if (len <= 0)
        return;
    const ssize_t w = write(g_helper_fd, frame, (size_t)len);
    if (w == len) {
        if (atomic_fetch_add(&g_sent[k], 1) == 0)
            vti_log("MIDI port \"%s\": first message handed to the helper\n", g_name[k]);
        return;
    }
    if (w < 0 && errno != EAGAIN) { // the helper ended (logout, crash): started again at a later message
        vti_log("MIDI ports: the helper is gone (%s)\n", strerror(errno));
        close(g_helper_fd);
        g_helper_fd = -1;
    }
    atomic_fetch_add(&g_failed[k], 1);
}

static void send_to(int k, const uint8_t *msg, int n)
{
    if (g_via_helper) {
        helper_send(k, msg, n);
        return;
    }
    const MIDIEndpointRef src = atomic_load(&g_source[k]);
    if (!src)
        return;
    _Alignas(MIDIPacketList) Byte buf[64];
    MIDIPacketList *list = (MIDIPacketList *)buf;
    MIDIPacket *pkt = MIDIPacketListInit(list);
    // A real time stamp, not 0: with 0 Live's MIDI In light blinks but the notes never reach the track (#29, E423)
    pkt = MIDIPacketListAdd(list, sizeof buf, pkt, mach_absolute_time(), (ByteCount)n, msg);
    const OSStatus st = pkt ? MIDIReceived(src, list) : (OSStatus)-1;
    if (st == noErr) {
        if (atomic_fetch_add(&g_sent[k], 1) == 0)
            vti_log("MIDI port \"%s\": first message handed to macOS\n", g_name[k]);
    } else if (atomic_fetch_add(&g_failed[k], 1) == 0) { // once: this runs per message on the USB reader thread
        vti_log("MIDI port \"%s\": message not handed to macOS (%d)\n", g_name[k], (int)st);
    }
}

void vti_keyport_send(const uint8_t *msg, int n) { send_to(0, msg, n); }
void vti_dinport_send(const uint8_t *msg, int n) { send_to(1, msg, n); }

void vti_keyport_log_counts(void)
{
    for (int k = 0; k < 2; k++)
        if (g_via_helper)
            vti_log("MIDI port \"%s\": %u messages handed to the helper, %u not\n", g_name[k],
                    atomic_load(&g_sent[k]), atomic_load(&g_failed[k]));
        else if (atomic_load(&g_source[k]))
            vti_log("MIDI port \"%s\": %u messages handed to macOS, %u failed\n", g_name[k], atomic_load(&g_sent[k]),
                    atomic_load(&g_failed[k]));
}

// --- the helper (vtibroker -midiports UID, started by the root broker through launchctl asuser) ---
static void on_frame(void *user, int port, const uint8_t *msg, int n)
{
    (void)user;
    const int k = vti_keyport_mac_source(port);
    if (k >= 0)
        send_to(k, msg, n);
}

int vti_keyport_helper_main(const char *uid_arg)
{
    const unsigned uid = vti_keyport_helper_uid(uid_arg);
    if (uid == 0) {
        vti_log("midiports: no user number, exiting\n");
        return 2;
    }
    // root only had to start it in the user's session: the ports themselves need no rights
    if (geteuid() == 0) {
        const struct passwd *pw = getpwuid((uid_t)uid);
        if (!pw || initgroups(pw->pw_name, (int)pw->pw_gid) || setgid(pw->pw_gid) || setuid((uid_t)uid) ||
            setuid(0) == 0) {
            vti_log("midiports: could not give up root for user %u, exiting\n", uid);
            return 2;
        }
    }
    if (!make_sources())
        return 1;
    vti_midifeed_reader r;
    vti_midifeed_init(&r);
    uint8_t buf[256];
    for (;;) {
        const ssize_t n = read(0, buf, sizeof buf);
        if (n > 0) {
            vti_midifeed_read(&r, buf, (int)n, on_frame, NULL);
        } else if (n == 0 || errno != EINTR) {
            break; // the broker closed the pipe (its exit, a crash too): the ports go with it, as before
        }
    }
    vti_keyport_log_counts();
    return 0;
}

#pragma clang diagnostic pop
