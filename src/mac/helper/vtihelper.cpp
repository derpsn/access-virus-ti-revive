// vtihelper.cpp - TI Revive macOS stage 2 helper (findings/design-macos-bridge.md): an x86_64 process (runs under
// Rosetta) that hosts the original Intel-only Access "Virus TI" VST3 for the native arm64 bridge plug-in
// (src/mac/bridge). Protocol: src/mac/common/tirbridge.h.
//
//   vtihelper --serve <socket path> <daw pid>   spawned by the bridge: serves all bridge instances of one DAW process
//   vtihelper --probe <plugin.vst3> [seconds]   diagnostics: buses, parameters, real-time process() with test notes
//
// Threads: the main thread runs the CFRunLoop (the plug-in's timers send its MIDI and poll the device, E104) and
// executes every call into the plug-in except process(); a control thread per bridge connection; a process thread
// per instance, woken by the bridge through a named semaphore.
//
// Build (on the Mac; P = vst3sdk/pluginterfaces, MIT):
//   clang++ -arch x86_64 -std=c++17 -O2 -I vst3sdk -I src/mac/common -o vtihelper vtihelper.cpp
//     $P/base/funknown.cpp $P/base/coreiids.cpp -framework CoreFoundation
#include <CoreFoundation/CoreFoundation.h>
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdarg>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dispatch/dispatch.h>
#include <fcntl.h>
#include <map>
#include <mutex>
#include <pthread.h>
#include <os/os_sync_wait_on_address.h>
#include <signal.h>
#include <string>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <vector>

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "tirbridge.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

// interface IDs used here (the SDK declares them; one translation unit defines them)
namespace Steinberg {
DEF_CLASS_IID(IBStream)
namespace Vst {
DEF_CLASS_IID(IComponent)
DEF_CLASS_IID(IAudioProcessor)
DEF_CLASS_IID(IEditController)
DEF_CLASS_IID(IMidiMapping)
DEF_CLASS_IID(IComponentHandler)
DEF_CLASS_IID(IHostApplication)
DEF_CLASS_IID(IEventList)
DEF_CLASS_IID(IParameterChanges)
DEF_CLASS_IID(IParamValueQueue)
} // namespace Vst
} // namespace Steinberg

static void hlog(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "vtihelper: ");
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

static void to_utf8(const TChar *s, char *out, size_t n)
{
    size_t k = 0;
    for (; s && *s && k + 4 < n; s++) {
        uint32_t c = *s;
        if (c < 0x80)
            out[k++] = (char)c;
        else if (c < 0x800)
            out[k++] = (char)(0xc0 | c >> 6), out[k++] = (char)(0x80 | (c & 0x3f));
        else
            out[k++] = (char)(0xe0 | c >> 12), out[k++] = (char)(0x80 | (c >> 6 & 0x3f)),
            out[k++] = (char)(0x80 | (c & 0x3f));
    }
    out[k] = 0;
}

template <class F> static void on_main(F f) // run on the main thread (the plug-in's UI thread) and wait
{
    if (pthread_main_np())
        f();
    else
        dispatch_sync_f(dispatch_get_main_queue(), &f, [](void *p) { (*(F *)p)(); });
}

// ------------------------------------------------------------------------------- host-side objects
#define IMPLEMENT_FUNKNOWN(I)                                                                                          \
    std::atomic<uint32> refs{1};                                                                                       \
    uint32 PLUGIN_API addRef() override { return ++refs; }                                                             \
    uint32 PLUGIN_API release() override { return --refs; } /* owned by the helper */                                  \
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
        const char *n = "TI Revive";
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
};
static Host g_host;

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

struct ParamQueue : IParamValueQueue {
    IMPLEMENT_FUNKNOWN(IParamValueQueue)
    ParamID id = 0;
    std::vector<std::pair<int32, ParamValue>> pts;
    ParamID PLUGIN_API getParameterId() override { return id; }
    int32 PLUGIN_API getPointCount() override { return (int32)pts.size(); }
    tresult PLUGIN_API getPoint(int32 i, int32 &off, ParamValue &v) override
    {
        if (i < 0 || i >= (int32)pts.size())
            return kInvalidArgument;
        off = pts[i].first, v = pts[i].second;
        return kResultOk;
    }
    tresult PLUGIN_API addPoint(int32 off, ParamValue v, int32 &index) override
    {
        index = (int32)pts.size();
        pts.push_back({off, v});
        return kResultOk;
    }
};

struct ParamChanges : IParameterChanges {
    IMPLEMENT_FUNKNOWN(IParameterChanges)
    std::vector<ParamQueue *> queues; // reused between blocks (no allocation once warmed up)
    int32 used = 0;
    ~ParamChanges()
    {
        for (auto *q : queues)
            delete q;
    }
    void clear() { used = 0; }
    int32 PLUGIN_API getParameterCount() override { return used; }
    IParamValueQueue *PLUGIN_API getParameterData(int32 i) override
    {
        return i >= 0 && i < used ? queues[i] : nullptr;
    }
    IParamValueQueue *PLUGIN_API addParameterData(const ParamID &id, int32 &index) override
    {
        for (int32 i = 0; i < used; i++)
            if (queues[i]->id == id) {
                index = i;
                return queues[i];
            }
        if (used == (int32)queues.size())
            queues.push_back(new ParamQueue);
        ParamQueue *q = queues[used];
        q->id = id;
        q->pts.clear();
        index = used++;
        return q;
    }
};

struct MemoryStream : IBStream {
    IMPLEMENT_FUNKNOWN(IBStream)
    std::vector<uint8_t> data;
    int64 pos = 0;
    tresult PLUGIN_API read(void *buf, int32 n, int32 *got) override
    {
        int32 k = (int32)std::max<int64>(0, std::min<int64>(n, (int64)data.size() - pos));
        memcpy(buf, data.data() + pos, k);
        pos += k;
        if (got)
            *got = k;
        return kResultOk;
    }
    tresult PLUGIN_API write(void *buf, int32 n, int32 *wrote) override
    {
        if (pos + n > (int64)data.size())
            data.resize(pos + n);
        memcpy(data.data() + pos, buf, n);
        pos += n;
        if (wrote)
            *wrote = n;
        return kResultOk;
    }
    tresult PLUGIN_API seek(int64 off, int32 mode, int64 *result) override
    {
        int64 p = mode == kIBSeekSet ? off : mode == kIBSeekCur ? pos + off : (int64)data.size() + off;
        if (p < 0)
            return kInvalidArgument;
        pos = p;
        if (result)
            *result = pos;
        return kResultOk;
    }
    tresult PLUGIN_API tell(int64 *p) override
    {
        if (p)
            *p = pos;
        return kResultOk;
    }
};

// ------------------------------------------------------------------------------- the original plug-in
static IPluginFactory *g_factory;
static PClassInfo g_class;

static bool load_original(const char *path)
{
    CFStringRef p = CFStringCreateWithCString(nullptr, path, kCFStringEncodingUTF8);
    CFURLRef url = CFURLCreateWithFileSystemPath(nullptr, p, kCFURLPOSIXPathStyle, true);
    CFBundleRef bundle = CFBundleCreate(nullptr, url);
    CFRelease(p);
    CFRelease(url);
    if (!bundle || !CFBundleLoadExecutable(bundle)) {
        hlog("cannot load %s", path);
        return false;
    }
    auto entry = (bool (*)(CFBundleRef))CFBundleGetFunctionPointerForName(bundle, CFSTR("bundleEntry"));
    if (entry && !entry(bundle))
        return false;
    auto get = (IPluginFactory * (*)()) CFBundleGetFunctionPointerForName(bundle, CFSTR("GetPluginFactory"));
    if (!get || !(g_factory = get()))
        return false;
    for (int32 i = 0, n = g_factory->countClasses(); i < n; i++)
        if (g_factory->getClassInfo(i, &g_class) == kResultOk && !strcmp(g_class.category, kVstAudioEffectClass))
            return true;
    return false;
}

struct Instance;
static void notify(Instance *in, uint32_t type, const void *p, uint32_t len);

struct Instance : IComponentHandler {
    IMPLEMENT_FUNKNOWN(IComponentHandler)
    uint32_t id = 0;
    int conn = -1; // control socket of the owning bridge (notifications)
    IComponent *comp = nullptr;
    IAudioProcessor *proc = nullptr;
    IEditController *ctl = nullptr;
    tirb_shm *shm = nullptr;
    char shm_name[32];
    pthread_t thread{};
    std::atomic<bool> quit{false};
    int32 out_buses = 0, max_block = 512;
    EventList events;
    ParamChanges pin, pout;
    ProcessContext ctx{};

    // IComponentHandler: edits from the original's UI -> the bridge (-> DAW automation)
    tresult PLUGIN_API beginEdit(ParamID p) override { return edit(0, p, 0); }
    tresult PLUGIN_API performEdit(ParamID p, ParamValue v) override { return edit(1, p, v); }
    tresult PLUGIN_API endEdit(ParamID p) override { return edit(2, p, 0); }
    tresult PLUGIN_API restartComponent(int32 flags) override
    {
        notify(this, TIRB_N_RESTART, &flags, 4);
        return kResultOk;
    }
    tresult edit(int32 what, ParamID p, ParamValue v)
    {
        tirb_param_edit e{what, p, v};
        notify(this, TIRB_N_PARAM_EDIT, &e, sizeof e);
        return kResultOk;
    }
};

static std::mutex g_conn_mx; // serialises writes on bridge sockets (replies vs notifications)

static int io_all(int fd, void *p, size_t n, bool wr)
{
    for (uint8_t *b = (uint8_t *)p; n;) {
        ssize_t r = wr ? send(fd, b, n, 0) : recv(fd, b, n, 0);
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0)
            return -1;
        b += r;
        n -= (size_t)r;
    }
    return 0;
}

static int send_msg(int fd, uint32_t type, uint32_t inst, uint32_t seq, const void *p, uint32_t len)
{
    tirb_msg h{type, inst, seq, len};
    std::lock_guard<std::mutex> g(g_conn_mx);
    return io_all(fd, &h, sizeof h, true) || (len && io_all(fd, (void *)p, len, true)) ? -1 : 0;
}

static void notify(Instance *in, uint32_t type, const void *p, uint32_t len)
{
    if (in->conn >= 0)
        send_msg(in->conn, type, in->id, 0, p, len);
}

static void bump_and_wake(uint32_t *seq, uint32_t v)
{
    __atomic_store_n(seq, v, __ATOMIC_RELEASE);
    os_sync_wake_by_address_any(seq, 4, OS_SYNC_WAKE_BY_ADDRESS_SHARED);
}

// process thread: one block per req_seq bump by the bridge
static void *process_thread(void *arg)
{
    Instance *in = (Instance *)arg;
    std::vector<AudioBusBuffers> outs(in->out_buses);
    float *chans[TIRB_OUT_CH];
    uint32_t seen = 0;
    for (;;) {
        tirb_shm *s = in->shm;
        uint32_t req;
        while ((req = __atomic_load_n(&s->req_seq, __ATOMIC_ACQUIRE)) == seen && !in->quit)
            os_sync_wait_on_address_with_timeout(&s->req_seq, seen, 4, OS_SYNC_WAIT_ON_ADDRESS_SHARED,
                                                 OS_CLOCK_MACH_ABSOLUTE_TIME, 100000000); // 100 ms: check quit
        if (in->quit)
            break;
        seen = req;
        uint32_t frames = s->frames > (uint32_t)in->max_block ? (uint32_t)in->max_block : s->frames;
        // inputs
        in->events.ev.clear();
        for (uint32_t i = 0; i < s->num_events && i < TIRB_MAX_EVENTS; i++) {
            const tirb_event &te = s->events[i];
            Event e{};
            e.busIndex = te.bus;
            e.sampleOffset = te.sample_offset;
            e.type = (uint16)te.type;
            e.flags = (uint16)te.flags;
            memcpy(&e.noteOn, te.data, sizeof te.data); // note on/off, poly pressure, ... (no pointers)
            in->events.ev.push_back(e);
        }
        in->pin.clear();
        for (uint32_t i = 0; i < s->num_pchanges && i < TIRB_MAX_PCHANGES; i++) {
            int32 idx;
            IParamValueQueue *q = in->pin.addParameterData(s->pchanges[i].id, idx);
            q->addPoint(s->pchanges[i].sample_offset, s->pchanges[i].value, idx);
        }
        in->pout.clear();
        const tirb_transport &t = s->transport;
        ProcessContext &c = in->ctx;
        c.state = t.state;
        c.sampleRate = t.sample_rate;
        c.projectTimeSamples = t.project_time_samples;
        c.systemTime = t.system_time;
        c.continousTimeSamples = t.continous_time_samples;
        c.projectTimeMusic = t.project_time_music;
        c.barPositionMusic = t.bar_position_music;
        c.cycleStartMusic = t.cycle_start_music;
        c.cycleEndMusic = t.cycle_end_music;
        c.tempo = t.tempo;
        c.timeSigNumerator = t.time_sig_num;
        c.timeSigDenominator = t.time_sig_den;
        // outputs straight into shared memory
        for (int32 b = 0, ch = 0; b < in->out_buses; b++) {
            outs[b].numChannels = 2;
            outs[b].silenceFlags = 0;
            outs[b].channelBuffers32 = chans + ch;
            chans[ch] = s->out[ch], chans[ch + 1] = s->out[ch + 1];
            ch += 2;
        }
        ProcessData d{};
        d.processMode = kRealtime;
        d.symbolicSampleSize = kSample32;
        d.numSamples = (int32)frames;
        d.numOutputs = in->out_buses;
        d.outputs = outs.data();
        d.inputEvents = &in->events;
        d.inputParameterChanges = &in->pin;
        d.outputParameterChanges = &in->pout;
        d.processContext = &in->ctx;
        in->proc->process(d);
        uint32_t no = 0;
        for (int32 i = 0; i < in->pout.used && no < 64; i++) {
            ParamQueue *q = in->pout.queues[i];
            if (!q->pts.empty())
                s->out_pchanges[no++] = {q->id, q->pts.back().first, q->pts.back().second};
        }
        s->num_out_pchanges = no;
        bump_and_wake(&s->done_seq, req);
    }
    return nullptr;
}

static int g_daw_pid;
static std::map<uint32_t, Instance *> g_instances;
static std::mutex g_inst_mx;
static std::atomic<uint32_t> g_next_id{1};

static Instance *create_instance(int conn, tirb_create_reply &r)
{
    Instance *in = new Instance;
    in->id = g_next_id++;
    in->conn = conn;
    bool ok = false;
    on_main([&] {
        if (g_factory->createInstance(g_class.cid, IComponent::iid, (void **)&in->comp) != kResultOk || !in->comp)
            return;
        in->comp->initialize(&g_host);
        if (in->comp->queryInterface(IEditController::iid, (void **)&in->ctl) != kResultOk) {
            TUID cid;
            if (in->comp->getControllerClassId(cid) == kResultOk &&
                g_factory->createInstance(cid, IEditController::iid, (void **)&in->ctl) == kResultOk && in->ctl)
                in->ctl->initialize(&g_host);
        }
        if (in->ctl)
            in->ctl->setComponentHandler(in);
        if (in->comp->queryInterface(IAudioProcessor::iid, (void **)&in->proc) != kResultOk)
            return;
        memset(&r, 0, sizeof r);
        r.num_inputs = 0; // the original's audio inputs ("Dummy In", "Sidechain") are not bridged
        int32 nout = in->comp->getBusCount(kAudio, kOutput);
        in->out_buses = nout > TIRB_OUT_CH / 2 ? TIRB_OUT_CH / 2 : nout;
        r.num_outputs = (uint32_t)in->out_buses;
        for (int32 b = 0; b < in->out_buses; b++) {
            BusInfo bi{};
            in->comp->getBusInfo(kAudio, kOutput, b, bi);
            r.out_channels[b] = 2;
            to_utf8(bi.name, r.out_names[b], sizeof r.out_names[b]);
        }
        r.event_inputs = (uint32_t)in->comp->getBusCount(kEvent, kInput);
        if (r.event_inputs) {
            BusInfo bi{};
            in->comp->getBusInfo(kEvent, kInput, 0, bi);
            r.event_channels = (uint32_t)bi.channelCount;
        }
        r.param_count = in->ctl ? (uint32_t)in->ctl->getParameterCount() : 0;
        r.latency = in->proc->getLatencySamples();
        ok = true;
    });
    if (!ok) {
        hlog("instance %u: creating the original failed", in->id);
        return nullptr; // leaks the half-made instance on purpose: the original may not survive a terminate here
    }
    snprintf(in->shm_name, sizeof in->shm_name, TIRB_SHM_FMT, g_daw_pid, in->id);
    shm_unlink(in->shm_name);
    int fd = shm_open(in->shm_name, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd < 0 || ftruncate(fd, sizeof(tirb_shm)))
        return nullptr;
    void *m = mmap(nullptr, sizeof(tirb_shm), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (m == MAP_FAILED)
        return nullptr;
    in->shm = (tirb_shm *)m;
    memset(in->shm, 0, sizeof(tirb_shm));
    strcpy(r.shm_name, in->shm_name);
    pthread_create(&in->thread, nullptr, process_thread, in);
    std::lock_guard<std::mutex> g(g_inst_mx);
    g_instances[in->id] = in;
    hlog("instance %u created (%u outputs, %u parameters)", in->id, r.num_outputs, r.param_count);
    return in;
}

static void destroy_instance(Instance *in)
{
    in->quit = true;
    os_sync_wake_by_address_any(&in->shm->req_seq, 4, OS_SYNC_WAKE_BY_ADDRESS_SHARED);
    pthread_join(in->thread, nullptr);
    on_main([&] {
        in->proc->setProcessing(false);
        in->comp->setActive(false);
        if (in->ctl && (void *)in->ctl != (void *)in->comp)
            in->ctl->terminate();
        in->comp->terminate();
    });
    munmap(in->shm, sizeof(tirb_shm));
    shm_unlink(in->shm_name);
    {
        std::lock_guard<std::mutex> g(g_inst_mx);
        g_instances.erase(in->id);
    }
    hlog("instance %u destroyed", in->id);
    // the original's objects are released by terminate(); our Instance stays allocated (no use-after-free risk
    // from late notifications)
}

// one thread per bridge connection
static void *conn_thread(void *arg)
{
    int fd = (int)(intptr_t)arg;
    std::vector<uint8_t> buf;
    std::vector<Instance *> owned;
    for (;;) {
        tirb_msg h;
        if (io_all(fd, &h, sizeof h, false))
            break;
        buf.resize(h.len);
        if (h.len && io_all(fd, buf.data(), h.len, false))
            break;
        Instance *in = nullptr;
        {
            std::lock_guard<std::mutex> g(g_inst_mx);
            auto it = g_instances.find(h.instance);
            in = it == g_instances.end() ? nullptr : it->second;
        }
        uint32_t rt = h.type | TIRB_REPLY;
        switch (h.type) {
        case TIRB_HELLO: {
            uint32_t v = TIRB_VERSION;
            send_msg(fd, rt, 0, h.seq, &v, 4);
            break;
        }
        case TIRB_CREATE: {
            tirb_create_reply r{};
            Instance *n = create_instance(fd, r);
            if (n)
                owned.push_back(n);
            send_msg(fd, rt, n ? n->id : 0, h.seq, n ? &r : nullptr, n ? sizeof r : 0);
            break;
        }
        case TIRB_DESTROY:
            if (in) {
                destroy_instance(in);
                owned.erase(std::remove(owned.begin(), owned.end(), in), owned.end());
            }
            send_msg(fd, rt, h.instance, h.seq, nullptr, 0);
            break;
        case TIRB_PARAM_INFO: {
            uint32_t first = h.len >= 8 ? ((uint32_t *)buf.data())[0] : 0;
            uint32_t count = h.len >= 8 ? ((uint32_t *)buf.data())[1] : 0;
            std::vector<uint8_t> out(4);
            on_main([&] {
                uint32_t total = in && in->ctl ? (uint32_t)in->ctl->getParameterCount() : 0;
                memcpy(out.data(), &total, 4);
                for (uint32_t i = first; i < total && i < first + count; i++) {
                    ParameterInfo pi{};
                    in->ctl->getParameterInfo((int32)i, pi);
                    tirb_param p{};
                    p.id = pi.id;
                    p.step_count = pi.stepCount;
                    p.flags = pi.flags;
                    p.unit_id = pi.unitId;
                    p.default_value = pi.defaultNormalizedValue;
                    to_utf8(pi.title, p.title, sizeof p.title);
                    to_utf8(pi.shortTitle, p.short_title, sizeof p.short_title);
                    to_utf8(pi.units, p.units, sizeof p.units);
                    out.insert(out.end(), (uint8_t *)&p, (uint8_t *)&p + sizeof p);
                }
            });
            send_msg(fd, rt, h.instance, h.seq, out.data(), (uint32_t)out.size());
            break;
        }
        case TIRB_MIDI_MAPPING: {
            std::vector<tirb_midimap> maps;
            on_main([&] {
                IMidiMapping *mm = nullptr;
                if (!in || !in->ctl || in->ctl->queryInterface(IMidiMapping::iid, (void **)&mm) != kResultOk || !mm)
                    return;
                for (int16 ch = 0; ch < 16; ch++)
                    for (int16 cc = 0; cc < kCountCtrlNumber; cc++) {
                        ParamID id;
                        if (mm->getMidiControllerAssignment(0, ch, cc, id) == kResultOk)
                            maps.push_back({0, ch, cc, 0, id});
                    }
                mm->release();
            });
            std::vector<uint8_t> out(4);
            uint32_t n = (uint32_t)maps.size();
            memcpy(out.data(), &n, 4);
            out.insert(out.end(), (uint8_t *)maps.data(), (uint8_t *)(maps.data() + n));
            send_msg(fd, rt, h.instance, h.seq, out.data(), (uint32_t)out.size());
            break;
        }
        case TIRB_GET_STATE: {
            MemoryStream ms;
            on_main([&] {
                if (in)
                    in->comp->getState(&ms);
            });
            send_msg(fd, rt, h.instance, h.seq, ms.data.data(), (uint32_t)ms.data.size());
            break;
        }
        case TIRB_SET_STATE: {
            MemoryStream ms;
            ms.data = buf;
            int32_t r = -1;
            on_main([&] {
                if (!in)
                    return;
                r = in->comp->setState(&ms);
                if (in->ctl && (void *)in->ctl != (void *)in->comp) { // separate controller: mirror the state
                    ms.pos = 0;
                    in->ctl->setComponentState(&ms);
                }
            });
            send_msg(fd, rt, h.instance, h.seq, &r, 4);
            break;
        }
        case TIRB_SETUP: {
            int32_t r = -1;
            if (in && h.len >= sizeof(tirb_setup)) {
                tirb_setup su;
                memcpy(&su, buf.data(), sizeof su);
                on_main([&] {
                    in->max_block = su.max_block > TIRB_MAX_BLOCK ? TIRB_MAX_BLOCK : su.max_block;
                    ProcessSetup ps{su.process_mode, kSample32, in->max_block, su.sample_rate};
                    r = in->proc->setupProcessing(ps);
                });
            }
            send_msg(fd, rt, h.instance, h.seq, &r, 4);
            break;
        }
        case TIRB_ACTIVE: {
            int32_t r = -1;
            uint32_t on = h.len >= 4 ? *(uint32_t *)buf.data() : 0;
            if (in)
                on_main([&] {
                    if (on) {
                        for (int32 b = 0; b < in->comp->getBusCount(kAudio, kOutput); b++)
                            in->comp->activateBus(kAudio, kOutput, b, b < in->out_buses);
                        for (int32 b = 0; b < in->comp->getBusCount(kAudio, kInput); b++)
                            in->comp->activateBus(kAudio, kInput, b, false);
                        if (in->comp->getBusCount(kEvent, kInput) > 0)
                            in->comp->activateBus(kEvent, kInput, 0, true);
                    }
                    r = in->comp->setActive(on != 0);
                    in->proc->setProcessing(on != 0);
                });
            send_msg(fd, rt, h.instance, h.seq, &r, 4);
            break;
        }
        case TIRB_GET_PARAM: {
            double v = 0;
            uint32_t pid = h.len >= 4 ? *(uint32_t *)buf.data() : 0;
            if (in && in->ctl)
                on_main([&] { v = in->ctl->getParamNormalized(pid); });
            send_msg(fd, rt, h.instance, h.seq, &v, 8);
            break;
        }
        case TIRB_PARAM_STRING: {
            char s[256] = "";
            if (in && in->ctl && h.len >= 12) {
                uint32_t pid;
                double v;
                memcpy(&pid, buf.data(), 4);
                memcpy(&v, buf.data() + 4, 8);
                on_main([&] {
                    String128 str{};
                    if (in->ctl->getParamStringByValue(pid, v, str) == kResultOk)
                        to_utf8(str, s, sizeof s);
                });
            }
            send_msg(fd, rt, h.instance, h.seq, s, (uint32_t)strlen(s) + 1);
            break;
        }
        case TIRB_SET_PARAM:
            if (in && in->ctl && h.len >= 12) {
                uint32_t pid;
                double v;
                memcpy(&pid, buf.data(), 4);
                memcpy(&v, buf.data() + 4, 8);
                dispatch_async_f(dispatch_get_main_queue(), new std::pair<Instance *, std::pair<uint32_t, double>>(
                                                                in, {pid, v}),
                                 [](void *p) {
                                     auto *a = (std::pair<Instance *, std::pair<uint32_t, double>> *)p;
                                     a->first->ctl->setParamNormalized(a->second.first, a->second.second);
                                     delete a;
                                 });
            }
            break;
        default: // editor messages: next step (findings/design-macos-bridge.md)
            if (!(h.type == TIRB_EDITOR_EVENT || h.type == TIRB_EDITOR_MOVE))
                send_msg(fd, rt, h.instance, h.seq, nullptr, 0);
        }
    }
    for (Instance *in : owned) // bridge gone (DAW closed the plug-in or crashed)
        destroy_instance(in);
    close(fd);
    std::lock_guard<std::mutex> g(g_inst_mx);
    if (g_instances.empty()) {
        hlog("no instances left, exiting");
        exit(0);
    }
    return nullptr;
}

static int serve(const char *sock_path, int daw_pid)
{
    g_daw_pid = daw_pid;
    signal(SIGPIPE, SIG_IGN);
    if (!load_original("/Library/Audio/Plug-Ins/VST3/Virus TI.vst3"))
        return 1;
    struct sockaddr_un sa = {};
    sa.sun_family = AF_UNIX;
    snprintf(sa.sun_path, sizeof sa.sun_path, "%s", sock_path);
    unlink(sa.sun_path);
    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    if (bind(s, (struct sockaddr *)&sa, sizeof sa) || chmod(sa.sun_path, 0600) || listen(s, 8)) {
        hlog("socket %s: %s", sock_path, strerror(errno));
        return 1;
    }
    // exit with the DAW
    dispatch_source_t ds = dispatch_source_create(DISPATCH_SOURCE_TYPE_PROC, (uintptr_t)daw_pid, DISPATCH_PROC_EXIT,
                                                  dispatch_get_main_queue());
    static char sock_copy[sizeof sa.sun_path];
    strcpy(sock_copy, sa.sun_path);
    dispatch_source_set_event_handler_f(ds, [](void *) {
        unlink(sock_copy);
        _exit(0);
    });
    dispatch_resume(ds);
    if (kill(daw_pid, 0) && errno == ESRCH)
        return 0;
    pthread_t t;
    pthread_create(&t, nullptr, [](void *a) -> void * {
        int ls = (int)(intptr_t)a;
        for (;;) {
            int fd = accept(ls, nullptr, nullptr);
            if (fd < 0)
                continue;
            pthread_t c;
            pthread_create(&c, nullptr, conn_thread, (void *)(intptr_t)fd);
            pthread_detach(c);
        }
        return nullptr;
    }, (void *)(intptr_t)s);
    pthread_detach(t);
    hlog("serving %s for DAW pid %d", sock_path, daw_pid);
    CFRunLoopRun(); // main thread: the plug-in's timers and every non-audio call
    return 0;
}

// ------------------------------------------------------------------------------- probe (diagnostics)
static int probe(const char *path, int seconds)
{
    if (!load_original(path))
        return 1;
    tirb_create_reply r{};
    Instance *in = create_instance(-1, r);
    if (!in)
        return 1;
    printf("class \"%s\": %u outputs, %u event inputs (%u ch), %u parameters, latency %u\n", g_class.name,
           r.num_outputs, r.event_inputs, r.event_channels, r.param_count, r.latency);
    for (uint32_t b = 0; b < r.num_outputs; b++)
        printf("  out %u \"%s\"\n", b, r.out_names[b]);
    const double rate = 44100;
    const int block = 512;
    on_main([&] {
        in->max_block = block;
        ProcessSetup ps{kRealtime, kSample32, block, rate};
        in->proc->setupProcessing(ps);
        for (int32 b = 0; b < in->comp->getBusCount(kAudio, kOutput); b++)
            in->comp->activateBus(kAudio, kOutput, b, b < in->out_buses);
        if (in->comp->getBusCount(kEvent, kInput) > 0)
            in->comp->activateBus(kEvent, kInput, 0, true);
        in->comp->setActive(true);
        in->proc->setProcessing(true);
    });
    // drive the process thread like the bridge would, from a timer thread; the main thread runs the run loop
    struct Run {
        Instance *in;
        int seconds, block;
        double rate;
        std::atomic<bool> done{false};
    } run{in, seconds, block, rate};
    pthread_t th;
    pthread_create(&th, nullptr, [](void *a) -> void * {
        Run &r = *(Run *)a;
        tirb_shm *s = r.in->shm;
        long blocks = (long)(r.seconds * r.rate / r.block), per_sec = (long)(r.rate / r.block);
        double t0 = CFAbsoluteTimeGetCurrent();
        float peak = 0;
        for (long i = 0; i < blocks; i++) {
            s->frames = (uint32_t)r.block;
            s->num_events = 0;
            if (i % per_sec == per_sec / 4 || i % per_sec == 3 * per_sec / 4) {
                tirb_event &e = s->events[s->num_events++];
                memset(&e, 0, sizeof e);
                bool on = i % per_sec == per_sec / 4;
                e.type = on ? Event::kNoteOnEvent : Event::kNoteOffEvent;
                if (on) {
                    NoteOnEvent n{0, 60, 0.f, 0.8f, 0, -1};
                    memcpy(e.data, &n, sizeof n);
                } else {
                    NoteOffEvent n{0, 60, 0.f, -1, 0.f};
                    memcpy(e.data, &n, sizeof n);
                }
            }
            s->transport = {ProcessContext::kPlaying | ProcessContext::kTempoValid | ProcessContext::kTimeSigValid,
                            0, r.rate, 0, 0, 0, 0, 120, i * r.block, 0, i * r.block, 4, 4};
            uint32_t want = s->req_seq + 1;
            bump_and_wake(&s->req_seq, want);
            while (__atomic_load_n(&s->done_seq, __ATOMIC_ACQUIRE) != want)
                os_sync_wait_on_address(&s->done_seq, s->done_seq, 4, OS_SYNC_WAIT_ON_ADDRESS_SHARED);
            for (int c = 0; c < 2; c++)
                for (int k = 0; k < r.block; k++)
                    peak = fmaxf(peak, fabsf(s->out[c][k]));
            if ((i + 1) % per_sec == 0) {
                printf("t=%2lds output peak %.1f dBFS\n", (i + 1) / per_sec, peak > 0 ? 20 * log10f(peak) : -999.f);
                fflush(stdout);
                peak = 0;
            }
            double due = t0 + (i + 1) * r.block / r.rate, now = CFAbsoluteTimeGetCurrent();
            if (due > now)
                usleep((useconds_t)((due - now) * 1e6));
        }
        r.done = true;
        return nullptr;
    }, &run);
    while (!run.done)
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.1, false);
    pthread_join(th, nullptr);
    destroy_instance(in);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc >= 4 && !strcmp(argv[1], "--serve"))
        return serve(argv[2], atoi(argv[3]));
    if (argc >= 3 && !strcmp(argv[1], "--probe"))
        return probe(argv[2], argc > 3 ? atoi(argv[3]) : 6);
    fprintf(stderr, "usage: vtihelper --serve <socket> <daw pid> | --probe <plugin.vst3> [seconds]\n");
    return 1;
}
