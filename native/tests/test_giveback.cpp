// The Linux broker's give-back (E312): the port reset happens while the broker still holds all five interfaces, and
// only then they go back to snd-usb-audio - one probe after the reset. Releasing first let snd-usb-audio probe the
// Virus and the reset hit it mid-probe ("probe ... failed with error -71", the Virus hung). Hardware-free: the order.
#include "check.h"
extern "C" {
#include "giveback.h"
}
#include <string>

static int rec_claim(void *ctx, int i) { *(std::string *)ctx += "c" + std::to_string(i); return 0; }
static int rec_release(void *ctx, int i) { *(std::string *)ctx += "r" + std::to_string(i); return 0; }
static int rec_reset(void *ctx) { *(std::string *)ctx += "X"; return 0; }

TEST(giveback_resets_before_release)
{
    std::string calls;
    const vti_giveback_ops ops = {rec_claim, rec_release, rec_reset};
    CHECK(vti_giveback(&ops, &calls, 1) == 1);
    CHECK(calls == "c0c1c2c3c4Xr0r1r2r3r4");
}

TEST(giveback_without_reset)
{
    std::string calls; // the reset guard (E136) said no: interfaces back without a reset
    const vti_giveback_ops ops = {rec_claim, rec_release, rec_reset};
    CHECK(vti_giveback(&ops, &calls, 0) == 0);
    CHECK(calls == "c0c1c2c3c4r0r1r2r3r4");
}
