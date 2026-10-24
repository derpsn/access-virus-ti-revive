// nativehost - minimal VST3 test host for the native plug-in (R3 checks; the hardware runs scripts/hil.sh,
// hil-win.ps1, hil-mac.command). The editor actions (--editor, --click, ...) are Linux only.
//   nativehost <plugin.vst3> [--state FILE] [--param ID=NORM]... [--save FILE] [--seconds N] [--notes] [--wav FILE] [--scan]
// --notes plays C4 (channel 1) for the first half of every second, --wav writes Out 1 (16-bit stereo 44.1 kHz),
// --wav-bus N the output bus N instead (1..3, e.g. 3 = Out 3 = USB 3).
// --offline T: from T s on the blocks are offline (processMode kOffline) and sent as fast as the plug-in takes them,
// like a DAW's export; the wall time of that part is printed (the plug-in paces it to the Virus: ~1x).
// --offline-end T: real time again from T s on; --notes-off A:B: no --notes in [A, B) s (stopped after the render).
// --hold T: C4 on at T s and held to the end; --param-at T:ID=NORM: a parameter change at T s (repeatable) - e.g. to
// hear whether the Virus applies a change to a held note.
// --cid HEX: load that class (e.g. the original Access id D9909FB45F730543AC5397BE8958CD31, E221), else the first.
// --duplicate T: a second instance runs next to the first from the start (a DAW's duplicated track, issue #6) with
// the same notes; at T s the first is removed and the second takes over the Virus. --editor2 with it: the
// duplicate's editor too (its "Virus unavailable" page while it waits); with --editor both editors are open at once,
// as in REAPER at a duplicated track (E183: the second one replaced the process-wide VSTGUI run loop).
// --state loads a state (a raw component state, or vtihost's VTISTATE file: the original's state), checks that
// getState returns it unchanged, --param applies parameter changes through process(), --save writes getState.
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "hostclock.h"
#ifdef __linux__
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>
#include <poll.h>
#endif
#ifdef _WIN32
// sleep_until wakes on the system timer, 15.6 ms by default: a 11.6 ms block period would come in bursts
extern "C" __declspec(dllimport) unsigned int __stdcall timeBeginPeriod(unsigned int ms);
#endif
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

#ifdef __linux__
// Minimal Linux host side of the VST3 editor: IPlugFrame + IRunLoop (fd handlers, timers), pumped from main().
struct RunLoop : Steinberg::Linux::IRunLoop, IPlugFrame {
    struct Fd {
        Steinberg::Linux::IEventHandler *h;
        int fd;
    };
    struct Tm {
        Steinberg::Linux::ITimerHandler *h;
        int ms;
        std::chrono::steady_clock::time_point next;
    };
    std::vector<Fd> fds;
    std::vector<Tm> timers;
    tresult PLUGIN_API queryInterface(const TUID iid, void **obj) override
    {
        if (FUnknownPrivate::iidEqual(iid, Steinberg::Linux::IRunLoop::iid)) {
            *obj = static_cast<Steinberg::Linux::IRunLoop *>(this);
            return kResultOk;
        }
        if (FUnknownPrivate::iidEqual(iid, IPlugFrame::iid) || FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {
            *obj = static_cast<IPlugFrame *>(this);
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
    tresult PLUGIN_API registerEventHandler(Steinberg::Linux::IEventHandler *h, Steinberg::Linux::FileDescriptor fd) override
    {
        fds.push_back({h, fd});
        fprintf(stderr, "nativehost: fd handler %d registered\n", fd);
        return kResultOk;
    }
    // like REAPER: it keeps plain pointers to the handlers and called a freed one when a plug-in unregistered
    // another handler from inside a callback (round 0.8.22: a menu closed from a timer). Such a call aborts here, so
    // the GUI tests catch it; unregistering the handler that is being called is allowed (VSTGUI's own timers do it).
    const void *dispatching = nullptr;
    void check_unregister(const void *h, const char *what)
    {
        if (dispatching && h != dispatching && !getenv("VTI_HOST_LAX")) {
            fprintf(stderr, "nativehost: %s unregistered inside another handler's callback (REAPER crashes)\n", what);
            abort();
        }
    }
    tresult PLUGIN_API unregisterEventHandler(Steinberg::Linux::IEventHandler *h) override
    {
        check_unregister(h, "fd handler");
        fds.erase(std::remove_if(fds.begin(), fds.end(), [&](const Fd &f) { return f.h == h; }), fds.end());
        return kResultOk;
    }
    tresult PLUGIN_API registerTimer(Steinberg::Linux::ITimerHandler *h, Steinberg::Linux::TimerInterval ms) override
    {
        timers.push_back({h, (int)ms, std::chrono::steady_clock::now() + std::chrono::milliseconds(ms)});
        fprintf(stderr, "nativehost: timer %d ms registered\n", (int)ms);
        return kResultOk;
    }
    tresult PLUGIN_API unregisterTimer(Steinberg::Linux::ITimerHandler *h) override
    {
        check_unregister(h, "timer");
        timers.erase(std::remove_if(timers.begin(), timers.end(), [&](const Tm &t) { return t.h == h; }), timers.end());
        return kResultOk;
    }
    // the plug-in asks for a new size (its size menu): resize our window, then confirm with onSize like a DAW
    Display *dpy = nullptr;
    unsigned long win = 0;
    tresult PLUGIN_API resizeView(IPlugView *view, ViewRect *r) override
    {
#ifdef __linux__
        if (dpy && win) {
            XResizeWindow(dpy, (Window)win, (unsigned)r->getWidth(), (unsigned)r->getHeight());
            XFlush(dpy);
        }
#endif
        printf("resizeView %dx%d\n", r->getWidth(), r->getHeight());
        return view->onSize(r);
    }
    void pump()
    {
        for (size_t i = 0; i < fds.size(); i++) {
            pollfd p{fds[i].fd, POLLIN, 0};
            if (poll(&p, 1, 0) > 0) {
                dispatching = fds[i].h;
                fds[i].h->onFDIsSet(fds[i].fd);
                dispatching = nullptr;
            }
        }
        auto now = std::chrono::steady_clock::now();
        for (size_t i = 0; i < timers.size(); i++)
            if (now >= timers[i].next) {
                timers[i].next = now + std::chrono::milliseconds(timers[i].ms);
                dispatching = timers[i].h;
                timers[i].h->onTimer();
                dispatching = nullptr;
            }
    }
};

static void save_window_ppm(Display *d, Window w, const char *path)
{
    XWindowAttributes a;
    XGetWindowAttributes(d, w, &a);
    XImage *img = XGetImage(d, w, 0, 0, a.width, a.height, AllPlanes, ZPixmap);
    if (!img)
        return;
    if (FILE *f = fopen(path, "wb")) {
        fprintf(f, "P6\n%d %d\n255\n", a.width, a.height);
        for (int y = 0; y < a.height; y++)
            for (int x = 0; x < a.width; x++) {
                unsigned long px = XGetPixel(img, x, y);
                unsigned char rgb[3] = {(unsigned char)(px >> 16), (unsigned char)(px >> 8), (unsigned char)px};
                fwrite(rgb, 1, 3, f);
            }
        fclose(f);
    }
    XDestroyImage(img);
}
#endif

#ifdef __linux__
struct LinuxHost : HostApplication {
    RunLoop *loop = nullptr;
    tresult PLUGIN_API queryInterface(const TUID iid, void **obj) override
    {
        if (loop && FUnknownPrivate::iidEqual(iid, Steinberg::Linux::IRunLoop::iid)) {
            fprintf(stderr, "nativehost: run loop requested\n");
            *obj = static_cast<Steinberg::Linux::IRunLoop *>(loop);
            return kResultOk;
        }
        return HostApplication::queryInterface(iid, obj);
    }
};
#endif

static std::vector<uint8_t> read_file(const char *path)
{
    std::vector<uint8_t> d;
    if (FILE *f = fopen(path, "rb")) {
        for (int c; (c = fgetc(f)) != EOF;)
            d.push_back((uint8_t)c);
        fclose(f);
    }
    if (d.size() > 12 && !memcmp(d.data(), "VTISTATE", 8)) { // vtihost: u32 n + component state, ...
        uint32_t n;
        memcpy(&n, d.data() + 8, 4);
        d = std::vector<uint8_t>(d.begin() + 12, d.begin() + 12 + std::min<size_t>(n, d.size() - 12));
    }
    return d;
}

static std::vector<uint8_t> get_state(IComponent *c)
{
    auto *s = new MemoryStream;
    std::vector<uint8_t> d;
    if (c->getState(s) == kResultOk) {
        int64 size = 0;
        s->seek(0, IBStream::kIBSeekEnd, &size);
        d.resize((size_t)size);
        s->seek(0, IBStream::kIBSeekSet, nullptr);
        int32 got = 0;
        s->read(d.data(), (int32)d.size(), &got);
    }
    s->release();
    return d;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: nativehost <plugin.vst3> [--state FILE] [--param ID=NORM]... [--save FILE] [--seconds N]\n");
        return 2;
    }
#ifdef _WIN32
    timeBeginPeriod(1);
#endif
    const char *state = nullptr, *save = nullptr, *wav = nullptr;
    std::string cid; // --cid: the class to load, else the first audio class
    int wav_bus = 0; // the output bus that is measured and written (0 = Out 1)
    bool notes = false, editor = false, editor2 = false, stress = false, stress_notes = true, stress_params = true, scan = false;
    double stress_stop = 1e9;
    double transport_at = -1, loop_bars = 0; // --transport / --loop: a playing DAW transport for process()
    double duplicate = 0; // --duplicate T: remove the first of two instances after T s
    double sc_sine = 0; // --sidechain-sine HZ: a sine (-6 dBFS) on the Sidechain bus (input bus 1)
    std::vector<std::pair<double, double>> aheads; // (at s, ms)
    int block = 512; // --block N: host block size
    RoughCadence rough; // --rough T1:T2
    int rate = 44100; // --rate N: host sample rate
    std::vector<RateAt> rate_ats; // --rate-at R@T: the host rate R from T s on (a DAW whose project rate is switched)
    struct Action {
        double t;
        char kind; // c = click, d = drag, s = screenshot
        int x, y, dy;
        std::string file;
        bool done = false;
    };
    std::vector<Action> actions;
    double seconds = 1;
    double offline_at = -1; // --offline T
    double offline_end = -1; // --offline-end T: back to real time (a DAW's render ends, playback follows)
    std::vector<std::pair<ParamID, double>> changes;
    double hold = -1; // --hold: note on at this time, never off
    double notes_off_from = -1, notes_off_to = -1; // --notes-off A:B: no --notes in [A, B) s (a DAW stopped after a render)
    struct TimedChange {
        double t;
        ParamID id;
        double v;
        bool done;
    };
    std::vector<TimedChange> timed;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--state") && i + 1 < argc)
            state = argv[++i];
        else if (!strcmp(argv[i], "--save") && i + 1 < argc)
            save = argv[++i];
        else if (!strcmp(argv[i], "--wav") && i + 1 < argc)
            wav = argv[++i];
        else if (!strcmp(argv[i], "--wav-bus") && i + 1 < argc)
            wav_bus = std::max(0, atoi(argv[++i]) - 1);
        else if (!strcmp(argv[i], "--transport") && i + 1 < argc) // play at 120 BPM from T s on (process context)
            transport_at = atof(argv[++i]);
        else if (!strcmp(argv[i], "--loop") && i + 1 < argc) // with --transport: jump back to bar 1 every N bars
            loop_bars = atof(argv[++i]);
        else if (!strcmp(argv[i], "--notes"))
            notes = true;
        else if (!strcmp(argv[i], "--notes-off") && i + 1 < argc)
            sscanf(argv[++i], "%lf:%lf", &notes_off_from, &notes_off_to);
        else if (!strcmp(argv[i], "--hold") && i + 1 < argc)
            hold = atof(argv[++i]);
        else if (!strcmp(argv[i], "--param-at") && i + 1 < argc) {
            double t = 0, v = 0;
            unsigned id = 0;
            if (sscanf(argv[++i], "%lf:%u=%lf", &t, &id, &v) == 3)
                timed.push_back({t, (ParamID)id, v, false});
        }
        else if (!strcmp(argv[i], "--stress")) // MIDI load: notes on 16 channels + 32 automated parameters per block
            stress = true;
        else if (!strcmp(argv[i], "--stress-notes")) // only the notes of --stress
            stress = true, stress_params = false;
        else if (!strcmp(argv[i], "--stress-params")) // only the automation of --stress
            stress = true, stress_notes = false;
        else if (!strcmp(argv[i], "--ahead") && i + 1 < argc) { // --ahead MS@T: from T s on process MS ms ahead of
            // real time in a burst, like REAPER's anticipative FX processing after a playback start
            double ms = 0, t = 0;
            sscanf(argv[++i], "%lf@%lf", &ms, &t);
            aheads.push_back({t, ms});
        } else if (!strcmp(argv[i], "--stress-stop") && i + 1 < argc) // the automation of --stress ends after N s
            stress_stop = atof(argv[++i]);
        else if (!strcmp(argv[i], "--rough") && i + 1 < argc) { // --rough T1:T2: uneven block cadence (E337)
            if (!parse_rough(argv[++i], rough)) {
                fprintf(stderr, "--rough wants T1:T2 seconds, T1 < T2\n");
                return 2;
            }
        } else if (!strcmp(argv[i], "--block") && i + 1 < argc)
            block = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--rate") && i + 1 < argc) // host sample rate (the plug-in asks the Virus for 44.1 or 48 kHz)
            rate = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--rate-at") && i + 1 < argc) {
            RateAt r{};
            if (!parse_rate_at(argv[++i], r)) {
                fprintf(stderr, "nativehost: --rate-at wants RATE@SECONDS, e.g. 48000@10\n");
                return 2;
            }
            rate_ats.push_back(r);
        }
        else if (!strcmp(argv[i], "--editor"))
            editor = true;
        else if (!strcmp(argv[i], "--editor2"))
            editor2 = true;
        else if (!strcmp(argv[i], "--scan")) // what a DAW's plug-in scan does: initialize, query, terminate
            scan = true;
        else if ((!strcmp(argv[i], "--click") || !strcmp(argv[i], "--drag") || !strcmp(argv[i], "--shot") ||
                  !strcmp(argv[i], "--twice") || !strcmp(argv[i], "--type") || !strcmp(argv[i], "--move") ||
                  !strcmp(argv[i], "--rclick")) &&
                 i + 1 < argc) {
            // --click x,y@t  --rclick x,y@t (right button)  --drag x,y,dy@t  --shot file.ppm@t
            Action a{};
            a.kind = !strcmp(argv[i], "--type") ? 'y' : !strcmp(argv[i], "--move") ? 'm' : argv[i][2];
            std::string v = argv[++i];
            size_t at = v.rfind('@');
            a.t = at == std::string::npos ? 1 : atof(v.c_str() + at + 1);
            v = v.substr(0, at);
            if (a.kind == 's' || a.kind == 'y') // --shot file, --type text
                a.file = v;
            else
                sscanf(v.c_str(), "%d,%d,%d", &a.x, &a.y, &a.dy);
            actions.push_back(a);
        }
        else if (!strcmp(argv[i], "--reopen") && i + 1 < argc) { // --reopen T: close the editor at T s, open a new
            Action a{};                                           // one in the same window (a DAW's FX window)
            a.kind = 'o', a.t = atof(argv[++i]);
            actions.push_back(a);
        } else if (!strcmp(argv[i], "--duplicate") && i + 1 < argc)
            duplicate = atof(argv[++i]);
        else if (!strcmp(argv[i], "--sidechain-sine") && i + 1 < argc)
            sc_sine = atof(argv[++i]);
        else if (!strcmp(argv[i], "--offline") && i + 1 < argc)
            offline_at = atof(argv[++i]);
        else if (!strcmp(argv[i], "--offline-end") && i + 1 < argc)
            offline_end = atof(argv[++i]);
        else if (!strcmp(argv[i], "--cid") && i + 1 < argc) // the class to load (32 hex digits as moduleinfo prints them)
            cid = argv[++i];
        else if (!strcmp(argv[i], "--seconds") && i + 1 < argc)
            seconds = atof(argv[++i]);
        else if (!strcmp(argv[i], "--param") && i + 1 < argc) {
            const char *a = argv[++i], *eq = strchr(a, '=');
            if (eq)
                changes.push_back({(ParamID)strtoul(a, nullptr, 0), atof(eq + 1)});
        }
    }
    std::string err;
    auto module = VST3::Hosting::Module::create(argv[1], err);
    if (!module) {
        fprintf(stderr, "nativehost: %s\n", err.c_str());
        return 1;
    }
#ifdef __linux__
    static RunLoop runloop;
    auto *host = new LinuxHost;
    host->loop = &runloop;
#else
    auto *host = new HostApplication;
#endif
    auto factory = module->getFactory();
    factory.setHostContext(host);
    auto infos = factory.classInfos();
    VST3::Hosting::ClassInfo audio;
    bool found = false;
    for (auto &ci : infos) {
        printf("class %s \"%s\" %s\n", ci.ID().toString().c_str(), ci.name().c_str(), ci.category().c_str());
        // the first audio class (the plug-in's own id), or the one --cid names (e.g. the original's id, E221)
        if (ci.category() == kVstAudioEffectClass && !found && (cid.empty() || ci.ID().toString() == cid)) {
            audio = ci;
            found = true;
        }
    }
    if (!found) {
        fprintf(stderr, "nativehost: no audio class%s%s\n", cid.empty() ? "" : " with id ", cid.c_str());
        return 1;
    }
    printf("using class %s\n", audio.ID().toString().c_str());
    auto comp = factory.createInstance<IComponent>(audio.ID());
    if (!comp) {
        fprintf(stderr, "nativehost: createInstance failed\n");
        return 1;
    }
    printf("initialize -> %d\n", (int)comp->initialize(host));
    FUnknownPtr<IAudioProcessor> proc(comp);
    FUnknownPtr<IEditController> ctl(comp);
    if (ctl)
        printf("parameters: %d\n", (int)ctl->getParameterCount());
    if (scan) { // no activation: the plug-in must not have touched the Virus (broker log: no client)
        std::this_thread::sleep_for(std::chrono::seconds(2));
        printf("terminate -> %d\n", (int)comp->terminate());
        comp->release();
        return 0;
    }

    if (state) {
        auto in = read_file(state);
        auto *s = new MemoryStream(in.data(), (TSize)in.size());
        printf("setState(%zu bytes) -> %d\n", in.size(), (int)comp->setState(s));
        s->release();
        auto out = get_state(comp);
        printf("getState: %zu bytes, %s\n", out.size(), out == in ? "IDENTICAL to the loaded state" : "differs");
    }

#ifdef __linux__
    // XEmbed: the embedder maps the plug-in's window (real hosts do this)
    auto map_children = [](Display *d, Window w) {
        Window root_ret, parent_ret, *kids = nullptr;
        unsigned nkids = 0;
        if (XQueryTree(d, w, &root_ret, &parent_ret, &kids, &nkids)) {
            for (unsigned k = 0; k < nkids; k++)
                XMapWindow(d, kids[k]);
            if (kids)
                XFree(kids);
        }
        XFlush(d);
    };
    Display *dpy = nullptr;
    Window win = 0, win2 = 0;
    IPlugView *view = nullptr, *view2 = nullptr; // view2: the duplicate's, when both editors are open
    auto open_editor = [&](IEditController *ec, IPlugView *&view, Window &win) -> bool {
        if (!dpy)
            dpy = XOpenDisplay(nullptr);
        view = ec->createView(ViewType::kEditor);
        if (!dpy || !view) {
            fprintf(stderr, "nativehost: no display or no editor\n");
            return false;
        }
        ViewRect r{};
        view->getSize(&r);
        win = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy), 0, 0, r.getWidth(), r.getHeight(), 0, 0, 0);
        XMapWindow(dpy, win);
        XFlush(dpy);
        runloop.dpy = dpy;
        runloop.win = win;
        view->setFrame(&runloop);
        printf("editor %dx%d attached -> %d\n", r.getWidth(), r.getHeight(),
               (int)view->attached((void *)(uintptr_t)win, kPlatformTypeX11EmbedWindowID));
        map_children(dpy, win);
        return true;
    };
    if (editor && ctl && !open_editor(ctl, view, win))
        return 1;
#endif
    ProcessSetup ps{kRealtime, kSample32, block, static_cast<SampleRate>(rate)}; // explicit: newer clang refuses narrowing
    proc->setupProcessing(ps);
    comp->setActive(true);
    proc->setProcessing(true);
    HostProcessData data;
    data.prepare(*comp, block, kSample32);
    data.numSamples = block;
    // --duplicate: the second instance, activated after the first like a DAW adding a track
    IPtr<IComponent> comp2;
    FUnknownPtr<IAudioProcessor> proc2;
    HostProcessData data2;
    ParameterChanges pc2, outpc2;
    if (duplicate > 0) {
        comp2 = factory.createInstance<IComponent>(audio.ID());
        if (!comp2) {
            fprintf(stderr, "nativehost: second createInstance failed\n");
            return 1;
        }
        printf("duplicate: initialize -> %d\n", (int)comp2->initialize(host));
        proc2 = FUnknownPtr<IAudioProcessor>(comp2);
        proc2->setupProcessing(ps);
        comp2->setActive(true);
        proc2->setProcessing(true);
        data2.prepare(*comp2, block, kSample32);
        data2.numSamples = block;
        // a DAW's duplicate gets the original's state after its activation (Ableton: a second state upload, E149)
        auto st = get_state(comp);
        auto *ms = new MemoryStream(st.data(), (TSize)st.size());
        printf("duplicate: setState(%zu bytes) -> %d\n", st.size(), (int)comp2->setState(ms));
        ms->release();
#ifdef __linux__
        if (editor2) {
            FUnknownPtr<IEditController> ctl2(comp2);
            if (!ctl2 || !(view ? open_editor(ctl2, view2, win2) : open_editor(ctl2, view, win)))
                return 1;
            if (!view2)
                ctl = ctl2; // a --reopen opens the duplicate's editor again
        }
#endif
    }
    HostProcessData *outd = &data; // the instance whose output is measured / written
    ParameterChanges pc, outpc;
    EventList ev;
    std::vector<int16_t> pcm;
    float peak = 0;
    HostClock clk{rate, block};
    auto running = [&](int b) { return clk.seconds(b + 1) <= seconds + 1e-9; }; // block b ends within --seconds
    int wav_segment = 1; // --rate-at: one WAV per rate (WAV.wav, then WAV.2.wav, ...), each graded on its own
    auto write_wav = [&](int at_rate) {
        if (!wav)
            return;
        std::string path = wav;
        if (wav_segment > 1) {
            size_t dot = path.rfind(".wav");
            path = (dot == std::string::npos ? path : path.substr(0, dot)) + "." + std::to_string(wav_segment) + ".wav";
        }
        if (FILE *f = fopen(path.c_str(), "wb")) {
            uint32_t bytes = (uint32_t)(pcm.size() * 2), r = (uint32_t)at_rate, brate = r * 4, sz = 36 + bytes, fmt = 16;
            uint16_t pcmfmt = 1, ch = 2, align = 4, bits = 16;
            fwrite("RIFF", 1, 4, f), fwrite(&sz, 4, 1, f), fwrite("WAVEfmt ", 1, 8, f), fwrite(&fmt, 4, 1, f);
            fwrite(&pcmfmt, 2, 1, f), fwrite(&ch, 2, 1, f), fwrite(&r, 4, 1, f), fwrite(&brate, 4, 1, f);
            fwrite(&align, 2, 1, f), fwrite(&bits, 2, 1, f), fwrite("data", 1, 4, f), fwrite(&bytes, 4, 1, f);
            fwrite(pcm.data(), 2, pcm.size(), f);
            fclose(f);
            printf("wrote %s\n", path.c_str());
        }
    };
    auto t_next = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point t_offline{}, t_offline_end{};
    for (int b = 0; running(b); b++) {
        for (auto &ra : rate_ats)
            if (!ra.done && proc && clk.seconds(b) >= ra.t) {
                // what a DAW does when the project rate changes while it plays: stop, deactivate, set up, activate
                ra.done = true;
                printf("rate-at %.2f s: %d -> %d Hz\n", clk.seconds(b), clk.rate, ra.rate);
                fflush(stdout);
                auto t0 = std::chrono::steady_clock::now();
                proc->setProcessing(false);
                comp->setActive(false);
                ProcessSetup ps2{kRealtime, kSample32, block, static_cast<SampleRate>(ra.rate)};
                proc->setupProcessing(ps2);
                comp->setActive(true);
                proc->setProcessing(true);
                printf("rate-at: reactivated in %.0f ms\n",
                       std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
                write_wav(clk.rate);
                pcm.clear();
                wav_segment++;
                clk.set_rate(b, ra.rate);
                t_next = std::chrono::steady_clock::now();
            }
        const bool offline = offline_at >= 0 && clk.seconds(b) >= offline_at &&
                             (offline_end < 0 || clk.seconds(b) < offline_end);
        if (offline && t_offline == std::chrono::steady_clock::time_point{})
            t_offline = std::chrono::steady_clock::now();
        if (!offline && t_offline != std::chrono::steady_clock::time_point{} &&
            t_offline_end == std::chrono::steady_clock::time_point{})
        {
            // --offline-end: the render is over. The first real-time block comes one block period after the last
            // offline one, like a device callback; called at once it ran a block ahead and its first note was
            // ~12 ms late (E286)
            std::this_thread::sleep_for(std::chrono::microseconds((long long)block * 1000000 / clk.rate));
            t_offline_end = t_next = std::chrono::steady_clock::now();
        }
        data.processMode = offline ? kOffline : kRealtime;
        static ProcessContext pctx{};
        if (transport_at >= 0) { // a DAW transport: 120 BPM from transport_at on, looping every loop_bars bars
            double t = clk.seconds(b);
            pctx.sampleRate = clk.rate;
            pctx.tempo = 120.0;
            pctx.state = ProcessContext::kTempoValid | ProcessContext::kProjectTimeMusicValid;
            double qn = t >= transport_at ? (t - transport_at) * 2.0 : 0.0;
            if (loop_bars > 0)
                qn = std::fmod(qn, loop_bars * 4.0);
            pctx.projectTimeMusic = qn;
            if (t >= transport_at)
                pctx.state |= ProcessContext::kPlaying;
            data.processContext = &pctx;
        }
        ev.clear();
        if (notes) { // C4 on at the start of every second, off at the half
            const long rate = clk.rate, f0 = clk.frame(b), f1 = f0 + block;
            for (long t = (f0 / (rate / 2)) * (rate / 2); t < f1; t += (rate / 2))
                if (t >= f0 && !(t >= notes_off_from * rate && t < notes_off_to * rate)) {
                    Event e{};
                    e.sampleOffset = (int32)(t - f0);
                    if ((t / (rate / 2)) % 2 == 0) {
                        e.type = Event::kNoteOnEvent;
                        e.noteOn = {0, 60, 0, 0.8f, 0, -1};
                    } else {
                        e.type = Event::kNoteOffEvent;
                        e.noteOff = {0, 60, 0.f, -1, 0};
                    }
                    ev.addEvent(e);
                }
        }
        if (hold >= 0) { // one C4 from `hold` on, released only at the end
            long f0 = clk.frame(b), on = (long)(hold * clk.rate);
            if (on >= f0 && on < f0 + block) {
                Event e{};
                e.sampleOffset = (int32)(on - f0);
                e.type = Event::kNoteOnEvent;
                e.noteOn = {0, 60, 0, 0.8f, 0, -1};
                ev.addEvent(e);
                printf("hold: C4 on at %.2f s\n", hold);
            }
            if (!running(b + 1)) {
                Event e{};
                e.type = Event::kNoteOffEvent;
                e.noteOff = {0, 60, 0.f, -1, 0};
                ev.addEvent(e);
            }
        }
        if (stress && stress_notes) { // every block: a note on/off pair on each of the 16 channels (cycling pitches)
            for (int ch = 0; ch < 16; ch++) {
                Event e{};
                e.sampleOffset = ch * 30;
                e.type = Event::kNoteOffEvent;
                e.noteOff = {(int16)ch, (int16)(48 + (b + ch) % 24), 0.f, -1, 0};
                ev.addEvent(e);
                e.sampleOffset = ch * 30 + 5;
                e.type = Event::kNoteOnEvent;
                e.noteOn = {(int16)ch, (int16)(48 + (b + ch + 1) % 24), 0, 0.7f, 0, -1};
                ev.addEvent(e);
            }
        }
        data.inputEvents = &ev;
        pc.clearQueue();
        if (stress && stress_params && ctl && clk.seconds(b) < stress_stop) { // 32 automatable parameters moving every block
            static std::vector<ParamID> ids;
            if (ids.empty())
                for (int32 k = 0; k < ctl->getParameterCount() && ids.size() < 32; k++) {
                    ParameterInfo pi{};
                    if (ctl->getParameterInfo(k, pi) == kResultOk && (pi.flags & ParameterInfo::kCanAutomate))
                        ids.push_back(pi.id);
                }
            for (size_t k = 0; k < ids.size(); k++) {
                int32 idx;
                if (auto *q = pc.addParameterData(ids[k], idx))
                    q->addPoint((int32)(k * 15), 0.5 + 0.4 * std::sin((b + k * 7) * 0.05), idx);
            }
        }
        for (auto &c : timed)
            if (!c.done && clk.seconds(b + 1) > c.t) {
                c.done = true;
                int32 idx;
                if (auto *q = pc.addParameterData(c.id, idx)) {
                    q->addPoint(0, c.v, idx);
                    printf("param-at %.2f s: %u = %.4f\n", c.t, (unsigned)c.id, c.v);
                }
            }
        if (b == 1)
            for (auto &c : changes) {
                int32 idx;
                if (auto *q = pc.addParameterData(c.first, idx)) {
                    q->addPoint(0, c.second, idx);
                    printf("param %u = %.4f\n", (unsigned)c.first, c.second);
                }
            }
        data.inputParameterChanges = &pc;
        outpc.clearQueue();
        data.outputParameterChanges = &outpc;
        if (sc_sine > 0 && data.numInputs >= 2) // the same tone on both channels, continuous across blocks
            for (int32 c = 0; c < data.inputs[1].numChannels && c < 2; c++)
                if (float *s = data.inputs[1].channelBuffers32 ? data.inputs[1].channelBuffers32[c] : nullptr)
                    for (int i = 0; i < block; i++)
                        s[i] = 0.5f * (float)std::sin(2 * 3.14159265358979323846 * sc_sine * ((double)clk.frame(b) + i) / clk.rate);
        if (proc)
            proc->process(data);
        if (proc2) { // the duplicate plays the same notes (same clip on the duplicated track)
            data2.inputEvents = &ev;
            pc2.clearQueue();
            data2.inputParameterChanges = &pc2;
            outpc2.clearQueue();
            data2.outputParameterChanges = &outpc2;
            proc2->process(data2);
        }
        if (comp2 && proc && clk.seconds(b) >= duplicate) {
            // the user deletes the original track: the duplicate takes over the Virus
            printf("duplicate: removing the first instance at %.1f s\n", clk.seconds(b));
            fflush(stdout);
            proc->setProcessing(false);
            comp->setActive(false);
#ifdef __linux__
            if (view2) { // the first instance's editor goes with it; the duplicate's stays
                view->removed();
                view->release();
                XDestroyWindow(dpy, win);
                view = view2, win = win2, view2 = nullptr;
                runloop.win = win;
                FUnknownPtr<IEditController> ctl2(comp2);
                ctl = ctl2;
            }
#endif
            comp->terminate();
            proc = nullptr;
            outd = &data2;
        }
        for (int32 i = 0; i < outpc.getParameterCount(); i++) { // device -> host (knobs on the Virus)
            IParamValueQueue *q = outpc.getParameterData(i);
            int32 off;
            ParamValue v;
            if (q && q->getPoint(q->getPointCount() - 1, off, v) == kResultOk) {
                ParameterInfo pi{};
                if (ctl)
                    for (int32 k = 0; k < ctl->getParameterCount(); k++)
                        if (ctl->getParameterInfo(k, pi) == kResultOk && pi.id == q->getParameterId())
                            break;
                char t[128] = "";
                for (int k = 0; k < 127 && pi.title[k]; k++)
                    t[k] = (char)pi.title[k];
                printf("from the Virus: %u %s = %.3f\n", (unsigned)q->getParameterId(), t, v);
            }
        }
        if (outd->numOutputs > wav_bus && outd->outputs[wav_bus].numChannels >= 2) {
            float *l = outd->outputs[wav_bus].channelBuffers32[0], *r = outd->outputs[wav_bus].channelBuffers32[1];
            for (int i = 0; i < block; i++) {
                if (!std::isfinite(l[i]) || !std::isfinite(r[i])) {
                    static int bad;
                    if (bad++ < 3)
                        fprintf(stderr, "non-finite sample at block %d frame %d: %f %f\n", b, i, l[i], r[i]);
                }
                peak = std::max(peak, std::max(std::fabs(l[i]), std::fabs(r[i])));
                pcm.push_back((int16_t)std::max(-32768.f, std::min(32767.f, l[i] * 32768)));
                pcm.push_back((int16_t)std::max(-32768.f, std::min(32767.f, r[i] * 32768)));
            }
        }
        if ((long)clk.seconds(b + 1) != (long)clk.seconds(b)) { // once per second
            printf("t=%.0f s peak %.3f\n", clk.seconds(b + 1), peak);
            fflush(stdout);
            peak = 0;
        }
        t_next += std::chrono::microseconds((long long)block * 1000000 / clk.rate); // real time at the host rate
        for (auto &ah : aheads)
            if (ah.second > 0 && clk.seconds(b) >= ah.first) {
                t_next -= std::chrono::microseconds((long long)(ah.second * 1000));
                printf("ahead %.0f ms at %.1f s\n", ah.second, ah.first);
                ah.second = 0;
            }
#ifdef __linux__
        if (view) {
            double now = clk.seconds(b + 1);
            for (auto &a : actions)
                if (!a.done && now >= a.t) {
                    a.done = true;
                    if (a.kind == 'o') {
                        view->removed();
                        view->release();
                        view = ctl->createView(ViewType::kEditor);
                        if (!view) {
                            fprintf(stderr, "nativehost: no editor on reopen\n");
                            break;
                        }
                        view->setFrame(&runloop);
                        printf("editor reopened at %.1f s -> %d\n", now,
                               (int)view->attached((void *)(uintptr_t)win, kPlatformTypeX11EmbedWindowID));
                        map_children(dpy, win);
                        continue;
                    }
                    if (a.kind == 's') {
                        save_window_ppm(dpy, win, a.file.c_str());
                        printf("screenshot %s at %.1f s\n", a.file.c_str(), now);
                        continue;
                    }
                    if (a.kind == 'y') { // type text + Return through XTest
                        { // keyboard focus to the (VSTGUI child) window under the pointer - no window manager here
                            Window root, child = win, w = win;
                            int rx, ry, wx, wy;
                            unsigned mask;
                            while (XQueryPointer(dpy, w, &root, &child, &rx, &ry, &wx, &wy, &mask) && child)
                                w = child;
                            XSetInputFocus(dpy, w, RevertToPointerRoot, CurrentTime);
                        }
                        auto key = [&](KeySym ks, bool shift) {
                            KeyCode kc = XKeysymToKeycode(dpy, ks), sh = XKeysymToKeycode(dpy, XK_Shift_L);
                            if (shift)
                                XTestFakeKeyEvent(dpy, sh, True, 0);
                            XTestFakeKeyEvent(dpy, kc, True, 0);
                            XTestFakeKeyEvent(dpy, kc, False, 0);
                            if (shift)
                                XTestFakeKeyEvent(dpy, sh, False, 0);
                            XFlush(dpy);
                            for (int k = 0; k < 3; k++) {
                                runloop.pump();
                                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                            }
                        };
                        for (char ch : a.file) {
                            char nm[2] = {ch, 0};
                            KeySym ks = ch == ' ' ? XK_space : XStringToKeysym(nm);
                            key(ks, ch >= 'A' && ch <= 'Z');
                        }
                        key(XK_Return, false);
                        printf("typed \"%s\" at %.1f s\n", a.file.c_str(), now);
                        continue;
                    }
                    int wx, wy;
                    Window child;
                    XTranslateCoordinates(dpy, win, DefaultRootWindow(dpy), a.x, a.y, &wx, &wy, &child);
                    XTestFakeMotionEvent(dpy, -1, wx, wy, 0);
                    if (a.kind == 'm') { // hover only
                        XFlush(dpy);
                        printf("move %d,%d at %.1f s\n", a.x, a.y, now);
                        continue;
                    }
                    const unsigned button = a.kind == 'r' ? 3 : 1;
                    XTestFakeButtonEvent(dpy, button, True, 0);
                    XFlush(dpy);
                    for (int k = 1; a.kind == 'd' && k <= 10; k++) { // drag in 10 steps
                        runloop.pump();
                        std::this_thread::sleep_for(std::chrono::milliseconds(15));
                        XTestFakeMotionEvent(dpy, -1, wx, wy - a.dy * k / 10, 0);
                        XFlush(dpy);
                    }
                    runloop.pump();
                    XTestFakeButtonEvent(dpy, button, False, 0);
                    XFlush(dpy);
                    if (a.kind == 't') { // double click
                        runloop.pump();
                        std::this_thread::sleep_for(std::chrono::milliseconds(60));
                        XTestFakeButtonEvent(dpy, 1, True, 0);
                        XFlush(dpy);
                        runloop.pump();
                        XTestFakeButtonEvent(dpy, 1, False, 0);
                        XFlush(dpy);
                    }
                    printf("%s %d,%d at %.1f s\n", a.kind == 'c' ? "click" : a.kind == 'r' ? "right click" : a.kind == 't' ? "double click" : "drag", a.x, a.y, now);
                }
            runloop.pump();
        }
#endif
        if (offline)
            t_next = std::chrono::steady_clock::now(); // as fast as the plug-in returns
        else
            std::this_thread::sleep_until(t_next + std::chrono::microseconds(rough.shift_us(clk, b + 1)));
    }
    if (offline_at >= 0 && t_offline != std::chrono::steady_clock::time_point{}) {
        bool ended = t_offline_end != std::chrono::steady_clock::time_point{};
        double wall = std::chrono::duration<double>((ended ? t_offline_end : std::chrono::steady_clock::now()) - t_offline).count();
        double audio = (ended ? offline_end : seconds) - offline_at;
        printf("offline part: %.2f s of audio in %.2f s wall time (%.2fx)\n", audio, wall, wall > 0 ? audio / wall : 0.0);
    }
    write_wav(clk.rate);
    if (comp2 && !proc) { // the first instance is gone: the duplicate is the one to finish
        comp = comp2;
        proc = proc2;
        proc2 = nullptr;
    }
    if (proc2) { // --duplicate longer than --seconds: both still run
        proc2->setProcessing(false);
        comp2->setActive(false);
        comp2->terminate();
    }
    proc->setProcessing(false);
    comp->setActive(false);
#ifdef __linux__
    if (view) {
        view->removed();
        view->release();
        XDestroyWindow(dpy, win);
        XCloseDisplay(dpy);
    }
#endif
    if (save) {
        auto out = get_state(comp);
        if (FILE *f = fopen(save, "wb")) {
            fwrite(out.data(), 1, out.size(), f);
            fclose(f);
            printf("saved %zu bytes to %s\n", out.size(), save);
        }
    }
    comp->terminate();
    return 0;
}
