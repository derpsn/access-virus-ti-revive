// The bottom LCD shows the control under the mouse like the original (E353/E354): name, value, [stored patch value],
// part - written only when a field changed, cleared off a control only if hover filled it.
#include "check.h"
#include "editor/lcdhover.h"

using namespace vti;

namespace {

LcdFields cutoff(const char *value, const char *stored, int part = 1)
{
    return {"Filter 1 Cutoff", value, std::string("[") + stored + "]", "Part " + std::to_string(part)};
}

} // namespace

TEST(lcdhover_shows_the_control_under_the_mouse)
{
    LcdHover h;
    LcdFields lcd;
    CHECK(h.enter(cutoff("127", "127"), lcd));
    CHECK(lcd.param == "Filter 1 Cutoff" && lcd.value == "127" && lcd.orig == "[127]" && lcd.part == "Part 1");
}

TEST(lcdhover_writes_only_changes)
{
    LcdHover h;
    LcdFields lcd;
    h.enter(cutoff("61", "61"), lcd);
    CHECK(!h.enter(cutoff("61", "61"), lcd)); // the same control again: no repaint
    CHECK(h.enter(cutoff("61", "61", 2), lcd));
}

TEST(lcdhover_off_a_control_clears_what_it_filled)
{
    LcdHover h;
    LcdFields lcd;
    h.enter(cutoff("80", "61"), lcd);
    CHECK(h.leave(lcd));
    CHECK(lcd.param.empty() && lcd.value.empty() && lcd.orig.empty() && lcd.part.empty());
    CHECK(!h.leave(lcd)); // once
}

TEST(lcdhover_leaves_other_messages_alone)
{
    // "Virus connected", the bank sync progress ...: not hover's, the mouse moving over empty panel keeps them
    LcdHover h;
    LcdFields lcd{"Virus connected", "", "", ""};
    CHECK(!h.leave(lcd));
    CHECK(lcd.param == "Virus connected");
}

TEST(lcdhover_skips_the_editors_own_and_internal_parameters)
{
    CHECK(lcd_hover_shows("Filter 1/Cutoff"));
    CHECK(lcd_hover_shows("Multi Parameters/Part Volume"));
    CHECK(!lcd_hover_shows("VirusControl/SelectedPage"));
    CHECK(!lcd_hover_shows("VirusControl Links/Filter Link"));
    CHECK(!lcd_hover_shows("Internal/Browser"));
    CHECK(lcd_hover_shows("Internal MIDI Standard Parameters/Modulation Wheel"));
    CHECK(!lcd_hover_shows(""));
}

TEST(lcdhover_shows_global_settings)
{
    // Master Tune, Clock Tempo ... (no part): shown like the others; the page tabs are VirusControl/... (E353)
    CHECK(lcd_hover_shows("Global Master/Tuning"));
    CHECK(lcd_hover_shows("Multi Parameters/Clock Tempo"));
}
