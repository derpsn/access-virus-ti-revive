// tirbridge_vst3.cpp - "Virus TI (TI Revive)": the native (arm64) VST3 bridge for macOS (findings/design-macos-bridge.md).
// Runs in the natively started DAW and forwards everything to vtihelper (x86_64, under Rosetta), which hosts the
// original Intel-only Access plug-in: control over a Unix socket, audio/MIDI per block through shared memory
// (src/mac/common/tirbridge.h). Single-component VST3 like the original (component = controller), built on the
// VST3 pluginterfaces only (MIT).
//
// Build (on the Mac; P = vst3sdk/pluginterfaces):
//   clang++ -arch arm64 -std=c++17 -O2 -bundle -I vst3sdk -I src/mac/common -o "Virus TI (TI Revive)"
//     src/mac/bridge/tirbridge_vst3.cpp $P/base/funknown.cpp $P/base/coreiids.cpp -framework CoreFoundation
//   bundle layout: scripts/mac-build-bridge.sh (Contents/MacOS/<binary>, Contents/Resources/vtihelper, Info.plist)
#include <CoreFoundation/CoreFoundation.h>
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <dispatch/dispatch.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <functional>
#include <map>
#include <mutex>
#include <os/os_sync_wait_on_address.h>
#include <pthread.h>
#include <spawn.h>
#include <string>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <vector>

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "tirbridge.h"
#include "tirbridge_view.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace Steinberg {
DEF_CLASS_IID(IPluginBase)
DEF_CLASS_IID(IPluginFactory)
DEF_CLASS_IID(IPluginFactory2)
DEF_CLASS_IID(IBStream)
namespace Vst {
DEF_CLASS_IID(IComponent)
DEF_CLASS_IID(IAudioProcessor)
DEF_CLASS_IID(IEditController)
DEF_CLASS_IID(IMidiMapping)
DEF_CLASS_IID(IComponentHandler)
DEF_CLASS_IID(IEventList)
DEF_CLASS_IID(IParameterChanges)
DEF_CLASS_IID(IParamValueQueue)
} // namespace Vst
} // namespace Steinberg

// our class id (random, fixed forever: DAW projects refer to it)
static const TUID kBridgeCid = INLINE_UID(0x7A1E5C3B, 0x4D2F4E61, 0x9B0C8A17, 0x2F64D5E9);

static void blog(const char *fmt, ...)
{
    static std::mutex mx;
    std::lock_guard<std::mutex> g(mx);
    FILE *f = fopen("/tmp/tirevive-bridge.log", "a");
    if (!f)
        return;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    fprintf(f, "%ld.%03ld pid %d ", (long)ts.tv_sec % 100000, ts.tv_nsec / 1000000, getpid());
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

static void from_utf8(const char *s, TChar *out, int n) // ASCII + 2/3-byte UTF-8 -> UTF-16
{
    int k = 0;
    for (const uint8_t *p = (const uint8_t *)s; *p && k < n - 1;) {
        uint32_t c = *p++;
        if (c >= 0xe0 && p[0] && p[1])
            c = (c & 0x0f) << 12 | (p[0] & 0x3f) << 6 | (p[1] & 0x3f), p += 2;
        else if (c >= 0xc0 && p[0])
            c = (c & 0x1f) << 6 | (p[0] & 0x3f), p += 1;
        out[k++] = (TChar)c;
    }
    out[k] = 0;
}

// ------------------------------------------------------------------------------- helper connection
static std::string bundle_resources()
{
    Dl_info di{};
    dladdr((void *)&bundle_resources, &di); // .../X.vst3/Contents/MacOS/<binary>
    std::string p = di.dli_fname ? di.dli_fname : "";
    size_t k = p.rfind("/MacOS/");
    return k == std::string::npos ? "" : p.substr(0, k) + "/Resources";
}

static int connect_sock(const char *path)
{
    struct sockaddr_un sa = {};
    sa.sun_family = AF_UNIX;
    snprintf(sa.sun_path, sizeof sa.sun_path, "%s", path);
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (connect(fd, (struct sockaddr *)&sa, sizeof sa) == 0)
        return fd;
    close(fd);
    return -1;
}

// connect to this DAW's helper, starting it if needed (spawned by us so the DAW's Microphone permission covers it)
static int helper_connect()
{
    static std::mutex mx;
    std::lock_guard<std::mutex> g(mx);
    char sock[104];
    snprintf(sock, sizeof sock, TIRB_SOCK_FMT, (unsigned)getuid(), (int)getpid());
    int fd = connect_sock(sock);
    if (fd >= 0)
        return fd;
    std::string helper = bundle_resources() + "/vtihelper";
    char pid[16];
    snprintf(pid, sizeof pid, "%d", (int)getpid());
    const char *argv[] = {helper.c_str(), "--serve", sock, pid, nullptr};
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 2, "/tmp/tirevive-helper.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
    pid_t child;
    int e = posix_spawn(&child, helper.c_str(), &fa, nullptr, (char *const *)argv, nullptr);
    posix_spawn_file_actions_destroy(&fa);
    if (e) {
        blog("posix_spawn %s: %s", helper.c_str(), strerror(e));
        return -1;
    }
    blog("helper started (pid %d): %s", (int)child, helper.c_str());
    pthread_t t; // reap the helper when it exits (no zombie in the DAW)
    pthread_create(&t, nullptr, [](void *p) -> void * {
        int st;
        waitpid((pid_t)(intptr_t)p, &st, 0);
        return nullptr;
    }, (void *)(intptr_t)child);
    pthread_detach(t);
    for (int i = 0; i < 300; i++) { // up to 30 s: Rosetta translation + loading the original on first start
        usleep(100000);
        if ((fd = connect_sock(sock)) >= 0)
            return fd;
    }
    blog("helper did not come up");
    return -1;
}

struct Conn { // one control connection per bridge instance
    int fd = -1;
    std::mutex wr, mx;
    std::condition_variable cv;
    uint32_t seq = 0;
    std::map<uint32_t, std::vector<uint8_t>> replies;
    std::function<void(const tirb_msg &, const std::vector<uint8_t> &)> on_notify;
    pthread_t reader{};

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
    bool start()
    {
        if ((fd = helper_connect()) < 0)
            return false;
        int one = 1;
        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
        pthread_create(&reader, nullptr, [](void *a) -> void * {
            ((Conn *)a)->read_loop();
            return nullptr;
        }, this);
        return true;
    }
    void read_loop()
    {
        for (;;) {
            tirb_msg h;
            if (io_all(fd, &h, sizeof h, false))
                break;
            std::vector<uint8_t> b(h.len);
            if (h.len && io_all(fd, b.data(), h.len, false))
                break;
            if (h.type & TIRB_REPLY) {
                std::lock_guard<std::mutex> g(mx);
                replies[h.seq] = std::move(b);
                cv.notify_all();
            } else if (on_notify)
                on_notify(h, b);
        }
        blog("helper connection closed");
        std::lock_guard<std::mutex> g(mx);
        fd = -1;
        cv.notify_all();
    }
    bool post(uint32_t type, uint32_t inst, const void *p, uint32_t len, uint32_t seq = 0)
    {
        tirb_msg h{type, inst, seq, len};
        std::lock_guard<std::mutex> g(wr);
        return fd >= 0 && !io_all(fd, &h, sizeof h, true) && (!len || !io_all(fd, (void *)p, len, true));
    }
    // request/response; returns false on a dead helper or timeout
    bool call(uint32_t type, uint32_t inst, const void *p, uint32_t len, std::vector<uint8_t> *out = nullptr,
              int timeout_s = 30)
    {
        uint32_t s;
        {
            std::lock_guard<std::mutex> g(mx);
            s = ++seq;
        }
        if (!post(type, inst, p, len, s))
            return false;
        std::unique_lock<std::mutex> l(mx);
        if (!cv.wait_for(l, std::chrono::seconds(timeout_s), [&] { return replies.count(s) || fd < 0; }) ||
            !replies.count(s))
            return false;
        if (out)
            *out = std::move(replies[s]);
        replies.erase(s);
        return true;
    }
    void stop()
    {
        if (fd >= 0)
            shutdown(fd, SHUT_RDWR);
        if (reader)
            pthread_join(reader, nullptr);
        if (fd >= 0)
            close(fd);
        fd = -1;
    }
};

// ------------------------------------------------------------------------------- the plug-in
class Bridge : public IComponent, public IAudioProcessor, public IEditController, public IMidiMapping, public EditorLink {
  public:
    std::atomic<uint32> refs{1};
    Conn conn;
    uint32_t inst = 0;
    tirb_create_reply layout{};
    tirb_shm *shm = nullptr;
    std::vector<tirb_param> params;
    std::map<ParamID, int32> param_index;
    std::vector<std::atomic<double>> values; // last known normalized values (parallel to params)
    std::map<uint32_t, ParamID> midimap;     // (channel << 16 | controller) -> param id
    IComponentHandler *handler = nullptr;
    bool active = false, ok = false;
    double rate = 44100;
    int32 max_block = 512;
    uint32_t seq = 0;
    unsigned long misses = 0;
    std::vector<bool> out_active;

    virtual ~Bridge() = default;

    // FUnknown
    tresult PLUGIN_API queryInterface(const TUID iid, void **obj) override
    {
#define QI(I, P)                                                                                                       \
    if (FUnknownPrivate::iidEqual(iid, I::iid)) {                                                                      \
        *obj = static_cast<I *>(P);                                                                                    \
        addRef();                                                                                                      \
        return kResultOk;                                                                                              \
    }
        QI(FUnknown, static_cast<IComponent *>(this))
        QI(IPluginBase, static_cast<IComponent *>(this))
        QI(IComponent, this)
        QI(IAudioProcessor, this)
        QI(IEditController, this)
        QI(IMidiMapping, this)
#undef QI
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return ++refs; }
    uint32 PLUGIN_API release() override
    {
        uint32 r = --refs;
        if (!r)
            delete this;
        return r;
    }

    // IPluginBase
    tresult PLUGIN_API initialize(FUnknown *) override
    {
        conn.on_notify = [this](const tirb_msg &h, const std::vector<uint8_t> &b) { notification(h, b); };
        if (!conn.start())
            return kResultFalse;
        uint32_t hello[2] = {TIRB_VERSION, (uint32_t)getpid()};
        std::vector<uint8_t> r;
        if (!conn.call(TIRB_HELLO, 0, hello, sizeof hello, &r) || r.size() < 4)
            return kResultFalse;
        if (!conn.call(TIRB_CREATE, 0, nullptr, 0, &r, 60) || r.size() < sizeof layout) {
            blog("CREATE failed");
            return kResultFalse;
        }
        memcpy(&layout, r.data(), sizeof layout);
        // instance id comes back in the reply header; the helper also puts it in shm_name ("/tirb-<pid>-<id>")
        inst = (uint32_t)atoi(strrchr(layout.shm_name, '-') + 1);
        int fd = shm_open(layout.shm_name, O_RDWR, 0600);
        if (fd < 0)
            return kResultFalse;
        void *m = mmap(nullptr, sizeof(tirb_shm), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        close(fd);
        if (m == MAP_FAILED)
            return kResultFalse;
        shm = (tirb_shm *)m;
        seq = shm->req_seq;
        // parameters, in chunks
        for (uint32_t first = 0;;) {
            uint32_t q[2] = {first, 512};
            if (!conn.call(TIRB_PARAM_INFO, inst, q, sizeof q, &r) || r.size() < 4)
                break;
            uint32_t total;
            memcpy(&total, r.data(), 4);
            size_t n = (r.size() - 4) / sizeof(tirb_param);
            const tirb_param *p = (const tirb_param *)(r.data() + 4);
            params.insert(params.end(), p, p + n);
            first += (uint32_t)n;
            if (!n || first >= total)
                break;
        }
        values = std::vector<std::atomic<double>>(params.size());
        for (size_t i = 0; i < params.size(); i++) {
            param_index[params[i].id] = (int32)i;
            values[i] = params[i].default_value;
        }
        if (conn.call(TIRB_MIDI_MAPPING, inst, nullptr, 0, &r) && r.size() >= 4) {
            uint32_t n;
            memcpy(&n, r.data(), 4);
            const tirb_midimap *mm = (const tirb_midimap *)(r.data() + 4);
            for (uint32_t i = 0; i < n && 4 + (i + 1) * sizeof(tirb_midimap) <= r.size(); i++)
                midimap[(uint32_t)mm[i].channel << 16 | (uint16_t)mm[i].controller] = mm[i].id;
        }
        out_active.assign(layout.num_outputs, false);
        ok = true;
        blog("instance %u: %u outputs, %zu parameters, %zu MIDI mappings", inst, layout.num_outputs, params.size(),
             midimap.size());
        return kResultOk;
    }
    tresult PLUGIN_API terminate() override
    {
        if (ok)
            conn.call(TIRB_DESTROY, inst, nullptr, 0, nullptr, 10);
        conn.stop();
        if (shm)
            munmap(shm, sizeof(tirb_shm));
        shm = nullptr;
        ok = false;
        blog("instance %u terminated (%lu late blocks)", inst, misses);
        return kResultOk;
    }

    // IComponent
    tresult PLUGIN_API getControllerClassId(TUID cid) override
    {
        memcpy(cid, kBridgeCid, sizeof(TUID)); // single component
        return kResultOk;
    }
    tresult PLUGIN_API setIoMode(IoMode) override { return kResultOk; }
    int32 PLUGIN_API getBusCount(MediaType type, BusDirection dir) override
    {
        if (type == kAudio)
            return dir == kOutput ? (int32)layout.num_outputs : 0;
        return dir == kInput && layout.event_inputs ? 1 : 0;
    }
    tresult PLUGIN_API getBusInfo(MediaType type, BusDirection dir, int32 index, BusInfo &bus) override
    {
        if (type == kAudio && dir == kOutput && index >= 0 && index < (int32)layout.num_outputs) {
            bus.mediaType = kAudio;
            bus.direction = kOutput;
            bus.channelCount = 2;
            from_utf8(layout.out_names[index], bus.name, 128);
            bus.busType = index == 0 ? kMain : kAux;
            bus.flags = index == 0 ? BusInfo::kDefaultActive : 0;
            return kResultOk;
        }
        if (type == kEvent && dir == kInput && index == 0 && layout.event_inputs) {
            bus.mediaType = kEvent;
            bus.direction = kInput;
            bus.channelCount = (int32)layout.event_channels;
            from_utf8("Midi In", bus.name, 128);
            bus.busType = kMain;
            bus.flags = BusInfo::kDefaultActive;
            return kResultOk;
        }
        return kInvalidArgument;
    }
    tresult PLUGIN_API getRoutingInfo(RoutingInfo &, RoutingInfo &) override { return kNotImplemented; }
    tresult PLUGIN_API activateBus(MediaType type, BusDirection dir, int32 index, TBool state) override
    {
        if (type == kAudio && dir == kOutput && index >= 0 && index < (int32)out_active.size())
            out_active[index] = state;
        return kResultOk;
    }
    tresult PLUGIN_API setActive(TBool state) override
    {
        uint32_t on = state ? 1 : 0;
        std::vector<uint8_t> r;
        conn.call(TIRB_ACTIVE, inst, &on, 4, &r);
        active = state;
        return kResultOk;
    }
    tresult PLUGIN_API setState(IBStream *s) override
    {
        std::vector<uint8_t> data, r;
        char buf[65536];
        int32 got = 0;
        while (s && s->read(buf, sizeof buf, &got) == kResultOk && got > 0)
            data.insert(data.end(), buf, buf + got);
        if (!conn.call(TIRB_SET_STATE, inst, data.data(), (uint32_t)data.size(), &r, 60))
            return kResultFalse;
        refresh_values();
        return kResultOk;
    }
    tresult PLUGIN_API getState(IBStream *s) override
    {
        std::vector<uint8_t> r;
        if (!conn.call(TIRB_GET_STATE, inst, nullptr, 0, &r, 60))
            return kResultFalse;
        int32 wrote = 0;
        return s->write(r.data(), (int32)r.size(), &wrote);
    }

    // IAudioProcessor
    tresult PLUGIN_API setBusArrangements(SpeakerArrangement *, int32 nin, SpeakerArrangement *out, int32 nout) override
    {
        if (nin != 0 || nout != (int32)layout.num_outputs)
            return kResultFalse;
        for (int32 i = 0; i < nout; i++)
            if (out[i] != SpeakerArr::kStereo)
                return kResultFalse;
        return kResultOk;
    }
    tresult PLUGIN_API getBusArrangement(BusDirection dir, int32 index, SpeakerArrangement &arr) override
    {
        if (dir != kOutput || index < 0 || index >= (int32)layout.num_outputs)
            return kInvalidArgument;
        arr = SpeakerArr::kStereo;
        return kResultOk;
    }
    tresult PLUGIN_API canProcessSampleSize(int32 s) override { return s == kSample32 ? kResultOk : kResultFalse; }
    uint32 PLUGIN_API getLatencySamples() override { return layout.latency; }
    tresult PLUGIN_API setupProcessing(ProcessSetup &setup) override
    {
        rate = setup.sampleRate;
        max_block = std::min<int32>(setup.maxSamplesPerBlock, TIRB_MAX_BLOCK);
        tirb_setup su{setup.sampleRate, max_block, setup.processMode};
        std::vector<uint8_t> r;
        conn.call(TIRB_SETUP, inst, &su, sizeof su, &r);
        return kResultOk;
    }
    tresult PLUGIN_API setProcessing(TBool) override { return kResultOk; }
    uint32 PLUGIN_API getTailSamples() override { return kInfiniteTail; }

    tresult PLUGIN_API process(ProcessData &d) override
    {
        // parameter-only calls (numSamples 0) still reach the helper so automation is applied
        tirb_shm *s = shm;
        int32 frames = std::min<int32>(d.numSamples, TIRB_MAX_BLOCK);
        if (!s || !ok) {
            silence(d);
            return kResultOk;
        }
        s->frames = (uint32_t)frames;
        uint32_t ne = 0;
        if (d.inputEvents)
            for (int32 i = 0, n = d.inputEvents->getEventCount(); i < n && ne < TIRB_MAX_EVENTS; i++) {
                Event e;
                if (d.inputEvents->getEvent(i, e) != kResultOk)
                    continue;
                if (e.type == Event::kDataEvent || e.type == Event::kNoteExpressionTextEvent ||
                    e.type == Event::kChordEvent || e.type == Event::kScaleEvent)
                    continue; // events with pointers: not bridged (SysEx from the DAW is not used by the Virus TI)
                tirb_event &te = s->events[ne++];
                te.bus = e.busIndex;
                te.sample_offset = e.sampleOffset;
                te.type = e.type;
                te.flags = e.flags;
                memcpy(te.data, &e.noteOn, sizeof te.data);
            }
        s->num_events = ne;
        uint32_t np = 0;
        if (d.inputParameterChanges)
            for (int32 i = 0, n = d.inputParameterChanges->getParameterCount(); i < n; i++) {
                IParamValueQueue *q = d.inputParameterChanges->getParameterData(i);
                if (!q)
                    continue;
                ParamID id = q->getParameterId();
                for (int32 k = 0, m = q->getPointCount(); k < m && np < TIRB_MAX_PCHANGES; k++) {
                    int32 off;
                    ParamValue v;
                    if (q->getPoint(k, off, v) == kResultOk) {
                        s->pchanges[np++] = {id, off, v};
                        set_value(id, v);
                    }
                }
            }
        s->num_pchanges = np;
        tirb_transport &t = s->transport;
        memset(&t, 0, sizeof t);
        if (const ProcessContext *c = d.processContext) {
            t.state = c->state;
            t.sample_rate = c->sampleRate;
            t.project_time_samples = c->projectTimeSamples;
            t.system_time = c->systemTime;
            t.continous_time_samples = c->continousTimeSamples;
            t.project_time_music = c->projectTimeMusic;
            t.bar_position_music = c->barPositionMusic;
            t.cycle_start_music = c->cycleStartMusic;
            t.cycle_end_music = c->cycleEndMusic;
            t.tempo = c->tempo;
            t.time_sig_num = c->timeSigNumerator;
            t.time_sig_den = c->timeSigDenominator;
        }
        // hand the block to the helper and wait for it, at most about one block duration (never stall the DAW)
        uint32_t want = ++seq;
        __atomic_store_n(&s->req_seq, want, __ATOMIC_RELEASE);
        os_sync_wake_by_address_any(&s->req_seq, 4, OS_SYNC_WAKE_BY_ADDRESS_SHARED);
        uint64_t budget_ns = (uint64_t)(std::max<int32>(frames, 64) / rate * 1e9) + 2000000;
        uint64_t until = clock_gettime_nsec_np(CLOCK_UPTIME_RAW) + budget_ns;
        uint32_t done;
        while ((done = __atomic_load_n(&s->done_seq, __ATOMIC_ACQUIRE)) != want) {
            uint64_t now = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
            if (now >= until)
                break;
            os_sync_wait_on_address_with_timeout(&s->done_seq, done, 4, OS_SYNC_WAIT_ON_ADDRESS_SHARED,
                                                 OS_CLOCK_MACH_ABSOLUTE_TIME, until - now);
        }
        if (done != want) {
            if (!(misses++ % 100))
                blog("instance %u: block %u late (%lu so far)", inst, want, misses);
            silence(d);
            return kResultOk;
        }
        for (int32 b = 0; b < d.numOutputs && b < (int32)layout.num_outputs; b++) {
            AudioBusBuffers &o = d.outputs[b];
            for (int32 c = 0; c < o.numChannels && c < 2; c++)
                if (o.channelBuffers32 && o.channelBuffers32[c])
                    memcpy(o.channelBuffers32[c], s->out[b * 2 + c], frames * sizeof(float));
            o.silenceFlags = 0;
        }
        if (d.outputParameterChanges)
            for (uint32_t i = 0; i < s->num_out_pchanges && i < 64; i++) {
                int32 idx;
                if (IParamValueQueue *q = d.outputParameterChanges->addParameterData(s->out_pchanges[i].id, idx))
                    q->addPoint(s->out_pchanges[i].sample_offset, s->out_pchanges[i].value, idx);
            }
        return kResultOk;
    }
    static void silence(ProcessData &d)
    {
        for (int32 b = 0; b < d.numOutputs; b++) {
            AudioBusBuffers &o = d.outputs[b];
            for (int32 c = 0; c < o.numChannels; c++)
                if (o.channelBuffers32 && o.channelBuffers32[c])
                    memset(o.channelBuffers32[c], 0, d.numSamples * sizeof(float));
            o.silenceFlags = ~0ull;
        }
    }

    // IEditController
    tresult PLUGIN_API setComponentState(IBStream *) override { return kResultOk; } // single component
    int32 PLUGIN_API getParameterCount() override { return (int32)params.size(); }
    tresult PLUGIN_API getParameterInfo(int32 index, ParameterInfo &info) override
    {
        if (index < 0 || index >= (int32)params.size())
            return kInvalidArgument;
        const tirb_param &p = params[index];
        info.id = p.id;
        from_utf8(p.title, info.title, 128);
        from_utf8(p.short_title, info.shortTitle, 128);
        from_utf8(p.units, info.units, 128);
        info.stepCount = p.step_count;
        info.defaultNormalizedValue = p.default_value;
        info.unitId = p.unit_id;
        info.flags = p.flags;
        return kResultOk;
    }
    tresult PLUGIN_API getParamStringByValue(ParamID id, ParamValue v, String128 out) override
    {
        uint8_t q[12];
        memcpy(q, &id, 4);
        memcpy(q + 4, &v, 8);
        std::vector<uint8_t> r;
        if (!conn.call(TIRB_PARAM_STRING, inst, q, sizeof q, &r, 5) || r.empty() || !r[0])
            return kResultFalse;
        r.push_back(0);
        from_utf8((const char *)r.data(), out, 128);
        return kResultOk;
    }
    tresult PLUGIN_API getParamValueByString(ParamID, TChar *, ParamValue &) override { return kNotImplemented; }
    ParamValue PLUGIN_API normalizedParamToPlain(ParamID, ParamValue v) override { return v; }
    ParamValue PLUGIN_API plainParamToNormalized(ParamID, ParamValue v) override { return v; }
    ParamValue PLUGIN_API getParamNormalized(ParamID id) override
    {
        auto it = param_index.find(id);
        return it == param_index.end() ? 0 : values[it->second].load();
    }
    tresult PLUGIN_API setParamNormalized(ParamID id, ParamValue v) override
    {
        set_value(id, v);
        uint8_t q[12];
        memcpy(q, &id, 4);
        memcpy(q + 4, &v, 8);
        conn.post(TIRB_SET_PARAM, inst, q, sizeof q);
        return kResultOk;
    }
    tresult PLUGIN_API setComponentHandler(IComponentHandler *h) override
    {
        if (h)
            h->addRef();
        if (handler)
            handler->release();
        handler = h;
        return kResultOk;
    }
    IPlugView *PLUGIN_API createView(FIDString name) override
    {
        if (!ok || (name && strcmp(name, ViewType::kEditor)) || view)
            return nullptr;
        view = bridge_create_view(this);
        return view;
    }

    // EditorLink (called by the view on the DAW's main thread)
    IPlugView *view = nullptr;
    bool editor_open(double x, double y, double scale, tirb_editor_reply &rep) override
    {
        double q[3] = {x, y, scale};
        std::vector<uint8_t> r;
        if (!conn.call(TIRB_EDITOR_OPEN, inst, q, sizeof q, &r, 30) || r.size() < sizeof rep)
            return false;
        memcpy(&rep, r.data(), sizeof rep);
        return rep.width > 0 && rep.pixels_shm[0];
    }
    void editor_close() override { conn.call(TIRB_EDITOR_CLOSE, inst, nullptr, 0, nullptr, 10); }
    void editor_move(double x, double y) override
    {
        double q[2] = {x, y};
        conn.post(TIRB_EDITOR_MOVE, inst, q, sizeof q);
    }
    void editor_input(const tirb_input &in) override { conn.post(TIRB_EDITOR_EVENT, inst, &in, sizeof in); }
    void editor_view_gone() override { view = nullptr; }

    // IMidiMapping
    tresult PLUGIN_API getMidiControllerAssignment(int32 bus, int16 channel, CtrlNumber cc, ParamID &id) override
    {
        if (bus != 0)
            return kResultFalse;
        auto it = midimap.find((uint32_t)channel << 16 | (uint16_t)cc);
        if (it == midimap.end())
            return kResultFalse;
        id = it->second;
        return kResultOk;
    }

  private:
    void set_value(ParamID id, double v)
    {
        auto it = param_index.find(id);
        if (it != param_index.end())
            values[it->second] = v;
    }
    void refresh_values() // after setState: pull the current values (cheap enough: done once per preset load)
    {
        for (size_t i = 0; i < params.size(); i++) {
            std::vector<uint8_t> r;
            if (params[i].flags & ParameterInfo::kIsHidden)
                continue;
            if (conn.call(TIRB_GET_PARAM, inst, &params[i].id, 4, &r, 5) && r.size() >= 8) {
                double v;
                memcpy(&v, r.data(), 8);
                values[i] = v;
            }
        }
    }
    void notification(const tirb_msg &h, const std::vector<uint8_t> &b) // reader thread
    {
        if (h.type == TIRB_N_PARAM_EDIT && b.size() >= sizeof(tirb_param_edit)) {
            tirb_param_edit e;
            memcpy(&e, b.data(), sizeof e);
            if (e.what == 1)
                set_value(e.id, e.value);
            addRef();
            auto *ctx = new std::pair<Bridge *, tirb_param_edit>(this, e);
            dispatch_async_f(dispatch_get_main_queue(), ctx, [](void *p) { // the DAW expects edits on its UI thread
                auto *c = (std::pair<Bridge *, tirb_param_edit> *)p;
                if (IComponentHandler *hd = c->first->handler) {
                    if (c->second.what == 0)
                        hd->beginEdit(c->second.id);
                    else if (c->second.what == 1)
                        hd->performEdit(c->second.id, c->second.value);
                    else
                        hd->endEdit(c->second.id);
                }
                c->first->release();
                delete c;
            });
        } else if (h.type == TIRB_N_EDITOR_RESIZE && b.size() >= sizeof(tirb_editor_reply)) {
            tirb_editor_reply rep;
            memcpy(&rep, b.data(), sizeof rep);
            if (view)
                bridge_view_resized(view, rep);
        } else if (h.type == TIRB_N_RESTART && b.size() >= 4) {
            int32 flags;
            memcpy(&flags, b.data(), 4);
            addRef();
            auto *ctx = new std::pair<Bridge *, int32>(this, flags);
            dispatch_async_f(dispatch_get_main_queue(), ctx, [](void *p) {
                auto *c = (std::pair<Bridge *, int32> *)p;
                if (c->first->handler)
                    c->first->handler->restartComponent(c->second);
                c->first->release();
                delete c;
            });
        }
    }
};

// ------------------------------------------------------------------------------- factory and module entry
class Factory : public IPluginFactory2 {
  public:
    tresult PLUGIN_API queryInterface(const TUID iid, void **obj) override
    {
        if (FUnknownPrivate::iidEqual(iid, IPluginFactory2::iid) || FUnknownPrivate::iidEqual(iid, IPluginFactory::iid) ||
            FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {
            *obj = this;
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
    tresult PLUGIN_API getFactoryInfo(PFactoryInfo *info) override
    {
        memset(info, 0, sizeof *info);
        strcpy(info->vendor, "TI Revive");
        strcpy(info->url, "https://github.com/derpsn/access-virus-ti-revive");
        info->flags = PFactoryInfo::kUnicode;
        return kResultOk;
    }
    int32 PLUGIN_API countClasses() override { return 1; }
    tresult PLUGIN_API getClassInfo(int32 i, PClassInfo *info) override
    {
        if (i != 0)
            return kInvalidArgument;
        memset(info, 0, sizeof *info);
        memcpy(info->cid, kBridgeCid, sizeof(TUID));
        info->cardinality = PClassInfo::kManyInstances;
        strcpy(info->category, kVstAudioEffectClass);
        strcpy(info->name, "Virus TI (TI Revive)");
        return kResultOk;
    }
    tresult PLUGIN_API getClassInfo2(int32 i, PClassInfo2 *info) override
    {
        if (i != 0)
            return kInvalidArgument;
        memset(info, 0, sizeof *info);
        memcpy(info->cid, kBridgeCid, sizeof(TUID));
        info->cardinality = PClassInfo::kManyInstances;
        strcpy(info->category, kVstAudioEffectClass);
        strcpy(info->name, "Virus TI (TI Revive)");
        strcpy(info->subCategories, "Instrument|Synth");
        strcpy(info->vendor, "TI Revive");
        strcpy(info->version, "0.5.0");
        strcpy(info->sdkVersion, kVstVersionString);
        return kResultOk;
    }
    tresult PLUGIN_API createInstance(FIDString cid, FIDString iid, void **obj) override
    {
        if (!FUnknownPrivate::iidEqual(cid, kBridgeCid)) {
            *obj = nullptr;
            return kInvalidArgument;
        }
        Bridge *b = new Bridge;
        tresult r = b->queryInterface(iid, obj);
        b->release();
        return r;
    }
};

extern "C" {
__attribute__((visibility("default"))) bool bundleEntry(CFBundleRef) { return true; }
__attribute__((visibility("default"))) bool bundleExit() { return true; }
__attribute__((visibility("default"))) IPluginFactory *GetPluginFactory()
{
    static Factory f;
    return &f;
}
}
