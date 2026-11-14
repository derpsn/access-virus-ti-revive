// macgiveback.h (#37, 2026-11-14): when the macOS broker that captured the Virus (libusb audio, E511) gives it back to
// Apple's driver, a failed attach (a re-enumeration that timed out after 10 s on the reporter's Catalina Mac) leaves
// libusb's device counts broken, and libusb_close / libusb_exit then abort ("Assertion failed: (refcnt >= 0)").
// So after a failed attach the broker skips both and leaves (the process ends anyway). Hardware-free: the order.
#include "check.h"
extern "C" {
#include "macgiveback.h"
}
#include <string>

static int g_attach_result;
static int rec_attach(void *ctx) { *(std::string *)ctx += "A"; return g_attach_result; }
static void rec_close(void *ctx) { *(std::string *)ctx += "C"; }
static void rec_exit(void *ctx) { *(std::string *)ctx += "X"; }

TEST(macgiveback_attach_ok_then_close_and_exit)
{
    std::string calls;
    const vti_macgiveback_ops ops = {rec_attach, rec_close, rec_exit};
    g_attach_result = 0;
    CHECK(vti_macgiveback(&ops, &calls, 1) == 0);
    CHECK(calls == "ACX");
}

TEST(macgiveback_failed_attach_skips_close_and_exit)
{
    std::string calls;
    const vti_macgiveback_ops ops = {rec_attach, rec_close, rec_exit};
    g_attach_result = -7; // LIBUSB_ERROR_TIMEOUT, as #37's 10.15 s suggest
    CHECK(vti_macgiveback(&ops, &calls, 1) == -7);
    CHECK(calls == "A");
}

TEST(macgiveback_not_captured_just_closes)
{
    std::string calls; // the normal package: the broker never captured, Apple's driver kept the audio
    const vti_macgiveback_ops ops = {rec_attach, rec_close, rec_exit};
    g_attach_result = -7;
    CHECK(vti_macgiveback(&ops, &calls, 0) == 0);
    CHECK(calls == "CX");
}
