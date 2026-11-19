// nerdview.h - the table of Nerd mode in the Access look (ccapp/skinpage.cpp, user 2026-11-15): the traffic or a log
// in rows of the skin's LCD font. LogScroll keeps the view: at the bottom it follows the newest line; scrolled up it
// counts from the end, so new lines (and old ones dropped from a full log) leave the same lines on screen. fit_text
// fits a line into the LCD font's columns (ASCII 32..127). Header only, no drawing: unit-tested.
#pragma once
#include <algorithm>
#include <string>
#include <vector>

namespace vti::cc {

class LogScroll {
public:
    // `total` lines, `rows` on screen, `added` since the last call (only matters while scrolled up)
    void update(int total, int rows, int added = 0)
    {
        total_ = std::max(0, total), rows_ = std::max(1, rows);
        if (back_ > 0)
            back_ += std::max(0, added);
        clamp();
    }
    int first() const { return std::max(0, total_ - rows_ - back_); } // index of the top row's line
    bool following() const { return back_ == 0; }
    void wheel(int lines) // > 0: up, towards older lines
    {
        back_ += lines;
        clamp();
    }
    // the scrollbar's thumb on a track `track` pixels high: its top and height
    void thumb(double track, double &y, double &h) const
    {
        const int span = total_ - rows_;
        if (span <= 0) {
            y = 0, h = track;
            return;
        }
        h = std::max(kMinThumb, track * rows_ / total_);
        y = (track - h) * (1.0 - (double)back_ / span);
    }
    // a click at `at` pixels down the track: the thumb's middle goes there
    void click_track(double track, double at)
    {
        const int span = total_ - rows_;
        if (span <= 0)
            return;
        double y, h;
        thumb(track, y, h);
        const double way = track - h;
        const double f = way > 0 ? std::min(1.0, std::max(0.0, (at - h / 2) / way)) : 1.0;
        back_ = (int)((1.0 - f) * span + 0.5);
        clamp();
    }

private:
    static constexpr double kMinThumb = 12;
    int total_ = 0, rows_ = 1, back_ = 0; // back_: lines between the bottom row and the newest line
    void clamp() { back_ = std::min(std::max(back_, 0), std::max(0, total_ - rows_)); }
};

// `text` in at most `n` LCD characters: controls become spaces, every non-ASCII character one '?', too long ends in
// "..." (or is cut when n < 4)
inline std::string fit_text(const std::string &text, size_t n)
{
    std::string out;
    for (size_t i = 0; i < text.size(); i++) {
        const unsigned char c = (unsigned char)text[i];
        if (c >= 0x80) {
            if ((c & 0xC0) != 0x80) // a lead byte: one character; its continuation bytes are skipped
                out += '?';
        } else {
            out += c < 32 || c == 127 ? ' ' : (char)c;
        }
    }
    if (out.size() <= n)
        return out;
    return n >= 4 ? out.substr(0, n - 3) + "..." : out.substr(0, n);
}

// `text` in at most `max_lines` lines of `n` LCD characters, broken at spaces (the Options page's descriptions); a word
// longer than a line is cut, and text that does not fit ends the last line in "..."
inline std::vector<std::string> wrap_text(const std::string &text, size_t n, size_t max_lines)
{
    std::vector<std::string> lines;
    const std::string t = fit_text(text, std::string::npos); // ASCII only, as the LCD font
    size_t i = 0;
    while (i < t.size() && lines.size() < max_lines && n > 0) {
        while (i < t.size() && t[i] == ' ')
            i++;
        if (i >= t.size())
            break;
        if (lines.size() + 1 == max_lines && t.size() - i > n) { // the last line and more than it holds
            lines.push_back(fit_text(t.substr(i), n));
            return lines;
        }
        size_t end = std::min(t.size(), i + n);
        if (end < t.size()) { // break at the last space that fits, else cut the long word
            const size_t sp = t.rfind(' ', end);
            if (sp != std::string::npos && sp > i)
                end = sp;
        }
        std::string line = t.substr(i, end - i);
        while (!line.empty() && line.back() == ' ')
            line.pop_back();
        lines.push_back(line);
        i = end;
    }
    return lines;
}

} // namespace vti::cc
