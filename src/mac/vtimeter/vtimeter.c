// vtimeter.c - print the per-channel input peak of the "Virus TI" Core Audio device for a few seconds (diagnostics
// for the macOS plug-in audio path, E102/E103). Runs alongside a DAW (IOProcs are not exclusive).
// Build: clang -O2 -o vtimeter vtimeter.c -framework CoreAudio -framework CoreFoundation
// usage: vtimeter [seconds]
#include <CoreAudio/CoreAudio.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static float g_peak[16];
static unsigned g_ch, g_calls;

static OSStatus proc(AudioObjectID d, const AudioTimeStamp *n, const AudioBufferList *in, const AudioTimeStamp *it,
                     AudioBufferList *out, const AudioTimeStamp *ot, void *u)
{
    (void)d, (void)n, (void)it, (void)out, (void)ot, (void)u;
    g_calls++;
    unsigned ch = 0;
    for (UInt32 b = 0; in && b < in->mNumberBuffers; b++) {
        const AudioBuffer *ab = &in->mBuffers[b];
        UInt32 nc = ab->mNumberChannels, f = nc ? ab->mDataByteSize / (nc * 4) : 0;
        for (UInt32 k = 0; k < nc && ch + k < 16; k++)
            for (UInt32 i = 0; i < f; i++) {
                float x = fabsf(((const float *)ab->mData)[i * nc + k]);
                if (x > g_peak[ch + k])
                    g_peak[ch + k] = x;
            }
        ch += nc;
    }
    g_ch = ch;
    return 0;
}

int main(int argc, char **argv)
{
    int secs = argc > 1 ? atoi(argv[1]) : 5;
    AudioObjectPropertyAddress a = {kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal,
                                    kAudioObjectPropertyElementMain};
    AudioObjectID ids[64], dev = 0;
    UInt32 size = sizeof ids;
    AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, NULL, &size, ids);
    for (UInt32 i = 0; i < size / sizeof *ids && !dev; i++) {
        CFStringRef name = NULL;
        UInt32 n = sizeof name;
        AudioObjectPropertyAddress na = {kAudioObjectPropertyName, kAudioObjectPropertyScopeGlobal,
                                         kAudioObjectPropertyElementMain};
        char buf[128] = "";
        if (!AudioObjectGetPropertyData(ids[i], &na, 0, NULL, &n, &name) && name) {
            CFStringGetCString(name, buf, sizeof buf, kCFStringEncodingUTF8);
            CFRelease(name);
        }
        if (!strcmp(buf, "Virus TI"))
            dev = ids[i];
    }
    if (!dev) {
        fprintf(stderr, "no \"Virus TI\" Core Audio device\n");
        return 1;
    }
    AudioDeviceIOProcID id;
    AudioDeviceCreateIOProcID(dev, proc, NULL, &id);
    AudioDeviceStart(dev, id);
    for (int s = 0; s < secs; s++) {
        memset(g_peak, 0, sizeof g_peak);
        sleep(1);
        printf("%u calls, %u ch, peak dBFS:", g_calls, g_ch);
        for (unsigned c = 0; c < g_ch && c < 16; c++)
            printf(" %6.1f", g_peak[c] > 0 ? 20 * log10f(g_peak[c]) : -999.0f);
        printf("\n");
        fflush(stdout);
    }
    AudioDeviceStop(dev, id);
    AudioDeviceDestroyIOProcID(dev, id);
    return 0;
}
