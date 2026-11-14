// keystamp.c - macOS test sender for #39: a CoreMIDI source that plays a steady note grid, stamped the way the
// broker's "Virus TI Synth" port stamps the Virus' keys (mach_absolute_time() at the moment of MIDIReceived,
// src/broker/vtikeyport_mac.c). Record it in a DAW and compare where the notes land with the send log: a constant
// offset is latency, a drift or a step after some seconds is what #39 describes.
//
// The schedule is absolute (t0 + i * interval, mach_wait_until), so the sender itself does not drift; every send is
// logged with its scheduled and actual time. Notes: C4 on the first beat of each bar, E4 on the others.
//
// build: clang -O2 -Wall -o keystamp keystamp.c -framework CoreMIDI -framework CoreFoundation
//        (on our Mac the Command Line Tools' linker cannot read the 27.0 SDK: add
//        -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk)
// usage: keystamp [--interval-ms 500] [--count 120] [--len-ms 100] [--channel 1] [--name "TI Revive Test Keys"]
//                 [--log keystamp.csv]
//        It creates the port, waits for Enter (arm the track and start recording first), then plays.
#include <CoreFoundation/CoreFoundation.h>
#include <CoreMIDI/CoreMIDI.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/thread_policy.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations" // MIDI 1.0 calls, as in the broker's port

static mach_timebase_info_data_t tb;

static uint64_t ns_to_ticks(uint64_t ns) { return ns * tb.denom / tb.numer; }
static double ticks_to_us(int64_t t) { return (double)t * tb.numer / tb.denom / 1000.0; }

// A normal process wakes from mach_wait_until about 10 ms late (timer coalescing, seen in the first dry run on our
// Mac); the time-constraint policy, as an audio thread uses, wakes it on time. The broker's USB reader is a normal
// thread, but it wakes on USB completion, not on a timer, so this only makes the sender a clean reference.
static void realtime(uint64_t period)
{
    thread_time_constraint_policy_data_t p = {(uint32_t)period, (uint32_t)(period / 20), (uint32_t)(period / 10), 1};
    if (thread_policy_set(mach_thread_self(), THREAD_TIME_CONSTRAINT_POLICY, (thread_policy_t)&p,
                          THREAD_TIME_CONSTRAINT_POLICY_COUNT) != KERN_SUCCESS)
        fprintf(stderr, "keystamp: no time-constraint policy, sends may be late (see late_us)\n");
}

// one message, stamped "now" like vti_keyport_send; returns the stamp
static uint64_t send(MIDIEndpointRef src, const uint8_t *msg, int n)
{
    _Alignas(MIDIPacketList) Byte buf[64];
    MIDIPacketList *list = (MIDIPacketList *)buf;
    MIDIPacket *pkt = MIDIPacketListInit(list);
    const uint64_t now = mach_absolute_time();
    pkt = MIDIPacketListAdd(list, sizeof buf, pkt, now, (ByteCount)n, msg);
    if (!pkt || MIDIReceived(src, list) != noErr)
        fprintf(stderr, "keystamp: MIDIReceived failed\n");
    return now;
}

int main(int argc, char **argv)
{
    int interval_ms = 500, count = 120, len_ms = 100, channel = 1;
    const char *name = "TI Revive Test Keys", *logpath = "keystamp.csv";
    for (int i = 1; i < argc; i += 2) { // every option takes one value
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!v) {
            fprintf(stderr, "keystamp: %s needs a value\n", a);
            return 2;
        }
        if (!strcmp(a, "--interval-ms"))
            interval_ms = atoi(v);
        else if (!strcmp(a, "--count"))
            count = atoi(v);
        else if (!strcmp(a, "--len-ms"))
            len_ms = atoi(v);
        else if (!strcmp(a, "--channel"))
            channel = atoi(v);
        else if (!strcmp(a, "--name"))
            name = v;
        else if (!strcmp(a, "--log"))
            logpath = v;
        else {
            fprintf(stderr, "keystamp: unknown option %s\n", a);
            return 2;
        }
    }
    // bounded on purpose (AGENTS.md rule 2: no MIDI flood): at most 10 notes a second, at most 30 minutes
    if (interval_ms < 100 || len_ms < 10 || len_ms >= interval_ms || count < 1 || count > 18000 || channel < 1 ||
        channel > 16) {
        fprintf(stderr, "keystamp: interval >= 100 ms, 10 <= len < interval, 1 <= count <= 18000, channel 1..16\n");
        return 2;
    }
    mach_timebase_info(&tb);

    MIDIClientRef client = 0;
    MIDIEndpointRef src = 0;
    CFStringRef cfname = CFStringCreateWithCString(NULL, name, kCFStringEncodingUTF8);
    if (MIDIClientCreate(CFSTR("keystamp"), NULL, NULL, &client) != noErr ||
        MIDISourceCreate(client, cfname, &src) != noErr) {
        fprintf(stderr, "keystamp: no CoreMIDI source\n");
        return 1;
    }
    FILE *log = fopen(logpath, "w");
    if (!log) {
        fprintf(stderr, "keystamp: cannot write %s\n", logpath);
        return 1;
    }
    printf("Port \"%s\" is there. In the DAW: MIDI From = this port, arm the track, start recording,\n"
           "then press Enter here. %d notes, one every %d ms (%.1f s).\n",
           name, count, interval_ms, count * interval_ms / 1000.0);
    fflush(stdout);
    getchar();

    const uint8_t st_on = (uint8_t)(0x90 | (channel - 1)), st_off = (uint8_t)(0x80 | (channel - 1));
    const uint64_t step = ns_to_ticks((uint64_t)interval_ms * 1000000ull), len = ns_to_ticks((uint64_t)len_ms * 1000000ull);
    realtime(ns_to_ticks(10000000ull)); // 10 ms period: plenty for a note every >= 100 ms
    const uint64_t t0 = mach_absolute_time() + ns_to_ticks(200000000ull); // first note 200 ms after Enter
    fprintf(log, "index,note,scheduled_us,sent_us,late_us\n");
    double worst = 0;
    for (int i = 0; i < count; i++) {
        const uint8_t note = i % 4 == 0 ? 60 : 64;
        const uint64_t at = t0 + (uint64_t)i * step;
        mach_wait_until(at);
        const uint8_t on[3] = {st_on, note, 100};
        const uint64_t sent = send(src, on, 3);
        const double late = ticks_to_us((int64_t)(sent - at));
        if (late > worst)
            worst = late;
        fprintf(log, "%d,%d,%.1f,%.1f,%.1f\n", i, note, ticks_to_us((int64_t)(at - t0)), ticks_to_us((int64_t)(sent - t0)),
                late);
        mach_wait_until(at + len);
        const uint8_t off[3] = {st_off, note, 64};
        send(src, off, 3);
        if ((i + 1) % 20 == 0) {
            printf("%d of %d sent\n", i + 1, count);
            fflush(stdout);
        }
    }
    fclose(log);
    printf("done: %d notes, the latest send %.0f us after its schedule; log in %s\n", count, worst, logpath);
    MIDIEndpointDispose(src);
    MIDIClientDispose(client);
    CFRelease(cfname);
    return 0;
}

#pragma clang diagnostic pop
