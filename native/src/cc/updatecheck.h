// updatecheck.h - the opt-in update check (user 2026-11-15) and the bug report link.
// Off by default ("update-check" on the Control Center's Options page). When on, the Control Center asks GitHub at
// most once a day for the latest release - one HTTPS request through the system's own curl (wget on Linux when curl
// is missing), nothing about the user or the setup in it - and writes the answer to update.txt in plugin_dir(). The
// plug-in never goes online: it reads that file when it opens, shows a note once per new version and offers the
// download in its ? menu. The bug report link opens GitHub's form with the version filled in; nothing is sent by us.
#pragma once
#include <string>

namespace vti::cc {

constexpr const char *kReleasesUrl = "https://github.com/derpsn/access-virus-ti-revive/releases/latest";

// GitHub's answer for /releases/latest: true and "0.8.43" for a plain final release tag "v0.8.43"; false for a
// pre-release, a draft, an rc tag or anything that is not such an answer (error, rate limit, garbage)
bool latest_from_release_json(const std::string &json, std::string &version);

// a newer than b, by the numbers x.y.z; a final release is newer than an rc of the same number. An unreadable `a`
// is never newer, an unreadable `b` (our own version) is older than any readable `a`.
bool version_newer(const std::string &a, const std::string &b);

struct UpdateState {
    long long checked = 0; // when the Control Center last asked (Unix time), 0 = never
    std::string latest;    // the newest final release it saw ("" = none known)
    std::string seen;      // the release the plug-in's note was shown for
};
std::string format_update_state(const UpdateState &s);
bool parse_update_state(const std::string &text, UpdateState &s); // unknown lines and bad versions are dropped

bool check_due(long long now, const UpdateState &s);                          // a day after the last check
std::string update_available(const std::string &current, const UpdateState &s); // the newer version, or ""
bool show_update_note(const std::string &current, const UpdateState &s);       // newer and not shown yet

// The bug report form on GitHub with the "Versions" field filled in ("TI Revive 0.8.42, Linux").
std::string bug_report_url(const std::string &version, const std::string &os);
const char *os_family(); // "Windows", "macOS" or "Linux"

// I/O, in plugin_dir()/update.txt
std::string update_state_path();
UpdateState read_update_state();
bool write_update_state(const UpdateState &s);

// The check itself (Control Center only; blocks up to ~20 s, so not on a UI thread). True when GitHub answered with a
// release; `s` gets the time and the version. `error` says why not (no curl, no network, an odd answer).
bool run_update_check(UpdateState &s, std::string &error);

// The Control Center's call (at its start and when the option is switched on): when "update-check" is on and a day
// has passed, runs the check on a thread of its own and writes update.txt. Returns at once.
void start_update_check_if_due();

} // namespace vti::cc
