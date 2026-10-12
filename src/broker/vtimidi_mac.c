// vtimidi_mac.c - see vtimidi_mac.h. Layouts from the Access MIDI driver (REA, E095/E100):
//   VirusTICommMem (0x4000 B): u16 +0 version 2, u32 +4 size 0x4000, u16 +8 slot count 16, slots of 0x310 B
//   from +0xC: u8 +0 used, u8 +1 active, u32 +4 device key, char[] +0x8 port name "VirusTIMDP%08x",
//   +0x108 write shm "VirusTIMDW%08x", u32 +0x208 its size, +0x20C read shm "VirusTIMDR%08x", u32 +0x30C its size.
//   Ring (MDW/MDR, 0x1000 B): u32 +0 read index, u32 +4 write index, u32 +8 capacity in words = (size - 12) / 4,
//   u32 words from +0xC; empty when read == write, full when (write + 1) % capacity == read.
//   MDP (CFMessagePort): 'st01'..'st09' set the driver's write timer interval, 'stop' stops it; the driver drains
//   MDW on that timer. We drain MDW every millisecond regardless, so the messages are only logged.
#include "vtimidi_mac.h"
#include <CoreFoundation/CoreFoundation.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define COMM_SIZE 0x4000
#define SLOT_OFS 0xC
#define SLOT_SIZE 0x310
#define RING_SIZE 0x1000
#define RING_CAP ((RING_SIZE - 12) / 4)

extern void (*vti_log)(const char *fmt, ...);

static uint8_t *g_comm, *g_wr, *g_rd; // slot table, MDW ring, MDR ring
static int (*g_write)(const uint8_t *, int);
static pthread_mutex_t g_rd_lock = PTHREAD_MUTEX_INITIALIZER;
static volatile unsigned long g_n_out, g_n_in; // packets plug-in -> device, device -> plug-in (logged on change)

static uint8_t *map_shm(const char *name, size_t size)
{
    int fd = shm_open(name, O_RDWR | O_CREAT, 0644);
    if (fd < 0) {
        vti_log("midi: shm_open %s failed", name);
        return NULL;
    }
    ftruncate(fd, (off_t)size); // fails harmlessly if the object already has this size
    void *p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    return p == MAP_FAILED ? NULL : p;
}

static uint32_t *ring_hdr(uint8_t *r) { return (uint32_t *)r; }
static void ring_init(uint8_t *r)
{
    memset(r, 0, RING_SIZE);
    ring_hdr(r)[2] = RING_CAP;
}

static void drain_mdw(void)
{
    uint32_t *h = ring_hdr(g_wr), *words = h + 3;
    uint32_t rd = __atomic_load_n(&h[0], __ATOMIC_RELAXED), wr = __atomic_load_n(&h[1], __ATOMIC_ACQUIRE);
    uint8_t buf[256];
    int n = 0;
    while (rd != wr) {
        memcpy(buf + n, &words[rd], 4);
        n += 4;
        g_n_out++;
        rd = (rd + 1) % RING_CAP;
        if (n == sizeof buf || rd == wr) {
            if (g_write(buf, n))
                vti_log("midi: write of %d bytes failed", n);
            n = 0;
        }
    }
    __atomic_store_n(&h[0], rd, __ATOMIC_RELEASE);
}

void vtimidi_rx(const uint8_t *data, int len)
{
    if (!g_rd)
        return;
    pthread_mutex_lock(&g_rd_lock);
    uint32_t *h = ring_hdr(g_rd), *words = h + 3;
    uint32_t wr = __atomic_load_n(&h[1], __ATOMIC_RELAXED);
    for (int i = 0; i + 4 <= len; i += 4) {
        if (!data[i] && !data[i + 1] && !data[i + 2] && !data[i + 3])
            continue; // padding
        if ((wr + 1) % RING_CAP == __atomic_load_n(&h[0], __ATOMIC_ACQUIRE)) {
            vti_log("midi: MDR ring full, dropping");
            break;
        }
        memcpy(&words[wr], data + i, 4);
        wr = (wr + 1) % RING_CAP;
        g_n_in++;
    }
    __atomic_store_n(&h[1], wr, __ATOMIC_RELEASE);
    pthread_mutex_unlock(&g_rd_lock);
}

static CFDataRef on_port_msg(CFMessagePortRef port, SInt32 id, CFDataRef data, void *info)
{
    (void)port, (void)data, (void)info;
    vti_log("midi: port message '%c%c%c%c'", (char)(id >> 24), (char)(id >> 16), (char)(id >> 8), (char)id);
    return NULL;
}

static void on_timer(CFRunLoopTimerRef t, void *info)
{
    (void)t, (void)info;
    drain_mdw();
    static unsigned ticks;
    static unsigned long last_out, last_in;
    if (++ticks % 2000 == 0 && (g_n_out != last_out || g_n_in != last_in)) {
        vti_log("midi: %lu packets to the device, %lu from the device", g_n_out, g_n_in);
        last_out = g_n_out, last_in = g_n_in;
    }
}

static void *midi_thread(void *name)
{
    CFStringRef n = CFStringCreateWithCString(NULL, name, kCFStringEncodingASCII);
    Boolean shared = false;
    CFMessagePortRef port = CFMessagePortCreateLocal(NULL, n, on_port_msg, NULL, &shared);
    CFRelease(n);
    if (!port) {
        vti_log("midi: CFMessagePortCreateLocal %s failed (another MIDI driver running?)", (char *)name);
        return NULL;
    }
    CFRunLoopSourceRef src = CFMessagePortCreateRunLoopSource(NULL, port, 0);
    CFRunLoopAddSource(CFRunLoopGetCurrent(), src, kCFRunLoopDefaultMode);
    CFRunLoopTimerRef t = CFRunLoopTimerCreate(NULL, CFAbsoluteTimeGetCurrent() + 0.001, 0.001, 0, 0, on_timer, NULL);
    CFRunLoopAddTimer(CFRunLoopGetCurrent(), t, kCFRunLoopDefaultMode);
    vti_log("midi: serving slot 0 (%s)", (char *)name);
    CFRunLoopRun();
    return NULL;
}

int vtimidi_start(int (*write)(const uint8_t *data, int len))
{
    g_write = write;
    if (!(g_comm = map_shm("VirusTICommMem", COMM_SIZE)))
        return -1;
    memset(g_comm, 0, COMM_SIZE);
    *(uint16_t *)(g_comm + 0) = 2;
    *(uint32_t *)(g_comm + 4) = COMM_SIZE;
    *(uint16_t *)(g_comm + 8) = 16;
    uint8_t *s = g_comm + SLOT_OFS; // slot 0: the one Virus the broker serves
    static char port_name[32];
    snprintf(port_name, sizeof port_name, "VirusTIMDP%08x", 0);
    strcpy((char *)s + 0x8, port_name);
    snprintf((char *)s + 0x108, 0x100, "VirusTIMDW%08x", 0);
    snprintf((char *)s + 0x20C, 0x100, "VirusTIMDR%08x", 0);
    *(uint32_t *)(s + 0x208) = RING_SIZE;
    *(uint32_t *)(s + 0x30C) = RING_SIZE;
    if (!(g_wr = map_shm((char *)s + 0x108, RING_SIZE)) || !(g_rd = map_shm((char *)s + 0x20C, RING_SIZE)))
        return -1;
    ring_init(g_wr);
    ring_init(g_rd);
    *(uint32_t *)(s + 4) = VTI_MAC_DEVKEY;
    s[1] = 1; // active
    __atomic_store_n(&s[0], 1, __ATOMIC_RELEASE); // used: publish last
    pthread_t t;
    if (pthread_create(&t, NULL, midi_thread, port_name))
        return -1;
    pthread_detach(t);
    return 0;
}
