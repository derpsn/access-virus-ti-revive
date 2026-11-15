// Nerd mode in the Access look (cc/nerdview.h, user 2026-11-15): the table that shows the traffic and the logs. It
// scrolls by the wheel and the track, follows the newest line while it is at the bottom, stands still while new lines
// come in when it is scrolled up, and fits text into the LCD font's columns. Hardware-free.
#include "check.h"
#include "cc/nerdview.h"
#include <string>

using namespace vti::cc;

TEST(nerdview_follows_the_newest_line_at_the_bottom)
{
    LogScroll s;
    s.update(5, 15); // fewer lines than rows: all shown from the top
    CHECK(s.first() == 0 && s.following());
    s.update(40, 15);
    CHECK(s.first() == 25); // the last 15 of 40
    s.update(41, 15, 1);    // one more line: still at the bottom
    CHECK(s.first() == 26);
}

TEST(nerdview_stands_still_when_scrolled_up)
{
    LogScroll s;
    s.update(100, 15);
    s.wheel(10); // ten lines up
    CHECK(s.first() == 75 && !s.following());
    s.update(103, 15, 3); // three new lines at the end: the same lines stay on screen
    CHECK(s.first() == 75);
    s.wheel(-100); // back down to the end: follows again
    CHECK(s.first() == 88 && s.following());
    s.wheel(1000); // the top, no further
    CHECK(s.first() == 0);
}

// a full log drops its oldest lines while new ones come: the view counts from the end, so it stays on the same lines
// (their index moves down by the dropped count)
TEST(nerdview_full_log_keeps_the_view)
{
    LogScroll s;
    s.update(3000, 15);
    s.wheel(100);
    const int before = s.first(); // 2885
    s.update(3000, 15, 5);        // five new lines in, five old ones out (the log keeps 3000)
    CHECK(s.first() == before - 5);
}

TEST(nerdview_scrollbar_thumb_and_track_click)
{
    LogScroll s;
    s.update(10, 15);
    double y, h;
    s.thumb(150, y, h);
    CHECK(y == 0 && h == 150); // everything fits: a full thumb
    s.update(150, 15);         // ten pages
    s.thumb(150, y, h);
    CHECK(h == 15 && y == 135); // a tenth of the track, at the bottom
    s.click_track(150, 0);      // a click at the top of the track: the top
    CHECK(s.first() == 0 && !s.following());
    s.click_track(150, 150); // at the bottom: the end, following again
    CHECK(s.first() == 135 && s.following());
    s.update(1000, 15);
    s.thumb(150, y, h);
    CHECK(h >= 12); // a thumb you can see
}

TEST(nerdview_text_fits_the_lcd_columns)
{
    CHECK(fit_text("Note on C4", 20) == "Note on C4");
    CHECK(fit_text("0123456789ABCDEF", 10) == "0123456...");
    CHECK(fit_text("abc", 2) == "ab");
    CHECK(fit_text("tab\there", 20) == "tab here"); // the LCD font has ASCII 32..127 only
    CHECK(fit_text("caf\xc3\xa9", 20) == "caf?");   // UTF-8 beyond ASCII: one ? per character
}
