// autest.mm - test host for the Audio Unit (au/tirevive_au.mm): loads a component bundle directly and calls its factory,
// registers it in this process (AudioComponentRegister), so it works over SSH (auval needs the GUI session's registry). Opens the unit, optionally restores an AU state
// (ClassInfo, e.g. one in the classic TI Revive Audio Unit's format) and saves the current one, plays notes and
// renders, and saves the Cocoa view as PNG. --delete-open N closes the unit the way FL Studio deletes a channel with the
// window open (issue #16, E336): the view outlives the unit, then N seconds of run loop (the broker exits 30 s after its
// last client), and it prints whether the inner plug-in (plugin.vst3) is still loaded. AUTEST_PIN=1 keeps the inner plug-in
// loaded from outside (the 0.8.35 fix, on an older build); AUTEST_POOL=1 prints the pending autorelease pool.
// Build: clang++ -std=c++17 -fobjc-arc -o autest autest.mm -framework Cocoa -framework AudioToolbox -framework AudioUnit
// usage: autest <X.component> [--load state.plist] [--save state.plist] [--png view.png] [--seconds N] [--no-notes]
//               [--delete-open N]
#import <AudioToolbox/AudioToolbox.h>
#import <AudioUnit/AUCocoaUIView.h>
#import <Cocoa/Cocoa.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <mach-o/dyld.h>
extern "C" void _objc_autoreleasePoolPrint(void); // libobjc debug output (stderr): what the pool still holds
#include <string>
#include <vector>


int main(int argc, char **argv)
{
    if (argc < 2)
        return fprintf(stderr, "usage: autest <X.component> [--load f] [--save f] [--png f] [--seconds N] [--no-notes]\n"), 1;
    setvbuf(stdout, nullptr, _IONBF, 0); // every line reaches the log, also when the host crashes
    const char *load = nullptr, *save = nullptr, *png = nullptr;
    int secs = 2, delete_open = -1;
    bool notes = true;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--no-notes")) // e.g. to watch a bank sync run (notes pause it)
            notes = false;
        else if (i + 1 >= argc)
            break;
        else if (!strcmp(argv[i], "--load")) load = argv[++i];
        else if (!strcmp(argv[i], "--save")) save = argv[++i];
        else if (!strcmp(argv[i], "--png")) png = argv[++i];
        else if (!strcmp(argv[i], "--seconds")) secs = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--delete-open")) delete_open = atoi(argv[++i]);
    }
    @autoreleasepool {
        [NSApplication sharedApplication];
        NSBundle *b = [NSBundle bundleWithPath:[NSString stringWithUTF8String:argv[1]]];
        NSDictionary *ac = [[b infoDictionary][@"AudioComponents"] firstObject];
        if (!b || !ac)
            return fprintf(stderr, "no component bundle\n"), 1;
        void *h = dlopen([[b executablePath] UTF8String], RTLD_NOW);
        void *f = h ? dlsym(h, [ac[@"factoryFunction"] UTF8String]) : nullptr;
        if (!f)
            return fprintf(stderr, "factory %s not found (%s)\n", [ac[@"factoryFunction"] UTF8String], dlerror()), 1;
        auto code = [](NSString *s) {
            const char *c = [s UTF8String];
            return (OSType)((c[0] << 24) | (c[1] << 16) | (c[2] << 8) | c[3]);
        };
        AudioComponentDescription d = {code(ac[@"type"]), code(ac[@"subtype"]), code(ac[@"manufacturer"]), 0, 0};
        // registered in this process only (works over SSH, without the GUI session's AU registry)
        AudioComponent comp = AudioComponentRegister(&d, (__bridge CFStringRef)ac[@"name"],
                                                     [ac[@"version"] unsignedIntValue], (AudioComponentFactoryFunction)f);
        AudioUnit au = nullptr;
        OSStatus st = comp ? AudioComponentInstanceNew(comp, &au) : -1;
        printf("open %s -> %d\n", [ac[@"name"] UTF8String], (int)st);
        if (st)
            return 1;
        auto get = [&](AudioUnitPropertyID id, AudioUnitScope sc, AudioUnitElement el, void *data, UInt32 *size) {
            return AudioUnitGetProperty(au, id, sc, el, data, size);
        };
        auto set = [&](AudioUnitPropertyID id, AudioUnitScope sc, AudioUnitElement el, const void *data, UInt32 size) {
            return AudioUnitSetProperty(au, id, sc, el, data, size);
        };
        UInt32 maxf = 512, sz = sizeof maxf;
        set(kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &maxf, sz);
        st = AudioUnitInitialize(au);
        printf("initialize -> %d\n", (int)st);
        if (load) { // an AU state file (plist)
            NSData *data = [NSData dataWithContentsOfFile:[NSString stringWithUTF8String:load]];
            CFPropertyListRef pl = data ? (__bridge_retained CFPropertyListRef)[NSPropertyListSerialization
                                                  propertyListWithData:data options:0 format:nil error:nil]
                                        : nullptr;
            st = pl ? set(kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &pl, sizeof pl) : -1;
            printf("ClassInfo set from %s -> %d\n", load, (int)st);
        }
        // play: C4 on/off per second, render 512-frame blocks in real time
        AudioTimeStamp ts = {};
        ts.mFlags = kAudioTimeStampSampleTimeValid;
        std::vector<float> l(512), r(512);
        float peak = 0;
        int blocks = secs * 44100 / 512;
        for (int i = 0; i < blocks; i++) {
            if (notes && i % 86 == 0)
                MusicDeviceMIDIEvent(au, 0x90, 60, 100, 0);
            if (notes && i % 86 == 43)
                MusicDeviceMIDIEvent(au, 0x80, 60, 0, 0);
            struct { UInt32 n; AudioBuffer b[2]; } abl = {2, {{1, 512 * 4, l.data()}, {1, 512 * 4, r.data()}}};
            AudioUnitRenderActionFlags fl = 0;
            st = AudioUnitRender(au, &fl, &ts, 0, 512, (AudioBufferList *)&abl);
            for (float v : l)
                peak = std::max(peak, std::fabs(v));
            ts.mSampleTime += 512;
            [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:512.0 / 44100]];
        }
        printf("render -> %d, peak %.3f over %d s\n", (int)st, peak, secs);
        if (save) {
            CFPropertyListRef pl = nullptr;
            sz = sizeof pl;
            st = get(kAudioUnitProperty_ClassInfo, kAudioUnitScope_Global, 0, &pl, &sz);
            NSData *out = pl ? [NSPropertyListSerialization dataWithPropertyList:(__bridge id)pl
                                                                          format:NSPropertyListXMLFormat_v1_0
                                                                         options:0 error:nil]
                             : nil;
            printf("ClassInfo get -> %d, saved %s\n", (int)st, [out writeToFile:[NSString stringWithUTF8String:save]
                                                                    atomically:YES] ? save : "FAILED");
        }
        if (png) @autoreleasepool { // the Cocoa view, drawn offscreen; released (pool drained) before the unit closes:
            // its tracking-area helpers otherwise outlived the editor and crashed at the outer pool pop (issue #5)
            UInt32 vsz = 0;
            Boolean w;
            if (AudioUnitGetPropertyInfo(au, kAudioUnitProperty_CocoaUI, kAudioUnitScope_Global, 0, &vsz, &w) == noErr && vsz) {
                std::vector<char> buf(vsz);
                get(kAudioUnitProperty_CocoaUI, kAudioUnitScope_Global, 0, buf.data(), &vsz);
                auto *info = (AudioUnitCocoaViewInfo *)buf.data();
                NSBundle *vb = [NSBundle bundleWithURL:(__bridge NSURL *)info->mCocoaAUViewBundleLocation];
                Class cls = [vb classNamed:(__bridge NSString *)info->mCocoaAUViewClass[0]];
                id<AUCocoaUIBase> factory = [[cls alloc] init];
                NSView *v = [factory uiViewForAudioUnit:au withSize:NSMakeSize(907, 513)];
                for (int k = 0; k < 40; k++)
                    [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
                NSBitmapImageRep *rep = [v bitmapImageRepForCachingDisplayInRect:[v bounds]];
                [v cacheDisplayInRect:[v bounds] toBitmapImageRep:rep];
                NSData *p = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
                printf("view %.0fx%.0f, png %s\n", v.bounds.size.width, v.bounds.size.height,
                       [p writeToFile:[NSString stringWithUTF8String:png] atomically:YES] ? "written" : "FAILED");
                [v removeFromSuperview]; // the AU's view closes its editor here
                v = nil;
                factory = nil;
                for (int k = 0; k < 10; k++) // let deferred editor work finish while the unit still exists
                    [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.02]];
            } else
                printf("no Cocoa view\n");
        }
        if (delete_open >= 0) { // the view is still alive (the host releases it later) while the unit goes
            auto inner_loaded = [] {
                for (uint32_t k = 0; k < _dyld_image_count(); k++)
                    if (strstr(_dyld_get_image_name(k), "plugin.vst3"))
                        return true;
                return false;
            };
            UInt32 vsz = 0;
            Boolean w;
            NSView *v = nil;
            if (AudioUnitGetPropertyInfo(au, kAudioUnitProperty_CocoaUI, kAudioUnitScope_Global, 0, &vsz, &w) == noErr && vsz) {
                std::vector<char> buf(vsz);
                get(kAudioUnitProperty_CocoaUI, kAudioUnitScope_Global, 0, buf.data(), &vsz);
                auto *info = (AudioUnitCocoaViewInfo *)buf.data();
                NSBundle *vb = [NSBundle bundleWithURL:(__bridge NSURL *)info->mCocoaAUViewBundleLocation];
                Class cls = [vb classNamed:(__bridge NSString *)info->mCocoaAUViewClass[0]];
                id<AUCocoaUIBase> factory = [[cls alloc] init];
                v = [factory uiViewForAudioUnit:au withSize:NSMakeSize(907, 513)];
                for (int k = 0; k < 40; k++)
                    [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
            }
            printf("view %s, inner plug-in loaded: %s\n", v ? "open" : "none", inner_loaded() ? "yes" : "no");
            if (getenv("AUTEST_PIN")) // what the plug-in does itself since 0.8.35 (entry.cpp KeepLoaded), from outside
                for (uint32_t k = 0; k < _dyld_image_count(); k++)
                    if (strstr(_dyld_get_image_name(k), "plugin.vst3"))
                        printf("inner plug-in pinned: %s\n",
                               dlopen(_dyld_get_image_name(k), RTLD_LAZY | RTLD_NOLOAD | RTLD_NODELETE) ? "yes" : "no");
            [v removeFromSuperview]; // the window closes, the view itself is not released yet
            AudioUnitUninitialize(au);
            AudioComponentInstanceDispose(au); // the last instance: the wrapper unloads the inner plug-in
            printf("unit disposed, inner plug-in loaded: %s\n", inner_loaded() ? "yes" : "no");
            v = nil; // now the host lets go of the view
            for (int k = 0; k < delete_open * 10; k++)
                [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
            printf("%d s later: no crash, inner plug-in loaded: %s\n", delete_open, inner_loaded() ? "yes" : "no");
            if (getenv("AUTEST_POOL"))
                _objc_autoreleasePoolPrint();
            return 0;
        }
        AudioUnitUninitialize(au);
        AudioComponentInstanceDispose(au);
    }
    return 0;
}
