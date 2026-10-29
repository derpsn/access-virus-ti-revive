// audioin.cpp - see audioin.h.
#include "audioin.h"
extern "C" {
#include "vtiaudioshm.h"
}
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX // windows.h's min/max macros break std::min / std::max
#endif
#include <windows.h>
#endif
#ifdef _MSC_VER
#include <intrin.h>
#endif
#ifdef __linux__
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#ifdef __APPLE__
#include <CoreAudio/CoreAudio.h>
#include "gapfill.h"
#include "ratemeter.h"
#include "ratesw.h"
#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#endif

namespace vti {

#define shm_ ((vti_audio_shm *)map_)

// the broker publishes in_frames after the data (release); read it with acquire semantics
static inline int64_t load_acquire(volatile int64_t *p)
{
#ifdef _MSC_VER
    int64_t v = *p; // aligned 64-bit loads are atomic on x64; the barrier keeps later reads after it
    _ReadWriteBarrier();
    return v;
#else
    return __atomic_load_n(p, __ATOMIC_ACQUIRE);
#endif
}

AudioIn::~AudioIn() { close(); }

#ifdef __APPLE__
// macOS: the Virus' audio function is USB class compliant, so Core Audio offers it as an input device. The broker only
// carries MIDI there; this reader fills the same ring (vti_audio_shm, here in our own memory) from an IOProc, so the
// rest of AudioIn (drift handling, read_at) is shared with Linux and Windows.
struct MacAudio {
    AudioObjectID dev = kAudioObjectUnknown;
    AudioDeviceIOProcID proc = nullptr;
    vti_audio_shm *ring = nullptr;
    bool playing = false; // the output towards the Virus runs (IOProc thread)
    GapFill gaps;         // skipped IO cycles become silence, so ring frames stay the Virus' frames (E243)
    vti_rate_guard guard{}; // at most one rate switch per second (ratesw.h, rule 2; E112: flips can hang the Virus)
    vti::RateMeter meter;   // the rate the frames really arrive at (E377)
    vti_relock relock{};    // away and back when that is the other rate, three tries per stream
    uintptr_t id = 0;       // key in g_macs (the rate listener's user data)
};

// Rate following on macOS (design-rate.md step 4): Core Audio owns the Virus' USB audio, so the plug-in sets the
// device's nominal rate itself - only for its own stream start or resume, debounced - and follows changes by others.
static const AudioObjectPropertyAddress kRateAddr{kAudioDevicePropertyNominalSampleRate,
                                                  kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};

static Float64 nominal_rate(AudioObjectID dev)
{
    Float64 r = 0;
    UInt32 sz = sizeof r;
    AudioObjectGetPropertyData(dev, &kRateAddr, 0, nullptr, &sz, &r);
    return r;
}

static uint32_t now_ms()
{
    using namespace std::chrono;
    return (uint32_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// set the nominal rate and wait (up to 1 s) until the device reports it: the change is asynchronous
static bool set_nominal_rate(MacAudio *m, Float64 r)
{
    vti_rate_switched(&m->guard, now_ms());
    if (AudioObjectSetPropertyData(m->dev, &kRateAddr, 0, nullptr, sizeof r, &r))
        return false;
    for (int i = 0; i < 100; i++) {
        if (nominal_rate(m->dev) == r)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

// Open readers by id, for the rate listener: Core Audio may still run a callback while the listener is removed, so
// the callback finds its reader here under the mutex (or not at all) instead of using a pointer that close() freed.
// The mutex also serialises ring_rate() between the listener and request_rate() (one rate_gen step per change).
static std::mutex g_mac_mx;
static std::map<uintptr_t, MacAudio *> g_macs;
static uintptr_t g_mac_next = 1;

// the ring learns the device's rate (rate, then rate_gen; readers re-anchor on a new generation, vtiaudioshm.h);
// g_mac_mx held
static void ring_rate(MacAudio *m)
{
    const uint32_t v = nominal_rate(m->dev) == 48000 ? 48000 : 44100;
    if (m->ring->rate == v)
        return;
    m->gaps.set_rate((int)v);
    m->ring->rate = v;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    __atomic_add_fetch(&m->ring->rate_gen, 1, __ATOMIC_SEQ_CST);
}

// another program (Audio MIDI Setup, a second DAW) changed the Virus' rate: follow it instead of playing at the wrong
// speed; never switched back from here (E112)
static OSStatus rate_listener(AudioObjectID, UInt32, const AudioObjectPropertyAddress *, void *user)
{
    std::lock_guard<std::mutex> g(g_mac_mx);
    auto it = g_macs.find((uintptr_t)user);
    if (it != g_macs.end())
        ring_rate(it->second);
    return noErr;
}

// one switch of our own: our IOProc stops first (no audio while the device changes clock), then the rate, read back,
// then it runs again (request_rate, check_rate)
static void switch_rate(MacAudio *m, uint32_t want)
{
    AudioDeviceStop(m->dev, m->proc);
    if (!set_nominal_rate(m, want))
        fprintf(stderr, "audioin: the Virus did not take %u Hz (now %.0f)\n", want, nominal_rate(m->dev));
    {
        std::lock_guard<std::mutex> g(g_mac_mx);
        ring_rate(m);
    }
    AudioDeviceStart(m->dev, m->proc);
}

static std::string cf_string(AudioObjectID id, AudioObjectPropertySelector sel)
{
    AudioObjectPropertyAddress a{sel, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    CFStringRef cs = nullptr;
    UInt32 sz = sizeof cs;
    if (AudioObjectGetPropertyData(id, &a, 0, nullptr, &sz, &cs) || !cs)
        return {};
    char buf[256] = {};
    CFStringGetCString(cs, buf, sizeof buf, kCFStringEncodingUTF8);
    CFRelease(cs);
    return buf;
}

static int input_channels(AudioObjectID id)
{
    AudioObjectPropertyAddress a{kAudioDevicePropertyStreamConfiguration, kAudioObjectPropertyScopeInput,
                                 kAudioObjectPropertyElementMain};
    UInt32 sz = 0;
    if (AudioObjectGetPropertyDataSize(id, &a, 0, nullptr, &sz) || !sz)
        return 0;
    std::vector<uint8_t> buf(sz);
    auto *bl = (AudioBufferList *)buf.data();
    if (AudioObjectGetPropertyData(id, &a, 0, nullptr, &sz, bl))
        return 0;
    int n = 0;
    for (UInt32 i = 0; i < bl->mNumberBuffers; i++)
        n += (int)bl->mBuffers[i].mNumberChannels;
    return n;
}

// the Virus as Core Audio input device: name contains "Virus", at least 6 input channels (E082 order)
static AudioObjectID find_virus()
{
    AudioObjectPropertyAddress a{kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal,
                                 kAudioObjectPropertyElementMain};
    UInt32 sz = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &a, 0, nullptr, &sz))
        return kAudioObjectUnknown;
    std::vector<AudioObjectID> ids(sz / sizeof(AudioObjectID));
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, nullptr, &sz, ids.data()))
        return kAudioObjectUnknown;
    for (AudioObjectID id : ids)
        if (cf_string(id, kAudioObjectPropertyName).find("Virus") != std::string::npos &&
            input_channels(id) >= VTI_AUDIO_IN_CH)
            return id;
    return kAudioObjectUnknown;
}

// the Virus' 2 output channels (its USB input in "3 Outs / 1 Input": the plug-in's Sidechain bus, E167) from the
// ring's output part, by the broker's rules on Linux/Windows: start with 512 frames queued, at most 2048 ahead,
// silence when it runs dry. Our IOProc's output is mixed by Core Audio with other programs' output to the Virus.
static void play_out(MacAudio *m, AudioBufferList *out)
{
    if (!out || !out->mNumberBuffers || !out->mBuffers[0].mNumberChannels)
        return;
    vti_audio_shm *r = m->ring;
    const UInt32 frames = out->mBuffers[0].mDataByteSize / (sizeof(float) * out->mBuffers[0].mNumberChannels);
    int64_t w = __atomic_load_n(&r->out_write, __ATOMIC_ACQUIRE), rd = r->out_read, avail = w - rd;
    if (avail < 0 || avail > VTI_AUDIO_RING) { // the writer restarted or is far ahead: resynchronise
        rd = w;
        avail = 0;
        m->playing = false;
    }
    if (!m->playing && avail >= 512)
        m->playing = true;
    if (m->playing && avail > 2048) {
        rd = w - 512;
        avail = 512;
    }
    const int64_t n = m->playing ? std::min<int64_t>(avail, frames) : 0;
    int ch = 0;
    for (UInt32 b = 0; b < out->mNumberBuffers; b++) {
        AudioBuffer &ab = out->mBuffers[b];
        auto *dst = (float *)ab.mData;
        for (UInt32 f = 0; f < frames && dst; f++)
            for (UInt32 c = 0; c < ab.mNumberChannels; c++) {
                const int k = ch + (int)c;
                dst[f * ab.mNumberChannels + c] =
                    f < n && k < VTI_AUDIO_OUT_CH
                        ? r->out[((rd + f) & (VTI_AUDIO_RING - 1)) * VTI_AUDIO_OUT_CH + k] / 32768.0f
                        : 0.0f;
            }
        ch += (int)ab.mNumberChannels;
    }
    if (m->playing && n < frames)
        m->playing = false; // ran dry: refill before playing again
    __atomic_store_n(&r->out_read, rd + n, __ATOMIC_RELEASE);
}

// Core Audio delivers Float32 (interleaved or one buffer per stream); the ring holds int16 frames of 6 channels
static OSStatus io_proc(AudioObjectID, const AudioTimeStamp *, const AudioBufferList *in,
                        const AudioTimeStamp *in_time, AudioBufferList *outl, const AudioTimeStamp *, void *user)
{
    auto *m = (MacAudio *)user;
    auto *r = m->ring;
    play_out(m, outl);
    if (!in || !in->mNumberBuffers)
        return noErr;
    UInt32 frames = in->mBuffers[0].mNumberChannels
                        ? in->mBuffers[0].mDataByteSize / (sizeof(float) * in->mBuffers[0].mNumberChannels)
                        : 0;
    int64_t base = r->in_frames;
    // IO cycles Core Audio skipped (processor overload, e.g. during an export) are only visible in the device's sample
    // time: their frames become silence, so a ring frame stays a Virus frame and the stamps stay right (gapfill.h)
    if (in_time && (in_time->mFlags & kAudioTimeStampSampleTimeValid)) {
        const int64_t gap = m->gaps.update(in_time->mSampleTime, base);
        for (int64_t f = 0; f < gap && f < VTI_AUDIO_RING; f++)
            memset(r->in + ((base + f) & (VTI_AUDIO_RING - 1)) * VTI_AUDIO_IN_CH, 0,
                   VTI_AUDIO_IN_CH * sizeof(int16_t));
        base += gap;
    }
    for (UInt32 f = 0; f < frames; f++) {
        int16_t *dst = r->in + ((base + f) & (VTI_AUDIO_RING - 1)) * VTI_AUDIO_IN_CH;
        int ch = 0;
        for (UInt32 b = 0; b < in->mNumberBuffers && ch < VTI_AUDIO_IN_CH; b++) {
            const AudioBuffer &ab = in->mBuffers[b];
            const float *src = (const float *)ab.mData + f * ab.mNumberChannels;
            for (UInt32 c = 0; c < ab.mNumberChannels && ch < VTI_AUDIO_IN_CH; c++, ch++) {
                float v = src[c] * 32768.0f;
                dst[ch] = (int16_t)(v > 32767.0f ? 32767 : v < -32768.0f ? -32768 : v);
            }
        }
    }
    __atomic_store_n(&r->in_frames, base + frames, __ATOMIC_RELEASE);
    return noErr;
}
#endif

bool AudioIn::open(const char *linux_name, int rate)
{
    (void)rate; // macOS only: Linux and Windows ask the broker (Broker::audio)
    rate_gen_seen_ = -1;
    (void)linux_name; // Linux only
#ifdef __linux__
    char name[64];
    snprintf(name, sizeof name, VTI_AUDIO_SHM_LINUX, (unsigned)getuid());
    int fd = shm_open(linux_name ? linux_name : name, O_RDWR, 0);
    if (fd < 0)
        return false;
    // the broker's ring: ours and whole. One made by another user, or shorter than the layout (reading its header
    // raised SIGBUS in the host), is not used (review 2026-10-19 H4)
    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_uid != getuid() || st.st_size < (off_t)sizeof(vti_audio_shm)) {
        ::close(fd);
        return false;
    }
    void *p = mmap(nullptr, sizeof(vti_audio_shm), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    ::close(fd);
    if (p == MAP_FAILED)
        return false;
    map_ = p;
    if (shm_->magic != VTI_AUDIO_MAGIC || shm_->ring_frames != VTI_AUDIO_RING || shm_->in_ch != VTI_AUDIO_IN_CH) {
        close();
        return false;
    }
    pos_ = -1;
    return true;
#elif defined(_WIN32)
    HANDLE m = OpenFileMappingA(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, VTI_AUDIO_SHM_NAME);
    if (!m)
        return false;
    void *p = MapViewOfFile(m, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(vti_audio_shm));
    CloseHandle(m); // the view keeps the mapping alive
    if (!p)
        return false;
    map_ = p;
    if (shm_->magic != VTI_AUDIO_MAGIC || shm_->ring_frames != VTI_AUDIO_RING || shm_->in_ch != VTI_AUDIO_IN_CH) {
        close();
        return false;
    }
    pos_ = -1;
    return true;
#elif defined(__APPLE__)
    auto *m = new MacAudio;
    m->dev = find_virus();
    if (m->dev == kAudioObjectUnknown) {
        delete m;
        return false;
    }
    // the Virus at the wanted rate (44.1 or 48 kHz; the clock master is the synth) - set only when it differs
    const Float64 want = vti_rate_valid((uint32_t)rate);
    if (nominal_rate(m->dev) != want && !set_nominal_rate(m, want))
        fprintf(stderr, "audioin: the Virus did not take %.0f Hz (now %.0f)\n", want, nominal_rate(m->dev));
    m->ring = new vti_audio_shm();
    m->ring->magic = VTI_AUDIO_MAGIC, m->ring->version = VTI_AUDIO_VERSION;
    m->ring->rate = nominal_rate(m->dev) == 48000 ? 48000 : 44100;
    m->gaps.set_rate((int)m->ring->rate);
    m->ring->in_ch = VTI_AUDIO_IN_CH, m->ring->out_ch = VTI_AUDIO_OUT_CH, m->ring->ring_frames = VTI_AUDIO_RING;
    if (AudioDeviceCreateIOProcID(m->dev, io_proc, m, &m->proc) || AudioDeviceStart(m->dev, m->proc)) {
        if (m->proc)
            AudioDeviceDestroyIOProcID(m->dev, m->proc);
        delete m->ring;
        delete m;
        return false;
    }
    m->ring->streaming = 1;
    {
        std::lock_guard<std::mutex> g(g_mac_mx);
        m->id = g_mac_next++;
        g_macs[m->id] = m;
    }
    AudioObjectAddPropertyListener(m->dev, &kRateAddr, rate_listener, (void *)m->id);
    mac_ = m;
    map_ = m->ring;
    pos_ = -1;
    return true;
#else
    return false;
#endif
}

void AudioIn::close()
{
    rate_gen_seen_ = -1;
#ifdef __linux__
    if (map_)
        munmap(map_, sizeof(vti_audio_shm));
#elif defined(_WIN32)
    if (map_)
        UnmapViewOfFile(map_);
#elif defined(__APPLE__)
    if (auto *m = (MacAudio *)mac_) {
        AudioObjectRemovePropertyListener(m->dev, &kRateAddr, rate_listener, (void *)m->id);
        {
            std::lock_guard<std::mutex> g(g_mac_mx); // a callback still running finishes first, later ones find nothing
            g_macs.erase(m->id);
        }
        AudioDeviceStop(m->dev, m->proc);
        AudioDeviceDestroyIOProcID(m->dev, m->proc);
        delete m->ring;
        delete m;
        mac_ = nullptr;
    }
#endif
    map_ = nullptr;
}

bool AudioIn::streaming() const { return shm_ && shm_->streaming; }

int64_t AudioIn::filled() const
{
#ifdef __APPLE__
    return mac_ ? ((MacAudio *)mac_)->gaps.filled() : 0;
#else
    return 0; // the broker fills USB gaps itself (vtiusbaudio_libusb.c, "stream stalled")
#endif
}

int64_t AudioIn::newest_frame() const { return streaming() ? load_acquire(&shm_->in_frames) : -1; }

int AudioIn::rate() const
{
    const uint32_t r = shm_ ? shm_->rate : 0;
    return r == 48000 ? 48000 : 44100;
}

int AudioIn::request_rate(int rate)
{
#ifdef __APPLE__
    auto *m = (MacAudio *)mac_;
    const uint32_t want = vti_rate_valid((uint32_t)rate);
    if (!m || m->ring->rate == want || !vti_rate_may_switch(&m->guard, now_ms()))
        return this->rate();
    switch_rate(m, want);
#else
    (void)rate;
#endif
    return this->rate();
}

int AudioIn::check_rate(uint32_t now, uint32_t &to)
{
    to = 0;
#ifdef __APPLE__
    auto *m = (MacAudio *)mac_;
    if (!m)
        return VTI_RELOCK_NONE;
    const bool lost = m->meter.other_rate(newest_frame(), now, (int)m->ring->rate);
    const int step = vti_relock_step(&m->relock, lost, m->ring->rate, now, &to);
    if (step == VTI_RELOCK_AWAY || step == VTI_RELOCK_BACK)
        switch_rate(m, to); // a real switch: the same rate again would change nothing (E331)
    return step;
#else
    (void)now;
    return 0; // VTI_RELOCK_NONE: the broker relocks the stream itself
#endif
}

bool AudioIn::rate_changed(int &rate)
{
    if (!shm_)
        return false;
    const int64_t gen = shm_->rate_gen;
    const bool changed = rate_gen_seen_ >= 0 && gen != rate_gen_seen_;
    rate_gen_seen_ = gen;
    if (changed)
        rate = this->rate();
    return changed;
}

bool AudioIn::raw_peaks(int frames, int peak[6]) const
{
    for (int c = 0; c < 6; c++)
        peak[c] = 0;
    int64_t newest = newest_frame();
    if (newest < 0)
        return false;
    frames = std::min<int64_t>(frames, std::min<int64_t>(newest, VTI_AUDIO_RING - 1024));
    for (int64_t f = newest - frames; f < newest; f++) {
        const int16_t *fr = shm_->in + (f & (VTI_AUDIO_RING - 1)) * VTI_AUDIO_IN_CH;
        for (int c = 0; c < 6 && c < VTI_AUDIO_IN_CH; c++)
            peak[c] = std::max(peak[c], std::abs((int)fr[c]));
    }
    return true;
}

void AudioIn::read_at(float *l1, float *r1, float *l2, float *r2, int n, int64_t pos, float *l3, float *r3)
{
    int64_t newest = streaming() ? load_acquire(&shm_->in_frames) : -1;
    const float k = 1.0f / 32768.0f;
    const bool third = l3 && r3;
    for (int i = 0; i < n; i++) {
        int64_t f = pos + i;
        if (newest < 0 || f >= newest || f < newest - VTI_AUDIO_RING + 1024) {
            l1[i] = r1[i] = l2[i] = r2[i] = 0;
            if (third)
                l3[i] = r3[i] = 0;
            continue;
        }
        const int16_t *fr = shm_->in + (f & (VTI_AUDIO_RING - 1)) * VTI_AUDIO_IN_CH;
        l1[i] = fr[2] * k, r1[i] = fr[3] * k, l2[i] = fr[4] * k, r2[i] = fr[5] * k;
        if (third)
            l3[i] = fr[0] * k, r3[i] = fr[1] * k;
    }
}

void AudioIn::read(float *l1, float *r1, float *l2, float *r2, int n, int latency, float *l3, float *r3)
{
    const bool third = l3 && r3;
    auto silence = [&](int from) {
        for (int i = from; i < n; i++) {
            l1[i] = r1[i] = l2[i] = r2[i] = 0;
            if (third)
                l3[i] = r3[i] = 0;
        }
    };
    if (!streaming()) {
        silence(0);
        return;
    }
    int64_t newest = load_acquire(&shm_->in_frames);
    int64_t fill = newest - pos_;
    // (re)anchor when starting, after an underrun or when far behind (DAW stalled): far = a quarter ring beyond the
    // target and one block, or so close to the ring size that the synth would overwrite what this block reads. Half
    // the ring was the limit before: with 4096-frame blocks the target alone (2 * 4096 + 256) exceeded it and every
    // block jumped (E187)
    const int64_t far_limit = std::min<int64_t>((int64_t)latency + n + VTI_AUDIO_RING / 4, VTI_AUDIO_RING - n);
    if (pos_ < 0 || fill < n || fill > far_limit) {
        if (pos_ >= 0)
            drops_++;
        pos_ = newest - latency;
        fill = latency;
    }
    // drift: one frame per block at most, keeps the distance within +-128 frames of the target
    int step_back = 0;
    if (fill > latency + 128)
        pos_++;
    else if (fill < latency - 128)
        step_back = 1;
    const float k = 1.0f / 32768.0f;
    const int16_t *in = shm_->in;
    for (int i = 0; i < n; i++) {
        int64_t f = pos_ + i - (step_back && i > 0 ? 1 : 0);
        const int16_t *fr = in + (f & (VTI_AUDIO_RING - 1)) * VTI_AUDIO_IN_CH;
        l1[i] = fr[2] * k, r1[i] = fr[3] * k, l2[i] = fr[4] * k, r2[i] = fr[5] * k;
        if (third)
            l3[i] = fr[0] * k, r3[i] = fr[1] * k;
    }
    pos_ += n - step_back;
}

void AudioIn::read_raw_at(int16_t *dst, int n, int64_t pos) const
{
    int64_t newest = newest_frame();
    for (int i = 0; i < n; i++) {
        int64_t f = pos + i;
        if (!map_ || newest < 0 || f >= newest || f < newest - VTI_AUDIO_RING || f < 0)
            memset(dst + i * VTI_AUDIO_IN_CH, 0, VTI_AUDIO_IN_CH * sizeof(int16_t));
        else
            memcpy(dst + i * VTI_AUDIO_IN_CH, shm_->in + (f & (VTI_AUDIO_RING - 1)) * VTI_AUDIO_IN_CH,
                   VTI_AUDIO_IN_CH * sizeof(int16_t));
    }
}

void AudioIn::write_out(const int16_t *stereo, int n)
{
    if (!map_ || shm_->version < 2)
        return;
    int64_t w = shm_->out_write;
    for (int i = 0; i < n; i++)
        memcpy(&shm_->out[((w + i) & (VTI_AUDIO_RING - 1)) * VTI_AUDIO_OUT_CH], stereo + i * VTI_AUDIO_OUT_CH,
               VTI_AUDIO_OUT_CH * sizeof(int16_t));
#ifdef _MSC_VER
    _ReadWriteBarrier();
    *(volatile int64_t *)&shm_->out_write = w + n; // x64: the data stores are visible before this one
#else
    __atomic_store_n(&shm_->out_write, w + n, __ATOMIC_RELEASE);
#endif
}

int64_t AudioIn::out_written() const { return map_ ? load_acquire(&shm_->out_write) : -1; }

int64_t AudioIn::out_queued() const
{
    if (!map_)
        return 0;
    return load_acquire(&shm_->out_write) - load_acquire(&shm_->out_read);
}

} // namespace vti
