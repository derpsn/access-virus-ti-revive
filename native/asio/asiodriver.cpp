// asiodriver.cpp - "TI Revive Virus Audio" ASIO driver: the Virus as a sound card in any ASIO host (Windows), without the
// Access/Ploytec ASIO driver. It talks to the TI Revive broker like the plug-in does: the Virus' 6 input channels
// come from the broker's shared input ring, the 2 outputs go into its output ring (played towards the synth at the
// synth's rate). The synth is the clock master: a buffer switch happens whenever `buffer size` new frames have
// arrived from it, so the host never drifts against the Virus.
//
// Inputs: 1/2 Virus analog inputs, 3/4 USB 1, 5/6 USB 2 (the Virus' parts routed to USB). Outputs: 1/2 to the Virus
// outputs. 44.1 or 48 kHz, the Virus' two USB audio rates (rate following, design-rate.md step 4): setSampleRate asks
// the broker for the rate (it switches the stream only while no plug-in owns the Virus, owner.h); when the owning
// plug-in switches the stream under the driver, the host gets sampleRateDidChange and a reset request.
// Samples: 32-bit integer (ASIOSTInt32LSB).
//
// The IASIO interface is declared here from the ASIO ABI (a COM-style vtable; the CLSID doubles as the interface id,
// as every ASIO host expects). Registration: regsvr32 (DllRegisterServer) writes the COM class and
// HKLM\SOFTWARE\ASIO\TI Revive Virus Audio.
#include "device/audioin.h"
#include "device/broker.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <avrt.h>
#include <objbase.h>
#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <mutex>
#include <thread>
#include <vector>

namespace {

// ---- ASIO ABI --------------------------------------------------------------------------------------------------------
typedef long ASIOBool;
typedef long ASIOError;
typedef long ASIOSampleType;
enum : long {
    ASE_OK = 0,
    ASE_SUCCESS = 0x3f4847a0,
    ASE_NotPresent = -1000,
    ASE_HWMalfunction = -999,
    ASE_InvalidParameter = -998,
    ASE_InvalidMode = -997,
    ASE_SPNotAdvancing = -996,
    ASE_NoClock = -995,
    ASE_NoMemory = -994,
};
const ASIOSampleType ASIOSTInt32LSB = 18;
const long kAsioSelectorSupported = 1, kAsioResetRequest = 3;
struct ASIOSamples {
    unsigned long hi, lo;
};
typedef ASIOSamples ASIOTimeStamp;
struct ASIOClockSource {
    long index, associatedChannel, associatedGroup;
    ASIOBool isCurrentSource;
    char name[32];
};
struct ASIOChannelInfo {
    long channel;
    ASIOBool isInput, isActive;
    long channelGroup;
    ASIOSampleType type;
    char name[32];
};
struct ASIOBufferInfo {
    ASIOBool isInput;
    long channelNum;
    void *buffers[2];
};
struct ASIOCallbacks {
    void (*bufferSwitch)(long index, ASIOBool directProcess);
    void (*sampleRateDidChange)(double rate);
    long (*asioMessage)(long selector, long value, void *message, double *opt);
    void *(*bufferSwitchTimeInfo)(void *params, long index, ASIOBool directProcess);
};

struct IASIO : public IUnknown {
    virtual ASIOBool init(void *sysHandle) = 0;
    virtual void getDriverName(char *name) = 0;
    virtual long getDriverVersion() = 0;
    virtual void getErrorMessage(char *string) = 0;
    virtual ASIOError start() = 0;
    virtual ASIOError stop() = 0;
    virtual ASIOError getChannels(long *numInputChannels, long *numOutputChannels) = 0;
    virtual ASIOError getLatencies(long *inputLatency, long *outputLatency) = 0;
    virtual ASIOError getBufferSize(long *minSize, long *maxSize, long *preferredSize, long *granularity) = 0;
    virtual ASIOError canSampleRate(double sampleRate) = 0;
    virtual ASIOError getSampleRate(double *sampleRate) = 0;
    virtual ASIOError setSampleRate(double sampleRate) = 0;
    virtual ASIOError getClockSources(ASIOClockSource *clocks, long *numSources) = 0;
    virtual ASIOError setClockSource(long reference) = 0;
    virtual ASIOError getSamplePosition(ASIOSamples *sPos, ASIOTimeStamp *tStamp) = 0;
    virtual ASIOError getChannelInfo(ASIOChannelInfo *info) = 0;
    virtual ASIOError createBuffers(ASIOBufferInfo *bufferInfos, long numChannels, long bufferSize,
                                    ASIOCallbacks *callbacks) = 0;
    virtual ASIOError disposeBuffers() = 0;
    virtual ASIOError controlPanel() = 0;
    virtual ASIOError future(long selector, void *opt) = 0;
    virtual ASIOError outputReady() = 0;
};

// {F1DA6F3F-7A48-49A9-97F8-C3A8C008EABD}
const CLSID CLSID_VTI_ASIO = {0xf1da6f3f, 0x7a48, 0x49a9, {0x97, 0xf8, 0xc3, 0xa8, 0xc0, 0x08, 0xea, 0xbd}};
const char *DRIVER_NAME = "TI Revive Virus Audio";
const char *OLD_DRIVER_NAME = "Virus TI (native)"; // test builds before 2026-10-14
const char *CLSID_STR = "{F1DA6F3F-7A48-49A9-97F8-C3A8C008EABD}";

HINSTANCE g_module;
std::atomic<long> g_objects{0}, g_locks{0};

const int IN_CH = 6, OUT_CH = 2;
const double RATE = 44100; // before the ring is open
const char *IN_NAMES[IN_CH] = {"Analog In L", "Analog In R", "USB 1 L", "USB 1 R", "USB 2 L", "USB 2 R"};
const char *OUT_NAMES[OUT_CH] = {"Virus Out L", "Virus Out R"};
const int OUT_PREFILL = 512; // the broker starts playing with this much queued (vtibroker.c)
const int64_t START_MUTE = 4410; // frames of silence after a (re)start: the first USB block can carry a full-scale spike

void split64(uint64_t v, unsigned long &hi, unsigned long &lo)
{
    hi = (unsigned long)(v >> 32);
    lo = (unsigned long)(v & 0xFFFFFFFFu);
}

uint64_t now_ns()
{
    static LARGE_INTEGER f = [] {
        LARGE_INTEGER x;
        QueryPerformanceFrequency(&x);
        return x;
    }();
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (uint64_t)((double)c.QuadPart * 1e9 / (double)f.QuadPart);
}

// diagnostics: %LOCALAPPDATA%\virus-ti-native\asio.log, only when diag.on exists in that folder (like the plug-in)
void dlog(const char *fmt, ...)
{
    static std::string path = [] {
        char base[MAX_PATH];
        DWORD n = GetEnvironmentVariableA("LOCALAPPDATA", base, sizeof base);
        if (!n || n >= sizeof base)
            return std::string();
        std::string dir = std::string(base) + "\\virus-ti-native\\";
        return GetFileAttributesA((dir + "diag.on").c_str()) != INVALID_FILE_ATTRIBUTES ? dir + "asio.log" : std::string();
    }();
    if (path.empty())
        return;
    static std::mutex m;
    std::lock_guard<std::mutex> g(m);
    FILE *f = fopen(path.c_str(), "a");
    if (!f)
        return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(f, "%02d:%02d:%02d.%03d [%lu] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, GetCurrentProcessId());
    va_list a;
    va_start(a, fmt);
    vfprintf(f, fmt, a);
    va_end(a);
    fputc('\n', f);
    fclose(f);
}

class Driver : public IASIO {
public:
    Driver() { g_objects++; }
    virtual ~Driver()
    {
        stop();
        disposeBuffers();
        dlog("%p released", (void *)this);
        if (audio_on_)
            broker_.audio(false);
        broker_.close();
        audio_.close();
        if (ev_)
            CloseHandle(ev_);
        g_objects--;
    }

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **out) override
    {
        if (!out)
            return E_POINTER;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, CLSID_VTI_ASIO)) { // ASIO: the CLSID is the IID
            *out = static_cast<IASIO *>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        ULONG r = --refs_;
        if (!r)
            delete this;
        return r;
    }

    // IASIO
    ASIOBool init(void *) override
    {
        if (inited_)
            return 1;
        std::string err;
        broker_.set_name("TI Revive Virus Audio (ASIO)");
        if (!broker_.open(err)) { // starts vtibroker.exe on demand, like the plug-in
            error_ = "TI Revive broker not available: " + err;
            return 0;
        }
        if (!broker_.device_present()) {
            error_ = "no Virus TI connected";
            return 0;
        }
        // no wish (0): the rate that is set stays - the host's setSampleRate decides right after (review step 4)
        if (broker_.audio(true, 3000, 0) != 1 || !audio_.open()) {
            error_ = "the Virus' USB audio is not available (TI Revive WinUSB driver installed?)";
            return 0;
        }
        audio_on_ = true;
        int seen;
        audio_.rate_changed(seen); // the rate the ring was opened with is no change
        char nm[64]; // Broker::audio registered this per-process event; the broker signals it for every input block
        snprintf(nm, sizeof nm, "Local\\vtinative_audio_%lu", (unsigned long)GetCurrentProcessId());
        ev_ = OpenEventA(SYNCHRONIZE, FALSE, nm);
        inited_ = true;
        dlog("%p init: broker open, audio on", (void *)this);
        return 1;
    }
    void getDriverName(char *name) override { snprintf(name, 32, "%s", DRIVER_NAME); }
    long getDriverVersion() override { return 1; }
    void getErrorMessage(char *s) override { snprintf(s, 124, "%s", error_.c_str()); }

    ASIOError start() override
    {
        if (!callbacks_ || bufsize_ <= 0)
            return ASE_InvalidMode;
        if (running_)
            return ASE_OK;
        samples_ = 0;
        index_ = 0;
        pos_ = -1;
        running_ = true;
        dlog("%p start: buffer %ld", (void *)this, bufsize_);
        thread_ = std::thread([this] { clock(); });
        return ASE_OK;
    }
    ASIOError stop() override
    {
        if (running_)
            dlog("%p stop", (void *)this);
        running_ = false;
        if (thread_.joinable())
            thread_.join();
        return ASE_OK;
    }
    ASIOError getChannels(long *in, long *out) override
    {
        if (!inited_)
            return ASE_NotPresent;
        *in = IN_CH, *out = OUT_CH;
        return ASE_OK;
    }
    ASIOError getLatencies(long *in, long *out) override
    {
        if (!inited_)
            return ASE_NotPresent;
        long bs = bufsize_ > 0 ? bufsize_ : 256;
        *in = bs;                         // a block is handed over once it is complete
        *out = bs + OUT_PREFILL + 2 * (audio_.rate() == 48000 ? 48 : 44); // + the broker's prefill, two USB packets
        return ASE_OK;
    }
    ASIOError getBufferSize(long *mn, long *mx, long *pref, long *gran) override
    {
        *mn = 64, *mx = 2048, *pref = 256, *gran = -1; // powers of two; 256 like the original
        return ASE_OK;
    }
    ASIOError canSampleRate(double r) override { return r == 44100 || r == 48000 ? ASE_OK : ASE_NoClock; }
    ASIOError getSampleRate(double *r) override
    {
        *r = inited_ ? (double)audio_.rate() : RATE;
        return ASE_OK;
    }
    // Hosts call it while stopped. The broker decides (owner.h vti_rate_setter, one switch per second): a rate it
    // keeps is refused, so the host shows that it cannot run at it instead of playing at the wrong speed.
    ASIOError setSampleRate(double r) override
    {
        if (r == 0)
            return ASE_OK; // "external clock": the Virus is always the clock
        if (r != 44100 && r != 48000)
            return ASE_NoClock;
        if (!inited_)
            return ASE_NotPresent;
        if (audio_.rate() == (int)r)
            return ASE_OK;
        int res = broker_.audio(true, 3000, (uint32_t)r);
        if (!running_) { // while running the clock thread owns rate_changed() and resyncs itself (reset request)
            int seen;
            audio_.rate_changed(seen); // our own switch: no reset request for it
            pos_ = -1;                 // the stream restarted: resync at the next start
        }
        dlog("%p setSampleRate %.0f -> broker %d, ring %d Hz", (void *)this, r, res, audio_.rate());
        return res == 1 && audio_.rate() == (int)r ? ASE_OK : ASE_NoClock;
    }
    ASIOError getClockSources(ASIOClockSource *c, long *n) override
    {
        if (!c || !n || *n < 1)
            return ASE_InvalidParameter;
        memset(c, 0, sizeof *c);
        c->associatedChannel = -1, c->associatedGroup = -1, c->isCurrentSource = 1;
        snprintf(c->name, sizeof c->name, "Virus TI");
        *n = 1;
        return ASE_OK;
    }
    ASIOError setClockSource(long ref) override { return ref == 0 ? ASE_OK : ASE_InvalidParameter; }
    ASIOError getSamplePosition(ASIOSamples *pos, ASIOTimeStamp *ts) override
    {
        if (!running_)
            return ASE_SPNotAdvancing;
        split64(samples_.load(), pos->hi, pos->lo);
        split64(stamp_ns_.load(), ts->hi, ts->lo);
        return ASE_OK;
    }
    ASIOError getChannelInfo(ASIOChannelInfo *info) override
    {
        bool in = info->isInput != 0;
        long n = in ? IN_CH : OUT_CH;
        if (info->channel < 0 || info->channel >= n)
            return ASE_InvalidParameter;
        info->isActive = 0;
        for (auto &b : bufs_)
            if (b.isInput == info->isInput && b.channelNum == info->channel)
                info->isActive = 1;
        info->channelGroup = 0;
        info->type = ASIOSTInt32LSB;
        snprintf(info->name, sizeof info->name, "%s", in ? IN_NAMES[info->channel] : OUT_NAMES[info->channel]);
        return ASE_OK;
    }
    ASIOError createBuffers(ASIOBufferInfo *infos, long num, long size, ASIOCallbacks *cb) override
    {
        if (!inited_)
            return ASE_NotPresent;
        if (!infos || !cb || num <= 0 || size < 16 || size > 8192)
            return ASE_InvalidParameter;
        disposeBuffers();
        for (long i = 0; i < num; i++) {
            ASIOBufferInfo &b = infos[i];
            if (b.channelNum < 0 || b.channelNum >= (b.isInput ? IN_CH : OUT_CH)) {
                disposeBuffers();
                return ASE_InvalidParameter;
            }
            mem_.emplace_back(2 * (size_t)size, 0);
            b.buffers[0] = mem_.back().data();
            b.buffers[1] = mem_.back().data() + size;
            bufs_.push_back(b);
        }
        bufsize_ = size;
        callbacks_ = cb;
        return ASE_OK;
    }
    ASIOError disposeBuffers() override
    {
        stop();
        bufs_.clear();
        mem_.clear();
        callbacks_ = nullptr;
        return ASE_OK;
    }
    ASIOError controlPanel() override
    {
        MessageBoxA(nullptr,
                    "TI Revive Virus Audio (ASIO)\n\n44.1 kHz, the buffer size is set in your DAW (256 recommended).\n"
                    "Inputs: Virus analog in, USB 1, USB 2. Outputs: the Virus outputs.\n\n"
                    "The Virus is the clock master; the audio runs through the TI Revive broker.",
                    DRIVER_NAME, MB_OK | MB_ICONINFORMATION);
        return ASE_OK;
    }
    ASIOError future(long, void *) override { return ASE_InvalidParameter; }
    ASIOError outputReady() override { return ASE_NotPresent; } // outputs are taken right after bufferSwitch

private:
    std::atomic<ULONG> refs_{1};
    vti::Broker broker_;
    vti::AudioIn audio_;
    HANDLE ev_ = nullptr;
    bool inited_ = false, audio_on_ = false;
    std::string error_;
    std::vector<ASIOBufferInfo> bufs_;
    std::vector<std::vector<int32_t>> mem_;
    long bufsize_ = 0;
    ASIOCallbacks *callbacks_ = nullptr;
    std::atomic<bool> running_{false};
    std::thread thread_;
    std::atomic<uint64_t> samples_{0}, stamp_ns_{0};
    long index_ = 0;
    int64_t pos_ = -1; // next ring frame handed to the host

    // the clock: the broker signals every block it received from the Virus (~441 frames / 10 ms); each complete
    // buffer of new frames becomes one buffer switch. Outputs are queued towards the synth right after it.
    void clock()
    {
        DWORD task = 0;
        HANDLE mm = AvSetMmThreadCharacteristicsA("Pro Audio", &task);
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
        const long bs = bufsize_;
        std::vector<int16_t> raw((size_t)bs * vti::AudioIn::IN_CHANNELS), out((size_t)bs * OUT_CH);
        bool reset_sent = false;
        int64_t mute_until = 0;
        // stats per 5 s: largest gap between switches, longest host callback, output queue before each write
        uint64_t st_t0 = now_ns(), st_last = 0, gap_max = 0, cb_max = 0;
        int64_t q_min = INT64_MAX, q_max = INT64_MIN;
        long st_n = 0, st_resync = 0;
        bool can_reset = callbacks_->asioMessage &&
                         callbacks_->asioMessage(kAsioSelectorSupported, kAsioResetRequest, nullptr, nullptr) == 1;
        while (running_) {
            if (ev_)
                WaitForSingleObject(ev_, 20);
            else
                Sleep(2);
            int now_rate;
            if (audio_.rate_changed(now_rate)) { // the owning plug-in switched the stream: the host must follow
                dlog("%p the stream switched to %d Hz", (void *)this, now_rate);
                if (callbacks_->sampleRateDidChange)
                    callbacks_->sampleRateDidChange((double)now_rate);
                if (can_reset && !reset_sent) {
                    callbacks_->asioMessage(kAsioResetRequest, 0, nullptr, nullptr);
                    reset_sent = true;
                }
            }
            int64_t newest = audio_.newest_frame();
            if (newest < 0)
                continue;
            if (pos_ >= 0 && newest < pos_ && can_reset && !reset_sent) {
                // the ring started over (a new stream, the Virus came back): the host restarts the driver
                callbacks_->asioMessage(kAsioResetRequest, 0, nullptr, nullptr);
                reset_sent = true;
            }
            if (pos_ < 0 || newest - pos_ > 8192 || newest < pos_) { // start, a stall, a new stream: resync
                pos_ = newest - bs;
                mute_until = pos_ + START_MUTE;
                st_resync++;
            }
            while (running_ && newest - pos_ >= bs) {
                audio_.read_raw_at(raw.data(), bs, pos_);
                if (pos_ < mute_until)
                    memset(raw.data(), 0, raw.size() * sizeof(int16_t));
                for (auto &b : bufs_)
                    if (b.isInput) {
                        int32_t *d = (int32_t *)b.buffers[index_];
                        for (long i = 0; i < bs; i++)
                            d[i] = (int32_t)raw[(size_t)i * vti::AudioIn::IN_CHANNELS + b.channelNum] * 65536;
                    }
                uint64_t t = now_ns();
                stamp_ns_ = t;
                if (st_last && t - st_last > gap_max)
                    gap_max = t - st_last;
                st_last = t;
                callbacks_->bufferSwitch(index_, 1);
                cb_max = std::max<uint64_t>(cb_max, now_ns() - t);
                memset(out.data(), 0, out.size() * sizeof(int16_t));
                for (auto &b : bufs_)
                    if (!b.isInput) {
                        const int32_t *s = (const int32_t *)b.buffers[index_];
                        for (long i = 0; i < bs; i++) {
                            int64_t v = ((int64_t)s[i] + 32768) >> 16; // 32 -> 16 bit, rounded
                            out[(size_t)i * OUT_CH + b.channelNum] = (int16_t)(v > 32767 ? 32767 : v);
                        }
                    }
                int64_t q = audio_.out_queued();
                q_min = std::min(q_min, q), q_max = std::max(q_max, q);
                audio_.write_out(out.data(), bs);
                st_n++;
                samples_ += bs;
                pos_ += bs;
                index_ ^= 1;
            }
            if (now_ns() - st_t0 >= 5000000000ull) {
                dlog("%p %ld switches, gap max %.1f ms, callback max %.2f ms, out queued %lld..%lld, resyncs %ld",
                     (void *)this, st_n, gap_max / 1e6, cb_max / 1e6, (long long)(st_n ? q_min : 0),
                     (long long)(st_n ? q_max : 0), st_resync);
                st_t0 = now_ns(), gap_max = cb_max = 0, q_min = INT64_MAX, q_max = INT64_MIN, st_n = st_resync = 0;
            }
        }
        if (mm)
            AvRevertMmThreadCharacteristics(mm);
    }
};

class Factory : public IClassFactory {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **out) override
    {
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IClassFactory)) {
            *out = static_cast<IClassFactory *>(this);
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 2; } // static object
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown *outer, REFIID riid, void **out) override
    {
        if (outer)
            return CLASS_E_NOAGGREGATION;
        Driver *d = new (std::nothrow) Driver;
        if (!d)
            return E_OUTOFMEMORY;
        HRESULT hr = d->QueryInterface(riid, out);
        d->Release();
        return hr;
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override
    {
        lock ? g_locks++ : g_locks--;
        return S_OK;
    }
} g_factory;

LONG set_value(HKEY root, const char *key, const char *name, const char *value)
{
    return RegSetKeyValueA(root, key, name, REG_SZ, value, (DWORD)strlen(value) + 1);
}

} // namespace

// ---- DLL exports -----------------------------------------------------------------------------------------------------

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = h;
        DisableThreadLibraryCalls(h);
    }
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void **out)
{
    if (!IsEqualCLSID(clsid, CLSID_VTI_ASIO))
        return CLASS_E_CLASSNOTAVAILABLE;
    return g_factory.QueryInterface(riid, out);
}

STDAPI DllCanUnloadNow() { return g_objects == 0 && g_locks == 0 ? S_OK : S_FALSE; }

STDAPI DllRegisterServer()
{
    char path[MAX_PATH];
    if (!GetModuleFileNameA(g_module, path, sizeof path))
        return E_FAIL;
    std::string clsid = std::string("SOFTWARE\\Classes\\CLSID\\") + CLSID_STR, asio = std::string("SOFTWARE\\ASIO\\") + DRIVER_NAME;
    RegDeleteTreeA(HKEY_LOCAL_MACHINE, (std::string("SOFTWARE\\ASIO\\") + OLD_DRIVER_NAME).c_str()); // no second entry
    LONG r = set_value(HKEY_LOCAL_MACHINE, clsid.c_str(), nullptr, "TI Revive Virus Audio ASIO driver");
    r |= set_value(HKEY_LOCAL_MACHINE, (clsid + "\\InprocServer32").c_str(), nullptr, path);
    r |= set_value(HKEY_LOCAL_MACHINE, (clsid + "\\InprocServer32").c_str(), "ThreadingModel", "Apartment");
    r |= set_value(HKEY_LOCAL_MACHINE, asio.c_str(), "CLSID", CLSID_STR);
    r |= set_value(HKEY_LOCAL_MACHINE, asio.c_str(), "Description", DRIVER_NAME);
    return r == ERROR_SUCCESS ? S_OK : E_FAIL;
}

STDAPI DllUnregisterServer()
{
    std::string clsid = std::string("SOFTWARE\\Classes\\CLSID\\") + CLSID_STR, asio = std::string("SOFTWARE\\ASIO\\") + DRIVER_NAME;
    RegDeleteTreeA(HKEY_LOCAL_MACHINE, clsid.c_str());
    RegDeleteTreeA(HKEY_LOCAL_MACHINE, asio.c_str());
    RegDeleteTreeA(HKEY_LOCAL_MACHINE, (std::string("SOFTWARE\\ASIO\\") + OLD_DRIVER_NAME).c_str());
    return S_OK;
}
