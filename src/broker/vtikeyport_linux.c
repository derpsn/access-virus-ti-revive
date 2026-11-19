// vtikeyport_linux.c - see vtikeyport.h. Linux: two ports of an ALSA sequencer client "Virus TI", numbered and named as
// the kernel's own driver had them when it owned the Virus (E016: port 0 "Virus TI MIDI", port 1 "Virus TI Synth"),
// so a DAW finds them under the same names. The broker owns the Virus' interfaces through libusb, and with them gone
// from snd-usb-audio the kernel's ports are gone too. Straight on /dev/snd/seq with the kernel's ioctls (seqevent.h):
// no alsa-lib in the broker. PipeWire and JACK bridges pick sequencer ports up like any other.
#include "vtikeyport.h"
#include "seqevent.h"
#include "vticore.h"
#include "devslot.h"
#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define PORT_MIDI 0  // cable 0: the DIN MIDI In
#define PORT_SYNTH 1 // cable 1: the Virus keys

static atomic_int g_fd = -1;
static atomic_uint g_sent[2], g_failed[2]; // for the log: did the messages get as far as the sequencer
static atomic_int g_made[2];
static char g_name[2][32] = {"Virus TI MIDI", "Virus TI Synth"}; // Virus N of several: "Virus TI N MIDI" (#45)

static int make_port(int fd, int client, int port)
{
    struct snd_seq_port_info pi;
    memset(&pi, 0, sizeof pi);
    pi.addr.client = (unsigned char)client;
    pi.addr.port = (unsigned char)port;
    pi.flags = SNDRV_SEQ_PORT_FLG_GIVEN_PORT; // the kernel driver's numbers, not the next free ones
    snprintf(pi.name, sizeof pi.name, "%s", g_name[port]);
    pi.capability = SNDRV_SEQ_PORT_CAP_READ | SNDRV_SEQ_PORT_CAP_SUBS_READ; // a source only: nothing can write to it
    pi.type = SNDRV_SEQ_PORT_TYPE_MIDI_GENERIC | SNDRV_SEQ_PORT_TYPE_HARDWARE | SNDRV_SEQ_PORT_TYPE_PORT;
    pi.midi_channels = 16;
    if (ioctl(fd, SNDRV_SEQ_IOCTL_CREATE_PORT, &pi) < 0) {
        vti_log("MIDI port \"%s\": not created (%s)\n", g_name[port], strerror(errno));
        return 0;
    }
    return 1;
}

// Quick (no server to wait for, unlike CoreMIDI): runs on the broker's start-up path
void vti_keyport_start(void)
{
    vti_slot_port_name(g_name[0], sizeof g_name[0], "MIDI", vti_slot);
    vti_slot_port_name(g_name[1], sizeof g_name[1], "Synth", vti_slot);
    // non-blocking: the USB reader thread writes, and a full sequencer pool must cost a message, never a stall
    const int fd = open("/dev/snd/seq", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        vti_log("MIDI port: no ALSA sequencer (/dev/snd/seq: %s)\n", strerror(errno));
        return;
    }
    int client = -1;
    struct snd_seq_client_info ci;
    memset(&ci, 0, sizeof ci);
    if (ioctl(fd, SNDRV_SEQ_IOCTL_CLIENT_ID, &client) < 0 || client < 0) {
        vti_log("MIDI port: no sequencer client (%s)\n", strerror(errno));
        close(fd);
        return;
    }
    ci.client = client;
    if (ioctl(fd, SNDRV_SEQ_IOCTL_GET_CLIENT_INFO, &ci) == 0) {
        vti_slot_port_name(ci.name, sizeof ci.name, "", vti_slot);
        if (ioctl(fd, SNDRV_SEQ_IOCTL_SET_CLIENT_INFO, &ci) < 0) // the ports still work under the default name
            vti_log("MIDI port: client name not set (%s)\n", strerror(errno));
    }
    int any = 0;
    for (int k = 0; k < 2; k++) // both, as the kernel driver had them; one failing leaves the other
        if (make_port(fd, client, k)) {
            atomic_store(&g_made[k], 1);
            any = 1;
            vti_log("MIDI port \"%s\" is there (sequencer %d:%d, %s to the DAW, read-only)\n", g_name[k], client, k,
                    k == PORT_SYNTH ? "Virus keys" : "the Virus' MIDI In");
        }
    if (!any) {
        close(fd);
        return;
    }
    atomic_store(&g_fd, fd);
}

static void send_to(int port, const uint8_t *msg, int n)
{
    const int fd = atomic_load(&g_fd);
    if (fd < 0 || !atomic_load(&g_made[port]))
        return;
    uint8_t buf[sizeof(struct snd_seq_event) + 16];
    const int len = vti_seq_encode(msg, n, port, buf, (int)sizeof buf);
    if (len <= 0)
        return; // nothing a sequencer event can carry (vti_seq_encode drops incomplete and undefined messages)
    const ssize_t w = write(fd, buf, (size_t)len);
    if (w == len) {
        if (atomic_fetch_add(&g_sent[port], 1) == 0)
            vti_log("MIDI port \"%s\": first message handed to the sequencer\n", g_name[port]);
    } else if (atomic_fetch_add(&g_failed[port], 1) == 0) { // once: this runs per message on the USB reader thread
        vti_log("MIDI port \"%s\": message not handed to the sequencer (%s)\n", g_name[port],
                w < 0 ? strerror(errno) : "short write");
    }
}

void vti_keyport_send(const uint8_t *msg, int n) { send_to(PORT_SYNTH, msg, n); }
void vti_dinport_send(const uint8_t *msg, int n) { send_to(PORT_MIDI, msg, n); }

void vti_keyport_log_counts(void)
{
    for (int k = 0; k < 2; k++)
        if (atomic_load(&g_made[k]))
            vti_log("MIDI port \"%s\": %u messages handed to the sequencer, %u failed\n", g_name[k],
                    atomic_load(&g_sent[k]), atomic_load(&g_failed[k]));
}
