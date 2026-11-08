// The welcome and What's New pages (editor/infopage.h), in the original's Info page style: which page opens at start,
// the What's New list between two versions, the stored state and the button places from the original's layout.
// Hardware-free.
#include "check.h"
#include "editor/infopage.h"

using namespace vti;

TEST(infopage_version_compare)
{
    CHECK(version_cmp("0.8.39", "0.8.38") > 0);
    CHECK(version_cmp("0.8.38", "0.8.39") < 0);
    CHECK(version_cmp("0.8.9", "0.8.10") < 0); // numbers, not text
    CHECK(version_cmp("0.8.39", "0.8.39") == 0);
    CHECK(version_cmp("", "0.8.39") < 0);
}

TEST(infopage_state_round_trip)
{
    InfoState s;
    s.welcome_off = true;
    s.news_off = false;
    s.last_version = "0.8.39";
    InfoState t = info_state_parse(info_state_text(s));
    CHECK(t.welcome_off && !t.news_off && t.last_version == "0.8.39");
    t = info_state_parse("garbage\nlast=0.8.38\n");
    CHECK(!t.welcome_off && !t.news_off && t.last_version == "0.8.38");
}

// a new install: the welcome page, as the original's; it comes back at each start until "Don't show again"
TEST(infopage_welcome_on_a_new_install)
{
    InfoState s;
    CHECK(info_at_start(s, "0.8.39", false) == InfoStart::Welcome);
    s.last_version = "0.8.39"; // OK pressed: this version's news counts as seen
    CHECK(info_at_start(s, "0.8.39", true) == InfoStart::Welcome);
    s.welcome_off = true;
    CHECK(info_at_start(s, "0.8.39", true) == InfoStart::None);
}

// an update: What's New once, for the versions since the last one seen; someone who used TI Revive before this page
// existed (caches there, no state) gets What's New, not the welcome
TEST(infopage_whats_new_after_an_update)
{
    InfoState s;
    s.welcome_off = true;
    s.last_version = "0.8.38";
    CHECK(info_at_start(s, "0.8.39", true) == InfoStart::WhatsNew);
    s.last_version = "0.8.39";
    CHECK(info_at_start(s, "0.8.39", true) == InfoStart::None);
    InfoState old_user; // no state file, but a cache from an earlier version
    CHECK(info_at_start(old_user, "0.8.39", true) == InfoStart::WhatsNew);
    s.last_version = "0.8.38";
    s.news_off = true;
    CHECK(info_at_start(s, "0.8.39", true) == InfoStart::None);
}

TEST(infopage_whats_new_lists_the_versions_between)
{
    InfoPage p = whats_new_page("0.8.38", "0.8.39");
    CHECK(!p.paras.empty());
    CHECK(p.title.find("0.8.39") != std::string::npos);
    bool head = false;
    for (auto &para : p.paras)
        head |= para.bold && para.text.find("0.8.39") != std::string::npos;
    CHECK(head);
    // nothing newer than the running version is shown, and an unknown older one shows all entries up to it
    CHECK(whats_new_page("0.8.39", "0.8.39").paras.empty());
    CHECK(!whats_new_page("", "0.8.39").paras.empty());
}

TEST(infopage_welcome_text_is_ours)
{
    InfoPage p = welcome_page("0.8.39");
    CHECK(p.title.find("TI Revive") != std::string::npos);
    std::string all;
    for (auto &para : p.paras)
        all += para.text + "\n";
    CHECK(all.find("not affiliated") != std::string::npos); // the notice next to the Access look (AGENTS.md rule 1)
    CHECK(all.find("?") != std::string::npos);              // where the manuals, tips and help are
    CHECK(all.find("Access Support") == std::string::npos); // the original's text points there; it no longer exists
}

// places from the original's layout (Info page at 0/45, 907 x 426; buttons 123 x 23)
TEST(infopage_layout_from_the_original)
{
    CHECK(kInfoPage.x == 0 && kInfoPage.y == 45 && kInfoPage.w == 907 && kInfoPage.h == 426);
    CHECK(kInfoText.x == 52 && kInfoText.y == 70 && kInfoText.w == 786);
    CHECK(kInfoOk.x == 688 && kInfoOk.y == 45 + 376 && kInfoOk.w == 123 && kInfoOk.h == 23);
    CHECK(kInfoDontShow.x == 548 && kInfoDontShow.y == 45 + 376);
    CHECK(kInfoOk.contains(700, 430) && !kInfoOk.contains(600, 430));
}
