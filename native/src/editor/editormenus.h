// editormenus.h - TI Revive's own menus in the editor, built from plain state so they can be tested: the "?" menu
// (the original's "?" opens its manuals and videos at access-music.de; ours explains the controls, links the TI Revive
// website and issues, saves the logs for a bug report and has Panic). Each entry stands for one MenuAction; the
// editor carries the action out. Header only, no drawing.
#pragma once
#include "editor/popupmenu.h"
#include <string>
#include <vector>

namespace vti {

// where bug reports go (the "?" menu's "Report a Problem")
inline const char *issues_url() { return "https://github.com/derpsn/access-virus-ti-revive/issues"; }

enum class MenuAction { None, HelpMode, Website, Report, SaveLogs, DiagLog, Panic };

struct BuiltMenu {
    std::vector<PopupItem> items;
    std::vector<MenuAction> actions; // one per item; None for separators
    int checked = -1;                // the entry with the check mark, -1 for none
    MenuAction action_at(int i) const { return i >= 0 && i < (int)actions.size() ? actions[i] : MenuAction::None; }
    void add(MenuAction a, std::string text, bool enabled = true)
    {
        items.emplace_back(std::move(text), enabled);
        actions.push_back(a);
    }
    void line()
    {
        items.push_back(PopupItem::line());
        actions.push_back(MenuAction::None);
    }
};

struct HelpMenuState {
    bool connected = false; // the Virus is there: Panic can reach it
    bool diag_log = false;  // diag.on exists: the detailed log is written from the next start
};

inline BuiltMenu help_menu(const HelpMenuState &s)
{
    BuiltMenu m;
    m.add(MenuAction::HelpMode, "Explain the Controls");
    m.line();
    m.add(MenuAction::Website, "TI Revive Website and Manual");
    m.add(MenuAction::Report, "Report a Problem (GitHub)");
    m.add(MenuAction::SaveLogs, "Save Logs for a Bug Report...");
    if (s.diag_log)
        m.checked = (int)m.items.size();
    m.add(MenuAction::DiagLog, "Detailed Log (from the Next Start)");
    m.line();
    m.add(MenuAction::Panic, "Panic (All Notes Off)", s.connected);
    return m;
}

} // namespace vti
