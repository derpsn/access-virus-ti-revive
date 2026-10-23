// vtibroker_posix.c - the broker for macOS (and Linux): owns the Virus TI's vendor interfaces MI_03/MI_04 over libusb
// (vticore.c) and serves clients over a Unix domain socket with the same messages as on Windows (vtiproto.h).
// Started on demand (LaunchAgent / first client) and exits when no client has been connected for IDLE_EXIT_S
// seconds, like vtibroker.exe since 0.3.1 (E080). On macOS audio is not streamed here: the Virus' class-compliant
// audio function stays with Apple's USB audio driver (Core Audio), see findings/design-macos.md. On Linux the broker
// owns the whole device like on Windows (snd-usb-audio cannot share it, E113): VTIMSG_AUDIO streams the audio
// function on libusb (vtiusbaudio_libusb.c) into POSIX shared memory (vtiaudioshm.h), with VTIMSG_AUDIO_TICK to
// the audio client per block, see findings/design-linux.md.
//
// usage: vtibroker [-v] [-stay]     socket: /tmp/vtibroker-<uid>.sock (VTI_SOCK_FMT)
#include "vticore.h"
#include "vtiproto.h"
#include "brokerinfo.h"
#include "flashinfo.h"
#include "ctrlguard.h"
#include "owner.h"
#include "ratelever.h"
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#ifdef __APPLE__
#include "vtimidi_mac.h"
#include <launch.h>
#endif
#ifdef __linux__
#include "vtiaudioshm.h"
#include "vtiusbaudio_libusb.h"
#include "ratesw.h"
#include <fcntl.h>
#include <sys/mman.h>
#endif

#define VTI_SOCK_FMT "/tmp/vtibroker-%u.sock"
#ifdef MSG_NOSIGNAL
#define MSG_NOSIGNAL_OR_ZERO MSG_NOSIGNAL
#else
#define MSG_NOSIGNAL_OR_ZERO 0 // macOS: SIGPIPE is ignored in main()
#endif
#define MAX_CLIENTS 16
#define IDLE_EXIT_S 30

typedef struct {
    int fd;
    pthread_mutex_t wr; // one message at a time per client
    int audio;          // Linux: wants VTIMSG_AUDIO_TICK
    int tcp;            // Linux: came in over TCP (Wine), must send VTIMSG_AUTH first
    int notes;          // has sent note-ons: silence the Virus when it goes away (hanging-note protection)
    int observer;       // protocol 4: read-only observer (never the owner; gets everything through `q`)
    struct obsq *q;
    char name[48];      // from HELLO (protocol 4), for the status and the owner rule (owner.h)
    int ready;          // has been told its state (after HELLO: a waiting plug-in never sees the Virus briefly)
} client;

static client *g_clients[MAX_CLIENTS];
static pthread_mutex_t g_cs_clients = PTHREAD_MUTEX_INITIALIZER, g_cs_dev = PTHREAD_MUTEX_INITIALIZER,
                       g_cs_log = PTHREAD_MUTEX_INITIALIZER;
static vti *g_dev;
static int g_verbose, g_stay;

static void obs_log(const char *line); // observers get every log line (below)

static void blog(const char *fmt, ...)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm;
    localtime_r(&ts.tv_sec, &tm);
    char line[1024];
    int n = snprintf(line, sizeof line, "%02d:%02d:%02d.%03ld ", tm.tm_hour, tm.tm_min, tm.tm_sec,
                     ts.tv_nsec / 1000000);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line + n, sizeof line - (size_t)n, fmt, ap);
    va_end(ap);
    pthread_mutex_lock(&g_cs_log);
    fprintf(stderr, "%s\n", line);
    pthread_mutex_unlock(&g_cs_log);
    obs_log(line);
}

static void on_vti_log(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    buf[strcspn(buf, "\n")] = 0;
    blog("%s", buf);
}

static int write_all(int fd, const void *p, size_t n)
{
    for (const uint8_t *b = p; n;) {
        ssize_t w = send(fd, b, n, MSG_NOSIGNAL_OR_ZERO);
        if (w < 0 && errno == EINTR)
            continue;
        if (w <= 0)
            return -1;
        b += w;
        n -= (size_t)w;
    }
    return 0;
}

static int read_all(int fd, void *p, size_t n)
{
    for (uint8_t *b = p; n;) {
        ssize_t r = recv(fd, b, n, 0);
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0)
            return -1;
        b += r;
        n -= (size_t)r;
    }
    return 0;
}

static int send_msg(client *c, uint16_t type, const void *a, int alen, const void *b, int blen)
{
    vtimsg h = {type, (uint16_t)(alen + blen)};
    pthread_mutex_lock(&c->wr);
    int r = write_all(c->fd, &h, sizeof h) || (alen && write_all(c->fd, a, (size_t)alen)) ||
            (blen && write_all(c->fd, b, (size_t)blen));
    pthread_mutex_unlock(&c->wr);
    return r;
}

// The owner rule of owner.h, as on Windows: the first "TI Revive Virus Control" owns the Virus, plug-ins of other
// programs (another DAW, auval, a test host) wait until it leaves - they would initialise the Virus and restart its
// audio stream in the middle of the first one's session, which left it without USB audio until a power cycle (E112).
// Every other client (Control Center, tirevive-cc) shares the Virus with the owner (until 0.8.19 the first client
// of any kind owned it, so the Control Center waited while a DAW ran, E181).
static client *g_owner; // g_cs_clients: the owning plug-in

static int waits(const client *c) // g_cs_clients held
{
    return vti_client_waits(vti_is_plugin(c->name), c == g_owner);
}

// the Virus' identification blocks (VTIMSG_FLASHINFO, flashinfo.h): read once per device open with the original
// driver's fixed read-only sequence, then cached; the device lock is held per request only (MIDI keeps flowing)
static unsigned g_dev_gen; // counts device opens: the cache belongs to one
static uint8_t g_flash[VTI_FLASH_BLOCKS * 16];
static int g_flash_len, g_flash_busy;
static unsigned g_flash_gen;

static int flash_ctl(void *u, uint8_t rt, uint8_t req, uint16_t val, uint16_t idx, uint8_t *d, uint16_t len)
{
    unsigned gen = *(unsigned *)u;
    pthread_mutex_lock(&g_cs_dev);
    int r = g_dev && g_dev_gen == gen ? vti_control(g_dev, rt, req, val, idx, d, len) : -1;
    pthread_mutex_unlock(&g_cs_dev);
    return r;
}

static void flash_nap(int ms) { usleep((useconds_t)ms * 1000); }

static int flash_info(uint8_t *out)
{
    pthread_mutex_lock(&g_cs_dev);
    unsigned gen = g_dev_gen;
    int cached = g_dev && g_flash_len > 0 && g_flash_gen == gen, start = g_dev && !cached && !g_flash_busy;
    if (cached)
        memcpy(out, g_flash, (size_t)g_flash_len);
    if (start)
        g_flash_busy = 1;
    int n = cached ? g_flash_len : -1;
    pthread_mutex_unlock(&g_cs_dev);
    if (!start)
        return n;
    n = vti_flash_info(flash_ctl, flash_nap, &gen, out);
    blog("flash info read (read-only, E026): %d bytes", n);
    pthread_mutex_lock(&g_cs_dev);
    g_flash_busy = 0;
    if (n > 0 && g_dev_gen == gen) {
        memcpy(g_flash, out, (size_t)n);
        g_flash_len = n;
        g_flash_gen = gen;
    }
    pthread_mutex_unlock(&g_cs_dev);
    return n;
}

static int may_use(client *c) // not an observer, not a waiting plug-in
{
    pthread_mutex_lock(&g_cs_clients);
    int r = !c->observer && !waits(c);
    pthread_mutex_unlock(&g_cs_clients);
    return r;
}

static uint32_t state_of(const client *c, int dev_open) // g_cs_clients held
{
    return vti_client_state(dev_open, vti_is_plugin(c->name), c == g_owner);
}

static void send_states(int dev_open) // g_cs_clients held
{
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (g_clients[i] && !g_clients[i]->observer && g_clients[i]->ready) {
            uint32_t st = state_of(g_clients[i], dev_open);
            send_msg(g_clients[i], VTIMSG_STATE, &st, 4, NULL, 0);
        }
}

static void next_owner(void) // g_cs_dev and g_cs_clients held: the next waiting plug-in takes over
{
    for (int i = 0; i < MAX_CLIENTS && !g_owner; i++)
        if (g_clients[i] && !g_clients[i]->observer && vti_is_plugin(g_clients[i]->name))
            g_owner = g_clients[i];
    if (g_owner) {
        uint32_t st = g_dev != NULL;
        blog("the next program's plug-in owns the Virus now");
        if (g_owner->ready)
            send_msg(g_owner, VTIMSG_STATE, &st, 4, NULL, 0);
    }
}

// ---- observers (protocol 4, the Control Center's Nerd mode) ----
// Each observer has a byte ring of framed messages and a writer thread, so the libusb thread and the owner never wait
// for a slow observer: when its ring is full, the message is dropped for that observer only (counted in the status).
#define OBS_RING (512 * 1024)
typedef struct obsq {
    pthread_mutex_t mu;
    pthread_cond_t cv;
    uint8_t *ring;
    size_t head, len; // read position, bytes queued
    uint64_t dropped;
    int stop;
    client *c;
    pthread_t th;
} obsq;
static obsq *g_obs[MAX_CLIENTS]; // g_cs_obs
static int g_obs_count;          // g_cs_obs (read without it as a quick check)
static pthread_mutex_t g_cs_obs = PTHREAD_MUTEX_INITIALIZER;
static uint64_t g_cnt_out, g_cnt_in, g_cnt_ctl; // packets passed on (status)
static struct timespec g_t0;

static uint32_t uptime_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((ts.tv_sec - g_t0.tv_sec) * 1000 + (ts.tv_nsec - g_t0.tv_nsec) / 1000000);
}

static void ring_put(obsq *q, const void *p, size_t n) // q->mu held, room checked
{
    const uint8_t *b = p;
    size_t tail = (q->head + q->len) % OBS_RING, first = n < OBS_RING - tail ? n : OBS_RING - tail;
    memcpy(q->ring + tail, b, first);
    memcpy(q->ring, b + first, n - first);
    q->len += n;
}

// queues one message for every observer
static void obs_push(uint16_t type, const void *a, int alen, const void *b, int blen)
{
    if (alen + blen > VTIMSG_MAX)
        blen = VTIMSG_MAX - alen;
    vtimsg h = {type, (uint16_t)(alen + blen)};
    pthread_mutex_lock(&g_cs_obs);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        obsq *q = g_obs[i];
        if (!q)
            continue;
        pthread_mutex_lock(&q->mu);
        if (q->len + sizeof h + h.len > OBS_RING) {
            q->dropped++;
        } else {
            ring_put(q, &h, sizeof h);
            if (alen)
                ring_put(q, a, (size_t)alen);
            if (blen > 0)
                ring_put(q, b, (size_t)blen);
            pthread_cond_signal(&q->cv);
        }
        pthread_mutex_unlock(&q->mu);
    }
    pthread_mutex_unlock(&g_cs_obs);
}

static void obs_traffic(uint8_t kind, uint8_t ep, const void *a, int alen, const void *b, int blen)
{
    __atomic_add_fetch(kind == 0 ? &g_cnt_out : kind == 1 ? &g_cnt_in : &g_cnt_ctl, 1, __ATOMIC_RELAXED);
    if (!__atomic_load_n(&g_obs_count, __ATOMIC_RELAXED))
        return;
    uint8_t hdr[6] = {kind, ep};
    uint32_t t = uptime_ms();
    memcpy(hdr + 2, &t, 4);
    uint8_t body[VTIMSG_MAX];
    int n = alen < (int)sizeof body ? alen : (int)sizeof body;
    memcpy(body, a, (size_t)n);
    if (blen > 0 && n + blen <= (int)sizeof body)
        memcpy(body + n, b, (size_t)blen), n += blen;
    obs_push(VTIMSG_OBS_TRAFFIC, hdr, 6, body, n);
}

static void *obs_writer(void *arg) // sends the queued bytes; any byte range of the stream may go at once
{
    obsq *q = arg;
    uint8_t *out = malloc(64 * 1024);
    for (;;) {
        pthread_mutex_lock(&q->mu);
        while (!q->len && !q->stop)
            pthread_cond_wait(&q->cv, &q->mu);
        if (q->stop) {
            pthread_mutex_unlock(&q->mu);
            break;
        }
        size_t n = q->len < 64 * 1024 ? q->len : 64 * 1024, first = n < OBS_RING - q->head ? n : OBS_RING - q->head;
        memcpy(out, q->ring + q->head, first);
        memcpy(out + first, q->ring, n - first);
        q->head = (q->head + n) % OBS_RING;
        q->len -= n;
        pthread_mutex_unlock(&q->mu);
        pthread_mutex_lock(&q->c->wr);
        int r = write_all(q->c->fd, out, n);
        pthread_mutex_unlock(&q->c->wr);
        if (r) // the observer went away; its client thread notices on read
            break;
    }
    free(out);
    return NULL;
}

static void obs_log(const char *line)
{
    if (__atomic_load_n(&g_obs_count, __ATOMIC_RELAXED))
        obs_push(VTIMSG_OBS_LOG, line, (int)strlen(line), NULL, 0);
}

static int g_audio_on; // Linux: the broker streams the Virus' audio (status only)

// once a second: device, owner, clients, counters (tab-separated key=value, see vtiproto.h)
static void obs_status(int dev_open)
{
    if (!__atomic_load_n(&g_obs_count, __ATOMIC_RELAXED))
        return;
    char st[1024], clients[600] = "";
    const char *owner = "-";
    pthread_mutex_lock(&g_cs_clients);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        client *c = g_clients[i];
        if (!c)
            continue;
        const char *nm = c->name[0] ? c->name : "unnamed client";
        if (c == g_owner)
            owner = nm;
        size_t l = strlen(clients);
        snprintf(clients + l, sizeof clients - l, "%s%s%s", l ? ", " : "", nm,
                 c->observer ? " (observer)" : c == g_owner ? " (owner)" : waits(c) ? " (waiting)" : "");
    }
    char own[48];
    snprintf(own, sizeof own, "%s", owner);
    pthread_mutex_unlock(&g_cs_clients);
    snprintf(st, sizeof st,
             "device=%d\towner=%s\tclients=%s\taudio=%d\tto_device=%llu\tfrom_device=%llu\tcontrol=%llu\tuptime_s=%u",
             dev_open, own, clients, g_audio_on, (unsigned long long)g_cnt_out, (unsigned long long)g_cnt_in,
             (unsigned long long)g_cnt_ctl, uptime_ms() / 1000);
    // the drop counter differs per observer: appended per queue
    pthread_mutex_lock(&g_cs_obs);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        obsq *q = g_obs[i];
        if (!q)
            continue;
        char line[1100];
        pthread_mutex_lock(&q->mu);
        int n = snprintf(line, sizeof line, "%s\tdropped=%llu", st, (unsigned long long)q->dropped);
        vtimsg h = {VTIMSG_OBS_STATUS, (uint16_t)n};
        if (q->len + sizeof h + (size_t)n <= OBS_RING) {
            ring_put(q, &h, sizeof h);
            ring_put(q, line, (size_t)n);
            pthread_cond_signal(&q->cv);
        }
        pthread_mutex_unlock(&q->mu);
    }
    pthread_mutex_unlock(&g_cs_obs);
}

static void obs_attach(client *c)
{
    obsq *q = calloc(1, sizeof *q);
    q->ring = malloc(OBS_RING);
    q->c = c;
    pthread_mutex_init(&q->mu, NULL);
    pthread_cond_init(&q->cv, NULL);
    c->q = q;
    pthread_create(&q->th, NULL, obs_writer, q);
    pthread_mutex_lock(&g_cs_obs);
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (!g_obs[i]) {
            g_obs[i] = q;
            g_obs_count++;
            break;
        }
    pthread_mutex_unlock(&g_cs_obs);
}

static void obs_detach(client *c)
{
    obsq *q = c->q;
    if (!q)
        return;
    pthread_mutex_lock(&g_cs_obs);
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (g_obs[i] == q) {
            g_obs[i] = NULL;
            g_obs_count--;
        }
    pthread_mutex_unlock(&g_cs_obs);
    pthread_mutex_lock(&q->mu);
    q->stop = 1;
    pthread_cond_signal(&q->cv);
    pthread_mutex_unlock(&q->mu);
    pthread_join(q->th, NULL);
    pthread_mutex_destroy(&q->mu);
    pthread_cond_destroy(&q->cv);
    free(q->ring);
    free(q);
    c->q = NULL;
}

// ---- trace recorder (VTI_TRACE=<file>): every plug-in channel packet and control request with time stamps ----
// Binary records for offline analysis (scripts/vtitrace.py): file header "VTTRACE1", then per record
//   u32 size of the rest, u8 kind (0 = host->device write, 1 = device->host RX, 2 = control request), u8 endpoint,
//   u16 reserved, u64 monotonic ns, i64 synth frame (Linux: device frame counter of the audio stream, else -1),
//   payload (kind 2: 8-byte setup, i32 result, then the data).
static FILE *g_trace;
static pthread_mutex_t g_cs_trace = PTHREAD_MUTEX_INITIALIZER;
static int64_t trace_frame(void); // defined below (Linux: synth frame counter)

static void trace_rec(uint8_t kind, uint8_t ep, const void *a, int alen, const void *b, int blen)
{
    obs_traffic(kind, ep, a, alen, b, blen);
    if (!g_trace)
        return;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t ns = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
    int64_t frame = trace_frame();
    uint32_t size = 1 + 1 + 2 + 8 + 8 + (uint32_t)alen + (uint32_t)blen;
    uint16_t z = 0;
    pthread_mutex_lock(&g_cs_trace);
    fwrite(&size, 4, 1, g_trace), fwrite(&kind, 1, 1, g_trace), fwrite(&ep, 1, 1, g_trace), fwrite(&z, 2, 1, g_trace);
    fwrite(&ns, 8, 1, g_trace), fwrite(&frame, 8, 1, g_trace);
    if (alen)
        fwrite(a, 1, (size_t)alen, g_trace);
    if (blen)
        fwrite(b, 1, (size_t)blen, g_trace);
    fflush(g_trace);
    pthread_mutex_unlock(&g_cs_trace);
}

#ifdef __linux__
static void f5_report(const uint8_t *p, int len);
static void f5_second(void);
#endif

static volatile int g_rate_lever; // the rate's global was sent (E257): log the Virus' report of it

static void on_rx(void *user, uint8_t ep, const uint8_t *data, int len)
{
    (void)user;
    trace_rec(1, ep, data, len, NULL, 0);
    if (g_rate_lever && ep == VTI_EP_MI04_IN) {
        int v = vti_rate_report(data, len);
        if (v >= 0)
            blog("usbaudio: the Virus reports System Clock/Frequency = %s", v ? "48 kHz" : "44.1 kHz");
    }
#ifdef __APPLE__
    if (ep == VTI_EP_MI04_IN)
        vtimidi_rx(data, len);
#endif
    pthread_mutex_lock(&g_cs_clients); // the owner and every sharing client
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (g_clients[i] && !g_clients[i]->observer && g_clients[i]->ready && !waits(g_clients[i]))
            send_msg(g_clients[i], VTIMSG_RX, &ep, 1, data, len);
    pthread_mutex_unlock(&g_cs_clients);
    if (g_verbose) { // RX packets per endpoint, once per second
        static unsigned n[2], bytes[2];
        static time_t last;
        n[ep == VTI_EP_MI04_IN]++, bytes[ep == VTI_EP_MI04_IN] += (unsigned)len;
#ifdef __linux__
        if (ep == VTI_EP_MI04_IN)
            f5_report(data, len);
#endif
        static unsigned dumped;
        if (g_verbose > 1 && dumped++ < 20000) {
            char hex[3 * 32 + 1] = "";
            for (int k = 0; k < len && k < 32; k++)
                snprintf(hex + 3 * k, 4, " %02x", data[k]);
            blog("rx ep%02x %d:%s", ep, len, hex);
        }
        time_t now = time(NULL);
        if (now != last) {
            blog("rx: ep85 %u packets %u bytes, ep86 %u packets %u bytes", n[0], bytes[0], n[1], bytes[1]);
            n[0] = n[1] = bytes[0] = bytes[1] = 0;
            last = now;
        }
    }
}

#ifdef __linux__
// ---- Linux: the Virus' audio function, streamed by the broker (port of vtibroker.c's audio ring) ----
static vti_audio_shm *g_shm;
static pthread_mutex_t g_cs_tick = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cv_tick = PTHREAD_COND_INITIALIZER;
static int64_t g_tick_sent;

static int64_t mono_100ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (int64_t)ts.tv_sec * 10000000 + ts.tv_nsec / 100;
}

static int shm_create(void)
{
    if (g_shm)
        return 0;
    char name[64];
    snprintf(name, sizeof name, VTI_AUDIO_SHM_LINUX, (unsigned)getuid());
    int fd = shm_open(name, O_RDWR | O_CREAT, 0600);
    if (fd < 0 || ftruncate(fd, sizeof(vti_audio_shm))) {
        blog("audio: shared memory %s: %s", name, strerror(errno));
        if (fd >= 0)
            close(fd);
        return -1;
    }
    g_shm = mmap(NULL, sizeof(vti_audio_shm), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (g_shm == MAP_FAILED) {
        g_shm = NULL;
        return -1;
    }
    memset(g_shm, 0, sizeof *g_shm);
    g_shm->magic = VTI_AUDIO_MAGIC;
    g_shm->version = VTI_AUDIO_VERSION;
    g_shm->rate = VLA_RATE;
    g_shm->in_ch = VTI_AUDIO_IN_CH;
    g_shm->out_ch = VTI_AUDIO_OUT_CH;
    g_shm->ring_frames = VTI_AUDIO_RING;
    return 0;
}

static volatile int64_t g_in_last, g_in_gap_max, g_tick_last, g_tick_gap_max; // diagnostics (-v), 100 ns

static void audio_in(const int16_t *pcm, uint32_t frames, void *user) // libusb event thread
{
    (void)user;
    if (g_verbose) { // gaps between USB blocks (normally 10 ms)
        int64_t t = mono_100ns();
        if (g_in_last && t - g_in_last > g_in_gap_max)
            g_in_gap_max = t - g_in_last;
        if (g_in_last && t - g_in_last > 150000)
            blog("usb block late: %.1f ms after the previous one", (t - g_in_last) / 1e4);
        g_in_last = t;
    }
    int64_t w = g_shm->in_frames;
    for (uint32_t i = 0; i < frames; i++)
        memcpy(&g_shm->in[((w + i) & (VTI_AUDIO_RING - 1)) * VTI_AUDIO_IN_CH], pcm + i * VTI_AUDIO_IN_CH,
               VTI_AUDIO_IN_CH * sizeof(int16_t));
    g_shm->in_qpc = mono_100ns();
    __atomic_store_n(&g_shm->in_frames, w + frames, __ATOMIC_RELEASE); // publishes the data written above
    static FILE *rec; // diagnostics: VTI_RECORD=<file.raw> records device channels 2/3 (USB 1), 16-bit stereo
    static int rec_init;
    if (!rec_init) {
        rec_init = 1;
        const char *path = getenv("VTI_RECORD");
        if (path && (rec = fopen(path, "wb")))
            blog("recording USB 1 to %s", path);
    }
    if (rec)
        for (uint32_t i = 0; i < frames; i++)
            fwrite(pcm + i * VTI_AUDIO_IN_CH + 2, 2, 2, rec);
    pthread_mutex_lock(&g_cs_tick);
    pthread_cond_signal(&g_cv_tick);
    pthread_mutex_unlock(&g_cs_tick);
}

// OUT packet source: as vtibroker.c - playback starts with OUT_PREFILL frames buffered, latency bounded by OUT_MAX
#define OUT_PREFILL 512
#define OUT_MAX 2048
static int g_out_playing;
static uint64_t g_out_underruns, g_out_dropped, g_out_frames;

static void audio_out(int16_t *dst, uint32_t frames, void *user)
{
    (void)user;
    int64_t w = __atomic_load_n(&g_shm->out_write, __ATOMIC_ACQUIRE), r = g_shm->out_read;
    int64_t avail = w - r;
    if (avail < 0 || avail > VTI_AUDIO_RING) { // writer restarted or far ahead: resynchronise
        r = w;
        avail = 0;
        g_out_playing = 0;
    }
    if (!g_out_playing && avail >= OUT_PREFILL)
        g_out_playing = 1;
    if (g_out_playing && avail > OUT_MAX) {
        g_out_dropped += (uint64_t)(avail - OUT_PREFILL);
        r = w - OUT_PREFILL;
        avail = OUT_PREFILL;
    }
    uint32_t n = 0;
    if (g_out_playing) {
        n = avail < frames ? (uint32_t)avail : frames;
        for (uint32_t i = 0; i < n; i++)
            memcpy(dst + i * VTI_AUDIO_OUT_CH, &g_shm->out[((r + i) & (VTI_AUDIO_RING - 1)) * VTI_AUDIO_OUT_CH],
                   VTI_AUDIO_OUT_CH * sizeof(int16_t));
        if (n < frames) { // ran dry: refill before playing again
            g_out_underruns++;
            g_out_playing = 0;
        }
    }
    memset(dst + n * VTI_AUDIO_OUT_CH, 0, (frames - n) * VTI_AUDIO_OUT_CH * sizeof(int16_t));
    g_shm->out_read = r + n;
    g_out_frames += n;
}

// Block notifications go out from here, not from the USB thread, so a slow client never delays the stream.
static void *tick_thread(void *unused)
{
    (void)unused;
    for (;;) {
        pthread_mutex_lock(&g_cs_tick);
        while (!g_shm || __atomic_load_n(&g_shm->in_frames, __ATOMIC_ACQUIRE) == g_tick_sent)
            pthread_cond_wait(&g_cv_tick, &g_cs_tick);
        int64_t f = g_tick_sent = g_shm->in_frames;
        pthread_mutex_unlock(&g_cs_tick);
        static time_t last_f5;
        if (g_verbose && time(NULL) != last_f5) {
            last_f5 = time(NULL);
            f5_second();
        }
        int64_t t0 = g_verbose ? mono_100ns() : 0;
        pthread_mutex_lock(&g_cs_clients);
        for (int i = 0; i < MAX_CLIENTS; i++)
            if (g_clients[i] && g_clients[i]->audio)
                send_msg(g_clients[i], VTIMSG_AUDIO_TICK, &f, 8, NULL, 0);
        pthread_mutex_unlock(&g_cs_clients);
        if (g_verbose) { // tick sends: gap and how long the send took
            int64_t t = mono_100ns();
            if (g_tick_last && t - g_tick_last > g_tick_gap_max)
                g_tick_gap_max = t - g_tick_last;
            if ((g_tick_last && t - g_tick_last > 150000) || t - t0 > 50000)
                blog("tick late: %.1f ms after the previous one (send took %.1f ms)", (t - g_tick_last) / 1e4,
                     (t - t0) / 1e4);
            g_tick_last = t;
        }
    }
    return NULL;
}

// ---- diagnostics (-v): margin of the plug-in's F5 timestamps against the synth's frame counter (E128) ----
// Every timed event the plug-in sends is "F5 <14-bit sample time>" (cable 5, CIN 3) followed by the event. The synth
// reports a Sync Information Event (global 0x54) when events arrive too late for their time stamp. Logged: the time
// stamp minus the synth's current frame (extrapolated between USB blocks), relative to the first one of the session.
static pthread_mutex_t g_cs_f5 = PTHREAD_MUTEX_INITIALIZER;
static int g_f5_base = -1, g_f5_min = 1 << 30, g_f5_max = -(1 << 30), g_f5_n, g_f5_hist[16], g_f5_hn;

static int64_t synth_frame_now(void)
{
    int64_t f = __atomic_load_n(&g_shm->in_frames, __ATOMIC_ACQUIRE), q = g_shm->in_qpc;
    int64_t dt = mono_100ns() - q;
    return f + (dt > 0 && dt < 500000 ? dt * 44100 / 10000000 : 0);
}

static int64_t trace_frame(void)
{
    return g_shm && g_shm->streaming ? synth_frame_now() : -1;
}

static void f5_note(const uint8_t *p, int len) // a WRITE to EP 0x06
{
    if (!g_shm || !g_shm->streaming)
        return;
    for (int k = 0; k + 3 < len; k += 4)
        if ((p[k] & 0x0F) == 0x3 && p[k + 1] == 0xF5) {
            int ts = p[k + 2] | p[k + 3] << 7;
            int d = (int)((ts - synth_frame_now()) & 0x3FFF);
            pthread_mutex_lock(&g_cs_f5);
            if (g_f5_base < 0)
                g_f5_base = d;
            int rel = ((d - g_f5_base + 8192) & 0x3FFF) - 8192; // change of the margin since the session start
            g_f5_min = rel < g_f5_min ? rel : g_f5_min;
            g_f5_max = rel > g_f5_max ? rel : g_f5_max;
            g_f5_n++;
            g_f5_hist[g_f5_hn++ & 15] = rel;
            pthread_mutex_unlock(&g_cs_f5);
        }
}

static void f5_report(const uint8_t *p, int len) // RX from EP 0x86: Sync Information Event?
{
    for (int i = 0; i + 3 < len; i += 4)
        if ((p[i] & 0x0F) == 4 && p[i + 1] == 0x73 && p[i + 2] == 0x00 && p[i + 3] == 0x54) {
            char h[16 * 7] = "";
            pthread_mutex_lock(&g_cs_f5);
            for (int k = 0; k < 16 && k < g_f5_hn; k++)
                snprintf(h + strlen(h), sizeof h - strlen(h), " %d", g_f5_hist[(g_f5_hn - 1 - k) & 15]);
            pthread_mutex_unlock(&g_cs_f5);
            blog("SYNC REPORT from the synth at frame %lld; last F5 margins (newest first):%s",
                 g_shm ? (long long)synth_frame_now() : -1LL, h);
        }
}

static void f5_second(void) // once per second from the RX statistics
{
    pthread_mutex_lock(&g_cs_f5);
    if (g_f5_n)
        blog("f5: %d time stamps, margin change %d..%d frames (base %d); max gap usb %.1f ms, tick %.1f ms", g_f5_n,
             g_f5_min, g_f5_max, g_f5_base, g_in_gap_max / 1e4, g_tick_gap_max / 1e4);
    g_in_gap_max = g_tick_gap_max = 0;
    g_f5_n = 0, g_f5_min = 1 << 30, g_f5_max = -(1 << 30);
    pthread_mutex_unlock(&g_cs_f5);
}

static vti_rate_guard g_rate_guard; // at most one rate switch per second (ratesw.h, rule 2)

// the shared ring learns the stream's rate before frames at it arrive (rate, then rate_gen; readers re-anchor on a
// new generation, vtiaudioshm.h); a change counts for the debounce
static void shm_rate(uint32_t rate)
{
    if (g_shm->rate == rate)
        return;
    g_shm->rate = rate;
    __sync_synchronize();
    g_shm->rate_gen++;
    vti_rate_switched(&g_rate_guard, uptime_ms());
}

// g_cs_dev held, device open. `wanted` = the client's rate (protocol 5, else 44100); `owner` = it may switch the
// Virus (the plug-in that owns it, or any client while no plug-in does)
static int audio_start(uint32_t wanted, int owner)
{
    if (vla_running()) {
        uint32_t cur = vla_rate();
        uint32_t to = vti_rate_choose(cur, wanted, owner, vti_rate_may_switch(&g_rate_guard, uptime_ms()));
        if (to != cur) {
            if (vla_switch_rate(to, shm_rate) == -2) { // the stream stopped: give the interfaces back properly
                vla_stop();
                g_shm->streaming = 0;
                g_audio_on = 0;
                return -1;
            }
        } else if (vti_rate_wanted(wanted, cur) != cur)
            blog("audio: %.1f kHz asked, staying at %.1f kHz (%s)", wanted / 1000.0, cur / 1000.0,
                 owner ? "the last switch was less than a second ago" : "only the owner switches the rate");
        return 1;
    }
    g_audio_on = 0;
    if (shm_create())
        return -1;
    struct libusb_context *ctx;
    struct libusb_device_handle *h;
    vti_libusb(g_dev, &ctx, &h);
    g_shm->in_frames = 0;
    g_tick_sent = 0;
    g_f5_base = -1;
    g_shm->out_read = g_shm->out_write;
    g_out_playing = 0;
    g_out_frames = g_out_underruns = g_out_dropped = 0;
    // a fresh start protects no running stream: any explicit wish applies (still debounced); 0 keeps the last rate
    uint32_t last = g_shm->rate ? g_shm->rate : VLA_RATE;
    uint32_t rate = vti_rate_choose(last, wanted, 1, vti_rate_may_switch(&g_rate_guard, uptime_ms()));
    if (rate != vti_rate_wanted(wanted, last))
        blog("audio: %.1f kHz asked, starting at %.1f kHz (the last switch was less than a second ago)",
             wanted / 1000.0, rate / 1000.0);
    shm_rate(rate);
    if (vla_start(ctx, h, rate, audio_in, audio_out, NULL))
        return -1;
    g_shm->streaming = 1;
    g_audio_on = 1;
    return 1;
}

static void audio_stop(void) // g_cs_dev held
{
    if (!g_shm || !g_shm->streaming)
        return;
    vla_stop();
    g_shm->streaming = 0;
    g_audio_on = 0;
    if (g_out_frames)
        blog("audio out: %llu frames played, %llu underruns, %llu frames dropped", (unsigned long long)g_out_frames,
             (unsigned long long)g_out_underruns, (unsigned long long)g_out_dropped);
}
#else
static void audio_stop(void) {}
static int64_t trace_frame(void) { return -1; }
#endif

#ifdef __APPLE__
static int midi_write(const uint8_t *data, int len) // pump thread: plug-in channel packets from the MDW ring
{
    trace_rec(0, VTI_EP_MI04_OUT, data, len, NULL, 0);
    pthread_mutex_lock(&g_cs_dev);
    int r = g_dev ? vti_write(g_dev, VTI_EP_MI04_OUT, data, len) : -1;
    pthread_mutex_unlock(&g_cs_dev);
    return r;
}
#endif

static int client_count(void)
{
    int n = 0;
    pthread_mutex_lock(&g_cs_clients);
    for (int i = 0; i < MAX_CLIENTS; i++)
        n += g_clients[i] != NULL;
    pthread_mutex_unlock(&g_cs_clients);
    return n;
}

static void *device_thread(void *unused)
{
    (void)unused;
    time_t last_busy = time(NULL);
    for (;;) {
        if (client_count())
            last_busy = time(NULL);
        else if (!g_stay && time(NULL) - last_busy > IDLE_EXIT_S) {
            blog("no clients for %d s: exiting", IDLE_EXIT_S);
            pthread_mutex_lock(&g_cs_dev);
            audio_stop();
            if (g_dev)
                vti_close(g_dev);
            exit(0);
        }
        pthread_mutex_lock(&g_cs_dev);
        if (g_dev && vti_is_dead(g_dev)) {
            blog("device failed: closing");
            audio_stop();
            vti_close(g_dev);
            g_dev = NULL;
            pthread_mutex_lock(&g_cs_clients);
            send_states(0);
            pthread_mutex_unlock(&g_cs_clients);
        }
        if (!g_dev && (g_dev = vti_open(on_rx, NULL))) {
            g_dev_gen++;
            blog("device open, handshake sent");
            pthread_mutex_lock(&g_cs_clients);
            send_states(1);
            pthread_mutex_unlock(&g_cs_clients);
        }
#ifdef __linux__
        // the Virus left the wanted rate while streaming (E175/E241: after a wake, a replug): the stream already sends
        // it silent packets of its size; ask for the rate again, a few times per stream
        static int rate_tries;
        static uint32_t rate_tries_at; // a switch to another rate starts the tries again
        if (g_dev && g_shm && g_shm->streaming && vla_rate() != rate_tries_at) {
            rate_tries_at = vla_rate();
            rate_tries = 0;
        }
        if (g_dev && g_shm && g_shm->streaming) {
            if (vla_rate_lost()) {
                if (rate_tries < 3) {
                    rate_tries++;
                    // both masters of the rate (E257): SET_CUR on the endpoints and the panel's global parameter
                    uint32_t want = vla_rate();
                    int r = vla_set_rate_again();
                    uint8_t lever[16];
                    int n = vti_pack_rate(lever, sizeof lever, want);
                    int w = n > 0 ? vti_write(g_dev, VTI_EP_MI04_OUT, lever, n) : -1;
                    g_rate_lever = 1;
                    blog("usbaudio: the Virus sends %s kHz packets - asking for %.1f kHz again (%d/3): SET_CUR %s, "
                         "global 73 00 10 %02X -> %d", want == 48000 ? "44.1" : "48", want / 1000.0, rate_tries,
                         r ? "failed" : "accepted", want == 48000 ? 1 : 0, w);
                } else if (rate_tries++ == 3)
                    blog("usbaudio: the Virus stays at %s kHz - its sound stays silent; switch it off and on",
                         vla_rate() == 48000 ? "44.1" : "48");
            }
        } else
            rate_tries = 0;
#endif
        int open = g_dev != NULL;
        pthread_mutex_unlock(&g_cs_dev);
        static uint32_t last_status;
        if (uptime_ms() - last_status >= 1000) {
            last_status = uptime_ms();
            obs_status(open);
        }
        usleep(open ? 500000 : 2000000);
    }
    return NULL;
}

// SIGINT / SIGTERM (Ctrl+C, logout, systemd): close the device properly, so on Linux it is handed back to
// snd-usb-audio (vti_close resets the port) instead of staying claimed until it is replugged.
static void *signal_thread(void *arg)
{
    sigset_t *set = arg;
    int sig = 0;
    sigwait(set, &sig);
    blog("signal %d: exiting", sig);
    pthread_mutex_lock(&g_cs_dev);
    audio_stop();
    if (g_dev)
        vti_close(g_dev);
    exit(0);
    return NULL;
}

#ifdef __linux__
// ---- Linux: TCP listener for the wrapper under Wine (Wine's Winsock may lack AF_UNIX) ----
// 127.0.0.1 only, and every connection must first send VTIMSG_AUTH with the secret from the user's token file
// (~/.config/tirevive/broker.token, 0600: "port=<n>" and "token=<64 hex>"), so other local users cannot use it.
static char g_token[65];
static int g_tcp_port;

static int token_load(void)
{
    const char *home = getenv("HOME");
    if (!home)
        return -1;
    char dir[512], path[600];
    snprintf(dir, sizeof dir, "%s/.config/tirevive", home);
    snprintf(path, sizeof path, "%s/broker.token", dir);
    FILE *f = fopen(path, "r");
    if (f) {
        char line[128];
        while (fgets(line, sizeof line, f)) {
            if (!strncmp(line, "port=", 5))
                g_tcp_port = atoi(line + 5);
            else if (!strncmp(line, "token=", 6))
                sscanf(line + 6, "%64[0-9a-f]", g_token);
        }
        fclose(f);
        if (g_tcp_port > 0 && strlen(g_token) == 64)
            return 0;
    }
    // first start: create it (the Linux installer normally does this, see install-linux.sh)
    uint8_t rnd[32];
    FILE *u = fopen("/dev/urandom", "rb");
    if (!u || fread(rnd, 1, sizeof rnd, u) != sizeof rnd) {
        if (u)
            fclose(u);
        return -1;
    }
    fclose(u);
    for (int i = 0; i < 32; i++)
        snprintf(g_token + 2 * i, 3, "%02x", rnd[i]);
    if (g_tcp_port <= 0)
        g_tcp_port = VTI_TCP_PORT;
    mkdir(dir, 0700);
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0)
        return -1;
    dprintf(fd, "port=%d\ntoken=%s\n", g_tcp_port, g_token);
    close(fd);
    blog("created %s", path);
    return 0;
}

static int tcp_auth(client *c) // first message must be VTIMSG_AUTH {token}
{
    vtimsg h;
    char buf[VTIMSG_MAX];
    struct timeval tv = {5, 0};
    setsockopt(c->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    int ok = !read_all(c->fd, &h, sizeof h) && h.type == VTIMSG_AUTH && h.len == 64 && !read_all(c->fd, buf, 64) &&
             g_token[0] && !memcmp(buf, g_token, 64);
    tv.tv_sec = 0;
    setsockopt(c->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    if (!ok)
        blog("TCP client: authentication failed, closing");
    return ok;
}
#endif

static void *client_thread(void *arg)
{
    client *c = arg;
    uint8_t buf[VTIMSG_MAX];
#ifdef __linux__
    if (c->tcp && !tcp_auth(c)) {
        close(c->fd);
        pthread_mutex_destroy(&c->wr);
        free(c);
        return NULL;
    }
#endif
    int slot = -1;
    pthread_mutex_lock(&g_cs_clients);
    for (int i = 0; i < MAX_CLIENTS && slot < 0; i++)
        if (!g_clients[i]) {
            g_clients[i] = c;
            slot = i;
        }
    pthread_mutex_unlock(&g_cs_clients);
    if (slot < 0) {
        blog("too many clients");
        close(c->fd);
        pthread_mutex_destroy(&c->wr);
        free(c);
        return NULL;
    }
    blog("client connected");
    for (;;) {
        vtimsg h;
        if (read_all(c->fd, &h, sizeof h) || h.len > VTIMSG_MAX || read_all(c->fd, buf, h.len))
            break;
        if (!c->ready) { // the state goes out after HELLO (the name decides), or before an older client's first message
            pthread_mutex_lock(&g_cs_dev);
            pthread_mutex_lock(&g_cs_clients);
            if (h.type == VTIMSG_HELLO && h.len > 4) {
                size_t n = h.len - 4u < sizeof c->name - 1 ? h.len - 4u : sizeof c->name - 1;
                memcpy(c->name, buf + 4, n);
                c->name[n] = 0;
                for (char *p = c->name; *p; p++)
                    if ((unsigned char)*p < 32 || *p == '\t')
                        *p = ' ';
                if (vti_is_plugin(c->name) && !g_owner)
                    g_owner = c;
            }
            uint32_t st = state_of(c, g_dev != NULL);
            c->ready = 1;
            if (vti_is_plugin(c->name))
                blog(c == g_owner ? "the plug-in owns the Virus" : "plug-in waiting: another program's plug-in uses the Virus");
            pthread_mutex_unlock(&g_cs_clients);
            pthread_mutex_unlock(&g_cs_dev);
            if (h.type == VTIMSG_HELLO)
                blog("client hello, protocol %u%s%s", h.len >= 4 ? *(uint32_t *)buf : 0, c->name[0] ? ": " : "", c->name);
            send_msg(c, VTIMSG_STATE, &st, 4, NULL, 0);
            if (h.type == VTIMSG_HELLO) {
                char info[96];
                send_msg(c, VTIMSG_INFO, info, vti_info_text(info, sizeof info), NULL, 0); // our version (INFO box)
                continue;
            }
        }
        switch (h.type) {
        case VTIMSG_HELLO: // a second HELLO changes nothing: the name and the owner were decided with the first
            break;
        case VTIMSG_OBSERVE: { // read-only observer: give the Virus to the next program if we got it
            pthread_mutex_lock(&g_cs_dev);
            pthread_mutex_lock(&g_cs_clients);
            c->observer = 1;
            if (g_owner == c) {
                g_owner = NULL;
                next_owner();
            }
            pthread_mutex_unlock(&g_cs_clients);
            pthread_mutex_unlock(&g_cs_dev);
            uint32_t ver = VTI_PROTO_VERSION, st0 = 0;
            send_msg(c, VTIMSG_STATE, &st0, 4, NULL, 0); // an observer has no device
            send_msg(c, VTIMSG_OBSERVE_REPLY, &ver, 4, NULL, 0);
            obs_attach(c); // from here on everything goes through the queue
            blog("client is an observer (read-only)");
            break;
        }
        case VTIMSG_WRITE:
            if (h.len >= 1 && may_use(c)) {
                if (buf[0] == VTI_EP_MI04_OUT && !c->notes && vti_has_note_on(buf + 1, h.len - 1))
                    c->notes = 1;
                pthread_mutex_lock(&g_cs_dev);
                int r = g_dev ? vti_write(g_dev, buf[0], buf + 1, h.len - 1) : -1;
                pthread_mutex_unlock(&g_cs_dev);
                trace_rec(0, buf[0], buf + 1, h.len - 1, NULL, 0);
#ifdef __linux__
                if (g_verbose && buf[0] == VTI_EP_MI04_OUT)
                    f5_note(buf + 1, h.len - 1);
                if (g_verbose && g_shm) // note-ons (cable 2) with the device frame they were sent at
                    for (int k = 1; k + 3 < h.len; k += 4)
                        if (buf[k] == 0x29 && (buf[k + 1] & 0xf0) == 0x90 && buf[k + 3])
                            blog("note-on %02x at device frame %lld", buf[k + 2], (long long)g_shm->in_frames);
#endif
                if (g_verbose || r) {
                    char hex[3 * 24 + 1] = "";
                    for (int k = 0; k < h.len - 1 && k < 24; k++)
                        snprintf(hex + 3 * k, 4, " %02x", buf[1 + k]);
                    blog("write ep%02x %d bytes -> %d:%s", buf[0], h.len - 1, r, hex);
                }
            }
            break;
        case VTIMSG_FLASHINFO: { // the owner only (never an observer or a waiting plug-in)
            if (c->observer)
                break;
            uint8_t fl[4 + VTI_FLASH_BLOCKS * 16];
            int32_t n = may_use(c) ? flash_info(fl + 4) : -1;
            memcpy(fl, &n, 4);
            send_msg(c, VTIMSG_FLASHINFO_REPLY, fl, 4 + (n > 0 ? n : 0), NULL, 0);
            break;
        }
        case VTIMSG_CONTROL:
            if (c->observer) // read-only; and nothing may be written to an observer outside its queue
                break;
            if (h.len >= 12) {
                uint32_t seq;
                memcpy(&seq, buf, 4);
                uint8_t *s = buf + 4, data[VTIMSG_MAX];
                uint16_t wl = (uint16_t)(s[6] | s[7] << 8);
                if (wl > VTIMSG_MAX - 16)
                    wl = VTIMSG_MAX - 16;
                if (!(s[0] & 0x80))
                    memcpy(data, buf + 12, h.len - 12 < wl ? h.len - 12 : wl);
                int owner = may_use(c), ok = vti_control_allowed(s);
                if (!ok)
                    blog("refused control request %02x %02x (only standard reads pass, rule 2)", s[0], s[1]);
                pthread_mutex_lock(&g_cs_dev);
                int32_t r = g_dev && owner && ok ? vti_control(g_dev, s[0], s[1], (uint16_t)(s[2] | s[3] << 8),
                                                (uint16_t)(s[4] | s[5] << 8), data, wl)
                                  : -1;
                pthread_mutex_unlock(&g_cs_dev);
                { // trace file and observers
                    uint8_t rec[8 + 4];
                    memcpy(rec, s, 8);
                    memcpy(rec + 8, &r, 4);
                    trace_rec(2, 0, rec, 12, data, (s[0] & 0x80) ? (r > 0 ? r : 0) : wl);
                }
                uint8_t hdr[8];
                memcpy(hdr, &seq, 4);
                memcpy(hdr + 4, &r, 4);
                send_msg(c, VTIMSG_CONTROL_REPLY, hdr, 8, data, (s[0] & 0x80) && r > 0 ? r : 0);
            }
            break;
        case VTIMSG_AUDIO: { // macOS: audio comes from Core Audio, not from the broker (reply 0)
            if (c->observer)
                break;
            int32_t r = 0;
#ifdef __linux__
            uint32_t on = h.len >= 4 ? *(uint32_t *)buf : 0;
            pthread_mutex_lock(&g_cs_dev);
            pthread_mutex_lock(&g_cs_clients);
            int use = !waits(c), stop = vti_may_stop_audio(vti_is_plugin(c->name), c == g_owner, g_owner != NULL);
            int owner = c == g_owner || !g_owner;
            pthread_mutex_unlock(&g_cs_clients);
            uint32_t want = vti_audio_req_rate(buf, h.len);
            if (on)
                r = g_dev && use ? audio_start(want, owner) : -1;
            else if (stop)
                audio_stop();
            pthread_mutex_unlock(&g_cs_dev);
            pthread_mutex_lock(&g_cs_clients);
            c->audio = on && r == 1;
            pthread_mutex_unlock(&g_cs_clients);
            if (on)
                blog("audio on (%.1f kHz asked) -> %d%s", want / 1000.0, r, r == 1 ? (g_shm->rate == 48000 ?
                     ", 48 kHz" : ", 44.1 kHz") : "");
            else
                blog("audio off -> %d", r);
#endif
            send_msg(c, VTIMSG_AUDIO_REPLY, &r, 4, NULL, 0);
            break;
        }
        default:
            blog("client: unknown message %u", h.type);
        }
    }
    if (c->observer)
        obs_detach(c);
    blog("client disconnected%s", c->observer ? " (observer)" : "");
    pthread_mutex_lock(&g_cs_dev);
    if (c->notes && g_dev) { // a DAW that crashed or was killed cannot send its note-offs any more
        uint8_t off[256];
        int n = vti_pack_notes_off(off, sizeof off);
        blog("all notes off (the client had played notes) -> %d", n > 0 ? vti_write(g_dev, VTI_EP_MI04_OUT, off, n) : -1);
    }
    pthread_mutex_lock(&g_cs_clients);
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (g_clients[i] == c)
            g_clients[i] = NULL;
    if (g_owner == c) { // hand the Virus to the next waiting program's plug-in
        g_owner = NULL;
        audio_stop();
        next_owner();
    } else if (c->audio && !g_owner) { // a sharing client that started the stream while no plug-in was there
        int others = 0;
        for (int i = 0; i < MAX_CLIENTS; i++)
            others |= g_clients[i] && g_clients[i]->audio;
        if (!others)
            audio_stop();
    }
    pthread_mutex_unlock(&g_cs_clients);
    pthread_mutex_unlock(&g_cs_dev);
    close(c->fd);
    pthread_mutex_destroy(&c->wr);
    free(c);
    return NULL;
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        g_verbose += !strcmp(argv[i], "-v") + 2 * !strcmp(argv[i], "-vv");
        g_stay |= !strcmp(argv[i], "-stay");
    }
    signal(SIGPIPE, SIG_IGN);
    clock_gettime(CLOCK_MONOTONIC, &g_t0);
    static sigset_t sigs; // handled by signal_thread; blocked in every thread created after this
    sigemptyset(&sigs);
    sigaddset(&sigs, SIGINT);
    sigaddset(&sigs, SIGTERM);
    sigaddset(&sigs, SIGHUP);
    pthread_sigmask(SIG_BLOCK, &sigs, NULL);
    vti_log = on_vti_log;
    const char *tp = getenv("VTI_TRACE");
    if (tp && (g_trace = fopen(tp, "ab"))) {
        if (ftell(g_trace) == 0)
            fwrite("VTTRACE1", 1, 8, g_trace);
        blog("tracing to %s", tp);
    }
    struct sockaddr_un sa = {.sun_family = AF_UNIX};
    snprintf(sa.sun_path, sizeof sa.sun_path, VTI_SOCK_FMT, (unsigned)getuid());
    int s = -1;
#ifdef __APPLE__
    // Started by launchd socket activation (LaunchAgent with a "Listeners" socket, scripts/mac-install-broker.sh):
    // launchd owns the socket and starts us on the first connect; after the idle exit it starts us again.
    int *fds = NULL;
    size_t nfds = 0;
    if (launch_activate_socket("Listeners", &fds, &nfds) == 0 && nfds > 0) {
        s = fds[0];
        free(fds);
        blog("socket from launchd");
    }
#endif
#ifdef __linux__
    // Started by systemd socket activation (tirevive-broker.socket, installed by the Linux installer): the socket
    // arrives as fd 3; systemd starts us again on the next connect after the idle exit.
    const char *lpid = getenv("LISTEN_PID"), *lfds = getenv("LISTEN_FDS");
    int tcp = -1;
    // the TCP listener only served the classic plug-in under Wine (TI Revive 0.7); since 0.8 the plug-in is native and
    // uses the Unix socket, so TCP is off unless asked for (VTI_BROKER_TCP=1)
    const int tcp_wanted = getenv("VTI_BROKER_TCP") && atoi(getenv("VTI_BROKER_TCP")) == 1;
    if (lpid && lfds && (pid_t)atol(lpid) == getpid() && atoi(lfds) >= 1) {
        s = 3;
        if (atoi(lfds) >= 2)
            tcp = 4; // second ListenStream of tirevive-broker.socket: 127.0.0.1:<port>
        blog("socket from systemd%s", tcp >= 0 ? " (+ TCP)" : "");
    }
    if (!tcp_wanted && tcp >= 0) { // an older socket unit still passes the TCP socket: do not serve it
        close(tcp);
        tcp = -1;
    }
    if (tcp_wanted && token_load())
        blog("token file could not be read or created: TCP clients (Wine) cannot authenticate");
#endif
    if (s < 0) {
        s = socket(AF_UNIX, SOCK_STREAM, 0);
        // single instance: if a broker answers on the socket, leave; else remove a stale socket file
        if (connect(s, (struct sockaddr *)&sa, sizeof sa) == 0) {
            blog("another vtibroker is running, exiting");
            return 0;
        }
        close(s);
        unlink(sa.sun_path);
        s = socket(AF_UNIX, SOCK_STREAM, 0);
        if (bind(s, (struct sockaddr *)&sa, sizeof sa) || chmod(sa.sun_path, 0600) || listen(s, 8)) {
            blog("socket %s: %s", sa.sun_path, strerror(errno));
            return 1;
        }
    }
#ifdef __linux__
    if (tcp_wanted && tcp < 0 && g_tcp_port > 0) {
        tcp = socket(AF_INET, SOCK_STREAM, 0);
        int one = 1;
        setsockopt(tcp, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        struct sockaddr_in in = {.sin_family = AF_INET, .sin_port = htons((uint16_t)g_tcp_port),
                                 .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
        if (bind(tcp, (struct sockaddr *)&in, sizeof in) || listen(tcp, 8)) {
            blog("TCP 127.0.0.1:%d: %s", g_tcp_port, strerror(errno));
            close(tcp);
            tcp = -1;
        }
    }
    if (tcp >= 0)
        blog("TCP listener on 127.0.0.1:%d (token required)", g_tcp_port);
#endif
    blog("vtibroker starting (protocol %d, %s)", VTI_PROTO_VERSION, sa.sun_path);
#ifdef __APPLE__
    // stand-in for Access' MIDI driver towards the original Virus Control (E100): only the classic path of 0.7.x
    // (bridge + Rosetta helper, src/mac) used it; the native plug-in talks to the broker directly. Opt-in.
    if (getenv("VTI_BROKER_MIDISHIM") && atoi(getenv("VTI_BROKER_MIDISHIM")) == 1) {
        if (vtimidi_start(midi_write))
            blog("MIDI slot memory could not be created");
        else
            blog("MIDI driver stand-in for the original plug-in started (VTI_BROKER_MIDISHIM=1)");
    }
#endif
    pthread_t t;
    pthread_create(&t, NULL, signal_thread, &sigs);
    pthread_detach(t);
    pthread_create(&t, NULL, device_thread, NULL);
    pthread_detach(t);
#ifdef __linux__
    pthread_create(&t, NULL, tick_thread, NULL);
    pthread_detach(t);
#endif
    struct pollfd pf[2] = {{.fd = s, .events = POLLIN}, {.fd = -1, .events = POLLIN}};
#ifdef __linux__
    pf[1].fd = tcp;
#endif
    for (;;) {
        if (poll(pf, 2, -1) < 0)
            continue;
        for (int k = 0; k < 2; k++) {
            if (pf[k].fd < 0 || !(pf[k].revents & POLLIN))
                continue;
            int fd = accept(pf[k].fd, NULL, NULL);
            if (fd < 0) {
                if (errno != EINTR)
                    blog("accept: %s", strerror(errno));
                continue;
            }
            client *c = calloc(1, sizeof *c);
            c->fd = fd;
            c->tcp = k == 1;
#ifdef __linux__
            if (c->tcp) { // small messages (ticks every 10 ms, MIDI): no Nagle delay (bursts of 20-80 ms, E118)
                int one = 1;
                setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
            }
#endif
            pthread_mutex_init(&c->wr, NULL);
            pthread_create(&t, NULL, client_thread, c);
            pthread_detach(t);
        }
    }
}
