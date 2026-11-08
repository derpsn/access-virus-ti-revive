// infopage.h - TI Revive's welcome and What's New pages, drawn like the original's Info page: its background
// BG_Page_Info.png and its OK / Don't Show Again buttons from the user's own Access files, our own text on it. The
// original's text is a fixed image (TXT_Welcome.png, TXT_Changes.png) that points to Access' setup guide and support;
// ours says what TI Revive is and where its help is. The changelog is compiled in. Built from plain state, no drawing.
#pragma once
#include <cstdlib>
#include <string>
#include <vector>

namespace vti {

// places from the original's layout (editor.xml: the Info page container and its buttons), in editor pixels
struct InfoRect {
    int x, y, w, h;
    bool contains(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};
inline constexpr InfoRect kInfoPage{0, 45, 907, 426};            // BG_Page_Info.png
inline constexpr InfoRect kInfoText{52, 45 + 25, 786, 340};      // where the original's text image sits
inline constexpr InfoRect kInfoOk{688, 45 + 376, 123, 23};       // BTN_Ok_Red.png (two 23-pixel frames)
inline constexpr InfoRect kInfoDontShow{548, 45 + 376, 123, 23}; // BTN_Dont_Show_Again.png

struct InfoPara {
    std::string text;
    bool bold = false; // a heading
    bool warn = false; // a warning (setup tips)
};

struct InfoPage {
    std::string title;
    std::vector<InfoPara> paras;
};

// -1 / 0 / 1 like strcmp, by numbers: 0.8.9 < 0.8.10; "" is older than any version
inline int version_cmp(const std::string &a, const std::string &b)
{
    size_t i = 0, j = 0;
    while (i < a.size() || j < b.size()) {
        long x = i < a.size() ? std::strtol(a.c_str() + i, nullptr, 10) : -1;
        long y = j < b.size() ? std::strtol(b.c_str() + j, nullptr, 10) : -1;
        if (x != y)
            return x < y ? -1 : 1;
        i = a.find('.', i);
        j = b.find('.', j);
        i = i == std::string::npos ? a.size() : i + 1;
        j = j == std::string::npos ? b.size() : j + 1;
    }
    return 0;
}

// What's New, newest first; one entry per release, written at release time (only what was tested is listed)
struct ChangeNote {
    const char *version;
    std::vector<const char *> lines;
};
inline const std::vector<ChangeNote> &changelog()
{
    static const std::vector<ChangeNote> notes = {
        {"0.8.39",
         {"Lower latency, now the same as the original's: TI Revive's delay follows your audio buffer all the way "
          "down (before, never below 256 samples) and uses the original's smaller safety margin. At 64 samples and "
          "48 kHz a note now takes 76 ms instead of 82.7 ms.",
          "Notes you play on a keyboard skip that delay, as in the original: about 22 ms instead of 76 ms in DAWs that "
          "mark played notes (Ableton Live). Notes from clips keep their exact timing. A played note that would be cut "
          "by a clip note of the same key now waits for it instead.",
          "The Access manuals in the ? menu: Reference, Quickstart, Atomizer and the tutorials, opened from your own "
          "Access installation.",
          "The start pages work like the original's: \"Starting up\" goes straight to \"Syncing memory with Virus\" "
          "when banks have to be read, without the editor in between.",
          "This welcome page and this What's New page, in the original's style.",
          "The D button (direct monitoring) works: the part plays straight on Out 1.",
          "macOS: a saved project no longer switches the Virus' audio clock at start (the display showed \"Audio "
          "clock changed\" twice).",
          "macOS: when the Virus' audio stands still after the Mac wakes from sleep, TI Revive reconnects the Virus "
          "by itself, like unplugging and plugging it in (at most once a minute). macOS sometimes leaves the Virus' "
          "sound with gaps after a sleep until it is plugged in again.",
          "macOS: the background program that talks to the Virus now also runs on Intel Macs (issue #33); before, "
          "only the plug-in did.",
          "Panic and the editor's own messages reach the Virus at once, also with played notes on."}},
    };
    return notes;
}

inline InfoPage whats_new_page(const std::string &from, const std::string &to)
{
    InfoPage p;
    p.title = "What's New in TI Revive " + to;
    for (const ChangeNote &n : changelog()) {
        if (version_cmp(n.version, to) > 0 || version_cmp(n.version, from) <= 0)
            continue;
        p.paras.push_back({std::string("Version ") + n.version, true, false});
        for (const char *l : n.lines)
            p.paras.push_back({std::string("- ") + l, false, false});
    }
    return p;
}

inline InfoPage welcome_page(const std::string &version)
{
    InfoPage p;
    p.title = "Welcome to TI Revive " + version;
    p.paras = {
        {"TI Revive makes your Access Virus TI work on today's Windows, macOS and Linux, without the old Access "
         "drivers. The editor looks like the original Virus Control because it uses the pictures of your own Access "
         "installation. TI Revive is a community project and is not affiliated with or endorsed by Access Music."},
        {"Each time it starts, the plug-in connects to your Virus and checks its memory. The first time it reads "
         "everything, which takes several minutes; after that only banks that changed are read again. You can play "
         "meanwhile."},
        {"Click the ? at the bottom right whenever you need help: Explain the Controls, Setup Tips for Your DAW, the "
         "Access manuals of your installation, the TI Revive website, and Report a Problem if something does not work."},
        {"Have fun with your Virus TI!"},
    };
    return p;
}

// what the user chose, in <plug-in folder>/info-page.state
struct InfoState {
    bool welcome_off = false; // "Don't show again" on the welcome page
    bool news_off = false;    // "Don't show again" on What's New
    std::string last_version; // the newest version whose news were seen (OK or Don't show again)
};

inline std::string info_state_text(const InfoState &s)
{
    return std::string("welcome=") + (s.welcome_off ? "off" : "on") + "\nnews=" + (s.news_off ? "off" : "on") +
           "\nlast=" + s.last_version + "\n";
}

inline InfoState info_state_parse(const std::string &text)
{
    InfoState s;
    size_t at = 0;
    while (at < text.size()) {
        size_t nl = text.find('\n', at);
        const std::string line = text.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
        at = nl == std::string::npos ? text.size() : nl + 1;
        if (line == "welcome=off")
            s.welcome_off = true;
        else if (line == "news=off")
            s.news_off = true;
        else if (line.compare(0, 5, "last=") == 0)
            s.last_version = line.substr(5);
    }
    return s;
}

enum class InfoStart { None, Welcome, WhatsNew };

// which page opens with the editor. has_history: TI Revive ran here before (its caches exist), so a missing state
// file means an update from a version without these pages, not a new install
inline InfoStart info_at_start(const InfoState &s, const std::string &version, bool has_history)
{
    const bool updated = s.last_version.empty() ? has_history : version_cmp(version, s.last_version) > 0;
    if (updated && !s.news_off)
        return InfoStart::WhatsNew;
    if (!s.welcome_off && !(s.last_version.empty() && has_history))
        return InfoStart::Welcome;
    return InfoStart::None;
}

} // namespace vti
