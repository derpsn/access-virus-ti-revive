// owner.h - who may use the Virus when several clients are connected (both brokers; E148, E166, E181).
// Plug-ins of different programs must not share it: each sends its own sync ping and resets the Virus' only stamp
// clock. So the first "TI Revive Virus Control" owns it and further plug-ins wait (state 2, no RX, no writes) until it
// leaves. Every other client - Control Center, tirevive-cc, the ASIO driver, clients without a name - shares the Virus
// with the owner, but only the owner may stop its audio stream (a second program restarting it mid-session left the
// Virus without USB audio until a power cycle, E112). Header only: the brokers and the tests use it.
#pragma once
#include <stdint.h>
#include <string.h>

#define VTI_PLUGIN_NAME "TI Revive Virus Control"

static inline int vti_is_plugin(const char *name) { return strcmp(name, VTI_PLUGIN_NAME) == 0; }

#define VTI_ASIO_NAME "TI Revive Virus Audio (ASIO)"

static inline int vti_is_asio(const char *name) { return strcmp(name, VTI_ASIO_NAME) == 0; }

// Who may set the Virus' sample rate (Windows: one stream for every client, like the original's single driver rate;
// design-rate.md step 4): the owning plug-in, or the ASIO driver while no plug-in owns the Virus - never a client
// next to another program's plug-in (E112: restarting the stream under the owner).
static inline int vti_rate_setter(int is_owner, int has_owner, int asio) { return is_owner || (asio && !has_owner); }

static inline int vti_client_waits(int plugin, int plugin_owner) { return plugin && !plugin_owner; }

// the VTIMSG_STATE a client is told: 0 no Virus, 1 open, 2 another program's plug-in has it
static inline uint32_t vti_client_state(int dev_open, int plugin, int plugin_owner)
{
    if (!dev_open)
        return 0;
    return vti_client_waits(plugin, plugin_owner) ? 2 : 1;
}

// may this client's "audio off" stop the stream: the owning plug-in, or anyone while no plug-in owns the Virus
static inline int vti_may_stop_audio(int plugin, int plugin_owner, int owner_exists)
{
    return plugin ? plugin_owner : !owner_exists;
}
