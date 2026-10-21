// skinpage.cpp - see skinpage.h.
#include "skinpage.h"
#include "access/accessfiles.h"
#include "cc/ccskin.h"
#include "cc/controlcenter.h"
#include "model/remotedevice.h"
#include "skinview.h"
#include "vstgui/lib/cfileselector.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cvstguitimer.h"
#include "vstgui/standalone/include/iapplication.h"
#include "vstgui/standalone/include/ipreference.h"
#include <atomic>
#include <filesystem>
#include <cctype>
#include <mutex>
#include <thread>

using namespace VSTGUI;
using vti::cc::SkinNode;

namespace tirevive {
namespace {

// pages, as the original's expressions name them
enum Page { kStartup, kSelector, kBackup, kBurn, kUpdate };

// our parameters for the elements we add to the layout
const char *kNotSupported = "TIRevive/NotSupported", *kStatus = "TIRevive/Status",
           *kChangePath = "TIRevive/ChangePath", *kModel = "TIRevive/Model";

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
        if (worker_.joinable())
            worker_.join();
    }

    void activate(bool on)
    {
        setVisible(on);
        if (!on) {
            timer_ = nullptr;
            return;
        }
        timer_ = makeOwned<CVSTGUITimer>([this](CVSTGUITimer *) { view_->tick(); }, 50, true);
        if (page_ == kStartup)
            refresh();
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
        for (auto &b : footer_)
            if (p == b.param + "/Label")
                return b.label;
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
        if (p.find("BurnROMOK") != std::string::npos || p.find("VirusTI/Install") != std::string::npos)
            return false; // Burn Flash / OS update: not offered
        return true;
    }
    bool ready(const SkinNode &n) override { return n.expression.empty() || visible(n.expression); }
    void right_click(CPoint where) override
    {
        if (actions_.size_menu)
            actions_.size_menu(where);
    }

    void clicked(const SkinNode &n) override
    {
        const std::string &p = n.parameter;
        if (p == "ControlCenter/Tabs/ActiveTab") {
            std::lock_guard<std::mutex> l(mu_);
            if (found_ || n.value != 0)
                page_ = n.value == 0 ? kBackup : n.value == 1 ? kBurn : kUpdate;
        } else if (p == "ControlCenter/SelectVirus/SelectVirusOK") {
            std::lock_guard<std::mutex> l(mu_);
            page_ = kBackup;
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
        footer_ = {{"TIRevive/Nerd", "NERD MODE", actions_.nerd_mode},
                   {"TIRevive/Convert", "CONVERT LIBRARY", actions_.convert_library},
                   {"TIRevive/Plain", "PLAIN VIEW", actions_.plain_view}};
        for (auto &n : layout_.nodes) {
            if (!n.container || n.background.find("Footer") == std::string::npos)
                continue;
            int x = n.x + 14;
            for (auto &b : footer_) {
                SkinNode btn;
                btn.type = "button";
                btn.x = x, btn.y = n.y + 5, btn.w = 129, btn.h = 31;
                btn.parameter = b.param;
                btn.bitmap = "BTN_Button.png";
                n.children.push_back(btn);
                n.children.push_back(label(x + (129 - 6 * (int)b.label.size()) / 2, n.y + 16, 129, b.param + "/Label"));
                x += 143;
            }
            n.children.push_back(label(x + 4, n.y + 16, n.x + n.w - x - 18, kStatus));
        }
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
            if (!ok) {
                status_ = "No Virus found"; // 36 characters fit in the footer
                page_ = kSelector;
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
            page_ = kSelector;
        });
    }

    void choose_target()
    {
        // each backup in its own dated folder ("2026-10-15 1230") inside the chosen one, which starts at
        // <Documents>/Access Music/Virus TI/Backups
        std::string def = vti::cc::default_backup_dir();
        std::filesystem::path base = std::filesystem::path(def).parent_path();
        std::string stamp = std::filesystem::path(def).filename().string();
        std::error_code ec;
        std::filesystem::create_directories(base, ec);
        auto sel = owned(CNewFileSelector::create(getFrame(), CNewFileSelector::kSelectDirectory));
        if (sel) {
            sel->setTitle("Backup folder (a dated folder is created inside)");
            sel->setInitialDirectory(base.string().c_str());
        }
        bool shown = sel && sel->run([this, stamp](CNewFileSelector *s) {
            if (!s->getNumSelectedFiles())
                return;
            std::lock_guard<std::mutex> l(mu_);
            target_ = (std::filesystem::path(s->getSelectedFile(0)) / stamp).string();
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
        run([this, out] {
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
            }, err, &stop_);
            std::lock_guard<std::mutex> l(mu_);
            done_ = ok;
            status_ = ok ? "Done: " + std::to_string(fetched) + " read, RAM A..D saved" : "Not finished: " + err;
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

} // namespace tirevive
