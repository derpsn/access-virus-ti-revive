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
// client): it must stay the same. "VTI1".
#define KEYPORT_UNIQUE_ID 0x56544931

static _Atomic(MIDIEndpointRef) g_source;
static atomic_uint g_sent, g_failed; // for the log: did the keys get as far as macOS (#29)

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations" // MIDI 1.0 calls: the port speaks MIDI 1.0, macOS 10.x and up

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
    MIDIEndpointRef src = 0;
    st = MIDISourceCreate(client, CFSTR("Virus TI Synth"), &src);
    if (st != noErr) {
        vti_log("MIDI port: no source (%d)\n", (int)st);
        return NULL;
    }
    st = MIDIObjectSetIntegerProperty(src, kMIDIPropertyUniqueID, KEYPORT_UNIQUE_ID);
    if (st != noErr) // another process has it (a second broker): the port still works, a DAW may not remember it
        vti_log("MIDI port: unique ID not set (%d)\n", (int)st);
    atomic_store(&g_source, src);
    vti_log("MIDI port \"Virus TI Synth\" is there (Virus keys to the DAW, read-only)\n");
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

void vti_keyport_send(const uint8_t *msg, int n)
{
    const MIDIEndpointRef src = atomic_load(&g_source);
    if (!src)
        return;
    _Alignas(MIDIPacketList) Byte buf[64];
    MIDIPacketList *list = (MIDIPacketList *)buf;
    MIDIPacket *pkt = MIDIPacketListInit(list);
    // A real time stamp, not 0: with 0 Live's MIDI In light blinks but the notes never reach the track (#29, E423)
    pkt = MIDIPacketListAdd(list, sizeof buf, pkt, mach_absolute_time(), (ByteCount)n, msg);
    const OSStatus st = pkt ? MIDIReceived(src, list) : (OSStatus)-1;
    if (st == noErr) {
        if (atomic_fetch_add(&g_sent, 1) == 0)
            vti_log("MIDI port: first message handed to macOS\n");
    } else if (atomic_fetch_add(&g_failed, 1) == 0) { // once: this runs per key on the USB reader thread
        vti_log("MIDI port: message not handed to macOS (%d)\n", (int)st);
    }
}

void vti_keyport_log_counts(void)
{
    if (atomic_load(&g_source))
        vti_log("MIDI port: %u messages handed to macOS, %u failed\n", atomic_load(&g_sent), atomic_load(&g_failed));
}

#pragma clang diagnostic pop
