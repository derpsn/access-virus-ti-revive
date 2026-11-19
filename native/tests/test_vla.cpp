// The libusb audio stream (src/broker/vtiusbaudio_libusb.c, Linux and the macOS libusb package) against a stub libusb:
// transfers are kept in a list, the event thread hands back what the test completes or the code cancels.
// Own binary (vlatests): the stubs replace libusb's functions, nothing else may link the real library.
#include "check.h"
#include <libusb.h>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <vector>
extern "C" {
#include "vtiusbaudio_libusb.h"
}

namespace {
std::mutex mu;
std::vector<libusb_transfer *> submitted, pending; // pending: back to the code at the next event round
std::atomic<bool> fail_submit{false};
std::atomic<int> claims{0};

void quiet(const char *, ...) {}
void on_in(const int16_t *, uint32_t, void *) {}
void on_out(int16_t *, uint32_t, void *) {}

void hand_back(libusb_transfer *t, libusb_transfer_status st) // mu held
{
    for (size_t i = 0; i < submitted.size(); i++)
        if (submitted[i] == t) {
            submitted.erase(submitted.begin() + (long)i);
            t->status = st;
            pending.push_back(t);
            return;
        }
}

// every queued IN transfer comes back complete (no audio in it); returns once the code has seen them all
void complete_ins()
{
    {
        std::lock_guard<std::mutex> lk(mu);
        for (auto *t : std::vector<libusb_transfer *>(submitted))
            if (t->endpoint == 0x82)
                hand_back(t, LIBUSB_TRANSFER_COMPLETED);
    }
    for (int i = 0; i < 200; i++) {
        {
            std::lock_guard<std::mutex> lk(mu);
            if (pending.empty())
                break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5)); // the last callback returns
}
} // namespace

extern "C" {
void (*vti_log)(const char *fmt, ...) = quiet;

struct libusb_transfer *LIBUSB_CALL libusb_alloc_transfer(int iso_packets)
{
    return (libusb_transfer *)calloc(1, sizeof(libusb_transfer) + (size_t)iso_packets * sizeof(libusb_iso_packet_descriptor));
}
void LIBUSB_CALL libusb_free_transfer(struct libusb_transfer *t) { free(t); }
int LIBUSB_CALL libusb_submit_transfer(struct libusb_transfer *t)
{
    if (fail_submit)
        return LIBUSB_ERROR_IO;
    std::lock_guard<std::mutex> lk(mu);
    submitted.push_back(t);
    return 0;
}
int LIBUSB_CALL libusb_cancel_transfer(struct libusb_transfer *t)
{
    std::lock_guard<std::mutex> lk(mu);
    size_t n = pending.size();
    hand_back(t, LIBUSB_TRANSFER_CANCELLED);
    return pending.size() > n ? 0 : LIBUSB_ERROR_NOT_FOUND;
}
int LIBUSB_CALL libusb_handle_events_timeout_completed(libusb_context *, struct timeval *, int *)
{
    std::vector<libusb_transfer *> now;
    {
        std::lock_guard<std::mutex> lk(mu);
        now.swap(pending);
    }
    for (auto *t : now)
        t->callback(t);
    std::this_thread::sleep_for(std::chrono::microseconds(200));
    return 0;
}
int LIBUSB_CALL libusb_control_transfer(libusb_device_handle *, uint8_t, uint8_t, uint16_t, uint16_t, unsigned char *,
                                        uint16_t len, unsigned int)
{
    return len;
}
int LIBUSB_CALL libusb_claim_interface(libusb_device_handle *, int)
{
    claims++;
    return 0;
}
int LIBUSB_CALL libusb_release_interface(libusb_device_handle *, int) { return 0; }
int LIBUSB_CALL libusb_set_interface_alt_setting(libusb_device_handle *, int, int) { return 0; }
const char *LIBUSB_CALL libusb_error_name(int) { return "stub"; }
}

TEST(vla_stream_without_in_transfers_is_not_running_and_restarts)
{
    // audit #14: every IN resubmit fails without NO_DEVICE (a USB error burst): no audio comes any more, so the broker
    // must see a stopped stream, and the next start must stop the old one (event thread, OUT transfers) first
    auto *h = reinterpret_cast<libusb_device_handle *>(0x1);
    CHECK_EQ(vla_start(nullptr, h, 44100, on_in, on_out, nullptr), 0);
    CHECK(vla_running());
    CHECK_EQ(claims.load(), 3);
    fail_submit = true;
    complete_ins();
    CHECK(!vla_running());
    fail_submit = false;
    CHECK_EQ(vla_start(nullptr, h, 44100, on_in, on_out, nullptr), 0);
    CHECK_EQ(claims.load(), 6); // started again, not "already running"
    CHECK(vla_running());
    vla_stop();
    CHECK(!vla_running());
    std::lock_guard<std::mutex> lk(mu);
    CHECK(submitted.empty()); // every transfer came back before it was freed
}
