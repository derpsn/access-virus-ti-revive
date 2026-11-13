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

// the buttons are hit tested with contains(): the first pixel row and column belong to a button, the one just past
// its width or height does not (else a click between OK and Don't show again could hit both or neither)
TEST(infopage_rect_edges)
{
    const InfoRect r = kInfoOk;
    CHECK(r.contains(r.x, r.y));
    CHECK(r.contains(r.x + r.w - 1, r.y + r.h - 1));
    CHECK(!r.contains(r.x - 1, r.y) && !r.contains(r.x, r.y - 1));
    CHECK(!r.contains(r.x + r.w, r.y) && !r.contains(r.x, r.y + r.h));
}

// "" (no version seen yet) is older than any version, also 0; and a shorter version is older than a longer one with
// the same start, so 0.8 counts as before 0.8.0
TEST(infopage_version_compare_edges)
{
    CHECK(version_cmp("", "0") < 0 && version_cmp("0", "") > 0);
    CHECK(version_cmp("0.8", "0.8.0") < 0 && version_cmp("0.8.0", "0.8") > 0);
    CHECK(version_cmp("", "") == 0);
}

// What's New: the version lines are headings, the notes under them plain; nothing on the page is a warning, and the
// welcome and log pages are plain text too (the editor draws headings bold and warnings in red)
TEST(infopage_paragraph_kinds)
{
    InfoPage p = whats_new_page("0.8.38", "0.8.41");
    int headings = 0;
    for (auto &q : p.paras) {
        CHECK(!q.warn);
        if (q.text.compare(0, 8, "Version ") == 0) {
            CHECK(q.bold);
            headings++;
        } else {
            CHECK(!q.bold && q.text.compare(0, 2, "- ") == 0);
        }
    }
    CHECK(headings == 3);
    for (auto &q : welcome_page("0.8.41").paras)
        CHECK(!q.bold && !q.warn);
    for (auto &q : log_notice_page().paras)
        CHECK(!q.bold && !q.warn);
}

// each "off" of the state file is read back on its own: What's New switched off stays off after a restart
TEST(infopage_state_news_off_read_back)
{
    InfoState s;
    s.news_off = true;
    InfoState t = info_state_parse(info_state_text(s));
    CHECK(t.news_off && !t.welcome_off);
    CHECK(info_state_parse("news=off\n").news_off);
}

// closing a page: OK switches nothing off; Don't show again switches off only the page it was pressed on; the log
// notice counts as seen only from the version that switched the log on
TEST(infopage_closed_switches_off_only_its_page)
{
    InfoState s;
    s.last_version = "0.8.39";
    InfoState t = info_closed(s, InfoStart::Welcome, "0.8.41", false);
    CHECK(!t.welcome_off && !t.news_off);
    t = info_closed(s, InfoStart::WhatsNew, "0.8.41", false);
    CHECK(!t.welcome_off && !t.news_off);
    t = info_closed(s, InfoStart::Welcome, "0.8.41", true);
    CHECK(t.welcome_off && !t.news_off);
    t = info_closed(s, InfoStart::WhatsNew, "0.8.41", true);
    CHECK(t.news_off && !t.welcome_off);
    t = info_closed(s, InfoStart::LogNotice, "0.8.41", true);
    CHECK(!t.welcome_off && !t.news_off);
    // before 0.8.41 the pages did not say it, so it is still to be told after an update
    t = info_closed(s, InfoStart::Welcome, "0.8.40", false);
    CHECK(!t.log_notice_seen && t.last_version == "0.8.40");
    CHECK(!info_closed(s, InfoStart::None, "0.8.41", false).log_notice_seen);
}

namespace {
InfoPage fake_news(const std::vector<int> &lines_per_version)
{
    InfoPage p;
    int v = (int)lines_per_version.size();
    for (int n : lines_per_version) {
        p.paras.push_back({"Version 1." + std::to_string(v--), true, false});
        for (int i = 0; i < n; i++)
            p.paras.push_back({"- note " + std::to_string(i), false, false});
    }
    return p;
}
} // namespace

// several versions that fit at 11 pt (the smallest size for more than one) all stay, at 11 pt
TEST(infopage_fit_several_versions_at_11)
{
    InfoPage p = fake_news({2, 1, 2});
    InfoFit f = fit_info_page(p, p.paras.size() * (11 + 4), fake_height);
    CHECK(f.size == 11 && f.page.paras.size() == p.paras.size());
}

// one version that fits at 10 pt is shown at 10, not at the 9 pt of the fallback; one that fits nowhere stays whole
TEST(infopage_fit_one_version_sizes)
{
    InfoPage p = fake_news({3});
    InfoFit f = fit_info_page(p, p.paras.size() * (10 + 4), fake_height);
    CHECK(f.size == 10 && f.page.paras.size() == p.paras.size());
    f = fit_info_page(p, 1, fake_height);
    CHECK(f.size == 9 && f.page.paras.size() == p.paras.size());
    CHECK(f.page.paras.back().text == "- note 2");
}

// a version with one line, before the oldest: only the oldest goes when the rest fits; the pointer line is plain text
TEST(infopage_fit_drops_only_the_oldest)
{
    InfoPage p = fake_news({2, 1, 2}); // 1.3 (2 lines), 1.2 (1 line), 1.1 (2 lines)
    const double room = (3 + 2 + 1) * (11 + 4); // 1.3, 1.2 and the pointer line at 11 pt
    InfoFit f = fit_info_page(p, room, fake_height);
    CHECK(f.size == 11 && f.page.paras.size() == 6);
    CHECK(f.page.paras[3].text == "Version 1.2" && f.page.paras[4].text == "- note 0");
    CHECK(f.page.paras.back().text.find("Earlier versions") == 0);
    CHECK(!f.page.paras.back().bold && !f.page.paras.back().warn);
}

// an oldest version without lines (only its heading) goes alone, the one before it stays
TEST(infopage_fit_drops_a_heading_only_version)
{
    // headings are taller than lines here, so the page without 1.1's heading but with the pointer line is smaller
    auto tall_headings = [](const std::vector<InfoPara> &paras, double size) {
        double h = 0;
        for (auto &q : paras)
            h += size + (q.bold ? 8 : 4);
        return h;
    };
    InfoPage p = fake_news({2, 2, 0}); // 1.3, 1.2, then 1.1 with no lines
    const double room = 2 * (11 + 8) + 5 * (11 + 4); // 1.3, 1.2 and the pointer line at 11 pt
    InfoFit f = fit_info_page(p, room, tall_headings);
    CHECK(f.size == 11 && f.page.paras.size() == 7 && f.page.paras[3].text == "Version 1.2");
}

// a bold line inside a version that is not a version heading (a sub-heading) neither counts as a version nor stops
// the oldest version from going whole
TEST(infopage_fit_sub_headings_are_not_versions)
{
    InfoPage one = fake_news({2});
    one.paras.push_back({"Known issues", true, false});
    InfoFit f = fit_info_page(one, one.paras.size() * (9 + 4), fake_height); // one version: may go down to 9 pt
    CHECK(f.size == 9 && f.page.paras.size() == one.paras.size());
    InfoPage p = fake_news({2, 2});
    p.paras.push_back({"Known issues", true, false}); // under 1.1
    p.paras.push_back({"- issue", false, false});
    // room for all but the sub-heading's part: 1.1 still goes whole
    InfoFit g = fit_info_page(p, (6 + 1) * (11 + 4), fake_height);
    CHECK(g.page.paras.size() == 4 && g.page.paras[2].text == "- note 1");
}
