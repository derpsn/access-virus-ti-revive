// skinpage.cpp - see skinpage.h.
#include "skinpage.h"
#include "model/utf8.h"
#include "access/accessfiles.h"
#include "cc/ccskin.h"
#include "cc/controlcenter.h"
#include "model/remotedevice.h"
#include "skinview.h"
#include "nerdlive.h"
#include "cc/diag.h"
#include "cc/nerdview.h"
#include "cc/updatecheck.h"
#include "device/memsync.h"
#include "vstgui/lib/cfileselector.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cvstguitimer.h"
#include "vstgui/standalone/include/iapplication.h"
#include "vstgui/standalone/include/ipreference.h"
#include <atomic>
#include <filesystem>
#include <cctype>
#include <cstring>
#include <mutex>
#include <thread>

using namespace VSTGUI;
using vti::cc::SkinNode;

namespace tirevive {
namespace {

// pages, as the original's expressions name them; kNerd is TI Revive's own (nerd mode in the Access look, 2026-11-15)
enum Page { kStartup, kSelector, kBackup, kBurn, kUpdate, kNerd, kOptions }; // kNerd, kOptions: TI Revive's own

// our parameters for the elements we add to the layout
const char *kNotSupported = "TIRevive/NotSupported", *kStatus = "TIRevive/Status",
           *kChangePath = "TIRevive/ChangePath", *kModel = "TIRevive/Model";

// Nerd mode in the Access look: a page in the original's content area (0,42 686x379), drawn in its colours
// (skinview.h tr_* types). Geometry in frame pixels, as the original's layout.
const char *kNerdPage = "TIRevive/NerdPage", *kNerdLiveCols = "TIRevive/NerdLiveCols",
           *kNerdLogCols = "TIRevive/NerdLogCols", *kNerdMode = "TIRevive/NerdMode", *kNerdScroll = "TIRevive/NerdScroll",
           *kNerdAction = "TIRevive/NerdAction", *kFooter = "TIRevive/Footer";
constexpr int kNerdRows = 15, kNerdRowH = 14, kTableX = 19, kTableY = 128, kTableW = 648, kHeadH = 19;
constexpr int kRowsY = kTableY + kHeadH, kRowsH = kNerdRows * kNerdRowH, kScrollW = 10;
constexpr int kColTime = 25, kColDir = 107, kColText = 182; // live: TIME (12 characters) | DIRECTION | MESSAGE
enum NerdMode { kNerdLive, kNerdPlugin, kNerdBroker };

// the Options page in the Access look (2026-11-15): one row per plug-in option (cc/diag.h plugin_options), a check
// box, the title and the description in two LCD lines; a click on the row switches it
const char *kOptionsPage = "TIRevive/OptionsPage", *kOptRow = "TIRevive/OptRow", *kOptScroll = "TIRevive/OptScroll";
constexpr int kOptRows = 6, kOptRowH = 40, kOptY = 126, kOptX = 19, kOptW = 648;

// BACKUP & RESTORE gets the plain view's content: the RAM banks, the note on what is written; CANCEL while a backup
// runs (the plain window stays only for a missing Access installation)
const char *kBackupPanel = "TIRevive/BackupPanel", *kBusy = "TIRevive/Busy", *kCancel = "TIRevive/Cancel";
enum NerdAct { kActPause, kActClock, kActRecord, kActSave };

// USB does not tell the models apart (only the Snow by its product string): the user picks it by clicking the
// picture. Frames of the original's model picture strip: 1 Desktop, 2 Polar, 3 Keyboard, 5 Snow.
enum Model { kDesktop, kKeyboard, kPolar };
const char *kModelNames[] = {"Desktop", "Keyboard", "Polar"};
const int kModelFrames[] = {1, 3, 2};

class SkinPage : public CViewContainer, public SkinState {
public:
    SkinPage(std::unique_ptr<vti::AccessFiles> files, vti::cc::SkinLayout layout, std::string version,
             SkinPageActions actions)
        : CViewContainer(CRect(0, 0, layout.width, layout.height)), files_(std::move(files)),
          layout_(std::move(layout)), version_(std::move(version)), actions_(std::move(actions))
    {
        setBackgroundColor(CColor(0, 0, 0, 255));
        if (auto m = VSTGUI::Standalone::IApplication::instance().getPreferences().get("Model"))
            for (int i = 0; i < 3; i++)
                if (*m == kModelNames[i])
                    model_ = i;
        add_our_elements();
        view_ = new SkinView(CRect(0, 0, layout_.width, layout_.height), layout_, *files_, *this);
        addView(view_);
    }
    ~SkinPage() noexcept override
    {
        stop_ = true;
        cancel_ = true;
        if (worker_.joinable())
            worker_.join();
    }

    void activate(bool on)
    {
        setVisible(on);
        if (!on) {
            timer_ = nullptr;
            if (!live_.recording()) // a recording goes on behind the other pages
                live_.stop();
            return;
        }
        timer_ = makeOwned<CVSTGUITimer>([this](CVSTGUITimer *) {
            if (page_ == kNerd && ++nerd_ticks_ % 5 == 0) // the table four times a second
                nerd_refresh();
            view_->tick();
        }, 50, true);
        if (page_ == kStartup)
            refresh();
        if (page_ == kNerd)
            nerd_enter();
    }

    // ---- SkinState ----
    bool visible(const std::string &e) override
    {
        std::string x = e.substr(e.rfind('/') + 1);
        std::lock_guard<std::mutex> l(mu_);
        if (x == "tab4active")
            return page_ == kStartup;
        if (x == "tab5active")
            return page_ == kSelector;
        if (x == "tab1activeSnow")
            return page_ == kBackup && snow_;
        for (int m = 0; m < 3; m++)
            if (x == std::string("tab1active") + kModelNames[m])
                return page_ == kBackup && !snow_ && model_ == m;
        if (x == "tab2active")
            return page_ == kBurn;
        if (x == "tab3active")
            return page_ == kUpdate;
        if (e == kNerdPage)
            return page_ == kNerd;
        if (e == kOptionsPage)
            return page_ == kOptions;
        if (e.compare(0, 16, "TIRevive/OptSlot") == 0) // a row with an option in it
            return opt_scroll_.first() + atoi(e.c_str() + 16) < (int)vti::cc::plugin_options().size();
        if (e == kBackupPanel)
            return page_ == kBackup;
        if (e == kBusy)
            return busy_ && backing_up_;
        if (e == kNerdLiveCols)
            return page_ == kNerd && nerd_mode_ == kNerdLive;
        if (e == kNerdLogCols)
            return page_ == kNerd && nerd_mode_ != kNerdLive;
        if (x == "SelectRow0Enable")
            return found_;
        if (x == "BootReady")
            return found_ && !busy_;
        if (x == "BackupRestoreReady")
            return !target_.empty() && !busy_ && !done_;
        return false; // the Burn Flash / OS update elements, rows 1..6 (one Virus), ...
    }
    int value(const std::string &p) override
    {
        std::lock_guard<std::mutex> l(mu_);
        if (p == "ControlCenter/Tabs/ActiveTab")
            return page_ == kBackup ? 0 : page_ == kBurn ? 1 : page_ == kUpdate ? 2 : -1;
        if (p == "ControlCenter/SelectVirus/SelectedRow")
            return found_ ? 0 : -1;
        if (p == "ControlCenter/SelectVirus/Family") // picture strip: 1 = TI Desktop, 5 = TI Snow
            return !found_ ? 0 : snow_ ? 5 : kModelFrames[model_];
        if (p == kNerdMode)
            return nerd_mode_;
        if (p == kFooter) // the footer button of the page that is open stays down
            return page_ == kNerd ? 0 : page_ == kOptions ? 1 : -1;
        if (p.compare(0, 17, "TIRevive/OptCheck") == 0) { // "TIRevive/OptCheck<row>": that row's option
            const auto &opts = vti::cc::plugin_options();
            const int i = opt_scroll_.first() + atoi(p.c_str() + 17);
            return i >= 0 && i < (int)opts.size() && vti::cc::plugin_option(opts[(size_t)i].name) ? 1 : 0;
        }
        return 0;
    }
    std::string text(const std::string &p) override
    {
        std::lock_guard<std::mutex> l(mu_);
        std::string x = p.substr(p.rfind('/') + 1);
        if (x == "VersionText")
            return "TI REVIVE CONTROL CENTER " + version_ + "\n" + status_;
        if (x == "Row0VirusName")
            return found_ ? product_ : "";
        if (x == "SelectedVirusName")
            return !found_ ? "" : snow_ ? product_ : product_ + " " + kModelNames[model_];
        if (x == "Row0VirusTypeName")
            return found_ ? usb_ : "";
        if (x == "SelectedVirusOSLabel")
            return found_ ? "OS:" : "";
        if (x == "SelectedVirusOS")
            return found_ ? " -" : ""; // no request for it is documented yet
        if (x == "SelectedVirusStatusLabel")
            return found_ ? "STATUS:" : "";
        if (x == "SelectedVirusStatus")
            return found_ ? (busy_ ? " BUSY" : " READY") : "";
        if (x == "BackupRestorePath") // one line of 84 characters in the original's field: the end of the path
            return backup_text_.size() > 84 ? "..." + backup_text_.substr(backup_text_.size() - 81) : backup_text_;
        if (x == "InstallText" || p == kNotSupported)
            return "Not supported in TI Revive, on purpose: it writes the Virus' flash memory,\n"
                   "and a failure there can make the Virus unusable. Use Access' own software for it.";
        if (p == kStatus)
            return status_;
        for (auto &b : footer_) // first: the NERD MODE button's label shares the "TIRevive/Nerd" prefix
            if (p == b.param + "/Label")
                return b.label;
        if (p.compare(0, 13, "TIRevive/Nerd") == 0)
            return nerd_text(p);
        if (p.compare(0, 12, "TIRevive/Opt") == 0)
            return options_text(p);
        if (p.compare(0, 13, "TIRevive/Bank") == 0) { // 13 = strlen("TIRevive/Bank") // "TIRevive/Bank<0..3>", "TIRevive/BankTitle", "TIRevive/BankNote"
            if (p == "TIRevive/BankTitle")
                return "RAM BANKS";
            if (p == "TIRevive/BankNote")
                return "Only Restore writes to the Virus: programs into a RAM bank, never its flash. It works next to\n"
                       "your DAW. Burn Flash, OS updates and factory reset are not offered, on purpose (they write the\n"
                       "flash). Backups open in the plug-in once their .mid files are in the Patches folder.";
            const int b = p.back() - '0';
            return b >= 0 && b < 4 ? banks_[b] : "";
        }
        if (p == kCancel + std::string("/Label"))
            return "CANCEL";
        return "";
    }
    bool enabled(const SkinNode &n) override
    {
        const std::string &p = n.parameter;
        if (p.find("TriggerRestore") != std::string::npos)
            return !busy_ && found_ && actions_.restore; // RAM banks from a library, never flash (E313)
        if (p.find("TriggerBackup") != std::string::npos || p == kChangePath)
            return !busy_;
        if (p == kModel)
            return !snow_;
        if (p == kNerdAction && n.value == kActClock)
            return nerd_mode_ == kNerdLive; // clock lines are only in the live view
        if (p.find("BurnROMOK") != std::string::npos || p.find("VirusTI/Install") != std::string::npos)
            return false; // Burn Flash / OS update: not offered
        return true;
    }
    bool ready(const SkinNode &n) override { return n.expression.empty() || visible(n.expression); }
    bool animating() override { return page_ != kNerd && page_ != kOptions; } // our pages repaint when they change
    void right_click(CPoint where) override
    {
        if (actions_.size_menu)
            actions_.size_menu(where);
    }
    void wheel(CPoint where, double dy) override
    {
        (void)where;
        if (page_ == kNerd) {
            scroll_.wheel((int)(dy > 0 ? dy * 3 + 0.5 : dy * 3 - 0.5)); // three lines per notch
            nerd_show();
        } else if (page_ == kOptions) {
            opt_scroll_.wheel(dy > 0 ? 1 : -1); // one option per notch
            view_->invalid();
        }
    }
    bool thumb(const SkinNode &n, double &y, double &h) override
    {
        if (n.parameter == kOptScroll) {
            opt_scroll_.thumb(n.h, y, h);
            return true;
        }
        if (n.parameter != kNerdScroll)
            return false;
        scroll_.thumb(n.h, y, h);
        return true;
    }
    void track_click(const SkinNode &n, double at) override
    {
        if (n.parameter == kNerdScroll) {
            scroll_.click_track(n.h, at);
            nerd_show();
        } else if (n.parameter == kOptScroll) {
            opt_scroll_.click_track(n.h, at);
            view_->invalid();
        }
    }
    // the app's View > Nerd mode / Options with the Access look shown: the page in the frame
    void show_nerd()
    {
        if (page_ != kNerd)
            nerd_enter();
    }
    void show_options()
    {
        if (page_ != kOptions)
            options_enter();
    }

    void clicked(const SkinNode &n) override
    {
        const std::string &p = n.parameter;
        if (p == "ControlCenter/Tabs/ActiveTab") {
            std::unique_lock<std::mutex> l(mu_);
            if (found_ || n.value != 0) {
                const bool leaving = page_ == kNerd;
                page_ = n.value == 0 ? kBackup : n.value == 1 ? kBurn : kUpdate;
                const bool backup_page = page_ == kBackup;
                l.unlock();
                if (leaving)
                    nerd_leave();
                if (backup_page)
                    read_banks();
            }
        } else if (p == kFooter) { // our footer buttons: radios, so the open page's stays down
            if (n.value >= 0 && n.value < (int)footer_.size() && footer_[(size_t)n.value].action)
                footer_[(size_t)n.value].action();
        } else if (p == kOptRow) {
            option_toggle(n.value);
        } else if (p == kCancel) {
            cancel_ = true;
            std::lock_guard<std::mutex> l(mu_);
            status_ = "Cancelling...";
        } else if (p == kNerdMode) {
            nerd_set_mode(n.value);
        } else if (p == kNerdAction) {
            nerd_action(n.value);
        } else if (p == "ControlCenter/SelectVirus/SelectVirusOK") {
            {
                std::lock_guard<std::mutex> l(mu_);
                page_ = kBackup;
            }
            read_banks();
        } else if (p == "ControlCenter/VirusTI/TriggerBackup") {
            std::lock_guard<std::mutex> l(mu_); // the default target at once; the path field changes it
            target_ = vti::cc::default_backup_dir();
            done_ = false;
            backup_text_ = target_;
            status_ = "OK starts the backup";
        } else if (p.find("TriggerRestore") != std::string::npos && actions_.restore) {
            actions_.restore([this](const std::string &st) {
                std::lock_guard<std::mutex> l(mu_);
                status_ = st;
            });
        } else if (p == kChangePath) {
            choose_target();
        } else if (p == kModel) {
            std::lock_guard<std::mutex> l(mu_);
            model_ = (model_ + 1) % 3;
            VSTGUI::Standalone::IApplication::instance().getPreferences().set("Model", kModelNames[model_]);
            status_ = std::string("Model: ") + kModelNames[model_] + " (click picture)";
        } else if (p == "ControlCenter/VirusTI/BackupRestoreOK") {
            backup();
        } else {
            for (auto &b : footer_)
                if (p == b.param && b.action)
                    b.action();
        }
        view_->invalid();
    }

private:
    struct FooterButton {
        std::string param, label;
        std::function<void()> action;
    };
    std::unique_ptr<vti::AccessFiles> files_;
    vti::cc::SkinLayout layout_;
    std::string version_;
    SkinPageActions actions_;
    SkinView *view_ = nullptr;
    SharedPointer<CVSTGUITimer> timer_;
    std::vector<FooterButton> footer_;

    std::mutex mu_; // the fields below; the worker writes them
    Page page_ = kStartup;
    bool found_ = false, snow_ = false, done_ = false;
    int model_ = kDesktop;
    std::atomic<bool> busy_{false}, stop_{false};
    std::string product_, usb_, status_ = "Looking for the Virus...", backup_text_, target_;
    std::thread worker_;

    // the Options page and BACKUP & RESTORE's panel
    vti::cc::LogScroll opt_scroll_; // in rows of options
    std::string opt_msg_;
    std::string banks_[4];          // mu_: "RAM A: 128 programs, ..." (the worker fills them)
    std::atomic<bool> backing_up_{false}, cancel_{false};

    // nerd mode (UI thread only; text() reads it under mu_ like the rest)
    Live live_;
    Page nerd_back_ = kSelector; // where NERD MODE pressed again returns to
    int nerd_mode_ = kNerdLive, nerd_ticks_ = 0;
    bool nerd_paused_ = false, nerd_clock_ = false;
    vti::cc::LogScroll scroll_;
    uint64_t nerd_serial_ = 0;
    std::vector<LiveLine> nerd_live_; // the live view: only the rows on screen (from scroll_.first())
    std::vector<LiveLine> nerd_frozen_; // PAUSE: the whole list at that moment (the live one keeps moving)
    int nerd_total_ = 0;
    std::string nerd_shown_;          // what the rows showed last: repaint only on a change
    std::vector<std::string> nerd_log_;
    std::string nerd_status_, nerd_msg_, record_path_;
    std::string cell_[kNerdRows][3]; // live: time, direction, message; logs: the line in [0]

    SkinNode label(int x, int y, int w, const std::string &param)
    {
        SkinNode t;
        t.type = "textlabel";
        t.x = x, t.y = y, t.w = w, t.h = 10;
        t.parameter = param;
        t.bitmap = "FNT_LCD_5x7_White.png";
        return t;
    }

    // our elements in the original's layout: a note on Burn Flash, buttons + status in the empty footer
    void add_our_elements()
    {
        for (auto &n : layout_.nodes)
            for (auto &page : n.children)
                if (page.container && page.expression.find("tab1active") != std::string::npos) {
                    SkinNode path; // an invisible button over the original's file path field
                    path.type = "button";
                    path.x = page.x + 18, path.y = page.y + 128, path.w = 650, path.h = 24;
                    path.parameter = kChangePath;
                    page.children.push_back(path);
                } else if (page.container && page.expression.find("tab5active") != std::string::npos) {
                    add_model_button(page);
                } else if (page.container && page.expression.find("tab2active") != std::string::npos) {
                    auto t = label(page.x + 18, page.y + 185, 623, kNotSupported);
                    t.h = 20;
                    page.children.push_back(t);
                }
        // PLAIN VIEW is gone (2026-11-15): its content is on BACKUP & RESTORE; the plain window is only the fallback
        footer_ = {{"TIRevive/Nerd", "NERD MODE", [this] { page_ == kNerd ? leave_to_back() : nerd_enter(); }},
                   {"TIRevive/Options", "OPTIONS", [this] { page_ == kOptions ? leave_to_back() : options_enter(); }},
                   {"TIRevive/Convert", "CONVERT LIBRARY", actions_.convert_library}};
        for (auto &n : layout_.nodes) {
            if (!n.container || n.background.find("Footer") == std::string::npos)
                continue;
            int x = n.x + 14;
            for (size_t i = 0; i < footer_.size(); i++) {
                auto &b = footer_[i];
                SkinNode btn; // a radio: the button of the open page shows its pressed frame
                btn.type = "radio";
                btn.x = x, btn.y = n.y + 5, btn.w = 129, btn.h = 31;
                btn.parameter = kFooter;
                btn.value = (int)i;
                btn.bitmap = "BTN_Button.png";
                n.children.push_back(btn);
                // the text where the original's buttons have theirs (rows 9-15 of BTN_Backup.png; the LCD font's
                // capitals sit on rows 1-7 of their 9-pixel cell): the cell 8 pixels below the button's top
                n.children.push_back(label(x + (129 - 6 * (int)b.label.size()) / 2, n.y + 5 + 8, 129, b.param + "/Label"));
                x += 143;
            }
            n.children.push_back(label(x + 4, n.y + 5 + 8, n.x + n.w - x - 18, kStatus));
        }
        for (auto &n : layout_.nodes) // nerd mode: into the container that holds the original's pages
            if (n.container && n.background.empty() && n.y == 42 && !n.children.empty() &&
                n.children.front().expression.find("tab1active") != std::string::npos) {
                n.children.push_back(nerd_nodes(n.x, n.y, n.w, n.h));
                n.children.push_back(options_nodes(n.x, n.y, n.w, n.h));
                n.children.push_back(backup_nodes());
                add_cancel(n.children);
                break;
            }
    }

    SkinNode own(const char *type, int x, int y, int w, int h, const std::string &param = "", int value = 0)
    {
        SkinNode e;
        e.type = type;
        e.x = x, e.y = y, e.w = w, e.h = h;
        e.parameter = param;
        e.value = value;
        return e;
    }
    SkinNode button(int x, int y, const std::string &param, int value, const char *type = "button")
    {
        SkinNode b = own(type, x, y, 129, 31, param, value);
        b.bitmap = "BTN_Button.png";
        return b;
    }
    SkinNode centred(int x, int y, const std::string &param) // a button's label, centred on its 129 pixels
    {
        SkinNode t = label(x, y + 8, 129, param); // as the footer's labels: the original's text rows
        t.value = 1;
        return t;
    }

    // the page: title and mode buttons above a separator, the status, the table with a scrollbar, a message line and
    // the action buttons where the original pages have their OK button
    SkinNode nerd_nodes(int x, int y, int w, int h)
    {
        SkinNode page;
        page.container = true;
        page.x = x, page.y = y, page.w = w, page.h = h;
        page.expression = kNerdPage;
        auto &c = page.children;
        c.push_back(own("tr_backdrop", x, y, w, h, "", 52));
        c.push_back(own("tr_title", 21, y + 10, 220, 31, "TIRevive/NerdTitle"));
        const char *modes[] = {"LIVE", "PLUG-IN LOG", "BROKER LOG"};
        for (int i = 0; i < 3; i++) {
            const int bx = 253 + i * 143;
            c.push_back(button(bx, y + 10, kNerdMode, i, "radio"));
            c.push_back(centred(bx, y + 10, std::string("TIRevive/NerdModeLabel") + std::to_string(i)));
            (void)modes;
        }
        SkinNode st = label(21, kTableY - 26, 646, "TIRevive/NerdStatus");
        st.h = 20;
        c.push_back(st);
        c.push_back(own("tr_panel", kTableX, kTableY, kTableW, kHeadH + kRowsH + 4));
        c.push_back(own("tr_rows", kTableX, kRowsY, kTableW - kScrollW - 2, kRowsH, "", kNerdRowH));
        SkinNode live, logs; // the live view's three columns, the logs' one
        live.container = logs.container = true;
        live.x = logs.x = kTableX, live.y = logs.y = kTableY, live.w = logs.w = kTableW, live.h = logs.h = kHeadH + kRowsH;
        live.expression = kNerdLiveCols;
        logs.expression = kNerdLogCols;
        live.children.push_back(own("tr_vline", kColDir - 6, kTableY, 1, kHeadH + kRowsH));
        live.children.push_back(own("tr_vline", kColText - 6, kTableY, 1, kHeadH + kRowsH));
        live.children.push_back(label(kColTime, kTableY + 5, 60, "TIRevive/NerdHeadTime"));
        live.children.push_back(label(kColDir, kTableY + 5, 70, "TIRevive/NerdHeadDir"));
        live.children.push_back(label(kColText, kTableY + 5, 300, "TIRevive/NerdHeadText"));
        logs.children.push_back(label(kColTime, kTableY + 5, 300, "TIRevive/NerdHeadLine"));
        const int text_w = kTableX + kTableW - kScrollW - 4 - kColText;
        for (int r = 0; r < kNerdRows; r++) {
            const int ry = kRowsY + r * kNerdRowH + 3, rr = r;
            live.children.push_back(label(kColTime, ry, kColDir - kColTime - 8, "TIRevive/NerdCell" + std::to_string(rr) + "_0"));
            live.children.push_back(label(kColDir, ry, kColText - kColDir - 8, "TIRevive/NerdCell" + std::to_string(rr) + "_1"));
            live.children.push_back(label(kColText, ry, text_w, "TIRevive/NerdCell" + std::to_string(rr) + "_2"));
            logs.children.push_back(label(kColTime, ry, kTableX + kTableW - kScrollW - 4 - kColTime,
                                          "TIRevive/NerdCell" + std::to_string(rr) + "_0"));
        }
        c.push_back(live);
        c.push_back(logs);
        c.push_back(own("tr_scroll", kTableX + kTableW - kScrollW - 2, kRowsY, kScrollW, kRowsH, kNerdScroll));
        c.push_back(label(21, kRowsY + kRowsH + 10, 646, "TIRevive/NerdMsg"));
        for (int a = 0; a < 4; a++) {
            const int bx = 19 + a * 143;
            c.push_back(button(bx, 380, kNerdAction, a));
            c.push_back(centred(bx, 380, std::string("TIRevive/NerdActLabel") + std::to_string(a)));
        }
        return page;
    }

    // an invisible button over the model picture of the device selector (the picture is an "animation" element)
    static bool add_model_button_in(std::vector<SkinNode> &nodes)
    {
        for (auto &n : nodes) {
            if (n.container && add_model_button_in(n.children))
                return true;
            if (n.type == "animation" && n.parameter.find("SelectVirus/Family") != std::string::npos) {
                SkinNode b;
                b.type = "button";
                b.x = n.x, b.y = n.y, b.w = n.w, b.h = n.h;
                b.parameter = kModel;
                b.expression = n.expression; // only with a Virus in the list
                nodes.push_back(b);
                return true;
            }
        }
        return false;
    }
    void add_model_button(SkinNode &page) { add_model_button_in(page.children); }

    // ---- nerd mode ----
    std::string nerd_text(const std::string &p) // mu_ held (text())
    {
        static const char *modes[] = {"LIVE", "PLUG-IN LOG", "BROKER LOG"};
        if (p == "TIRevive/NerdTitle")
            return "NERD MODE";
        if (p.compare(0, 22, "TIRevive/NerdModeLabel") == 0) {
            const int i = p.back() - '0';
            return i >= 0 && i < 3 ? modes[i] : "";
        }
        if (p.compare(0, 21, "TIRevive/NerdActLabel") == 0) {
            switch (p.back() - '0') {
            case kActPause: return nerd_paused_ ? "RESUME" : "PAUSE";
            case kActClock: return nerd_clock_ ? "CLOCK: SHOWN" : "CLOCK: HIDDEN";
            case kActRecord: return live_.recording() ? "STOP RECORDING" : "RECORD...";
            case kActSave: return "SAVE LOGS...";
            }
            return "";
        }
        if (p == "TIRevive/NerdStatus")
            return nerd_status_;
        if (p == "TIRevive/NerdMsg")
            return nerd_msg_;
        if (p == "TIRevive/NerdHeadTime")
            return "Time";
        if (p == "TIRevive/NerdHeadDir")
            return "Direction";
        if (p == "TIRevive/NerdHeadText")
            return "Message";
        if (p == "TIRevive/NerdHeadLine")
            return nerd_mode_ == kNerdPlugin ? "Plug-in log" : "Broker log";
        if (p.compare(0, 17, "TIRevive/NerdCell") == 0) { // "TIRevive/NerdCell<row>_<column>"
            const size_t u = p.find('_', 17);
            if (u == std::string::npos)
                return "";
            const int r = atoi(p.c_str() + 17), col = atoi(p.c_str() + u + 1);
            return r >= 0 && r < kNerdRows && col >= 0 && col < 3 ? cell_[r][col] : "";
        }
        return "";
    }

    // our pages (nerd mode, Options) open over the original's; the way back is the original page they came from
    void enter_own(Page p)
    {
        Page was;
        {
            std::lock_guard<std::mutex> l(mu_);
            was = page_;
            if (page_ != kNerd && page_ != kOptions)
                nerd_back_ = page_ == kStartup ? kSelector : page_;
            page_ = p;
        }
        if (was == kNerd && p != kNerd)
            nerd_leave();
        view_->invalid();
    }
    void leave_to_back() // the open page's footer button pressed again
    {
        Page was;
        {
            std::lock_guard<std::mutex> l(mu_);
            was = page_;
            page_ = nerd_back_;
        }
        if (was == kNerd)
            nerd_leave();
        if (nerd_back_ == kBackup)
            read_banks();
        view_->invalid();
    }
    void nerd_enter()
    {
        enter_own(kNerd);
        if (nerd_mode_ == kNerdLive)
            live_.start();
        nerd_refresh(true);
        view_->invalid();
    }
    void options_enter()
    {
        enter_own(kOptions);
        opt_scroll_.update((int)vti::cc::plugin_options().size(), kOptRows);
        opt_msg_.clear();
        view_->invalid();
    }
    void nerd_leave() // the page was left by a tab
    {
        if (!live_.recording())
            live_.stop();
        view_->invalid();
    }

    void nerd_set_mode(int m)
    {
        if (m < 0 || m > 2 || m == nerd_mode_)
            return;
        {
            std::lock_guard<std::mutex> l(mu_);
            nerd_mode_ = m;
        }
        scroll_ = vti::cc::LogScroll(); // another list: from its end
        nerd_serial_ = 0;
        nerd_total_ = 0;
        if (m == kNerdLive)
            live_.start();
        else if (!live_.recording())
            live_.stop();
        nerd_freeze();
        nerd_refresh(true);
    }
    void nerd_action(int a)
    {
        switch (a) {
        case kActPause:
            nerd_paused_ = !nerd_paused_;
            nerd_freeze();
            break;
        case kActClock:
            nerd_clock_ = !nerd_clock_;
            nerd_serial_ = 0;
            nerd_freeze(); // paused: the snapshot with or without the clock lines
            break;
        case kActRecord: record(); break;
        case kActSave: save(); break;
        }
        nerd_refresh(true);
    }

    // PAUSE in the live view: one copy of the whole list, shown and scrolled until RESUME. Holding row numbers was not
    // enough: a full list drops its oldest lines and the paused rows moved anyway (user 2026-11-15)
    void nerd_freeze()
    {
        nerd_frozen_.clear();
        if (nerd_paused_ && nerd_mode_ == kNerdLive) {
            std::string status;
            uint64_t serial = 0;
            const int total = live_.count(nerd_clock_, status, serial);
            live_.window(nerd_clock_, 0, total, nerd_frozen_);
            nerd_total_ = (int)nerd_frozen_.size();
            scroll_.update(nerd_total_, kNerdRows, 0);
        }
    }

    // new lines from the live view or the log file; `force` reads a log file at once (else once a second)
    void nerd_refresh(bool force = false)
    {
        int added = 0;
        if (nerd_mode_ == kNerdLive) {
            std::string status;
            uint64_t serial = 0;
            const int total = live_.count(nerd_clock_, status, serial); // the status goes on while paused
            if (!nerd_paused_) {
                added = nerd_serial_ ? (int)(serial - nerd_serial_) : 0;
                if (total < nerd_total_ && total < 3000) // the clock filter changed the list
                    added = 0;
                nerd_serial_ = serial;
                nerd_total_ = total;
            }
            // the plain page's wording is too long for 107 LCD characters: the same fields, shorter
            for (auto [from, to] : {std::pair<const char *, const char *>{"   ", "  "},
                                    {"Packets to / from the Virus: ", "Sent / received: "},
                                    {"Dropped here: ", "Dropped: "}})
                for (size_t at; (at = status.find(from)) != std::string::npos;)
                    status.replace(at, strlen(from), to);
            std::lock_guard<std::mutex> l(mu_);
            nerd_status_ = fit_lines(status, 107);
        } else {
            if (!force && (nerd_paused_ || nerd_ticks_ % 20 != 0))
                return;
            const size_t before = nerd_log_.size();
            nerd_log_ = nerd_mode_ == kNerdBroker ? vti::cc::broker_log_tail(2000)
                                                   : vti::cc::tail_file(vti::cc::plugin_log_path(), 2000);
            added = nerd_log_.size() > before ? (int)(nerd_log_.size() - before) : 0;
            const bool on = vti::cc::plugin_log_enabled();
            std::lock_guard<std::mutex> l(mu_);
            nerd_status_ = vti::cc::fit_text(nerd_mode_ == kNerdBroker
                                                 ? "Broker log: " + vti::cc::anonymize(vti::cc::broker_log_location())
                                                 : "Plug-in log: " + vti::cc::anonymize(vti::cc::plugin_log_path()) +
                                                       (on ? "" : "  (off: switch it on in OPTIONS)"),
                                             107);
        }
        const int total = nerd_mode_ == kNerdLive ? nerd_total_ : (int)nerd_log_.size();
        scroll_.update(total, kNerdRows, added);
        nerd_show();
    }
    // the rows from the scroll position; repaint only when they or the status changed
    void nerd_show()
    {
        if (nerd_mode_ == kNerdLive && nerd_paused_) { // the snapshot
            const int first = scroll_.first();
            nerd_live_.assign(nerd_frozen_.begin() + std::min<size_t>(first, nerd_frozen_.size()),
                              nerd_frozen_.begin() + std::min<size_t>(first + kNerdRows, nerd_frozen_.size()));
        } else if (nerd_mode_ == kNerdLive) {
            live_.window(nerd_clock_, scroll_.first(), kNerdRows, nerd_live_);
        }
        nerd_rows();
        std::string now = nerd_status_ + nerd_msg_;
        for (auto &row : cell_)
            for (auto &c : row)
                now += c;
        if (now != nerd_shown_) {
            nerd_shown_.swap(now);
            view_->invalid();
        }
    }
    static std::string fit_lines(const std::string &text, size_t n) // each line of a multi-line text
    {
        std::string out, line;
        for (size_t i = 0; i <= text.size(); i++)
            if (i == text.size() || text[i] == '\n') {
                out += (out.empty() ? "" : "\n") + vti::cc::fit_text(line, n);
                line.clear();
            } else {
                line += text[i];
            }
        return out;
    }
    // the visible rows from the scroll position
    void nerd_rows()
    {
        std::lock_guard<std::mutex> l(mu_);
        const int first = scroll_.first();
        const int dir_n = (kColText - kColDir - 8) / 6, text_n = (kTableX + kTableW - kScrollW - 4 - kColText) / 6,
                  line_n = (kTableX + kTableW - kScrollW - 4 - kColTime) / 6;
        for (int r = 0; r < kNerdRows; r++) {
            const int i = first + r;
            for (auto &c : cell_[r])
                c.clear();
            if (nerd_mode_ == kNerdLive) {
                if (r < (int)nerd_live_.size()) { // the window starts at `first`
                    const LiveLine &x = nerd_live_[(size_t)r];
                    cell_[r][0] = vti::cc::fit_text(x.time, 12);
                    cell_[r][1] = vti::cc::fit_text(x.dir, (size_t)dir_n);
                    cell_[r][2] = vti::cc::fit_text(x.ep.empty() ? x.text : x.ep + "  " + x.text, (size_t)text_n);
                }
            } else if (i < (int)nerd_log_.size()) {
                cell_[r][0] = vti::cc::fit_text(nerd_log_[(size_t)i], (size_t)line_n);
            }
        }
        if (nerd_mode_ == kNerdLive && nerd_total_ == 0)
            cell_[0][2] = "(nothing yet: start the plug-in or use the Control Center to see the traffic)";
        else if (nerd_mode_ != kNerdLive && nerd_log_.empty())
            cell_[0][0] = nerd_mode_ == kNerdBroker ? "(no broker log yet: it is written while a program uses the Virus)"
                                                    : "(no plug-in log yet)";
    }

    // everything to and from the Virus in a file, as a USB sniffer would show it (cc/capture.h)
    void record()
    {
        if (live_.recording()) {
            live_.stop_record();
            if (nerd_mode_ != kNerdLive || page_ != kNerd)
                live_.stop();
            nerd_msg_ = vti::cc::fit_text("Recording saved: " + record_path_, 107);
            return;
        }
        auto sel = owned(CNewFileSelector::create(getFrame(), CNewFileSelector::kSelectSaveFile));
        if (!sel)
            return;
        char stamp[32];
        time_t now = time(nullptr);
        strftime(stamp, sizeof stamp, "%Y-%m-%d-%H%M%S", localtime(&now));
        sel->setTitle("Record all traffic to and from the Virus");
        sel->setDefaultSaveName((std::string("TI-Revive-capture-") + stamp + ".txt").c_str());
        sel->setDefaultExtension(CFileExtension("Text", "txt"));
        const bool shown = sel->run([this](CNewFileSelector *s) {
            if (!s->getNumSelectedFiles())
                return;
            record_path_ = s->getSelectedFile(0);
            nerd_msg_ = vti::cc::fit_text(live_.start_record(record_path_, version_)
                                              ? "Recording to " + record_path_ + " (read-only)"
                                              : "Cannot write " + record_path_,
                                          107);
            nerd_refresh(true);
        });
        if (!shown)
            nerd_msg_ = "No file dialog could be opened (Linux: install zenity or kdialog).";
    }
    void save()
    {
        auto sel = owned(CNewFileSelector::create(getFrame(), CNewFileSelector::kSelectSaveFile));
        if (!sel)
            return;
        char stamp[32];
        time_t now = time(nullptr);
        strftime(stamp, sizeof stamp, "%Y-%m-%d-%H%M", localtime(&now));
        sel->setTitle("Save logs for a bug report");
        sel->setDefaultSaveName((std::string("TI-Revive-logs-") + stamp + ".txt").c_str());
        sel->setDefaultExtension(CFileExtension("Text", "txt"));
        const bool shown = sel->run([this](CNewFileSelector *s) {
            if (!s->getNumSelectedFiles())
                return;
            std::string out = s->getSelectedFile(0), err;
            nerd_msg_ = vti::cc::fit_text(vti::cc::collect_logs(out, version_, err)
                                              ? "Saved (home folder and user name replaced; look through it first): " + out
                                              : "Not saved: " + err,
                                          107);
            nerd_refresh(true);
        });
        if (!shown)
            nerd_msg_ = "No file dialog could be opened (Linux: install zenity or kdialog).";
    }

    // ---- the Options page ----
    std::string options_text(const std::string &p) // mu_ held (text())
    {
        const auto &opts = vti::cc::plugin_options();
        if (p == "TIRevive/OptTitle")
            return "OPTIONS";
        if (p == "TIRevive/OptIntro")
            return "The plug-in reads these when it starts: a change applies from its next start (reload it or restart\n"
                   "the DAW). The update check is the Control Center's own. Nothing here is sent to the Virus.";
        if (p == "TIRevive/OptMsg")
            return opt_msg_;
        for (const char *what : {"TIRevive/OptName", "TIRevive/OptHint"})
            if (p.compare(0, strlen(what), what) == 0) {
                const int i = opt_scroll_.first() + atoi(p.c_str() + strlen(what));
                if (i < 0 || i >= (int)opts.size())
                    return "";
                const auto &o = opts[(size_t)i];
                if (what[12] == 'N') // "TIRevive/OptName" against "TIRevive/OptHint"
                    return vti::cc::fit_text(o.title, 98);
                std::string out;
                for (auto &l : vti::cc::wrap_text(o.hint, 98, 2))
                    out += (out.empty() ? "" : "\n") + l;
                return out;
            }
        return "";
    }
    void option_toggle(int row)
    {
        const auto &opts = vti::cc::plugin_options();
        const int i = opt_scroll_.first() + row;
        if (i < 0 || i >= (int)opts.size())
            return;
        const auto &o = opts[(size_t)i];
        const bool on = !vti::cc::plugin_option(o.name);
        std::string err, msg;
        if (!vti::cc::set_plugin_option(o.name, on, err)) {
            msg = "Not changed: " + err;
        } else if (std::string(o.name) == "update-check") { // the Control Center's own job, at once
            msg = on ? "Check for updates: on - checking now, then once a day when the Control Center opens."
                     : "Check for updates: off - nothing is asked any more.";
            if (on)
                vti::cc::start_update_check_if_due(true);
        } else {
            msg = std::string(o.title) + (on ? ": on" : ": off") + " - from the next start of the plug-in.";
        }
        std::lock_guard<std::mutex> l(mu_);
        opt_msg_ = vti::cc::fit_text(msg, 107);
    }
    SkinNode options_nodes(int x, int y, int w, int h)
    {
        SkinNode page;
        page.container = true;
        page.x = x, page.y = y, page.w = w, page.h = h;
        page.expression = kOptionsPage;
        auto &c = page.children;
        c.push_back(own("tr_backdrop", x, y, w, h, "", 52));
        c.push_back(own("tr_title", 21, y + 10, 220, 31, "TIRevive/OptTitle"));
        SkinNode intro = label(21, kOptY - 24, 646, "TIRevive/OptIntro");
        intro.h = 20;
        c.push_back(intro);
        c.push_back(own("tr_panel", kOptX, kOptY, kOptW, kOptRows * kOptRowH + 4));
        c.push_back(own("tr_rows", kOptX, kOptY, kOptW - kScrollW - 2, kOptRows * kOptRowH, "", kOptRowH));
        for (int r = 0; r < kOptRows; r++) {
            const int ry = kOptY + r * kOptRowH;
            const std::string slot = "TIRevive/OptSlot" + std::to_string(r);
            SkinNode box = own("tr_check", kOptX + 10, ry + 12, 16, 16, "TIRevive/OptCheck" + std::to_string(r));
            box.expression = slot;
            c.push_back(box);
            c.push_back(label(kOptX + 36, ry + 6, 590, "TIRevive/OptName" + std::to_string(r)));
            SkinNode hint = label(kOptX + 36, ry + 19, 590, "TIRevive/OptHint" + std::to_string(r));
            hint.h = 20;
            c.push_back(hint);
            SkinNode hit = own("button", kOptX, ry, kOptW - kScrollW - 2, kOptRowH, kOptRow, r); // the whole row
            hit.expression = slot;
            c.push_back(hit);
        }
        c.push_back(own("tr_scroll", kOptX + kOptW - kScrollW - 2, kOptY, kScrollW, kOptRows * kOptRowH, kOptScroll));
        c.push_back(label(21, kOptY + kOptRows * kOptRowH + 12, 646, "TIRevive/OptMsg"));
        return page;
    }

    // ---- BACKUP & RESTORE: the RAM banks and the note (the plain view's content) ----
    SkinNode backup_nodes()
    {
        SkinNode p;
        p.container = true;
        p.x = 19, p.y = 218, p.w = 648, p.h = 152;
        p.expression = kBackupPanel;
        SkinNode panel = own("tr_panel", p.x, p.y, p.w, p.h);
        panel.back = 0x49535db4; // the panel colour (73,83,93) at ~70 %: the photo shows through, as in the file path field
        p.children.push_back(panel);
        p.children.push_back(label(p.x + 8, p.y + 8, 300, "TIRevive/BankTitle"));
        for (int b = 0; b < 4; b++)
            p.children.push_back(label(p.x + 8, p.y + 26 + b * 14, 632, "TIRevive/Bank" + std::to_string(b)));
        SkinNode note = label(p.x + 8, p.y + 92, 632, "TIRevive/BankNote");
        note.h = 30;
        p.children.push_back(note);
        return p;
    }
    void add_cancel(std::vector<SkinNode> &into) // where the original's pages have their OK button, left of it
    {
        SkinNode b = button(398, 380, kCancel, 0);
        b.expression = kBusy;
        into.push_back(b);
        SkinNode t = centred(398, 380, kCancel + std::string("/Label"));
        t.expression = kBusy;
        into.push_back(t);
    }
    // the four RAM banks: how many programs the plug-in's cache has current (the plain view's lines); a read-only
    // request per bank, the same as the plug-in's bank sync
    void read_banks()
    {
        if (!found_ || busy_)
            return;
        {
            std::lock_guard<std::mutex> l(mu_);
            for (auto &b : banks_)
                b = "...";
        }
        run([this] {
            vti::Broker broker;
            broker.set_name("TI Revive Control Center");
            std::string err;
            if (!broker.open(err) || !broker.device_present()) {
                std::lock_guard<std::mutex> l(mu_);
                for (auto &b : banks_)
                    b.clear();
                banks_[0] = "No Virus found";
                return;
            }
            vti::MemorySync sync(broker);
            const std::string cache = vti::cc::cache_dir();
            for (int bank = 1; bank <= 4 && !stop_; bank++) {
                const std::string name = std::string("RAM ") + (char)('A' + bank - 1);
                const auto fp = sync.bank_info(bank);
                std::string line;
                if (fp.size() != 128) {
                    line = name + ":  no answer";
                } else {
                    vti::Bank b;
                    int current = 0;
                    if (vti::load_bank_file(cache + "/" + name, b))
                        for (int p = 0; p < 128; p++)
                            current += vti::RemoteDevice::single_fingerprint(b[p]) == fp[p];
                    line = name + ":  128 programs, " + std::to_string(current) + " current in the cache";
                    if (current)
                        line += "  (1: " + vti::cc::patch_name(b[0]) + ")";
                }
                std::lock_guard<std::mutex> l(mu_);
                banks_[bank - 1] = vti::cc::fit_text(line, 105);
            }
        });
    }

    template <class F> void run(F &&f)
    {
        if (busy_.exchange(true))
            return;
        if (worker_.joinable())
            worker_.join();
        worker_ = std::thread([this, f = std::forward<F>(f)] {
            f();
            busy_ = false;
        });
    }

    void refresh()
    {
        run([this] {
            vti::Broker broker;
            broker.set_name("TI Revive Control Center");
            std::string err;
            vti::cc::DeviceInfo di;
            bool ok = broker.open(err) && broker.device_present() && vti::cc::read_device_info(broker, di, err);
            std::lock_guard<std::mutex> l(mu_);
            found_ = ok;
            Page &to = page_ == kNerd ? nerd_back_ : page_; // nerd mode open: it stays, its way back changes
            if (!ok) {
                status_ = "No Virus found"; // 36 characters fit in the footer
                to = kSelector;
                return;
            }
            std::string up = di.product_name;
            for (auto &c : up)
                c = (char)toupper((unsigned char)c);
            snow_ = up.find("SNOW") != std::string::npos;
            product_ = up;
            char u[64];
            snprintf(u, sizeof u, "USB %04X:%04X %X.%02X", di.vendor, di.product, di.bcd >> 8, di.bcd & 0xFF);
            usb_ = u;
            status_ = "Virus found";
            to = kSelector;
        });
    }

    void choose_target()
    {
        // each backup in its own dated folder ("2026-10-15 1230") inside the chosen one, which starts at
        // <Documents>/Access Music/Virus TI/Backups
        std::string def = vti::cc::default_backup_dir();
        std::filesystem::path base = vti::u8path(def).parent_path();
        std::string stamp = vti::u8str(vti::u8path(def).filename());
        std::error_code ec;
        std::filesystem::create_directories(base, ec);
        auto sel = owned(CNewFileSelector::create(getFrame(), CNewFileSelector::kSelectDirectory));
        if (sel) {
            sel->setTitle("Backup folder (a dated folder is created inside)");
            sel->setInitialDirectory(vti::u8str(base).c_str());
        }
        bool shown = sel && sel->run([this, stamp](CNewFileSelector *s) {
            if (!s->getNumSelectedFiles())
                return;
            std::lock_guard<std::mutex> l(mu_);
            target_ = vti::u8str(vti::u8path(s->getSelectedFile(0)) / vti::u8path(stamp));
            done_ = false;
            backup_text_ = target_;
            status_ = "OK starts the backup";
        });
        if (!shown) { // no file dialog (Linux without zenity/kdialog): the default folder
            std::lock_guard<std::mutex> l(mu_);
            target_ = def;
            done_ = false;
            backup_text_ = target_;
            status_ = "OK starts the backup";
        }
    }

    void backup()
    {
        std::string out;
        {
            std::lock_guard<std::mutex> l(mu_);
            out = target_;
        }
        if (out.empty())
            return;
        cancel_ = false;
        backing_up_ = true; // CANCEL shows while it runs
        run([this, out] {
            struct Done {
                std::atomic<bool> &f;
                ~Done() { f = false; }
            } done{backing_up_};
            vti::Broker broker;
            broker.set_name("TI Revive Control Center");
            std::string err;
            if (!broker.open(err) || !broker.device_present()) {
                std::lock_guard<std::mutex> l(mu_);
                status_ = "No Virus found";
                return;
            }
            int fetched = 0;
            bool ok = vti::cc::backup_ram(broker, "1234", out, [&](int bank, int p, bool f, const vti::Dump &) {
                fetched += f;
                char s[64];
                snprintf(s, sizeof s, "Backing up RAM %c: %d / 128", 'A' + bank - 1, p + 1);
                std::lock_guard<std::mutex> l(mu_);
                status_ = s;
            }, err, &cancel_);
            std::lock_guard<std::mutex> l(mu_);
            done_ = ok;
            status_ = ok ? "Done: " + std::to_string(fetched) + " read, RAM A..D saved"
                         : cancel_ ? "Backup cancelled" : "Not finished: " + err;
        });
    }
};

} // namespace

CViewContainer *make_skin_page(std::unique_ptr<vti::AccessFiles> files, const std::string &version,
                               SkinPageActions actions, std::string &error)
{
    if (!files)
        return nullptr;
    vti::cc::SkinLayout layout;
    if (!vti::cc::parse_skin(files->get("editor.xml"), layout, error))
        return nullptr;
    auto *p = new SkinPage(std::move(files), std::move(layout), version, std::move(actions));
    p->setVisible(false);
    return p;
}

CPoint skin_page_size(CViewContainer *page) { return {page->getWidth(), page->getHeight()}; }

void set_skin_page_active(CViewContainer *page, bool on)
{
    if (auto *p = dynamic_cast<SkinPage *>(page))
        p->activate(on);
}

void show_skin_nerd(CViewContainer *page)
{
    if (auto *p = dynamic_cast<SkinPage *>(page))
        p->show_nerd();
}

void show_skin_options(CViewContainer *page)
{
    if (auto *p = dynamic_cast<SkinPage *>(page))
        p->show_options();
}

} // namespace tirevive
