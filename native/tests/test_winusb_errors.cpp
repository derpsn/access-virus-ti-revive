// When the Windows broker's MIDI reader gives up on the device (src/broker/vtireaderr.h): a removed or vanished
// device must end the reader, so the broker closes it and opens the Virus again when it comes back. ERROR_NO_SUCH_DEVICE
// (433) was missing: the reader logged it every 10 ms and the broker stayed on a stale handle (sim S124, #36's log).
#include "check.h"
#include "vtireaderr.h"

TEST(winusb_reader_gives_up_on_a_gone_device)
{
    CHECK(vti_read_error_dead(22, 0));   // ERROR_BAD_COMMAND
    CHECK(vti_read_error_dead(1167, 0)); // ERROR_DEVICE_NOT_CONNECTED
    CHECK(vti_read_error_dead(6, 0));    // ERROR_INVALID_HANDLE
    CHECK(vti_read_error_dead(433, 0));  // ERROR_NO_SUCH_DEVICE
}

TEST(winusb_reader_keeps_reading_through_stalls)
{
    CHECK(!vti_read_error_dead(31, 1));  // ERROR_GEN_FAILURE: a stalled pipe, reset and go on
    CHECK(!vti_read_error_dead(31, 50));
    CHECK(vti_read_error_dead(31, 51));  // stalled beyond recovery
    CHECK(!vti_read_error_dead(995, 0)); // ERROR_OPERATION_ABORTED: a cancelled read, not a gone device
}
