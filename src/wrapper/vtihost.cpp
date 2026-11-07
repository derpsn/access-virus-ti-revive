// vtihost.cpp - minimal VST3 test host for Windows / Wine (Linux port, findings/design-linux.md): loads a VST3
// (the TI Revive wrapper), activates it at 44.1 kHz / 512, runs process() in real time on its own thread while the
// main thread pumps Windows messages (the Access plug-in's timers send its MIDI and poll the device), plays a C4
// for half of every second after <quiet> seconds, prints the output peak per second and writes output bus 1 to a WAV.
//
// usage: vtihost <plugin.vst3> [seconds=60] [out.wav] [quiet=0]
// env: VTI_BLOCK=64..512 (block size), VTI_EDITOR=<file.bmp> (open the editor, screenshot it every 10 s),
//      VTI_LISTPARAMS=1 (print all parameters of the controller and exit),
//      VTI_PARAMS="id=normalized@seconds,..." (scripted parameter changes through process(), e.g. automation tests)
//      VTI_SAVESTATE=<file> (component + controller state at the end), VTI_LOADSTATE=<file> (restore before activation;
//      file: "VTISTATE" u32 n + component state, u32 n + controller state)
//      VTI_LIVE=alt|all (the C4 of every odd second, or all of them, flagged Event::kIsLive: played live, E400),
//      VTI_STOPPED=1 (transport stopped, the C4 still plays and is recorded: playing in with the sequencer stopped)
//      VTI_PLAY_AT=<s> (stopped until s, then Play from 0, the C4 going on all the time: what a DAW does at Play)
// build: make -C src/wrapper (x86_64-w64-mingw32-g++, VST3 pluginterfaces from work/vst3sdk, MIT)
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/gui/iplugview.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace Steinberg {
DEF_CLASS_IID(FUnknown)
DEF_CLASS_IID(IPluginFactory)
DEF_CLASS_IID(IPlugView)
DEF_CLASS_IID(IPlugFrame)
DEF_CLASS_IID(IBStream)
namespace Vst {
DEF_CLASS_IID(IComponent)
DEF_CLASS_IID(IAudioProcessor)
DEF_CLASS_IID(IEditController)
DEF_CLASS_IID(IComponentHandler)
DEF_CLASS_IID(IHostApplication)
DEF_CLASS_IID(IEventList)
DEF_CLASS_IID(IParameterChanges)
DEF_CLASS_IID(IParamValueQueue)
} // namespace Vst
} // namespace Steinberg

#define IMPLEMENT_FUNKNOWN(I)                                                                                          \
    std::atomic<uint32> refs{1};                                                                                       \
    uint32 PLUGIN_API addRef() override { return ++refs; }                                                             \
    uint32 PLUGIN_API release() override { return --refs; }                                                            \
    tresult PLUGIN_API queryInterface(const TUID iid, void **obj) override                                             \
    {                                                                                                                  \
        if (FUnknownPrivate::iidEqual(iid, I::iid) || FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {                 \
            addRef();                                                                                                  \
            *obj = this;                                                                                               \
            return kResultOk;                                                                                          \
        }                                                                                                              \
        *obj = nullptr;                                                                                                \
        return kNoInterface;                                                                                           \
    }

struct Host : IHostApplication {
    IMPLEMENT_FUNKNOWN(IHostApplication)
    tresult PLUGIN_API getName(String128 name) override
    {
        const char *n = "TI Revive test host";
        int i = 0;
        for (; n[i]; i++)
            name[i] = (TChar)n[i];
        name[i] = 0;
        return kResultOk;
    }
    tresult PLUGIN_API createInstance(TUID, TUID, void **obj) override
    {
        *obj = nullptr;
        return kNotImplemented;
    }
} g_host;

struct Handler : IComponentHandler {
    IMPLEMENT_FUNKNOWN(IComponentHandler)
    tresult PLUGIN_API beginEdit(ParamID) override { return kResultOk; }
    tresult PLUGIN_API performEdit(ParamID, ParamValue) override { return kResultOk; }
    tresult PLUGIN_API endEdit(ParamID) override { return kResultOk; }
    tresult PLUGIN_API restartComponent(int32 f) override
    {
        printf("restartComponent 0x%x\n", (int)f);
        return kResultOk;
    }
} g_handler;

struct EventList : IEventList {
    IMPLEMENT_FUNKNOWN(IEventList)
    std::vector<Event> ev;
    int32 PLUGIN_API getEventCount() override { return (int32)ev.size(); }
    tresult PLUGIN_API getEvent(int32 i, Event &e) override
    {
        if (i < 0 || i >= (int32)ev.size())
            return kInvalidArgument;
        e = ev[i];
        return kResultOk;
    }
    tresult PLUGIN_API addEvent(Event &e) override
    {
        ev.push_back(e);
        return kResultOk;
    }
};

struct Queue : IParamValueQueue { // one point per block is enough for scripted changes
    IMPLEMENT_FUNKNOWN(IParamValueQueue)
    ParamID id = 0;
    ParamValue value = 0;
    ParamID PLUGIN_API getParameterId() override { return id; }
    int32 PLUGIN_API getPointCount() override { return 1; }
    tresult PLUGIN_API getPoint(int32, int32 &off, ParamValue &v) override
    {
        off = 0, v = value;
        return kResultOk;
    }
    tresult PLUGIN_API addPoint(int32, ParamValue, int32 &) override { return kResultFalse; }
};

struct NoChanges : IParameterChanges { // scripted changes due in this block (VTI_PARAMS), otherwise none
    IMPLEMENT_FUNKNOWN(IParameterChanges)
    std::vector<Queue *> due;
    int32 PLUGIN_API getParameterCount() override { return (int32)due.size(); }
    IParamValueQueue *PLUGIN_API getParameterData(int32 i) override { return i < (int32)due.size() ? due[i] : nullptr; }
    IParamValueQueue *PLUGIN_API addParameterData(const ParamID &, int32 &) override { return nullptr; }
};

struct ScriptedChange {
    ParamID id;
    double value, at;
    bool done;
    Queue q;
};
static std::vector<ScriptedChange *> g_script;

static void wav_header(FILE *f, uint32_t frames)
{
    uint32_t data = frames * 2 * 2, v32;
    uint16_t v16;
    fseek(f, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, f), v32 = 36 + data, fwrite(&v32, 4, 1, f), fwrite("WAVEfmt ", 1, 8, f);
    v32 = 16, fwrite(&v32, 4, 1, f), v16 = 1, fwrite(&v16, 2, 1, f), v16 = 2, fwrite(&v16, 2, 1, f);
    v32 = 44100, fwrite(&v32, 4, 1, f), v32 = 44100 * 4, fwrite(&v32, 4, 1, f);
    v16 = 4, fwrite(&v16, 2, 1, f), v16 = 16, fwrite(&v16, 2, 1, f);
    fwrite("data", 1, 4, f), fwrite(&data, 4, 1, f);
}

static int g_block = 512; // VTI_BLOCK=64/128/256/512
static long g_stop_frames = -1; // VTI_TRANSPORT=<s>: stopped for s seconds, then Play from 0 with 16ths at 120 BPM
static int g_live = 0;          // VTI_LIVE: 0 no note flagged, 1 odd seconds (alt), 2 all
static bool g_stopped = false;  // VTI_STOPPED=1: the transport never plays, the second grid and the WAV go on
static long g_play_frames = -1; // VTI_PLAY_AT=<s>: stopped until s, then Play from 0; the second grid and the WAV go on

struct Run {
    IAudioProcessor *proc;
    int seconds, quiet;
    FILE *wav;
    std::vector<int32> out_ch, in_ch;
    std::atomic<bool> done{false};
};

static DWORD WINAPI process_thread(LPVOID arg)
{
    Run &r = *(Run *)arg;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    const int block = g_block;
    const double rate = 44100;
    long blocks = (long)(r.seconds * rate / block), per_sec = (long)(rate / block);
    std::vector<std::vector<std::vector<float>>> obuf(r.out_ch.size()), ibuf(r.in_ch.size());
    std::vector<std::vector<float *>> optr(r.out_ch.size()), iptr(r.in_ch.size());
    std::vector<AudioBusBuffers> outs(r.out_ch.size()), ins(r.in_ch.size());
    for (size_t b = 0; b < r.out_ch.size(); b++) {
        obuf[b].assign(r.out_ch[b], std::vector<float>(block));
        for (auto &c : obuf[b])
            optr[b].push_back(c.data());
        outs[b].numChannels = r.out_ch[b];
        outs[b].channelBuffers32 = optr[b].data();
    }
    for (size_t b = 0; b < r.in_ch.size(); b++) {
        ibuf[b].assign(r.in_ch[b], std::vector<float>(block));
        for (auto &c : ibuf[b])
            iptr[b].push_back(c.data());
        ins[b].numChannels = r.in_ch[b];
        ins[b].channelBuffers32 = iptr[b].data();
    }
    EventList events;
    NoChanges changes;
    ProcessContext ctx{};
    LARGE_INTEGER f, t0, now;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t0);
    float peak = 0;
    uint32_t wav_frames = 0;
    for (long i = 0; i < blocks; i++) {
        events.ev.clear();
        bool playing = !g_stopped;
        int64_t pos = (int64_t)i * block; // project position of this block
        if (g_play_frames >= 0) {
            playing = pos >= g_play_frames;
            pos = playing ? pos - g_play_frames : 0;
        }
        if (g_stop_frames >= 0) { // transport test: stopped, then Play from 0; 16th notes (5512.5 frames), 50 % long
            playing = pos >= g_stop_frames;
            pos = playing ? pos - g_stop_frames : 0;
            for (int64_t k = playing ? (int64_t)(pos / 2756.25) : 0; playing; k++) {
                int64_t at = (int64_t)(k * 2756.25 + 0.5); // even k = note-on, odd k = note-off
                if (at < pos)
                    continue;
                if (at >= pos + block)
                    break;
                Event e{};
                e.sampleOffset = (int32)(at - pos);
                if (!(k & 1)) {
                    e.type = Event::kNoteOnEvent;
                    e.noteOn = {0, 60, 0.f, 0.8f, 0, -1};
                } else {
                    e.type = Event::kNoteOffEvent;
                    e.noteOff = {0, 60, 0.f, -1, 0.f};
                }
                events.ev.push_back(e);
            }
        }
        // a C4 from 0.25 s to 0.75 s of every second (exact sample positions; whole blocks would drift)
        for (int on = 1; on >= 0 && g_stop_frames < 0; on--) {
            int64_t first = (int64_t)i * block, last = first + block;
            int64_t sec = first / 44100, at = sec * 44100 + (on ? 11025 : 33075);
            if (at < first)
                at += 44100;
            if (at >= last || at / 44100 < r.quiet)
                continue;
            Event e{};
            e.busIndex = 0;
            e.sampleOffset = (int32)(at - first);
            if (g_live == 2 || (g_live == 1 && (at / 44100) & 1))
                e.flags = Event::kIsLive;
            if (on) {
                e.type = Event::kNoteOnEvent;
                e.noteOn = {0, 60, 0.f, 0.8f, 0, -1};
            } else {
                e.type = Event::kNoteOffEvent;
                e.noteOff = {0, 60, 0.f, -1, 0.f};
            }
            events.ev.push_back(e);
        }
        ctx.state = (playing ? ProcessContext::kPlaying : 0) | ProcessContext::kTempoValid | ProcessContext::kTimeSigValid |
                    ProcessContext::kProjectTimeMusicValid | ProcessContext::kContTimeValid |
                    ProcessContext::kSystemTimeValid;
        ctx.continousTimeSamples = i * block;
        QueryPerformanceCounter(&now);
        ctx.systemTime = (int64)((double)now.QuadPart * 1e9 / f.QuadPart);
        ctx.sampleRate = rate;
        ctx.projectTimeSamples = pos;
        ctx.tempo = 120;
        ctx.timeSigNumerator = ctx.timeSigDenominator = 4;
        ctx.projectTimeMusic = pos / rate * 2;
        ProcessData d{};
        d.processMode = kRealtime;
        d.symbolicSampleSize = kSample32;
        d.numSamples = block;
        d.numInputs = (int32)ins.size();
        d.inputs = ins.empty() ? nullptr : ins.data();
        d.numOutputs = (int32)outs.size();
        d.outputs = outs.data();
        d.inputEvents = &events;
        changes.due.clear();
        for (auto *c : g_script)
            if (!c->done && (double)i * block / rate >= c->at) {
                c->done = true;
                c->q.id = c->id;
                c->q.value = c->value;
                changes.due.push_back(&c->q);
                printf("t=%.3f param %u = %.4f\n", (double)i * block / rate, (unsigned)c->id, c->value);
                fflush(stdout);
            }
        d.inputParameterChanges = &changes;
        d.processContext = &ctx;
        r.proc->process(d);
        if (!obuf.empty() && obuf[0].size() >= 2) {
            std::vector<int16_t> pcm(block * 2);
            for (int k = 0; k < block; k++)
                for (int c = 0; c < 2; c++) {
                    float x = obuf[0][c][k];
                    peak = fmaxf(peak, fabsf(x));
                    x = x > 1 ? 1 : x < -1 ? -1 : x;
                    pcm[k * 2 + c] = (int16_t)lrintf(x * 32767);
                }
            if (r.wav && (playing || g_stopped || g_play_frames >= 0))
                fwrite(pcm.data(), 4, block, r.wav), wav_frames += block;
        }
        if ((i + 1) % per_sec == 0) {
            printf("t=%3lds output peak %.1f dBFS\n", (i + 1) / per_sec, peak > 0 ? 20 * log10f(peak) : -999.f);
            fflush(stdout);
            peak = 0;
        }
        double due = (i + 1) * block / rate;
        for (;;) {
            QueryPerformanceCounter(&now);
            double el = (double)(now.QuadPart - t0.QuadPart) / f.QuadPart;
            if (el >= due)
                break;
            Sleep(due - el > 0.002 ? 1 : 0);
        }
    }
    if (r.wav) {
        wav_header(r.wav, wav_frames);
        fclose(r.wav);
    }
    r.done = true;
    return 0;
}

// Editor test (VTI_EDITOR): the plug-in's view in a top-level Win32 window, saved as a BMP (PrintWindow).
static HWND g_editor_wnd;
static void save_bmp(HWND w, const char *path)
{
    RECT rc;
    GetClientRect(w, &rc);
    int cx = rc.right, cy = rc.bottom;
    if (cx <= 0 || cy <= 0)
        return;
    HDC wdc = GetDC(w), mdc = CreateCompatibleDC(wdc);
    BITMAPINFO bi{};
    bi.bmiHeader = {sizeof(BITMAPINFOHEADER), cx, -cy, 1, 24, BI_RGB};
    void *bits = nullptr;
    HBITMAP bmp = CreateDIBSection(wdc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(mdc, bmp);
    if (!PrintWindow(w, mdc, PW_CLIENTONLY))
        BitBlt(mdc, 0, 0, cx, cy, wdc, 0, 0, SRCCOPY);
    GdiFlush();
    int stride = (cx * 3 + 3) & ~3;
    BITMAPFILEHEADER fh{0x4d42, (DWORD)(sizeof fh + sizeof bi.bmiHeader + stride * cy), 0, 0,
                        (DWORD)(sizeof fh + sizeof bi.bmiHeader)};
    FILE *f = fopen(path, "wb");
    if (f) {
        BITMAPINFOHEADER h = bi.bmiHeader;
        fwrite(&fh, sizeof fh, 1, f), fwrite(&h, sizeof h, 1, f), fwrite(bits, stride, cy, f);
        fclose(f);
        printf("editor screenshot %dx%d -> %s\n", cx, cy, path);
    }
    SelectObject(mdc, old);
    DeleteObject(bmp);
    DeleteDC(mdc);
    ReleaseDC(w, wdc);
}

struct Frame : IPlugFrame { // the plug-in asks for window size changes through this
    IMPLEMENT_FUNKNOWN(IPlugFrame)
    tresult PLUGIN_API resizeView(IPlugView *view, ViewRect *r) override
    {
        printf("resizeView %dx%d\n", r->getWidth(), r->getHeight());
        if (g_editor_wnd) {
            RECT wr{0, 0, r->getWidth(), r->getHeight()};
            AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);
            SetWindowPos(g_editor_wnd, nullptr, 0, 0, wr.right - wr.left, wr.bottom - wr.top, SWP_NOMOVE | SWP_NOZORDER);
        }
        view->onSize(r);
        return kResultOk;
    }
} g_frame;

static IPlugView *open_editor(IEditController *ctl)
{
    IPlugView *view = ctl ? ctl->createView(ViewType::kEditor) : nullptr;
    if (!view) {
        printf("createView -> none\n");
        return nullptr;
    }
    printf("HWND supported: %d\n", view->isPlatformTypeSupported(kPlatformTypeHWND) == kResultTrue);
    view->setFrame(&g_frame);
    ViewRect r{};
    view->getSize(&r);
    WNDCLASSA wc{};
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "vtihost";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassA(&wc);
    RECT wr{0, 0, r.getWidth(), r.getHeight()};
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);
    g_editor_wnd = CreateWindowA("vtihost", "Virus TI (TI Revive) - test host", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0,
                                 wr.right - wr.left, wr.bottom - wr.top, nullptr, nullptr, wc.hInstance, nullptr);
    tresult a = view->attached(g_editor_wnd, kPlatformTypeHWND);
    printf("editor %dx%d attached -> %d\n", r.getWidth(), r.getHeight(), (int)a);
    fflush(stdout);
    return view;
}

// Minimal IBStream over a byte vector (state save/load).
struct MemStream : IBStream {
    std::vector<uint8_t> d;
    size_t pos = 0;
    tresult PLUGIN_API queryInterface(const TUID iid, void **obj) override
    {
        if (FUnknownPrivate::iidEqual(iid, IBStream::iid) || FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {
            *obj = this;
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
    tresult PLUGIN_API read(void *buf, int32 n, int32 *got) override
    {
        size_t k = std::min((size_t)std::max(n, 0), d.size() - std::min(pos, d.size()));
        memcpy(buf, d.data() + pos, k);
        pos += k;
        if (got)
            *got = (int32)k;
        return kResultOk;
    }
    tresult PLUGIN_API write(void *buf, int32 n, int32 *done) override
    {
        if (pos + n > d.size())
            d.resize(pos + n);
        memcpy(d.data() + pos, buf, n);
        pos += n;
        if (done)
            *done = n;
        return kResultOk;
    }
    tresult PLUGIN_API seek(int64 p, int32 mode, int64 *res) override
    {
        int64 base = mode == IBStream::kIBSeekSet ? 0 : mode == IBStream::kIBSeekCur ? (int64)pos : (int64)d.size();
        pos = (size_t)std::max<int64>(0, base + p);
        if (res)
            *res = (int64)pos;
        return kResultOk;
    }
    tresult PLUGIN_API tell(int64 *p) override
    {
        *p = (int64)pos;
        return kResultOk;
    }
};

static void save_state(IComponent *comp, IEditController *ctl, const char *path)
{
    MemStream a, b;
    tresult ra = comp->getState(&a);
    printf("getState -> %d (%zu bytes)", (int)ra, a.d.size());
    if (ctl)
    {
        tresult rb = ctl->getState(&b);
        printf(", controller getState -> %d (%zu bytes)", (int)rb, b.d.size());
    }
    printf("\n");
    FILE *f = fopen(path, "wb");
    if (!f)
        return;
    uint32_t n = (uint32_t)a.d.size(), m = (uint32_t)b.d.size();
    fwrite("VTISTATE", 1, 8, f);
    fwrite(&n, 4, 1, f);
    fwrite(a.d.data(), 1, n, f);
    fwrite(&m, 4, 1, f);
    fwrite(b.d.data(), 1, m, f);
    fclose(f);
}

static void load_state(IComponent *comp, IEditController *ctl, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        printf("cannot open %s\n", path);
        return;
    }
    char magic[8];
    uint32_t n = 0, m = 0;
    MemStream a, b;
    if (fread(magic, 1, 8, f) == 8 && !memcmp(magic, "VTISTATE", 8) && fread(&n, 4, 1, f) == 1) {
        a.d.resize(n);
        if (fread(a.d.data(), 1, n, f) == n && fread(&m, 4, 1, f) == 1) {
            b.d.resize(m);
            if (fread(b.d.data(), 1, m, f) != m)
                b.d.clear();
        }
    }
    fclose(f);
    printf("setState -> %d (%zu bytes)", (int)comp->setState(&a), a.d.size());
    if (ctl) {
        a.pos = 0;
        printf(", setComponentState -> %d", (int)ctl->setComponentState(&a));
        if (!b.d.empty())
            printf(", controller setState -> %d", (int)ctl->setState(&b));
    }
    printf("\n");
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: vtihost <plugin.vst3> [seconds=60] [out.wav] [quiet=0]\n");
        return 2;
    }
    int seconds = argc > 2 ? atoi(argv[2]) : 60, quiet = argc > 4 ? atoi(argv[4]) : 0;
    HMODULE m = LoadLibraryA(argv[1]);
    if (!m) {
        printf("LoadLibrary failed: %lu\n", GetLastError());
        return 1;
    }
    auto init = (bool (*)())GetProcAddress(m, "InitDll");
    if (init)
        init();
    auto gpf = (IPluginFactory * (PLUGIN_API *)()) GetProcAddress(m, "GetPluginFactory");
    IPluginFactory *fac = gpf ? gpf() : nullptr;
    if (!fac)
        return 1;
    for (int i = 0; i < fac->countClasses(); i++) { // all classes with their ids (the rewrite reuses them)
        PClassInfo c{};
        if (fac->getClassInfo(i, &c) != kResultOk)
            continue;
        printf("class %d: ", i);
        for (int k = 0; k < 16; k++)
            printf("%02X", (uint8_t)c.cid[k]);
        printf(" %s \"%s\"\n", c.category, c.name);
    }
    PClassInfo ci{};
    int idx = -1;
    for (int i = 0; i < fac->countClasses() && idx < 0; i++)
        if (fac->getClassInfo(i, &ci) == kResultOk && !strcmp(ci.category, kVstAudioEffectClass))
            idx = i;
    if (idx < 0)
        return 1;
    printf("class \"%s\"\n", ci.name);
    IComponent *comp = nullptr;
    if (fac->createInstance(ci.cid, IComponent::iid, (void **)&comp) != kResultOk || !comp)
        return 1;
    printf("initialize -> %d\n", (int)comp->initialize(&g_host));
    IAudioProcessor *proc = nullptr;
    comp->queryInterface(IAudioProcessor::iid, (void **)&proc);
    IEditController *ctl = nullptr;
    if (comp->queryInterface(IEditController::iid, (void **)&ctl) != kResultOk) {
        TUID cid;
        if (comp->getControllerClassId(cid) == kResultOk &&
            fac->createInstance(cid, IEditController::iid, (void **)&ctl) == kResultOk && ctl)
            ctl->initialize(&g_host);
    }
    if (ctl)
        ctl->setComponentHandler(&g_handler);
    if (ctl && getenv("VTI_LISTPARAMS")) { // id, title, short title, units, steps, default, flags
        for (int32 i = 0; i < ctl->getParameterCount(); i++) {
            ParameterInfo pi{};
            ctl->getParameterInfo(i, pi);
            char t[256], st[256], u[256];
            auto u8 = [](const TChar *w, char *o) {
                int k = 0;
                for (; w[k] && k < 255; k++)
                    o[k] = w[k] < 128 ? (char)w[k] : '?';
                o[k] = 0;
            };
            u8(pi.title, t), u8(pi.shortTitle, st), u8(pi.units, u);
            printf("%d\t%u\t%s\t%s\t%s\t%d\t%.6f\t%d\n", i, (unsigned)pi.id, t, st, u, (int)pi.stepCount,
                   pi.defaultNormalizedValue, (int)pi.flags);
        }
        fflush(stdout);
        TerminateProcess(GetCurrentProcess(), 0);
    }
    if (const char *sp = getenv("VTI_PARAMS")) { // "id=norm@sec,id=norm@sec"
        for (const char *p = sp; *p;) {
            unsigned id = 0;
            double v = 0, at = 0;
            int n = 0;
            if (sscanf(p, "%u=%lf@%lf%n", &id, &v, &at, &n) == 3) {
                auto *c = new ScriptedChange{id, v, at, false, {}};
                g_script.push_back(c);
                if (ctl)
                    ctl->setParamNormalized(id, v); // the controller side too, as a host does
            }
            const char *comma = strchr(p, ',');
            if (!comma)
                break;
            p = comma + 1;
        }
    }
    Run r{};
    r.proc = proc;
    r.seconds = seconds;
    r.quiet = quiet;
    r.wav = argc > 3 ? fopen(argv[3], "wb") : nullptr;
    if (r.wav)
        wav_header(r.wav, 0);
    std::vector<SpeakerArrangement> oa, ia;
    for (int32 b = 0; b < comp->getBusCount(kAudio, kOutput); b++) {
        BusInfo bi{};
        comp->getBusInfo(kAudio, kOutput, b, bi);
        r.out_ch.push_back(bi.channelCount);
        oa.push_back(bi.channelCount == 2 ? SpeakerArr::kStereo : SpeakerArr::kMono);
        comp->activateBus(kAudio, kOutput, b, true);
    }
    for (int32 b = 0; b < comp->getBusCount(kAudio, kInput); b++) {
        BusInfo bi{};
        comp->getBusInfo(kAudio, kInput, b, bi);
        r.in_ch.push_back(bi.channelCount);
        ia.push_back(bi.channelCount == 2 ? SpeakerArr::kStereo : SpeakerArr::kMono);
        comp->activateBus(kAudio, kInput, b, true);
    }
    if (comp->getBusCount(kEvent, kInput) > 0)
        comp->activateBus(kEvent, kInput, 0, true);
    printf("%zu output buses, %zu input buses\n", r.out_ch.size(), r.in_ch.size());
    proc->setBusArrangements(ia.empty() ? nullptr : ia.data(), (int32)ia.size(), oa.data(), (int32)oa.size());
    if (getenv("VTI_TRANSPORT"))
        g_stop_frames = (long)(atof(getenv("VTI_TRANSPORT")) * 44100);
    if (getenv("VTI_BLOCK"))
        g_block = atoi(getenv("VTI_BLOCK"));
    if (const char *lv = getenv("VTI_LIVE"))
        g_live = !strcmp(lv, "all") ? 2 : !strcmp(lv, "alt") ? 1 : 0;
    g_stopped = getenv("VTI_STOPPED") != nullptr;
    if (getenv("VTI_PLAY_AT"))
        g_play_frames = (long)(atof(getenv("VTI_PLAY_AT")) * 44100);
    if (const char *ls = getenv("VTI_LOADSTATE"))
        load_state(comp, ctl, ls);
    ProcessSetup ps{kRealtime, kSample32, g_block, 44100};
    printf("block %d\n", g_block);
    printf("setupProcessing -> %d\n", (int)proc->setupProcessing(ps));
    printf("setActive -> %d\n", (int)comp->setActive(true));
    printf("setProcessing -> %d\n", (int)proc->setProcessing(true));
    printf("latency %u samples\n", (unsigned)proc->getLatencySamples());
    fflush(stdout);
    const char *shot = getenv("VTI_EDITOR");
    IPlugView *view = shot ? open_editor(ctl) : nullptr;
    DWORD next_shot = GetTickCount() + 10000;
    HANDLE th = CreateThread(nullptr, 0, process_thread, &r, 0, nullptr);
    while (!r.done) { // main thread: the plug-in's windows / timers
        if (view && GetTickCount() >= next_shot) {
            save_bmp(g_editor_wnd, shot);
            next_shot += 10000;
        }
        MSG msg;
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        MsgWaitForMultipleObjects(1, &th, FALSE, 10, QS_ALLINPUT);
    }
    WaitForSingleObject(th, INFINITE);
    if (view) {
        save_bmp(g_editor_wnd, shot);
        view->removed();
        view->release();
        DestroyWindow(g_editor_wnd);
    }
    proc->setProcessing(false);
    if (const char *ss = getenv("VTI_SAVESTATE"))
        save_state(comp, ctl, ss);
    comp->setActive(false);
    if (ctl && (void *)ctl != (void *)comp) {
        ctl->terminate();
        ctl->release();
    }
    comp->terminate();
    comp->release();
    fac->release();
    printf("terminated, unloading\n");
    fflush(stdout);
    auto exitdll = (bool (*)())GetProcAddress(m, "ExitDll");
    if (exitdll)
        exitdll();
    printf("FreeLibrary -> %d\n", FreeLibrary(m));
    printf("done\n");
    fflush(stdout);
    // the original's Virus Control.dll can hang in DLL_PROCESS_DETACH at process exit after its editor was open
    // (E119); a host process does not need the clean exit
    TerminateProcess(GetCurrentProcess(), 0);
    return 0;
}
