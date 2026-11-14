// owneruid.h (libusb-audio branch, E511): a broker started with sudo serves the user who started it; the plug-in
// trusts a broker of its own user or root, nobody else
extern "C" {
#include "owneruid.h"
}
#include "check.h"
#include <string>

TEST(owneruid_sudo_broker_serves_the_calling_user)
{
    CHECK(vti_owner_uid(0, 0, "501") == 501);   // sudo from user 501: the socket and ring are 501's
    CHECK(vti_owner_uid(501, 501, "777") == 501); // not root: SUDO_UID means nothing
    CHECK(vti_owner_uid(501, 501, nullptr) == 501);
    CHECK(vti_owner_uid(0, 0, nullptr) == 0);   // root without sudo: its own
    CHECK(vti_owner_uid(0, 0, "") == 0);        // empty, junk, 0 or out of range: its own
    CHECK(vti_owner_uid(0, 0, "abc") == 0);
    CHECK(vti_owner_uid(0, 0, "501x") == 0);
    CHECK(vti_owner_uid(0, 0, "0") == 0);
    CHECK(vti_owner_uid(0, 0, "-5") == 0);
    CHECK(vti_owner_uid(0, 0, "99999999999") == 0);
    CHECK(vti_owner_uid(0, 0, "2147483648") == 0); // just above 32 bits: long is 32-bit on Windows (CI caught it)
    CHECK(vti_owner_uid(0, 0, "2147483647") == 2147483647u);
}

TEST(owneruid_plugin_trusts_its_user_or_root)
{
    CHECK(vti_peer_trusted(501, 501));
    CHECK(vti_peer_trusted(0, 501)); // the sudo broker of the libusb-audio test
    CHECK(!vti_peer_trusted(502, 501)); // another user's socket in /tmp (review 2026-10-19 H4)
    CHECK(!vti_peer_trusted((unsigned)-1, 501)); // peer unknown
}

// the libusb package's root service (LaunchDaemon, #37): launchd sets no SUDO_UID, its plist names the user in
// VTI_OWNER_UID; that one wins when set, SUDO_UID stays for the Terminal test (vti_owner_uid's checks apply to both)
TEST(owneruid_launchd_service_names_its_user)
{
    CHECK(std::string(vti_owner_env("501", nullptr)) == "501");
    CHECK(std::string(vti_owner_env("501", "502")) == "501");
    CHECK(std::string(vti_owner_env(nullptr, "502")) == "502");
    CHECK(std::string(vti_owner_env("", "502")) == "502");
    CHECK(vti_owner_env(nullptr, nullptr) == nullptr);
    CHECK(vti_owner_uid(0, 0, vti_owner_env("501", nullptr)) == 501);
    CHECK(vti_owner_uid(501, 501, vti_owner_env("777", nullptr)) == 501); // not root: the variable means nothing
}

// the root broker lets in only the user it serves and root: another user on the Mac must not reach a root service
TEST(owneruid_root_broker_takes_its_user_only)
{
    CHECK(vti_client_allowed(501, 501));
    CHECK(vti_client_allowed(0, 501));
    CHECK(!vti_client_allowed(502, 501));
    CHECK(!vti_client_allowed((unsigned)-1, 501)); // peer unknown
}
