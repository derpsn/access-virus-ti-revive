// keyporthelper.h (#37, E544): the libusb package's broker runs as root from launchd, outside the user's session, and
// macOS shows CoreMIDI ports only to the session that makes them. So a root broker starts a helper in its user's session
// (launchctl asuser) and hands it the messages (midifeed.h); a broker of the user itself makes the ports as before.
extern "C" {
#include "keyporthelper.h"
}
#include "check.h"
#include <string>

TEST(keyporthelper_only_a_root_broker_with_a_user_uses_the_helper)
{
    CHECK(vti_keyport_via_helper(0, 501));    // the libusb package's LaunchDaemon (VTI_OWNER_UID 501)
    CHECK(!vti_keyport_via_helper(501, 501)); // the normal package: the broker of the user, started by the plug-in
    CHECK(!vti_keyport_via_helper(0, 0));     // root without a user to serve: nobody's session to show ports in
}

TEST(keyporthelper_command_runs_in_the_users_session)
{
    char uid[16];
    const char *argv[VTI_KEYPORT_HELPER_ARGS];
    CHECK(vti_keyport_helper_argv(argv, uid, sizeof uid, "/Library/Application Support/TI Revive/vtibroker", 501));
    CHECK(std::string(argv[0]) == "/bin/launchctl");
    CHECK(std::string(argv[1]) == "asuser");
    CHECK(std::string(argv[2]) == "501");
    CHECK(std::string(argv[3]) == "/Library/Application Support/TI Revive/vtibroker");
    CHECK(std::string(argv[4]) == "-midiports");
    CHECK(std::string(argv[5]) == "501"); // the helper gives up root for this user before it makes the ports
    CHECK(argv[6] == nullptr);
    CHECK(!vti_keyport_helper_argv(argv, uid, sizeof uid, "/x/vtibroker", 0)); // never as root's
    CHECK(!vti_keyport_helper_argv(argv, uid, sizeof uid, nullptr, 501));
    CHECK(!vti_keyport_helper_argv(argv, uid, 2, "/x/vtibroker", 501)); // no room for the number
}

// a helper that ended (the user logged out, a crash) is started again at the next message, at most every 10 s: the
// USB reader thread hands the messages over and must not spend its time starting processes
TEST(keyporthelper_restart_at_most_every_ten_seconds)
{
    CHECK(vti_keyport_restart_due(100000, -1));     // never started
    CHECK(!vti_keyport_restart_due(105000, 100000)); // 5 s after the last start
    CHECK(vti_keyport_restart_due(110000, 100000));
}

// the helper's own argument: a user number, nothing else, never root
TEST(keyporthelper_helper_takes_only_a_user_number)
{
    CHECK(vti_keyport_helper_uid("501") == 501);
    CHECK(vti_keyport_helper_uid("0") == 0);
    CHECK(vti_keyport_helper_uid("") == 0);
    CHECK(vti_keyport_helper_uid(nullptr) == 0);
    CHECK(vti_keyport_helper_uid("501x") == 0);
    CHECK(vti_keyport_helper_uid("-1") == 0);
    CHECK(vti_keyport_helper_uid("99999999999") == 0);
}

// midifeed.h numbers the ports as Linux and Windows (0 "TI Revive MIDI", 1 "TI Revive Synth"); the Mac's sources are
// [0] "TI Revive Synth", [1] "TI Revive MIDI"
TEST(keyporthelper_feed_port_to_mac_source)
{
    CHECK(vti_keyport_mac_source(1) == 0);
    CHECK(vti_keyport_mac_source(0) == 1);
    CHECK(vti_keyport_mac_source(2) == -1);
}
