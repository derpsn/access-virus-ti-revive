// Who may use the Virus when several clients are connected to a broker (both brokers, round 0.8.20): plug-ins of
// different programs never share it (each sync ping resets the Virus' only stamp clock, E148), every other client -
// Control Center, tirevive-cc, the ASIO driver - shares it with the plug-in that owns it. Hardware-free.
#include "check.h"
extern "C" {
#include "owner.h"
}

TEST(owner_plugin_by_name)
{
    CHECK(vti_is_plugin("TI Revive Virus Control") == 1);
    CHECK(vti_is_plugin("TI Revive Control Center") == 0);
    CHECK(vti_is_plugin("TI Revive Control Center (Nerd mode)") == 0);
    CHECK(vti_is_plugin("tirevive-cc") == 0);
    CHECK(vti_is_plugin("") == 0); // clients before protocol 4 send no name: they share
}

TEST(owner_control_center_shares_with_the_plugin)
{
    // macOS user test M2 (0.8.19): the Control Center was told "another program uses the Virus" while Ableton ran
    CHECK(vti_client_state(1, 0, 0) == 1); // Control Center next to the owning plug-in: open
    CHECK(vti_client_state(1, 1, 1) == 1); // the owning plug-in
    CHECK(vti_client_state(1, 1, 0) == 2); // a plug-in of a second program waits
    CHECK(vti_client_state(0, 0, 0) == 0); // no Virus
    CHECK(vti_client_state(0, 1, 0) == 0);
    CHECK(vti_client_waits(1, 0) == 1);
    CHECK(vti_client_waits(1, 1) == 0);
    CHECK(vti_client_waits(0, 0) == 0);
}

TEST(owner_audio_stop)
{
    // the stream belongs to the owning plug-in: a sharing client's "audio off" must not cut it (E112)
    CHECK(vti_may_stop_audio(1, 1, 1) == 1); // the owner
    CHECK(vti_may_stop_audio(1, 0, 1) == 0); // a waiting plug-in
    CHECK(vti_may_stop_audio(0, 0, 1) == 0); // Control Center next to the owner
    CHECK(vti_may_stop_audio(0, 0, 0) == 1); // no plug-in there: the client that started it
}

// Rate following on Windows (design-rate.md step 4): one stream for every client, like the original's single driver
// rate. The owning plug-in sets it; the ASIO driver only while no plug-in owns the Virus (E112: a second program
// restarting the stream under the owner); the Control Center and other clients never.
TEST(owner_rate_setter)
{
    CHECK(vti_is_asio("TI Revive Virus Audio (ASIO)"));
    CHECK(!vti_is_asio(VTI_PLUGIN_NAME));
    CHECK(vti_rate_setter(1, 1, 0));  // the owning plug-in
    CHECK(vti_rate_setter(0, 0, 1));  // the ASIO driver, no plug-in there
    CHECK(!vti_rate_setter(0, 1, 1)); // the ASIO driver next to another program's plug-in
    CHECK(!vti_rate_setter(0, 0, 0)); // the Control Center
    CHECK(!vti_rate_setter(0, 1, 0));
}
