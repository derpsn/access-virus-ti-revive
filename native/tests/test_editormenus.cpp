// TI Revive's own menus in the editor (editor/editormenus.h): the "?" menu (the original's opens its manuals; ours
// explains the controls, links the website and the issues, saves logs and has Panic) - entries, order, separators
// and which action each entry stands for. Hardware-free.
#include "check.h"
#include "editor/editormenus.h"

using namespace vti;

static int index_of(const BuiltMenu &m, MenuAction a)
{
    for (size_t i = 0; i < m.actions.size(); i++)
        if (m.actions[i] == a)
            return (int)i;
    return -1;
}

TEST(editormenus_help_menu_entries_in_order)
{
    HelpMenuState s;
    s.connected = true;
    BuiltMenu m = help_menu(s);
    CHECK(m.items.size() == m.actions.size());
    const MenuAction order[] = {MenuAction::HelpMode, MenuAction::Website, MenuAction::Report, MenuAction::SaveLogs,
                                MenuAction::DiagLog, MenuAction::Panic};
    int last = -1;
    for (MenuAction a : order) {
        int i = index_of(m, a);
        CHECK(i > last);
        last = i;
    }
    CHECK(m.items[index_of(m, MenuAction::HelpMode)].text == "Explain the Controls");
    CHECK(m.items[index_of(m, MenuAction::Panic)].text == "Panic (All Notes Off)");
    // separators stand for no action and are never chosen
    int lines = 0;
    for (size_t i = 0; i < m.items.size(); i++)
        if (m.items[i].separator) {
            lines++;
            CHECK(m.actions[i] == MenuAction::None);
        }
    CHECK(lines == 2);
}

TEST(editormenus_help_menu_panic_needs_the_virus)
{
    HelpMenuState s;
    s.connected = false;
    BuiltMenu m = help_menu(s);
    CHECK(!m.items[index_of(m, MenuAction::Panic)].enabled);
    s.connected = true;
    m = help_menu(s);
    CHECK(m.items[index_of(m, MenuAction::Panic)].enabled);
}

TEST(editormenus_help_menu_diag_log_checked_when_on)
{
    HelpMenuState s;
    BuiltMenu m = help_menu(s);
    CHECK(m.checked == -1);
    s.diag_log = true;
    m = help_menu(s);
    CHECK(m.checked == index_of(m, MenuAction::DiagLog));
}

TEST(editormenus_action_at)
{
    HelpMenuState s;
    BuiltMenu m = help_menu(s);
    CHECK(m.action_at(-1) == MenuAction::None);
    CHECK(m.action_at((int)m.actions.size()) == MenuAction::None);
    CHECK(m.action_at(0) == MenuAction::HelpMode);
}

TEST(editormenus_links)
{
    CHECK(std::string(issues_url()) == "https://github.com/derpsn/access-virus-ti-revive/issues");
}

// right-click where there is no control: Undo/Redo with what they take back, Panic, then the editor sizes (the
// menu this click had before)
TEST(editormenus_edit_menu_undo_redo_and_sizes)
{
    EditMenuState s;
    s.zoom = 1.5;
    s.undo = "Cutoff (Part 1)";
    s.connected = true;
    BuiltMenu m = edit_menu(s);
    CHECK(m.items.size() == m.actions.size() && m.args.size() == m.actions.size());
    CHECK(m.items[0].text == "Undo Cutoff (Part 1)" && m.items[0].enabled && m.actions[0] == MenuAction::Undo);
    CHECK(m.items[1].text == "Redo" && !m.items[1].enabled && m.actions[1] == MenuAction::Redo);
    CHECK(m.items[index_of(m, MenuAction::Panic)].enabled);
    int sizes = 0;
    for (size_t i = 0; i < m.actions.size(); i++)
        if (m.actions[i] == MenuAction::Size) {
            CHECK(m.items[i].text == "Size " + std::to_string(kEditorSizes[m.args[i]].percent) + " %");
            sizes++;
        }
    CHECK(sizes == (int)(sizeof kEditorSizes / sizeof kEditorSizes[0]));
    CHECK(m.action_at(m.checked) == MenuAction::Size && kEditorSizes[m.args[m.checked]].percent == 150);
    CHECK(m.arg_at(m.checked) == m.args[m.checked] && m.arg_at(-1) == 0);
}

TEST(editormenus_edit_menu_nothing_to_undo)
{
    EditMenuState s;
    s.redo = "Load watercop (Part 2)";
    BuiltMenu m = edit_menu(s);
    CHECK(m.items[0].text == "Undo" && !m.items[0].enabled);
    CHECK(m.items[1].text == "Redo Load watercop (Part 2)" && m.items[1].enabled);
    CHECK(!m.items[index_of(m, MenuAction::Panic)].enabled); // not connected
    s.zoom = 1.3; // no size entry fits: none checked
    CHECK(edit_menu(s).checked == -1);
}

// Compare for the selected part, after Undo/Redo; its text says which way it goes (the menu has one check mark,
// the size's)
TEST(editormenus_edit_menu_compare)
{
    EditMenuState s;
    BuiltMenu m = edit_menu(s);
    CHECK(index_of(m, MenuAction::Compare) < 0); // no part selected (no editor state yet)
    s.part = 1;
    m = edit_menu(s);
    int i = index_of(m, MenuAction::Compare);
    CHECK(i > index_of(m, MenuAction::Redo) && i < index_of(m, MenuAction::Panic));
    CHECK(m.items[i].text == "Compare with Loaded Patch (Part 2)" && m.items[i].enabled);
    s.comparing = true;
    m = edit_menu(s);
    i = index_of(m, MenuAction::Compare);
    CHECK(m.items[i].text == "End Compare, Back to Your Edit (Part 2)");
}
