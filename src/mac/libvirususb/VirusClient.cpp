// VirusClient.cpp - TI Revive's replacement for Access' libVirusUSB.dylib on macOS (findings/design-macos.md,
// E093-E096). Virus Control.bundle links "/Library/Application Support/Access Music/Virus TI/Common/libVirusUSB.dylib"
// and calls these VirusClient methods; the class itself (vtable, typeinfo, data members) is defined in Virus Control,
// so only the non-virtual member functions are provided here, with the exact signatures (= the same mangled names).
//
// Calls are logged to /tmp/tirevive-libvirususb.log. Device presence, device info and USB control requests go
// through vtibroker (Unix socket /tmp/vtibroker-<uid>.sock, vtiproto.h); MIDI goes through the broker's stand-in for
// the Access MIDI driver (src/broker/vtimidi_mac.c). Audio: an IOProc on Apple's class-compliant "Virus TI" Core
// Audio device (AppleUSBAudio, 6 in / 2 out) feeds the client callback at vtable +0x18 like Access' HAL did (E102).
//
// Build (on the Mac, x86_64 for the Rosetta process that hosts the Intel-only Access plug-in):
//   clang++ -arch x86_64 -dynamiclib -std=c++17 -O2 -install_name \
//     "/Library/Application Support/Access Music/Virus TI/Common/libVirusUSB.dylib" -o libVirusUSB.dylib \
//     VirusClient.cpp -framework CoreAudio -framework CoreFoundation
#include <CoreAudio/CoreAudio.h>
#include <cerrno>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <pthread.h>
#include <map>
#include <unistd.h>

// Types as used in the mangled names of the Access library (sizes from the decompiled code)
enum EReqDirection : int {};
enum EReqLevel : int {};
enum EReqRecipient : int {};
enum EReqRequest : int {};
enum E_VC_INT_PARAM : int {};
struct VC_DeviceInfo;       // 0x214 bytes: u16, u8, u16 x3, u8 x4, u32, char[256] x2 (E094)
struct VC_ClientInfoList;

class VirusClient {
  public:
    int vc_init();
    int vc_term();
    int vc_open(void *deviceID);
    int vc_close();
    bool vc_isOpen();
    int vc_start(unsigned long framesize);
    int vc_stop();
    unsigned long vc_getNumDevices();
    void *vc_getDeviceID(unsigned long index);
    void *vc_getCurrentDeviceID();
    bool vc_getDeviceInfo(unsigned long index, VC_DeviceInfo &info);
    bool vc_request(EReqDirection dir, EReqLevel level, EReqRecipient recipient, EReqRequest request,
                    unsigned short value, unsigned short index, char *data, unsigned short length);
    unsigned long vc_getBuffersize();
    unsigned long vc_getSamplerate();
    bool vc_setSamplerate(unsigned long rate);
    bool vc_getLatencies(unsigned long &in, unsigned long &out);
    long vc_getIntParam(E_VC_INT_PARAM param);
    long vc_setIntParam(E_VC_INT_PARAM param, long value);
    bool vc_isPluginMaster();
    unsigned long vc_getNumOfHubs();
    VC_ClientInfoList *vc_getClientInfo();
    void vc_freeClientInfo(VC_ClientInfoList *list);
    long vc_getConfigStatus() const;
    unsigned long vc_getLibraryVersion() const;
    unsigned long vc_getAudioDriverVersion() const;
    unsigned long vc_getHostInterfaceVersion() const;
    unsigned long vc_getBandwidthAvailable() const;
};

static void vlog(const char *fmt, ...)
{
    static pthread_mutex_t mx = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&mx);
    FILE *f = fopen("/tmp/tirevive-libvirususb.log", "a");
    if (f) {
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
    pthread_mutex_unlock(&mx);
}

// ------------------------------------------------------------------------------------------- broker client
// Messages (src/broker/vtiproto.h): header {u16 type, u16 len} + payload.
enum { MSG_HELLO = 1, MSG_CONTROL = 3, MSG_RX = 0x81, MSG_CONTROL_REPLY = 0x83, MSG_STATE = 0x84 };

static int g_fd = -1;
static volatile int g_connected;           // broker reports the Virus open
static pthread_mutex_t g_wr = PTHREAD_MUTEX_INITIALIZER, g_ctl = PTHREAD_MUTEX_INITIALIZER;
// The reply fields hold one transaction. Keep concurrent callers from replacing
// its sequence or payload before the waiting caller consumes the response.
static std::mutex g_control_transaction;
static pthread_cond_t g_ctl_cv = PTHREAD_COND_INITIALIZER;
static uint32_t g_ctl_seq, g_ctl_done_seq;
static int32_t g_ctl_result;
static uint8_t g_ctl_buf[4096];

static int io_all(int fd, void *p, size_t n, int wr)
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

static int send_msg(uint16_t type, const void *a, int alen, const void *b, int blen)
{
    uint16_t h[2] = {type, (uint16_t)(alen + blen)};
    pthread_mutex_lock(&g_wr);
    int r = g_fd < 0 || io_all(g_fd, h, 4, 1) || (alen && io_all(g_fd, (void *)a, alen, 1)) ||
            (blen && io_all(g_fd, (void *)b, blen, 1));
    pthread_mutex_unlock(&g_wr);
    return r;
}

static void *reader(void *)
{
    uint8_t buf[4096];
    for (;;) {
        uint16_t h[2];
        if (io_all(g_fd, h, 4, 0) || h[1] > sizeof buf || io_all(g_fd, buf, h[1], 0))
            break;
        if (h[0] == MSG_STATE && h[1] >= 4) {
            g_connected = buf[0] != 0;
            vlog("broker: device %s", g_connected ? "connected" : "disconnected");
        } else if (h[0] == MSG_CONTROL_REPLY && h[1] >= 8) {
            pthread_mutex_lock(&g_ctl);
            memcpy(&g_ctl_done_seq, buf, 4);
            memcpy(&g_ctl_result, buf + 4, 4);
            memcpy(g_ctl_buf, buf + 8, h[1] - 8);
            pthread_cond_broadcast(&g_ctl_cv);
            pthread_mutex_unlock(&g_ctl);
        }
        // MSG_RX (EP 0x85/0x86 data): relayed to Virus Control's MIDI path in a later step
    }
    vlog("broker connection lost");
    g_connected = 0;
    close(g_fd);
    g_fd = -1;
    return nullptr;
}

static int broker_connect()
{
    if (g_fd >= 0)
        return 0;
    struct sockaddr_un sa = {};
    sa.sun_family = AF_UNIX;
    snprintf(sa.sun_path, sizeof sa.sun_path, "/tmp/vtibroker-%u.sock", (unsigned)getuid());
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (connect(fd, (struct sockaddr *)&sa, sizeof sa)) {
        close(fd);
        return -1; // TODO: start the broker on demand (LaunchAgent)
    }
    g_fd = fd;
    uint32_t ver = 2;
    send_msg(MSG_HELLO, &ver, 4, nullptr, 0);
    pthread_t t;
    pthread_create(&t, nullptr, reader, nullptr);
    pthread_detach(t);
    for (int i = 0; i < 50 && !g_connected; i++)
        usleep(10000);
    vlog("connected to vtibroker (device %s)", g_connected ? "present" : "absent");
    return 0;
}

// USB control request through the broker; returns bytes transferred or -1
static int broker_control(uint8_t type, uint8_t req, uint16_t value, uint16_t index, uint8_t *data, uint16_t len)
{
    std::lock_guard<std::mutex> transaction(g_control_transaction);
    if (broker_connect())
        return -1;
    uint8_t hdr[12];
    pthread_mutex_lock(&g_ctl);
    uint32_t seq = ++g_ctl_seq;
    pthread_mutex_unlock(&g_ctl);
    memcpy(hdr, &seq, 4);
    hdr[4] = type, hdr[5] = req, hdr[6] = value & 0xff, hdr[7] = value >> 8, hdr[8] = index & 0xff, hdr[9] = index >> 8;
    hdr[10] = len & 0xff, hdr[11] = len >> 8;
    if (send_msg(MSG_CONTROL, hdr, 12, (type & 0x80) ? nullptr : data, (type & 0x80) ? 0 : len))
        return -1;
    struct timespec until;
    clock_gettime(CLOCK_REALTIME, &until);
    until.tv_sec += 3;
    pthread_mutex_lock(&g_ctl);
    while (g_ctl_done_seq != seq && pthread_cond_timedwait(&g_ctl_cv, &g_ctl, &until) == 0)
        ;
    int r = g_ctl_done_seq == seq ? g_ctl_result : -1;
    if (r > 0 && (type & 0x80))
        memcpy(data, g_ctl_buf, r < len ? r : len);
    pthread_mutex_unlock(&g_ctl);
    return r;
}

static char g_device; // the one device: its address is the opaque device ID handed to Virus Control
static int g_open;

__attribute__((constructor)) static void loaded() { vlog("TI Revive libVirusUSB loaded"); }

int VirusClient::vc_init() { vlog("%p vc_init", this); return 1; }
int VirusClient::vc_term() { vlog("%p vc_term", this); return 1; }
// ------------------------------------------------------------------------------------------- audio
// Client callback (Virus Control VirusDeviceObject vtable +0x18, FUN_000ec434; E102):
//   (this, frames, in channels, in bytes, in data, out channels, out bytes, out data, sample time)
// Access' HAL passed the frame count where Core Audio passes the device ID. Samples are int32 interleaved, 6 in
// (channels 2/3 audio, 4/5 the Virus' sync timestamps) and 2 out; the Virus must run at 44.1 kHz (E070).
typedef void (*audio_cb)(VirusClient *, unsigned long frames, unsigned long in_ch, unsigned long in_bytes,
                         const int32_t *in, unsigned long out_ch, unsigned long out_bytes, int32_t *out,
                         unsigned long long sample_time);
enum { IN_CH = 6, OUT_CH = 2, MAX_FRAMES = 8192 };

struct audio_client {
    VirusClient *vc;
    AudioDeviceIOProcID proc = nullptr;
    unsigned long calls = 0, peak = 0; // diagnostics, logged at vc_stop
    bool running = false;
    int32_t in[MAX_FRAMES * IN_CH], out[MAX_FRAMES * OUT_CH];
};
static std::map<VirusClient *, audio_client *> g_clients;
static pthread_mutex_t g_clients_mx = PTHREAD_MUTEX_INITIALIZER;
static AudioObjectID g_dev;

static AudioObjectID find_virus_device()
{
    AudioObjectPropertyAddress a = {kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal,
                                    kAudioObjectPropertyElementMain};
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &a, 0, nullptr, &size))
        return 0;
    AudioObjectID ids[64];
    size = size > sizeof ids ? sizeof ids : size;
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, nullptr, &size, ids))
        return 0;
    for (UInt32 i = 0; i < size / sizeof(AudioObjectID); i++) {
        CFStringRef name = nullptr;
        UInt32 n = sizeof name;
        AudioObjectPropertyAddress na = {kAudioObjectPropertyName, kAudioObjectPropertyScopeGlobal,
                                         kAudioObjectPropertyElementMain};
        if (AudioObjectGetPropertyData(ids[i], &na, 0, nullptr, &n, &name) || !name)
            continue;
        char buf[128] = "";
        CFStringGetCString(name, buf, sizeof buf, kCFStringEncodingUTF8);
        CFRelease(name);
        if (!strcmp(buf, "Virus TI"))
            return ids[i];
    }
    return 0;
}

template <class T> static bool dev_get(AudioObjectPropertySelector sel, AudioObjectPropertyScope scope, T &v)
{
    AudioObjectPropertyAddress a = {sel, scope, kAudioObjectPropertyElementMain};
    UInt32 n = sizeof v;
    return g_dev && !AudioObjectGetPropertyData(g_dev, &a, 0, nullptr, &n, &v);
}
template <class T> static bool dev_set(AudioObjectPropertySelector sel, T v)
{
    AudioObjectPropertyAddress a = {sel, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
    return g_dev && !AudioObjectSetPropertyData(g_dev, &a, 0, nullptr, sizeof v, &v);
}

static OSStatus ioproc(AudioObjectID, const AudioTimeStamp *now, const AudioBufferList *inl, const AudioTimeStamp *intime,
                       AudioBufferList *outl, const AudioTimeStamp *, void *user)
{
    (void)now;
    audio_client *c = (audio_client *)user;
    if (!inl || !inl->mNumberBuffers || !intime)
        return 0;
    // gather the device's float input (one interleaved buffer or several) into int32 frames
    UInt32 frames = 0, ch = 0;
    for (UInt32 b = 0; b < inl->mNumberBuffers; b++) {
        const AudioBuffer &ab = inl->mBuffers[b];
        if (!ab.mNumberChannels || !ab.mData)
            continue;
        UInt32 f = ab.mDataByteSize / (ab.mNumberChannels * 4);
        if (!frames)
            frames = f > MAX_FRAMES ? MAX_FRAMES : f;
        const float *src = (const float *)ab.mData;
        for (UInt32 k = 0; k < ab.mNumberChannels && ch < IN_CH; k++, ch++)
            for (UInt32 i = 0; i < frames; i++) {
                float x = src[i * ab.mNumberChannels + k] * 2147483648.0f;
                c->in[i * IN_CH + ch] = x >= 2147483647.0f ? INT32_MAX : x <= -2147483648.0f ? INT32_MIN : (int32_t)x;
            }
    }
    for (; ch < IN_CH; ch++)
        for (UInt32 i = 0; i < frames; i++)
            c->in[i * IN_CH + ch] = 0;
    if (!frames)
        return 0;
    if (!c->calls++)
        vlog("%p   first IOProc: %u buffers, buffer 0 %u ch %u bytes, %u frames", c->vc, inl->mNumberBuffers,
             inl->mBuffers[0].mNumberChannels, inl->mBuffers[0].mDataByteSize, frames);
    for (UInt32 i = 0; i < frames; i++) { // input peak of the audio pair (device channels 2/3)
        int32_t a = c->in[i * IN_CH + 2], b = c->in[i * IN_CH + 3];
        unsigned long m = (unsigned long)(a < 0 ? -(int64_t)a : a) | (unsigned long)(b < 0 ? -(int64_t)b : b);
        if (m > c->peak)
            c->peak = m;
    }
    audio_cb cb = (audio_cb)(*(void ***)c->vc)[3];
    cb(c->vc, frames, IN_CH, frames * IN_CH * 4, c->in, OUT_CH, frames * OUT_CH * 4, c->out,
       (unsigned long long)intime->mSampleTime);
    // the plug-in's 2 output channels to the device (summed into Core Audio's output, not exclusive)
    if (outl)
        for (UInt32 b = 0, och = 0; b < outl->mNumberBuffers && och < OUT_CH; b++) {
            AudioBuffer &ab = outl->mBuffers[b];
            float *dst = (float *)ab.mData;
            UInt32 f = ab.mDataByteSize / (ab.mNumberChannels * 4);
            f = f < frames ? f : frames;
            for (UInt32 k = 0; k < ab.mNumberChannels && och < OUT_CH; k++, och++)
                for (UInt32 i = 0; i < f; i++)
                    dst[i * ab.mNumberChannels + k] = c->out[i * OUT_CH + och] * (1.0f / 2147483648.0f);
        }
    return 0;
}

int VirusClient::vc_open(void *id)
{
    if (id != &g_device || !g_connected) {
        vlog("%p vc_open %p -> 0 (no device)", this, id);
        return 0;
    }
    if (!g_dev)
        g_dev = find_virus_device();
    pthread_mutex_lock(&g_clients_mx);
    audio_client *c = g_clients.count(this) ? g_clients[this] : (g_clients[this] = new audio_client);
    c->vc = this;
    OSStatus st = 0;
    if (g_dev && !c->proc)
        st = AudioDeviceCreateIOProcID(g_dev, ioproc, c, &c->proc);
    pthread_mutex_unlock(&g_clients_mx);
    g_open = 1;
    vlog("%p vc_open %p -> 1 (Core Audio device %u, IOProc %s)", this, id, g_dev, c->proc ? "ok" : "FAILED");
    if (st)
        vlog("%p   AudioDeviceCreateIOProcID: %d", this, (int)st);
    return 1;
}
int VirusClient::vc_close()
{
    vlog("%p vc_close", this);
    vc_stop();
    pthread_mutex_lock(&g_clients_mx);
    if (g_clients.count(this)) {
        audio_client *c = g_clients[this];
        if (c->proc)
            AudioDeviceDestroyIOProcID(g_dev, c->proc);
        g_clients.erase(this);
        delete c;
    }
    pthread_mutex_unlock(&g_clients_mx);
    return 1;
}
bool VirusClient::vc_isOpen() { return g_open; }
int VirusClient::vc_start(unsigned long fs)
{
    pthread_mutex_lock(&g_clients_mx);
    audio_client *c = g_clients.count(this) ? g_clients[this] : nullptr;
    pthread_mutex_unlock(&g_clients_mx);
    if (!c || !c->proc) {
        vlog("%p vc_start framesize %lu -> no device", this, fs);
        return 0xbad10; // as the original when the stream does not start
    }
    if (fs)
        dev_set(kAudioDevicePropertyBufferFrameSize, (UInt32)fs);
    OSStatus st = c->running ? 0 : AudioDeviceStart(g_dev, c->proc);
    c->running = !st;
    Float64 rate = 0;
    dev_get(kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal, rate);
    vlog("%p vc_start framesize %lu -> %s (device at %.0f Hz)", this, fs, st ? "FAILED" : "running", rate);
    return st ? 0xbad10 : 0;
}
int VirusClient::vc_stop()
{
    pthread_mutex_lock(&g_clients_mx);
    audio_client *c = g_clients.count(this) ? g_clients[this] : nullptr;
    pthread_mutex_unlock(&g_clients_mx);
    if (c && c->running) {
        AudioDeviceStop(g_dev, c->proc);
        c->running = false;
        vlog("%p vc_stop after %lu IOProc calls, input peak (ch 2/3) 0x%08lx", this, c->calls, c->peak);
        c->calls = c->peak = 0;
        return 1;
    }
    vlog("%p vc_stop", this);
    return 1;
}
unsigned long VirusClient::vc_getNumDevices()
{
    broker_connect();
    static int last = -1;
    if (g_connected != last) // the plug-in polls this constantly: log changes only
        vlog("%p vc_getNumDevices -> %d", this, g_connected);
    last = g_connected;
    return g_connected ? 1 : 0;
}
void *VirusClient::vc_getDeviceID(unsigned long i)
{
    vlog("%p vc_getDeviceID %lu", this, i);
    return i == 0 && g_connected ? &g_device : nullptr;
}
void *VirusClient::vc_getCurrentDeviceID() { vlog("%p vc_getCurrentDeviceID", this); return g_open ? &g_device : nullptr; }
bool VirusClient::vc_getDeviceInfo(unsigned long i, VC_DeviceInfo &info)
{
    vlog("%p vc_getDeviceInfo %lu", this, i);
    if (i != 0 || !g_connected)
        return false;
    // layout (E094, E099): +4 idVendor, +6 idProduct, +8 bcdDevice, +0x10 u32 key (locationID on the original),
    // +0x14 vendor string, +0x114 product string. Virus Control picks the model from product + bcdDevice
    // ("Virus TI"/"VIRUS TI" 0x100 = TI, 0x300 = TI2, "Virus TI Snow" 0x100 = Snow); anything else is an
    // "Unknown Device" whose model pointer stays NULL and crashes the plug-in in initialize (E099).
    uint8_t *p = (uint8_t *)&info;
    memset(p, 0, 0x214);
    static uint8_t dd[18];
    static uint16_t vid = 0x133e, pid = 0x0815, rel = 0x0300; // Virus TI2 defaults
    if (!dd[0] && broker_control(0x80, 6, 0x0100, 0, dd, sizeof dd) >= 14) { // GET_DESCRIPTOR(device), once
        vid = (uint16_t)(dd[8] | dd[9] << 8);
        pid = (uint16_t)(dd[10] | dd[11] << 8);
        rel = (uint16_t)(dd[12] | dd[13] << 8);
    }
    uint32_t key = 0x00100000; // = VTI_MAC_DEVKEY (src/broker/vtimidi_mac.h): pairs the device with MIDI slot 0
    memcpy(p + 4, &vid, 2);
    memcpy(p + 6, &pid, 2);
    memcpy(p + 8, &rel, 2);
    memcpy(p + 0x10, &key, 4);
    strcpy((char *)p + 0x14, "Access Music");
    strcpy((char *)p + 0x114, "Virus TI");
    vlog("%p   device %04x:%04x bcdDevice %04x", this, vid, pid, rel);
    return true;
}
bool VirusClient::vc_request(EReqDirection d, EReqLevel l, EReqRecipient r, EReqRequest q, unsigned short v,
                             unsigned short x, char *data, unsigned short n)
{
    // EReq* values are raw bmRequestType bits (E096: direction 0 / 0x80)
    int rr = broker_control((uint8_t)(d | l | r), (uint8_t)q, v, x, (uint8_t *)data, n);
    vlog("%p vc_request dir %d level %d recipient %d request 0x%02x value 0x%04x index 0x%04x len %u -> %d", this, d,
         l, r, q, v, x, n, rr);
    return rr >= 0;
}
unsigned long VirusClient::vc_getBuffersize()
{
    UInt32 fs = 0;
    dev_get(kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeGlobal, fs);
    vlog("%p vc_getBuffersize -> %u", this, fs);
    return fs;
}
unsigned long VirusClient::vc_getSamplerate()
{
    Float64 r = 0;
    dev_get(kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal, r);
    vlog("%p vc_getSamplerate -> %.0f", this, r);
    return (unsigned long)r;
}
bool VirusClient::vc_setSamplerate(unsigned long r)
{
    if (!g_dev)
        g_dev = find_virus_device();
    Float64 cur = 0;
    dev_get(kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal, cur);
    bool ok = cur == (Float64)r || dev_set(kAudioDevicePropertyNominalSampleRate, (Float64)r);
    for (int i = 0; ok && i < 100 && cur != (Float64)r; i++) { // the change is asynchronous
        usleep(10000);
        dev_get(kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal, cur);
    }
    vlog("%p vc_setSamplerate %lu -> %d (device at %.0f Hz)", this, r, ok, cur);
    return ok;
}
bool VirusClient::vc_getLatencies(unsigned long &in, unsigned long &out)
{
    UInt32 li = 0, lo = 0, si = 0, so = 0;
    dev_get(kAudioDevicePropertyLatency, kAudioObjectPropertyScopeInput, li);
    dev_get(kAudioDevicePropertyLatency, kAudioObjectPropertyScopeOutput, lo);
    dev_get(kAudioDevicePropertySafetyOffset, kAudioObjectPropertyScopeInput, si);
    dev_get(kAudioDevicePropertySafetyOffset, kAudioObjectPropertyScopeOutput, so);
    in = li + si, out = lo + so;
    vlog("%p vc_getLatencies -> in %lu out %lu", this, in, out);
    return g_dev != 0;
}
long VirusClient::vc_getIntParam(E_VC_INT_PARAM p) { vlog("%p vc_getIntParam %d", this, p); return -1; }
long VirusClient::vc_setIntParam(E_VC_INT_PARAM p, long v) { vlog("%p vc_setIntParam %d %ld", this, p, v); return -1; }
bool VirusClient::vc_isPluginMaster() { vlog("%p vc_isPluginMaster", this); return true; }
unsigned long VirusClient::vc_getNumOfHubs() { vlog("%p vc_getNumOfHubs", this); return 0; }
VC_ClientInfoList *VirusClient::vc_getClientInfo() { vlog("%p vc_getClientInfo", this); return nullptr; }
void VirusClient::vc_freeClientInfo(VC_ClientInfoList *l) { vlog("%p vc_freeClientInfo %p", this, (void *)l); }
long VirusClient::vc_getConfigStatus() const { vlog("%p vc_getConfigStatus", this); return 0; }
unsigned long VirusClient::vc_getLibraryVersion() const { vlog("%p vc_getLibraryVersion", this); return 0x030302; }
unsigned long VirusClient::vc_getAudioDriverVersion() const { vlog("%p vc_getAudioDriverVersion", this); return 0x030302; }
unsigned long VirusClient::vc_getHostInterfaceVersion() const { vlog("%p vc_getHostInterfaceVersion", this); return 0x030302; }
unsigned long VirusClient::vc_getBandwidthAvailable() const { vlog("%p vc_getBandwidthAvailable", this); return 1; }
