// The Control Center's Options page layout (cc/optionslayout.h): with the seventh option (live-later, 0.8.41) the
// last row ran past the fixed 470 px page and the status message, placed at a fixed height near the bottom, was drawn
// on top of it; a too-short window cut the list off without a scrollbar (user, Linux test 2026-11-14).
#include "check.h"
#include "cc/optionslayout.h"
using namespace vti::cc;

TEST(options_message_sits_below_the_last_row)
{
    for (int n = 0; n <= 12; n++) {
        const OptionsLayout l = options_layout(n, 1000);
        CHECK(l.message_y >= l.rows_top + n * l.row);  // never on an option
        CHECK(l.height >= l.message_y + l.message_h); // and inside the page
    }
}

TEST(options_page_fits_all_rows_when_the_screen_allows)
{
    const OptionsLayout l = options_layout(7, 1000);
    CHECK(!l.scrolls);
    CHECK(l.list_h == 7 * l.row); // the whole list visible: no scrollbar
    CHECK(l.height <= 1000);
}

TEST(options_list_scrolls_when_the_page_would_be_too_tall)
{
    const OptionsLayout l = options_layout(7, 400); // a small screen: the page may be at most 400 high
    CHECK(l.scrolls);
    CHECK(l.height <= 400);
    CHECK(l.list_h < 7 * l.row);                 // part of the list visible, the rest by the scrollbar
    CHECK(l.list_h >= l.row);                    // at least one whole row stays visible
    CHECK(l.message_y >= l.rows_top + l.list_h); // the message under the visible list, not on it
}

TEST(options_page_never_gets_absurdly_small)
{
    const OptionsLayout l = options_layout(7, 50); // a nonsense limit still leaves one row and the message
    CHECK(l.list_h >= l.row);
    CHECK(l.height >= l.rows_top + l.row + l.message_h);
}
