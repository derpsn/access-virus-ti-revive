// The brokers' control request filter (AGENTS.md rule 2): a client may only read standard information (descriptors,
// status, configuration); vendor and class requests, and every host-to-device request, never reach the Virus.
// Hardware-free: the decision only.
#include "check.h"
extern "C" {
#include "ctrlguard.h"
}

static int allowed(uint8_t rt, uint8_t req)
{
    const uint8_t setup[8] = {rt, req, 0, 1, 0, 0, 18, 0};
    return vti_control_allowed(setup);
}

TEST(ctrlguard_standard_reads)
{
    CHECK(allowed(0x80, 6) == 1);  // GET_DESCRIPTOR (device): plug-in and Control Center ask for the model this way
    CHECK(allowed(0x80, 0) == 1);  // GET_STATUS
    CHECK(allowed(0x80, 8) == 1);  // GET_CONFIGURATION
    CHECK(allowed(0x81, 6) == 1);  // GET_DESCRIPTOR (interface)
    CHECK(allowed(0x81, 10) == 1); // GET_INTERFACE
    CHECK(allowed(0x82, 0) == 1);  // GET_STATUS (endpoint)
}

TEST(ctrlguard_blocks_vendor_and_writes)
{
    CHECK(allowed(0x40, 1) == 0);    // vendor OUT to the device (flash protocol address write, E026/E040)
    CHECK(allowed(0x41, 1) == 0);    // vendor OUT to the interface
    CHECK(allowed(0xC0, 0x81) == 0); // vendor IN (flash status/data, E040): not needed by any client
    CHECK(allowed(0xC1, 0x81) == 0);
    CHECK(allowed(0x21, 1) == 0);    // class OUT (e.g. audio SET_CUR)
    CHECK(allowed(0xA1, 0x81) == 0); // class IN
    CHECK(allowed(0x00, 9) == 0);    // SET_CONFIGURATION
    CHECK(allowed(0x01, 11) == 0);   // SET_INTERFACE
    CHECK(allowed(0x00, 7) == 0);    // SET_DESCRIPTOR
    CHECK(allowed(0x02, 1) == 0);    // CLEAR_FEATURE (endpoint)
    CHECK(allowed(0x80, 7) == 0);    // a standard IN type with a host-to-device request number
    CHECK(allowed(0x83, 6) == 0);    // recipient "other"
    CHECK(allowed(0x80, 12) == 0);   // SYNCH_FRAME is not needed either
}

// audit 2026-11-17 #2: a client's WRITE names its endpoint; every client writes the plug-in channel EP 0x06 only.
// EP 0x05 (MI03 OUT) is the USB chip's command endpoint (one known command, the driver's 4E 73 52 01 at device start, E030; the broker sends it itself); an IN endpoint on libusb
// would run an IN transfer into the client's buffer and steal the reader's data
TEST(ctrlguard_write_only_to_the_plugin_channel)
{
    CHECK(vti_write_allowed(0x06) == 1);
    CHECK(vti_write_allowed(0x05) == 0);
    CHECK(vti_write_allowed(0x85) == 0);
    CHECK(vti_write_allowed(0x86) == 0);
    CHECK(vti_write_allowed(0x00) == 0);
    CHECK(vti_write_allowed(0x01) == 0);
    CHECK(vti_write_allowed(0x02) == 0); // audio OUT is the broker's own (shared memory), never a client write
    CHECK(vti_write_allowed(0xFF) == 0);
}
