// Help mode texts (editor/helptext): numbered copies share one text, titles name the section, internal fields have none
#include "check.h"
#include "editor/helptext.h"

using namespace vti;

TEST(help_shared_texts)
{
    HelpText a, b;
    CHECK(help_for("Assign Slot 1/Amount 1", a) && help_for("Assign Slot 6/Amount 3", b) && a.text == b.text);
    CHECK(help_for("LFO 3/Rate", a) && a.title == "Rate (LFO 3)");
    CHECK(help_for("Arpeggiator User Pattern/Step 32 Velocity", a) && !a.text.empty());
    CHECK(help_for("Internal MIDI Standard Parameters/07 Volume", a) && a.title == "Part volume and activity");
    CHECK(help_for("Multi Part Parameter/Output Select Control3", a) && a.text.find("USB 3") != std::string::npos);
    // section locking works since round 0.8.36 (E378): the note tells how to use it
    CHECK(help_for("Locks/Matrix Slot 4", a) && a.text.find("Not supported") == std::string::npos &&
          a.text.find("Double-click") != std::string::npos);
    CHECK(help_for("LockView/Part Lock Indicator", a) && a.text.find("Not supported") == std::string::npos);
}

TEST(help_own_text_wins)
{
    HelpText osc1, generic;
    CHECK(help_for("LFO 1/Dest Osc1", osc1) && help_for("LFO 1/Dest Osc2", generic) && osc1.text != generic.text);
    CHECK(help_for("Filter 2/Offset for FilterLink", osc1) && osc1.text.find("offset") == std::string::npos);
    CHECK(help_for("Filter 1/Cutoff", osc1) && osc1.title == "Cutoff (Filter 1)");
    CHECK(help_for("Suboscillator/Volume", osc1) && osc1.title == "Sub oscillator"); // no "(Suboscillator)"
    CHECK(help_for("Delay/Send", osc1) && osc1.title == "Delay send");
}

TEST(help_none_for_internal_fields)
{
    HelpText t;
    CHECK(!help_for("Debug/Sync Events", t));
    CHECK(!help_for("Internal System Info/TIOS Version String", t));
    CHECK(!help_for("No Such/Parameter", t));
    CHECK(help_count() > 250);
}

TEST(help_none_for_display_only_indicator)
{
    // the global lock indicator is always Enabled="false" and empty while nothing is locked; since the help mode
    // also frames disabled controls (0.8.18) a note made it an empty framed box next to the ? button (user report)
    HelpText t;
    CHECK(!help_for("VirusControl/Global Section Lock Indicator", t));
    CHECK(help_for("VirusControl/Display Parameter", t)); // display-only too, but it shows something
}

TEST(help_inactive_reason)
{
    // a condition name of the layout as a reason a reader understands
    CHECK(inactive_reason("VirusControl/Display/Chorus/Type Is Off") == "Chorus Type is Off");
    CHECK(inactive_reason("Parameter Active Condition/Delay/Type Is NOT Classic") == "Delay Type is not Classic");
    CHECK(inactive_reason("").empty());
}

TEST(help_depends_reason)
{
    // a control greyed by its parameter's own condition: the parameters that condition tests, readable
    CHECK(depends_reason({"Input/Mode"}) == "depends on Input Mode");
    CHECK(depends_reason({"Vocoder/Mode", "Filter 2/Cutoff Link"}) == "depends on Vocoder Mode, Filter 2 Cutoff Link");
    CHECK(depends_reason({}).empty());
}
