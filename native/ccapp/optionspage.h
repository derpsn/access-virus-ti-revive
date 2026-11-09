// optionspage.h - the Control Center's Options page (0.8.41): the plug-in's options (cc/diag.h plugin_options) as
// on/off boxes with a line on what each does. They are flag files the plug-in reads when an instance starts, so a
// change applies from the next start of the plug-in. Nothing here sends anything to the Virus. A page of the main
// window like Nerd mode (a second window stayed blank on Linux/GTK).
#pragma once
#include "vstgui/lib/cviewcontainer.h"
#include <functional>

namespace tirevive {

constexpr VSTGUI::CCoord kOptionsWidth = 640, kOptionsHeight = 470;

// The page (kOptionsWidth x kOptionsHeight at 0,0), hidden. `back` is called by its "Back" button.
VSTGUI::CViewContainer *make_options_page(std::function<void()> back);
// Shows / hides it; showing reads the options' current state again.
void set_options_page_active(VSTGUI::CViewContainer *page, bool on);

} // namespace tirevive
