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

// What's New with several versions (from the ? menu: all of them) overflowed the page on the Mac and on Linux even at
// 9 pt (user 2026-11-10, screenshot): the newest version always stays, older ones only while they fit at 11 pt or
// more, and then a line where the rest is
namespace {
// a stand-in for the editor's text measuring: every paragraph is one line of (size + 4) pixels
double fake_height(const std::vector<InfoPara> &paras, double size) { return paras.size() * (size + 4); }
} // namespace

TEST(infopage_fit_keeps_everything_when_it_fits)
{
    InfoPage p = whats_new_page("0.8.38", "0.8.41");
    const size_t n = p.paras.size();
    InfoFit f = fit_info_page(p, 1e9, fake_height);
    CHECK(f.page.paras.size() == n && f.size == 13);
}

TEST(infopage_fit_drops_older_versions_first)
{
    InfoPage p = whats_new_page("0.8.38", "0.8.41"); // 0.8.41, 0.8.40, 0.8.39
    InfoPage newest = whats_new_page("0.8.40", "0.8.41");
    // room for the newest version and the pointer line at 11 pt, not for 0.8.40 as well
    const double room = (newest.paras.size() + 1) * (11 + 4);
    InfoFit f = fit_info_page(p, room, fake_height);
    CHECK(f.size >= 11);
    CHECK(f.page.paras.size() == newest.paras.size() + 1);
    CHECK(f.page.paras.front().text == "Version 0.8.41");
    CHECK(f.page.paras.back().text.find("Earlier versions") == 0);
    CHECK(f.page.paras.back().text.find("Every version") != std::string::npos);
    CHECK(fake_height(f.page.paras, f.size) <= room);
    for (auto &q : f.page.paras)
        CHECK(q.text != "Version 0.8.40" && q.text != "Version 0.8.39");
}

TEST(infopage_fit_newest_alone_may_go_smaller)
{
    InfoPage p = whats_new_page("0.8.40", "0.8.41"); // one version only: never dropped, down to 9 pt
    const double room = p.paras.size() * (9 + 4);
    InfoFit f = fit_info_page(p, room, fake_height);
    CHECK(f.page.paras.size() == p.paras.size() && f.size == 9);
    InfoPage w = welcome_page("0.8.41"); // no versions at all: only the size changes
    InfoFit g = fit_info_page(w, w.paras.size() * (10 + 4), fake_height);
    CHECK(g.page.paras.size() == w.paras.size() && g.size == 10);
}

// 0.8.41 switches the plug-in log on for everyone (user 2026-11-10: the plug-in must say so and how to switch it off).
// What's New says it; someone who switched What's New off gets a short page of its own, once
TEST(infopage_log_notice_once_for_news_off)
{
    InfoState s;
    s.welcome_off = true;
    s.news_off = true;
    s.last_version = "0.8.39";
    CHECK(info_at_start(s, "0.8.41", true) == InfoStart::LogNotice);
    CHECK(info_at_start(s, "0.8.40", true) == InfoStart::None); // a version before the change: nothing to tell
    InfoState t = info_closed(s, InfoStart::LogNotice, "0.8.41", false);
    CHECK(t.log_notice_seen && t.news_off && t.last_version == "0.8.41");
    CHECK(info_at_start(t, "0.8.41", true) == InfoStart::None);
    CHECK(info_at_start(t, "0.8.42", true) == InfoStart::None); // never again
    // What's New on: that page carries the notice, and closing it counts as seen
    s.news_off = false;
    CHECK(info_at_start(s, "0.8.41", true) == InfoStart::WhatsNew);
    InfoState u = info_closed(s, InfoStart::WhatsNew, "0.8.41", true);
    CHECK(u.log_notice_seen && u.news_off);
    // a new install: the welcome page says it
    InfoState fresh;
    CHECK(info_at_start(fresh, "0.8.41", false) == InfoStart::Welcome);
    CHECK(info_closed(fresh, InfoStart::Welcome, "0.8.41", false).log_notice_seen);
    // stored
    CHECK(info_state_parse(info_state_text(t)).log_notice_seen);
    CHECK(!info_state_parse("welcome=on\nnews=on\nlast=0.8.40\n").log_notice_seen);
    // the page says what it is and how to switch it off
    InfoPage n = log_notice_page();
    std::string all = n.title;
    for (auto &q : n.paras)
        all += " " + q.text;
    CHECK(all.find("? > Detailed Log") != std::string::npos && all.find("Options page") != std::string::npos);
    CHECK(all.find("nothing is sent") != std::string::npos || all.find("Nothing is sent") != std::string::npos);
}
