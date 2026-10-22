// skinpage.h - the Control Center in the look of Access' Virus Control Center: its layout and images from the
// user's own installer (AccessFiles::open_control_center), TI Revive's logic behind them (src/cc). Pages as in the
// original: start, device selector, Backup & Restore (our restore: RAM banks from a library, never flash, E313); Burn
// Flash and OS Updater are shown but not offered (they write the Virus' flash). Our additions sit in the original's empty footer: Nerd mode, Convert library, plain view.
#pragma once
#include "vstgui/lib/cviewcontainer.h"
#include <functional>
#include <memory>
#include <string>

namespace vti {
class AccessFiles;
}

namespace tirevive {

struct SkinPageActions {
    std::function<void()> nerd_mode, convert_library, plain_view;
    std::function<void(VSTGUI::CPoint)> size_menu; // right-click where there is no control
    // RESTORE: the app's restore (file, then the bank menu); its status lines go to the given function too
    std::function<void(std::function<void(const std::string &)>)> restore;
};

// nullptr if the user's installer has no Control Center (the app then shows its plain page); `error` says why
VSTGUI::CViewContainer *make_skin_page(std::unique_ptr<vti::AccessFiles> files, const std::string &version,
                                       SkinPageActions actions, std::string &error);
VSTGUI::CPoint skin_page_size(VSTGUI::CViewContainer *page);
void set_skin_page_active(VSTGUI::CViewContainer *page, bool on);

} // namespace tirevive
