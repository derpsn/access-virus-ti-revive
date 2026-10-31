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
