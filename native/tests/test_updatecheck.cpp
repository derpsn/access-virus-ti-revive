// The opt-in update check and the bug report link (cc/updatecheck.h): what GitHub's "latest release" answer says, which
// version is newer, the small state file the Control Center writes and the plug-in reads, when the next check is due
// and when the plug-in shows its note. Hardware-free, no network.
#include "check.h"
#include "cc/updatecheck.h"
#include <string>

using namespace vti::cc;

TEST(updatecheck_reads_the_tag_of_the_latest_release)
{
    std::string v;
    CHECK(latest_from_release_json(R"({"url":"x","tag_name":"v0.8.43","name":"TI Revive 0.8.43","prerelease":false})", v));
    CHECK(v == "0.8.43");
    CHECK(latest_from_release_json("{\n  \"tag_name\" : \"v1.0.0\",\n  \"draft\": false\n}", v));
    CHECK(v == "1.0.0");
    // a pre-release or draft never counts (GitHub's /releases/latest leaves them out; checked anyway)
    CHECK(!latest_from_release_json(R"({"tag_name":"v0.8.43-rc1","prerelease":true})", v));
    CHECK(!latest_from_release_json(R"({"tag_name":"v0.8.44","prerelease":false,"draft":true})", v));
    CHECK(!latest_from_release_json(R"({"tag_name":"v0.8.43-rc1","prerelease":false})", v)); // the name says rc
    // anything else: an error page, a rate limit, garbage
    CHECK(!latest_from_release_json(R"({"message":"API rate limit exceeded"})", v));
    CHECK(!latest_from_release_json("", v));
    CHECK(!latest_from_release_json(R"({"tag_name":"latest"})", v));
    CHECK(!latest_from_release_json(R"({"tag_name":"v0.8"})", v));
    CHECK(!latest_from_release_json(R"({"tag_name":"v0.8.43; rm -rf"})", v));
}

TEST(updatecheck_compares_versions_by_their_numbers)
{
    CHECK(version_newer("0.8.43", "0.8.42"));
    CHECK(version_newer("0.8.100", "0.8.99")); // numbers, not text
    CHECK(version_newer("0.9.0", "0.8.42"));
    CHECK(version_newer("1.0.0", "0.8.42"));
    CHECK(!version_newer("0.8.42", "0.8.42"));
    CHECK(!version_newer("0.8.41", "0.8.42"));
    CHECK(version_newer("0.8.42", "0.8.42-rc1")); // our own rc build: the final of the same number is newer
    CHECK(!version_newer("0.8.42-rc1", "0.8.42"));
    CHECK(!version_newer("garbage", "0.8.42"));
    CHECK(version_newer("0.8.43", "garbage")); // an unreadable own version: any real release is newer
}

TEST(updatecheck_state_file_round_trip)
{
    UpdateState s;
    s.checked = 1794700000;
    s.latest = "0.8.43";
    s.seen = "0.8.42";
    UpdateState r;
    CHECK(parse_update_state(format_update_state(s), r));
    CHECK(r.checked == 1794700000 && r.latest == "0.8.43" && r.seen == "0.8.42");
    CHECK(parse_update_state("", r) && r.checked == 0 && r.latest.empty()); // no file yet: nothing known
    CHECK(parse_update_state("checked 12\nlatest x;y\n", r) && r.checked == 12 && r.latest.empty()); // bad version dropped
}

TEST(updatecheck_once_a_day)
{
    UpdateState s;
    CHECK(check_due(1000, s)); // never checked
    s.checked = 1000;
    CHECK(!check_due(1000 + 3600, s));
    CHECK(!check_due(1000 + 86399, s));
    CHECK(check_due(1000 + 86400, s));
    CHECK(check_due(500, s)); // the clock went back: check again rather than never
}

TEST(updatecheck_plugin_note_once_per_version)
{
    UpdateState s;
    CHECK(update_available("0.8.42", s).empty());
    s.latest = "0.8.43";
    CHECK(update_available("0.8.42", s) == "0.8.43");
    CHECK(update_available("0.8.43", s).empty());
    CHECK(update_available("0.8.44", s).empty()); // a dev build newer than the release
    CHECK(show_update_note("0.8.42", s));
    s.seen = "0.8.43";
    CHECK(!show_update_note("0.8.42", s)); // shown once; the ? menu entry stays
    CHECK(update_available("0.8.42", s) == "0.8.43");
}

TEST(updatecheck_bug_report_link_prefills_the_versions_field)
{
    const std::string u = bug_report_url("0.8.42", "Linux");
    CHECK(u.rfind("https://github.com/derpsn/access-virus-ti-revive/issues/new?", 0) == 0);
    CHECK(u.find("template=bug_report.yml") != std::string::npos);
    CHECK(u.find("versions=TI%20Revive%200.8.42%2C%20Linux") != std::string::npos);
    CHECK(bug_report_url("0.8.42 & x", "Linux").find("%26") != std::string::npos); // encoded, never raw
}
