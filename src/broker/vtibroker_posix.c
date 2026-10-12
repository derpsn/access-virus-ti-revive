// vtibroker_posix.c - the broker for macOS (and Linux): owns the Virus TI's vendor interfaces MI_03/MI_04 over libusb
// (vticore.c) and serves clients over a Unix domain socket with the same messages as on Windows (vtiproto.h).
// Started on demand (LaunchAgent / first client) and exits when no client has been connected for IDLE_EXIT_S
// seconds, like vtibroker.exe since 0.3.1 (E080). Audio is not streamed here: on macOS the Virus' class-compliant
// audio function stays with Apple's USB audio driver (Core Audio), see findings/design-macos.md.
//
// usage: vtibroker [-v] [-stay]     socket: /tmp/vtibroker-<uid>.sock (VTI_SOCK_FMT)
#include "vticore.h"
#include "vtiproto.h"
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#ifdef __APPLE__
#include "vtimidi_mac.h"
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
} client;

static client *g_clients[MAX_CLIENTS];
static pthread_mutex_t g_cs_clients = PTHREAD_MUTEX_INITIALIZER, g_cs_dev = PTHREAD_MUTEX_INITIALIZER,
                       g_cs_log = PTHREAD_MUTEX_INITIALIZER;
static vti *g_dev;
static int g_verbose, g_stay;

static void blog(const char *fmt, ...)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm;
    localtime_r(&ts.tv_sec, &tm);
    pthread_mutex_lock(&g_cs_log);
    fprintf(stderr, "%02d:%02d:%02d.%03ld ", tm.tm_hour, tm.tm_min, tm.tm_sec, ts.tv_nsec / 1000000);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    pthread_mutex_unlock(&g_cs_log);
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

static void broadcast(uint16_t type, const void *a, int alen, const void *b, int blen)
{
    pthread_mutex_lock(&g_cs_clients);
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (g_clients[i])
            send_msg(g_clients[i], type, a, alen, b, blen);
    pthread_mutex_unlock(&g_cs_clients);
}

static void on_rx(void *user, uint8_t ep, const uint8_t *data, int len)
{
    (void)user;
#ifdef __APPLE__
    if (ep == VTI_EP_MI04_IN)
        vtimidi_rx(data, len);
#endif
    broadcast(VTIMSG_RX, &ep, 1, data, len);
}

#ifdef __APPLE__
static int midi_write(const uint8_t *data, int len) // pump thread: plug-in channel packets from the MDW ring
{
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
            if (g_dev)
                vti_close(g_dev);
            exit(0);
        }
        pthread_mutex_lock(&g_cs_dev);
        if (g_dev && vti_is_dead(g_dev)) {
            blog("device failed: closing");
            vti_close(g_dev);
            g_dev = NULL;
            uint32_t st = 0;
            broadcast(VTIMSG_STATE, &st, 4, NULL, 0);
        }
        if (!g_dev && (g_dev = vti_open(on_rx, NULL))) {
            blog("device open, handshake sent");
            uint32_t st = 1;
            broadcast(VTIMSG_STATE, &st, 4, NULL, 0);
        }
        int open = g_dev != NULL;
        pthread_mutex_unlock(&g_cs_dev);
        usleep(open ? 500000 : 2000000);
    }
    return NULL;
}

static void *client_thread(void *arg)
{
    client *c = arg;
    uint8_t buf[VTIMSG_MAX];
    blog("client connected");
    pthread_mutex_lock(&g_cs_dev);
    uint32_t st = g_dev != NULL;
    pthread_mutex_unlock(&g_cs_dev);
    send_msg(c, VTIMSG_STATE, &st, 4, NULL, 0);
    for (;;) {
        vtimsg h;
        if (read_all(c->fd, &h, sizeof h) || h.len > VTIMSG_MAX || read_all(c->fd, buf, h.len))
            break;
        switch (h.type) {
        case VTIMSG_HELLO:
            blog("client hello, protocol %u", h.len >= 4 ? *(uint32_t *)buf : 0);
            break;
        case VTIMSG_WRITE:
            if (h.len >= 1) {
                pthread_mutex_lock(&g_cs_dev);
                int r = g_dev ? vti_write(g_dev, buf[0], buf + 1, h.len - 1) : -1;
                pthread_mutex_unlock(&g_cs_dev);
                if (g_verbose || r)
                    blog("write ep%02x %d bytes -> %d", buf[0], h.len - 1, r);
            }
            break;
        case VTIMSG_CONTROL:
            if (h.len >= 12) {
                uint32_t seq;
                memcpy(&seq, buf, 4);
                uint8_t *s = buf + 4, data[VTIMSG_MAX];
                uint16_t wl = (uint16_t)(s[6] | s[7] << 8);
                if (wl > VTIMSG_MAX - 16)
                    wl = VTIMSG_MAX - 16;
                if (!(s[0] & 0x80))
                    memcpy(data, buf + 12, h.len - 12 < wl ? h.len - 12 : wl);
                pthread_mutex_lock(&g_cs_dev);
                int32_t r = g_dev ? vti_control(g_dev, s[0], s[1], (uint16_t)(s[2] | s[3] << 8),
                                                (uint16_t)(s[4] | s[5] << 8), data, wl)
                                  : -1;
                pthread_mutex_unlock(&g_cs_dev);
                uint8_t hdr[8];
                memcpy(hdr, &seq, 4);
                memcpy(hdr + 4, &r, 4);
                send_msg(c, VTIMSG_CONTROL_REPLY, hdr, 8, data, (s[0] & 0x80) && r > 0 ? r : 0);
            }
            break;
        case VTIMSG_AUDIO: { // macOS: audio comes from Core Audio, not from the broker
            int32_t r = 0;
            send_msg(c, VTIMSG_AUDIO_REPLY, &r, 4, NULL, 0);
            break;
        }
        default:
            blog("client: unknown message %u", h.type);
        }
    }
    blog("client disconnected");
    pthread_mutex_lock(&g_cs_clients);
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (g_clients[i] == c)
            g_clients[i] = NULL;
    pthread_mutex_unlock(&g_cs_clients);
    close(c->fd);
    pthread_mutex_destroy(&c->wr);
    free(c);
    return NULL;
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        g_verbose |= !strcmp(argv[i], "-v");
        g_stay |= !strcmp(argv[i], "-stay");
    }
    signal(SIGPIPE, SIG_IGN);
    vti_log = on_vti_log;
    struct sockaddr_un sa = {.sun_family = AF_UNIX};
    snprintf(sa.sun_path, sizeof sa.sun_path, VTI_SOCK_FMT, (unsigned)getuid());
    int s = socket(AF_UNIX, SOCK_STREAM, 0);
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
    blog("vtibroker starting (protocol %d, %s)", VTI_PROTO_VERSION, sa.sun_path);
#ifdef __APPLE__
    if (vtimidi_start(midi_write)) // stands in for Access' MIDI driver towards Virus Control (E100)
        blog("MIDI slot memory could not be created");
#endif
    pthread_t t;
    pthread_create(&t, NULL, device_thread, NULL);
    pthread_detach(t);
    for (;;) {
        int fd = accept(s, NULL, NULL);
        if (fd < 0) {
            if (errno != EINTR)
                blog("accept: %s", strerror(errno));
            continue;
        }
        client *c = calloc(1, sizeof *c);
        c->fd = fd;
        pthread_mutex_init(&c->wr, NULL);
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
            close(fd);
            pthread_mutex_destroy(&c->wr);
            free(c);
            continue;
        }
        pthread_create(&t, NULL, client_thread, c);
        pthread_detach(t);
    }
}
