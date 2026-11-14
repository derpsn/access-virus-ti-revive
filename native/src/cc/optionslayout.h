// optionslayout.h - where the Control Center's Options page (ccapp/optionspage.cpp) puts its rows and its status
// message, from the number of options and the tallest page the screen allows. The page used a fixed height with the
// message at a fixed place near the bottom: the seventh option (0.8.41) ran under it and was cut off, without a
// scrollbar (user, Linux test 2026-11-14). Now the message always sits under the list, and when all rows do not
// fit, the list keeps as many whole rows as fit and scrolls. Header only, no VSTGUI: unit-tested.
#pragma once

namespace vti::cc {

struct OptionsLayout {
    int rows_top = 88;  // the list's top (title, Back button and the intro line above it)
    int row = 58;       // one option: its box and a two-line hint
    int message_h = 22; // the status line ("...: on - from the next start of the plug-in")
    int list_h = 0;     // visible height of the list (a scroll view when smaller than all rows)
    int message_y = 0;  // the status line's top, under the visible list
    int height = 0;     // the page's height
    bool scrolls = false;
};

// n options, max_height = the tallest page that fits on the screen
inline OptionsLayout options_layout(int n, int max_height)
{
    OptionsLayout l;
    const int gap = 6, bottom = 12;
    const int fixed = l.rows_top + gap + l.message_h + bottom;
    const int all = (n > 0 ? n : 0) * l.row;
    l.list_h = all;
    if (fixed + all > max_height && n > 0) {
        int rows = (max_height - fixed) / l.row; // whole rows only, the rest by the scrollbar
        if (rows < 1)
            rows = 1;
        if (rows < n) {
            l.list_h = rows * l.row;
            l.scrolls = true;
        }
    }
    l.message_y = l.rows_top + l.list_h + gap;
    l.height = l.message_y + l.message_h + bottom;
    return l;
}

} // namespace vti::cc
