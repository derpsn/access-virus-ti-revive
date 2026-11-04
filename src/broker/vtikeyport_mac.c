// vtikeyport_mac.c - see vtikeyport_mac.h
#include "vtikeyport_mac.h"
#include "vticore.h"
#include <CoreFoundation/CoreFoundation.h>
#include <CoreMIDI/CoreMIDI.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>

// DAWs remember a port by this number, also when it comes back after the broker was gone (it exits 30 s after its last
// client): it must stay the same. "VTI1".
#define KEYPORT_UNIQUE_ID 0x56544931

static _Atomic(MIDIEndpointRef) g_source;

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
    Byte buf[64];
    MIDIPacketList *list = (MIDIPacketList *)buf;
    MIDIPacket *pkt = MIDIPacketListInit(list);
    pkt = MIDIPacketListAdd(list, sizeof buf, pkt, 0, (ByteCount)n, msg); // 0 = now
    if (pkt)
        MIDIReceived(src, list);
}

#pragma clang diagnostic pop
