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
    const MenuAction order[] = {MenuAction::HelpMode, MenuAction::SetupTips, MenuAction::Website, MenuAction::Report, MenuAction::SaveLogs,
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

// The Access manuals (editor/manuals.h): the PDFs TI Revive's setup copies from the user's own Access installer next
// to the Access files (Documentation/Manuals), Snow names for a Snow, the TI's where the Snow has none
TEST(manuals_file_names_ti_and_snow)
{
    CHECK(manual_file(kManualReference, false) == "Virus TI Reference.pdf");
    CHECK(manual_file(kManualReference, true) == "Virus TI Snow Reference.pdf");
    CHECK(manual_file(kManualQuickstart, true) == "Virus TI Snow Quickstart.pdf");
    CHECK(manual_file(kManualAtomizer, true) == "Atomizer Manual.pdf"); // one for both
    CHECK(manual_file(kManualTutorial, false) == "Virus Control Tutorial.pdf");
    CHECK(manual_file(kManualProgramming, false) == "Programming Analogue Synths.pdf");
    CHECK(std::string(manual_title(kManualReference, true)) == "Virus TI Snow Reference Manual (PDF)");
    CHECK(std::string(manual_title(kManualReference, false)) == "Virus TI Reference Manual (PDF)");
}

TEST(manuals_folders_next_to_the_access_files)
{
    // Windows/Linux: <...>/Virus TI/Common/Virus Control.dll -> <...>/Virus TI/Documentation
    auto d = manual_dirs("/home/u/.local/share/tirevive/access/Access Music/Virus TI/Common/Virus Control.dll", "");
    CHECK(!d.empty() && d[0] == "/home/u/.local/share/tirevive/access/Access Music/Virus TI/Documentation");
    // a plain Windows Access install keeps Documentation under Program Files (x86)
    d = manual_dirs("C:/Program Files/Access Music/Virus TI/Common/Virus Control.dll", "C:/Program Files (x86)");
    bool x86 = false;
    for (auto &s : d)
        x86 |= s == "C:/Program Files (x86)/Access Music/Virus TI/Documentation";
    CHECK(x86);
    d = manual_dirs("C:\\Program Files\\Access Music\\Virus TI\\Common\\Virus Control.dll", "");
    CHECK(!d.empty() && d[0] == "C:/Program Files/Access Music/Virus TI/Documentation");
    // macOS: the bundle's resources; the setup copies the manuals to /Library/Documentation
    d = manual_dirs("/Library/Application Support/Access Music/Virus TI/Common/Virus Control.bundle/Contents/Resources", "");
    bool mac = false;
    for (auto &s : d)
        mac |= s == "/Library/Documentation/Access Music/Virus TI";
    CHECK(mac);
    CHECK(manual_dirs("", "").size() >= 1); // the macOS folder is always tried
}

TEST(manuals_find_snow_falls_back_to_ti)
{
    std::vector<std::string> dirs = {"/a", "/b"}; // Documentation roots
    auto only = [](std::vector<std::string> have) {
        return [have](const std::string &p) {
            for (auto &h : have)
                if (h == p)
                    return true;
            return false;
        };
    };
    CHECK(find_manual(dirs, kManualReference, true, only({"/b/Manuals/Virus TI Snow Reference.pdf"})) ==
          "/b/Manuals/Virus TI Snow Reference.pdf");
    CHECK(find_manual(dirs, kManualReference, true, only({"/a/Manuals/Virus TI Reference.pdf"})) ==
          "/a/Manuals/Virus TI Reference.pdf");
    CHECK(find_manual(dirs, kManualAtomizer, false, only({})).empty());
    // the MSI keeps the programming tutorial under Tutorials, the Mac package under Manuals
    CHECK(find_manual(dirs, kManualProgramming, false,
                      only({"/a/Tutorials/Programming Tutorial/Programming Analogue Synths.pdf"})) ==
          "/a/Tutorials/Programming Tutorial/Programming Analogue Synths.pdf");
    CHECK(find_manual(dirs, kManualProgramming, false, only({"/b/Manuals/Programming Analogue Synths.pdf"})) ==
          "/b/Manuals/Programming Analogue Synths.pdf");
}

// the "?" menu lists the manuals it found, as the original's does, in their own group; none found: no group
TEST(editormenus_help_menu_manuals_when_found)
{
    HelpMenuState s;
    BuiltMenu m = help_menu(s);
    CHECK(index_of(m, MenuAction::Manual) < 0);
    s.manuals = {kManualReference, kManualAtomizer};
    s.snow = true;
    m = help_menu(s);
    int i = index_of(m, MenuAction::Manual);
    CHECK(i > index_of(m, MenuAction::SetupTips) && i < index_of(m, MenuAction::Website));
    CHECK(m.items[i].text == "Virus TI Snow Reference Manual (PDF)" && m.arg_at(i) == kManualReference);
    CHECK(m.action_at(i + 1) == MenuAction::Manual && m.arg_at(i + 1) == kManualAtomizer);
    CHECK(m.items[i - 1].separator);
    int lines = 0;
    for (auto &it : m.items)
        lines += it.separator;
    CHECK(lines == 3);
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

// Randomize the selected part's unlocked sections, one entry per amount (randomizer.h), after Compare
TEST(editormenus_edit_menu_randomize)
{
    EditMenuState s;
    CHECK(index_of(edit_menu(s), MenuAction::Randomize) < 0); // no part selected
    s.part = 0;
    BuiltMenu m = edit_menu(s);
    int n = 0, first = -1;
    for (size_t i = 0; i < m.actions.size(); i++)
        if (m.actions[i] == MenuAction::Randomize) {
            if (first < 0)
                first = (int)i;
            CHECK(m.args[i] == n);
            n++;
        }
    CHECK(n == 3 && first > index_of(m, MenuAction::Compare) && first < index_of(m, MenuAction::Panic));
    CHECK(m.items[first].text == "Randomize Unlocked Sections: Subtle (10 %)");
    CHECK(m.items[first + 2].text == "Randomize Unlocked Sections: Strong (50 %)");
}
