// vtikeyport_mac.c - see vtikeyport_mac.h
#include "vtikeyport_mac.h"
#include "vticore.h"
#include <CoreFoundation/CoreFoundation.h>
#include <CoreMIDI/CoreMIDI.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>

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

// MIDIClientCreate can wait for the MIDI server: not on the broker's start-up path
static void *start_thread(void *arg)
{
    (void)arg;
    MIDIClientRef client = 0;
    OSStatus st = MIDIClientCreate(CFSTR("TI Revive"), NULL, NULL, &client);
    if (st != noErr) {
        vti_log("MIDI port: no CoreMIDI client (%d)\n", (int)st);
        return NULL;
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
    return NULL;
}

void vti_keyport_start(void)
{
    pthread_t t;
    if (pthread_create(&t, NULL, start_thread, NULL) == 0)
        pthread_detach(t);
    else
        vti_log("MIDI port: thread not started\n");
}

static void send_to(int k, const uint8_t *msg, int n)
{
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
        if (atomic_load(&g_source[k]))
            vti_log("MIDI port \"%s\": %u messages handed to macOS, %u failed\n", g_name[k], atomic_load(&g_sent[k]),
                    atomic_load(&g_failed[k]));
}

#pragma clang diagnostic pop
