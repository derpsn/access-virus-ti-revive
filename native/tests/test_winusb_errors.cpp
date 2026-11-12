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

// The libusb reader (vticore.c, macOS and Linux) had no such rule: any error but NO_DEVICE was retried at once, so a
// stalled endpoint spun at ~25000 reads a second and filled the broker log (#37: 6.3 M LIBUSB_ERROR_PIPE lines in 2
// minutes on Catalina). Now as on Windows: a stall is cleared, every error waits, a run of errors ends the reader.
TEST(libusb_reader_gives_up_on_a_gone_device)
{
    CHECK(vti_libusb_read_dead(-4, 1));   // LIBUSB_ERROR_NO_DEVICE
    CHECK(!vti_libusb_read_dead(-9, 1));  // LIBUSB_ERROR_PIPE: clear the halt and go on
    CHECK(!vti_libusb_read_dead(-1, 50)); // LIBUSB_ERROR_IO, still in range
    CHECK(vti_libusb_read_dead(-9, 51));  // stalled beyond recovery: the broker closes and opens the Virus again
    CHECK(vti_libusb_read_dead(-99, 51)); // LIBUSB_ERROR_OTHER, the same
}

TEST(libusb_reader_clears_only_a_stall)
{
    CHECK(vti_libusb_stalled(-9));
    CHECK(!vti_libusb_stalled(-1));
    CHECK(!vti_libusb_stalled(-4));
}

TEST(reader_logs_only_the_start_of_an_error_run)
{
    CHECK(vti_read_error_logged(1));
    CHECK(vti_read_error_logged(3));
    CHECK(!vti_read_error_logged(4));
    CHECK(!vti_read_error_logged(51));
}
