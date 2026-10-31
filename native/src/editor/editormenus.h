// editormenus.h - TI Revive's own menus in the editor, built from plain state so they can be tested: the "?" menu
// (the original's "?" opens its manuals and videos at access-music.de; ours explains the controls, links the TI Revive
// website and issues, saves the logs for a bug report and has Panic) and the menu of a right-click where there is no
// control (Undo/Redo, Panic and the editor's size). Each entry stands for one MenuAction (and an argument); the
// editor carries the action out. Header only, no drawing.
#pragma once
#include "editor/popupmenu.h"
#include <string>
#include <vector>

namespace vti {

// where bug reports go (the "?" menu's "Report a Problem")
inline const char *issues_url() { return "https://github.com/derpsn/access-virus-ti-revive/issues"; }

enum class MenuAction { None, HelpMode, Website, Report, SaveLogs, DiagLog, Panic, Undo, Redo, Size };

// the editor sizes of the right-click menu (the original has no such setting)
struct EditorSize {
    double zoom;
    int percent;
};
inline constexpr EditorSize kEditorSizes[] = {{1.0, 100}, {1.25, 125}, {1.5, 150}, {1.75, 175}, {2.0, 200}};

struct BuiltMenu {
    std::vector<PopupItem> items;
    std::vector<MenuAction> actions; // one per item; None for separators
    std::vector<int> args;           // one per item: the action's argument (Size: index in kEditorSizes)
    int checked = -1;                // the entry with the check mark, -1 for none
    MenuAction action_at(int i) const { return i >= 0 && i < (int)actions.size() ? actions[i] : MenuAction::None; }
    int arg_at(int i) const { return i >= 0 && i < (int)args.size() ? args[i] : 0; }
    void add(MenuAction a, std::string text, bool enabled = true, int arg = 0)
    {
        items.emplace_back(std::move(text), enabled);
        actions.push_back(a);
        args.push_back(arg);
    }
    void line()
    {
        items.push_back(PopupItem::line());
        actions.push_back(MenuAction::None);
        args.push_back(0);
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

struct EditMenuState {
    std::string undo, redo; // what Undo / Redo would take back or do again ("" = nothing)
    bool connected = false;
    double zoom = 1.0; // the editor's size now
};

inline BuiltMenu edit_menu(const EditMenuState &s)
{
    BuiltMenu m;
    m.add(MenuAction::Undo, s.undo.empty() ? "Undo" : "Undo " + s.undo, !s.undo.empty());
    m.add(MenuAction::Redo, s.redo.empty() ? "Redo" : "Redo " + s.redo, !s.redo.empty());
    m.line();
    m.add(MenuAction::Panic, "Panic (All Notes Off)", s.connected);
    m.line();
    for (int i = 0; i < (int)(sizeof kEditorSizes / sizeof kEditorSizes[0]); i++) {
        if (s.zoom > kEditorSizes[i].zoom - 0.01 && s.zoom < kEditorSizes[i].zoom + 0.01)
            m.checked = (int)m.items.size();
        m.add(MenuAction::Size, "Size " + std::to_string(kEditorSizes[i].percent) + " %", true, i);
    }
    return m;
}

} // namespace vti
