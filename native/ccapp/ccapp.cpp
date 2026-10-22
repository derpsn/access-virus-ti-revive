// ccapp.cpp - "TI Revive Control Center": the window around src/cc/controlcenter.h (phase 1, read-only), built with
// VSTGUI's standalone library like the plug-in's editor. One window: the connected Virus and its RAM banks, backup
// of the RAM banks as patch libraries, export of a library (.mid <-> .syx).
//
// The work (broker requests, ~1 s per program) runs on a worker thread; a timer copies its state into the labels.
// The broker serves one program at a time, so the app connects only while an action runs and never keeps the Virus
// from the plug-in. Firmware/OS update, Burn To Flash, factory reset and renaming are not offered, on purpose: they
// write the Virus' flash (AGENTS.md rule 2).
#include "access/accessfiles.h"
#include "access/library.h"
#include "cc/controlcenter.h"
#include "nerdmode.h"
#include "skinpage.h"
#include "model/remotedevice.h"
#include "vstgui/lib/cfileselector.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/controls/cbuttons.h"
#include "vstgui/standalone/include/ipreference.h"
#include "vstgui/lib/controls/coptionmenu.h"
#include "vstgui/lib/controls/icontrollistener.h"
#include "vstgui/lib/controls/ctextlabel.h"
#include "vstgui/lib/cvstguitimer.h"
#include "vstgui/standalone/include/helpers/appdelegate.h"
#include "vstgui/standalone/include/helpers/windowcontroller.h"
#include "vstgui/standalone/include/helpers/windowlistener.h"
#include "vstgui/standalone/include/iapplication.h"
#include "vstgui/standalone/include/icommand.h"
#include "vstgui/standalone/include/iwindow.h"
#include <atomic>
#include <cmath>
#include <filesystem>
#include <mutex>
#include <thread>

using namespace VSTGUI;
using namespace VSTGUI::Standalone;
namespace fs = std::filesystem;

#ifndef TIREVIVE_VERSION
#define TIREVIVE_VERSION "0.0.0"
#endif

namespace {

const CColor kBack(43, 43, 43), kText(230, 230, 230), kDim(160, 160, 160), kAccent(240, 170, 60);
constexpr CCoord W = 640, H = 470, M = 20;
enum Tag { kRefresh = 1, kBackup, kCancel, kFolder, kExport, kNerd, kSkin, kRestore };
enum UiPage { kPlainPage, kSkinPage, kNerdPage };
const Command kNerdCommand{"View", "Nerd mode"};
// the window's size (the original layout is small on today's screens); kept in the app's preferences
const double kSizes[] = {1.0, 1.25, 1.5, 1.75, 2.0};
const Command kSizeCommands[] = {{"View", "Size 100 %"}, {"View", "Size 125 %"}, {"View", "Size 150 %"},
                                 {"View", "Size 175 %"}, {"View", "Size 200 %"}};

// What the worker reports; the UI thread reads it in the timer.
struct Shared {
    std::mutex mu;
    std::string device = "Press Refresh to look for the Virus.", status;
    std::string banks[4], folder;
    bool dirty = true;
};

// The window's close button: canClose runs right away, while GTK ends the main loop with the last window before the
// asynchronous onClosed (and never calls onQuit) - the app was destroyed at exit with its timer still running.
struct CloseHook : WindowControllerAdapter {
    std::function<void()> closing;
    bool canClose(const IWindow &) override
    {
        if (closing)
            closing();
        return true;
    }
};

class App : public Application::DelegateAdapter,
            public WindowListenerAdapter,
            public IControlListener,
            public ICommandHandler {
public:
    App() : DelegateAdapter({"TI Revive Control Center", TIREVIVE_VERSION, "io.github.derpsn.tirevive.controlcenter"}) {}
    ~App() override { join(); }

    void finishLaunching() override
    {
        WindowConfiguration config;
        config.title = "TI Revive Control Center";
        config.autoSaveFrameName = "ControlCenterWindow";
        config.style.border().close().centered();
        config.size = {W, H};
        IApplication::instance().registerCommand(kNerdCommand, 0);
        for (auto &c : kSizeCommands)
            IApplication::instance().registerCommand(c, 0);
        if (auto z = IApplication::instance().getPreferences().get("Zoom"))
            zoom_ = std::clamp(atof(z->data()), 1.0, 2.0);
        auto ctl = std::make_shared<CloseHook>();
        ctl->closing = [this] { release_ui(); };
        window_ = IApplication::instance().createWindow(config, ctl);
        // (a controller, not nullptr: VSTGUI builds the window menu through it without a null check)
        if (!window_)
            return IApplication::instance().quit();
        frame_ = makeOwned<CFrame>(CRect(0, 0, W, H), nullptr);
        frame_->setBackgroundColor(kBack);
        main_ = new CViewContainer(CRect(0, 0, W, H));
        main_->setBackgroundColor(kBack);
        frame_->addView(main_);
        build();
        nerd_ = tirevive::make_nerd_page(TIREVIVE_VERSION, [this] { show_page(look_); });
        frame_->addView(nerd_);
        // the look of Access' Virus Control Center, when the user's installer has it (else the plain page)
        std::string err;
        skin_ = tirevive::make_skin_page(vti::AccessFiles::open_control_center(err), TIREVIVE_VERSION,
                                         {[this] { show_page(kNerdPage); }, [this] { export_library(); },
                                          [this] { show_page(kPlainPage); }, [this](CPoint p) { size_menu(p); },
                                          [this](std::function<void(const std::string &)> st) {
                                              choose_restore(std::move(st));
                                          }},
                                         err);
        if (skin_)
            frame_->addView(skin_);
        else
            buttons_[6]->setVisible(false);
        window_->setContentView(frame_);
        window_->registerWindowListener(this);
        set(&Shared::folder, vti::cc::default_backup_dir());
        show_page(skin_ ? kSkinPage : kPlainPage); // also sets the window size (GTK does not apply config.size)
        window_->show();
        timer_ = makeOwned<CVSTGUITimer>([this](CVSTGUITimer *) { tick(); }, 100, true);
    }
    // "View > Nerd mode" in the menu (macOS menu bar, the window's menu on Windows)
    bool canHandleCommand(const Command &c) override
    {
        for (auto &s : kSizeCommands)
            if (c == s)
                return true;
        return c == kNerdCommand;
    }
    bool handleCommand(const Command &c) override
    {
        for (int i = 0; i < 5; i++)
            if (c == kSizeCommands[i]) {
                set_zoom(kSizes[i]);
                return true;
            }
        if (c != kNerdCommand)
            return false;
        show_page(kNerdPage);
        return true;
    }

    void onClosed(const IWindow &w) override
    {
        if (&w != window_.get()) // another window (Nerd mode)
            return;
        release_ui();
        IApplication::instance().quit();
    }

    void valueChanged(CControl *c) override
    {
        if (c->getValue() < 0.5f) // buttons: act on press only
            return;
        switch (c->getTag()) {
        case kRefresh: refresh(); break;
        case kBackup: backup(); break;
        case kCancel: stop_ = true; break;
        case kFolder: choose_folder(); break;
        case kExport: export_library(); break;
        case kRestore: choose_restore(); break;
        case kNerd: show_page(kNerdPage); break;
        case kSkin: show_page(kSkinPage); break;
        }
    }

    void onQuit() override { release_ui(); } // Windows, macOS (menu Quit)

private:
    // VSTGUI's platform layer is gone at exit before this app object: a timer stopped then crashed (user, Linux,
    // round 0.8.20: segmentation fault on closing the Control Center)
    void release_ui()
    {
        stop_ = true;
        timer_ = nullptr;
    }

    WindowPtr window_;
    SharedPointer<CFrame> frame_;
    CViewContainer *main_ = nullptr, *nerd_ = nullptr, *skin_ = nullptr; // the pages, owned by the frame
    UiPage look_ = kPlainPage; // plain or Access look: where Nerd mode's "Back" returns to
    UiPage page_ = kPlainPage;
    double zoom_ = 1.0;
    CPoint win_size_; // the current page's window size (zoomed)
    bool sized_ = false;

    void set_zoom(double z)
    {
        zoom_ = z;
        char t[16];
        snprintf(t, sizeof t, "%g", z);
        IApplication::instance().getPreferences().set("Zoom", t);
        show_page(page_);
    }
    void size_menu(CPoint where) // right-click in the Access look (the plain pages: View menu)
    {
        auto *menu = new COptionMenu();
        for (int i = 0; i < 5; i++) {
            auto *item = menu->addEntry(kSizeCommands[i].name);
            if (std::fabs(zoom_ - kSizes[i]) < 0.01)
                item->setChecked(true);
        }
        menu->popup(frame_, where, [this](COptionMenu *m) {
            int idx = m->getLastResult();
            if (idx >= 0 && idx < 5)
                set_zoom(kSizes[idx]);
        });
        menu->forget();
    }
    bool plain_refreshed_ = false;
    SharedPointer<CVSTGUITimer> timer_;
    CTextLabel *device_ = nullptr, *banks_[4] = {}, *status_ = nullptr, *folder_ = nullptr;
    CTextButton *buttons_[8] = {};
    Shared sh_;
    std::thread worker_;
    std::atomic<bool> busy_{false}, stop_{false};

    CTextLabel *label(CCoord y, CCoord h, const char *text, const CColor &color, CFontRef font = kNormalFont,
                      CCoord x = M, CCoord w = W - 2 * M)
    {
        auto *l = new CTextLabel(CRect(x, y, x + w, y + h), text);
        l->setFontColor(color);
        l->setFont(font);
        l->setHoriAlign(kLeftText);
        l->setTransparency(true);
        l->setTextTruncateMode(CTextLabel::kTruncateHead);
        main_->addView(l);
        return l;
    }
    CTextButton *button(CCoord x, CCoord y, CCoord w, int tag, const char *title)
    {
        auto *b = new CTextButton(CRect(x, y, x + w, y + 28), this, tag, title);
        main_->addView(b);
        return b;
    }

    void build()
    {
        auto title = makeOwned<CFontDesc>(*kSystemFont);
        title->setSize(18);
        title->setStyle(kBoldFace);
        label(14, 28, "TI Revive Control Center", kText, title);
        device_ = label(48, 22, "", kAccent);
        label(80, 20, "RAM banks", kDim, kNormalFontSmall);
        for (int i = 0; i < 4; i++)
            banks_[i] = label(100 + i * 22, 22, "", kText);
        label(196, 20, "Backup folder", kDim, kNormalFontSmall);
        folder_ = label(216, 22, "", kText, kNormalFont, M, W - 2 * M - 110);
        buttons_[3] = button(W - M - 100, 213, 100, kFolder, "Change...");
        buttons_[0] = button(M, 256, 100, kRefresh, "Refresh");
        buttons_[1] = button(M + 110, 256, 160, kBackup, "Back up RAM banks");
        buttons_[7] = button(M + 280, 256, 110, kRestore, "Restore...");
        buttons_[2] = button(M + 400, 256, 70, kCancel, "Cancel");
        buttons_[4] = button(W - M - 130, 256, 130, kExport, "Convert library...");
        status_ = new CMultiLineTextLabel(CRect(M, 298, W - M, 360));
        status_->setFontColor(kText);
        status_->setFont(kNormalFont);
        status_->setHoriAlign(kLeftText);
        status_->setTransparency(true);
        static_cast<CMultiLineTextLabel *>(status_)->setLineLayout(CMultiLineTextLabel::LineLayout::wrap);
        main_->addView(status_);
        buttons_[5] = button(W - M - 110, 14, 110, kNerd, "Nerd mode");
        buttons_[6] = button(W - M - 230, 14, 110, kSkin, "Access look");
        auto *note = new CMultiLineTextLabel(CRect(M, 372, W - M, H - 10));
        note->setText("Only Restore writes to the Virus: programs into a RAM bank, never its flash. It works next to "
                      "your DAW (the plug-in and the Control Center share the Virus). Firmware/OS updates, Burn To "
                      "Flash and factory reset are not supported, on purpose (they write the Virus' flash). Backups "
                      "open in the plug-in once their .mid files are copied into the Patches folder.");
        note->setFontColor(kDim);
        note->setFont(kNormalFontSmall);
        note->setHoriAlign(kLeftText);
        note->setTransparency(true);
        note->setLineLayout(CMultiLineTextLabel::LineLayout::wrap);
        main_->addView(note);
    }

    // The pages share this window (a second window stayed blank on Linux/GTK); the window takes each one's size.
    // Only one page talks to the broker at a time: it serves one program (and one connection) at a time.
    void show_page(UiPage p)
    {
        if (p != kNerdPage)
            look_ = p;
        CPoint size = p == kNerdPage ? CPoint(tirevive::kNerdWidth, tirevive::kNerdHeight)
                      : p == kSkinPage ? tirevive::skin_page_size(skin_)
                                       : CPoint(W, H);
        main_->setVisible(p == kPlainPage);
        tirevive::set_nerd_page_active(nerd_, p == kNerdPage);
        if (skin_)
            tirevive::set_skin_page_active(skin_, p == kSkinPage);
        // the page's own size, then the zoom (VSTGUI scales drawing and mouse events)
        frame_->setZoom(1.0);
        frame_->setSize(size.x, size.y);
        frame_->setZoom(zoom_);
        win_size_ = {size.x * zoom_, size.y * zoom_};
        window_->setSize(win_size_);
        page_ = p;
        window_->setTitle(p == kNerdPage ? "TI Revive Control Center - Nerd mode" : "TI Revive Control Center");
        frame_->invalid();
        if (p == kPlainPage && !plain_refreshed_) {
            plain_refreshed_ = true;
            refresh();
        }
    }

    void tick()
    {
        if (!sized_) { // the window only takes a size once it is on screen (macOS, Windows and GTK ignored the one set
            sized_ = true; // before show(): the Access look started cut off or with an empty margin, user round 0.8.20)
            window_->setSize(win_size_);
            printf("TI Revive Control Center: window %dx%d\n", (int)win_size_.x, (int)win_size_.y); // gui_test.sh
            fflush(stdout);
        }
        bool busy = busy_;
        for (int i : {0, 1, 2, 3, 4, 7}) { // while an action runs only Cancel; otherwise everything but Cancel
            bool on = (i == 2) == busy;
            buttons_[i]->setMouseEnabled(on);
            buttons_[i]->setAlphaValue(on ? 1.f : 0.4f);
        }
        std::lock_guard<std::mutex> lk(sh_.mu);
        if (!sh_.dirty)
            return;
        sh_.dirty = false;
        device_->setText(sh_.device.c_str());
        folder_->setText(sh_.folder.c_str());
        for (int i = 0; i < 4; i++)
            banks_[i]->setText(sh_.banks[i].c_str());
        status_->setText(sh_.status.c_str());
    }
    void set(std::string Shared::*field, const std::string &v)
    {
        std::lock_guard<std::mutex> lk(sh_.mu);
        sh_.*field = v;
        sh_.dirty = true;
    }
    void set_bank(int i, const std::string &v)
    {
        std::lock_guard<std::mutex> lk(sh_.mu);
        sh_.banks[i] = v;
        sh_.dirty = true;
    }

    void join()
    {
        if (worker_.joinable())
            worker_.join();
    }
    template <class F> void run(F &&f)
    {
        if (busy_.exchange(true))
            return;
        join();
        stop_ = false;
        worker_ = std::thread([this, f = std::forward<F>(f)] {
            f();
            busy_ = false;
        });
    }

    // a broker connection for one action; false (and the reason in the device line) if there is no Virus for us
    bool connect(vti::Broker &broker)
    {
        std::string err;
        broker.set_name("TI Revive Control Center");
        if (broker.open(err) && broker.device_present())
            return true;
        set(&Shared::device, err.empty() ? "No Virus: not connected or switched off."
                                         : "No connection to the TI Revive broker: " + err);
        for (int i = 0; i < 4; i++)
            set_bank(i, "");
        return false;
    }

    void refresh()
    {
        run([this] {
            set(&Shared::status, "Looking for the Virus...");
            vti::Broker broker;
            if (!connect(broker))
                return set(&Shared::status, "Connect the Virus, switch it on and press Refresh.");
            std::string err;
            vti::cc::DeviceInfo di;
            if (!vti::cc::read_device_info(broker, di, err))
                return set(&Shared::status, err);
            char line[160];
            snprintf(line, sizeof line, "%s %s  (USB %04x:%04x, bcdDevice %x.%02x)", di.manufacturer.c_str(),
                     di.product_name.c_str(), di.vendor, di.product, di.bcd >> 8, di.bcd & 0xFF);
            set(&Shared::device, line);
            vti::MemorySync sync(broker);
            std::string cache = vti::cc::cache_dir();
            for (int bank = 1; bank <= 4 && !stop_; bank++) {
                std::string name = std::string("RAM ") + (char)('A' + bank - 1);
                auto fp = sync.bank_info(bank);
                if (fp.size() != 128) {
                    set_bank(bank - 1, name + ":  no answer");
                    continue;
                }
                vti::Bank b;
                int current = 0;
                if (vti::load_bank_file(cache + "/" + name, b))
                    for (int p = 0; p < 128; p++)
                        current += vti::RemoteDevice::single_fingerprint(b[p]) == fp[p];
                std::string s = name + ":  128 programs, " + std::to_string(current) + " current in the cache";
                if (current)
                    s += "  (1: " + vti::cc::patch_name(b[0]) + ")";
                set_bank(bank - 1, s);
            }
            set(&Shared::status, "Ready. A backup takes a few seconds for programs that are current in the cache, "
                                 "about 1 s for each other one.");
        });
    }

    void backup()
    {
        std::string out;
        {
            std::lock_guard<std::mutex> lk(sh_.mu);
            out = sh_.folder;
        }
        run([this, out] {
            vti::Broker broker;
            if (!connect(broker))
                return set(&Shared::status, "Connect the Virus, switch it on and try again.");
            std::string err;
            int fetched = 0;
            bool ok = vti::cc::backup_ram(broker, "1234", out, [&](int bank, int p, bool f, const vti::Dump &d) {
                fetched += f;
                char s[160];
                snprintf(s, sizeof s, "Backing up RAM %c: %d / 128%s%s", 'A' + bank - 1, p + 1, f ? "  read: " : "",
                         f ? vti::cc::patch_name(d).c_str() : "");
                set(&Shared::status, s);
            }, err, &stop_);
            if (ok)
                set(&Shared::status, "Backup done (" + std::to_string(fetched) + " programs read from the Virus): " +
                                         out);
            else
                set(&Shared::status, "Backup not finished: " + err);
            if (ok) // the next backup gets its own folder
                set(&Shared::folder, vti::cc::default_backup_dir());
        });
    }

    // Restore a RAM bank from a library (a backup's "RAM B.mid", or any library): the file, then a menu whose entry
    // names the bank to overwrite - choosing it is the confirmation. RAM only, never flash (controlcenter.h, E313).
    void choose_restore(std::function<void(const std::string &)> also = nullptr)
    {
        auto open = owned(CNewFileSelector::create(frame_, CNewFileSelector::kSelectFile));
        if (!open)
            return;
        open->setTitle("Backup or library to restore into a RAM bank");
        std::string dir;
        {
            std::lock_guard<std::mutex> lk(sh_.mu);
            dir = sh_.folder;
        }
        dir = fs::exists(dir) ? dir : fs::path(dir).parent_path().string(); // the backups folder
        open->setInitialDirectory(dir.c_str());
        open->addFileExtension(CFileExtension("Patch library", "mid"));
        open->addFileExtension(CFileExtension("SysEx file", "syx"));
        if (!open->run([this, also](CNewFileSelector *s) {
                if (s->getNumSelectedFiles())
                    restore_menu(s->getSelectedFile(0), also);
            }))
            no_dialog();
    }

    // `also`: the Access look's status line (its RESTORE button), besides the plain page's
    void restore_menu(const std::string &file, std::function<void(const std::string &)> also)
    {
        auto status = [this, also](const std::string &st) {
            set(&Shared::status, st);
            if (also)
                also(st);
        };
        auto lib = std::make_shared<vti::PatchLibrary>();
        std::string err;
        auto files = vti::AccessFiles::open("", err);
        if (!vti::load_library_file(file, *lib, files ? files->default_page_b() : std::vector<uint8_t>()) ||
            lib->dumps.empty())
            return status("No Virus TI patches in " + file);
        const std::string name = fs::path(file).filename().string();
        const int guess = vti::cc::bank_from_name(fs::path(file).stem().string());
        auto *menu = new COptionMenu();
        for (int b = 1; b <= 4; b++) {
            auto *item = menu->addEntry(("Overwrite RAM " + std::string(1, (char)('A' + b - 1)) + " with \"" + name +
                                         "\" (only programs that differ)")
                                            .c_str());
            item->setChecked(b == guess);
        }
        menu->addSeparator();
        menu->addEntry("Cancel");
        CPoint where;
        if (page_ == kPlainPage) {
            CRect r = buttons_[7]->getViewSize();
            where = CPoint(r.left, r.bottom);
        } else
            frame_->getCurrentMouseLocation(where);
        menu->popup(frame_, where, [this, lib, name, status](COptionMenu *m) {
            int b = m->getLastResult() + 1;
            if (b >= 1 && b <= 4)
                restore(b, lib, name, status);
        });
        menu->forget();
    }

    void restore(int bank, std::shared_ptr<vti::PatchLibrary> lib, const std::string &name,
                 std::function<void(const std::string &)> status)
    {
        run([this, bank, lib, name, status] {
            const char b = (char)('A' + bank - 1);
            vti::Broker broker;
            if (!connect(broker))
                return status("Connect the Virus, switch it on and try again.");
            status(std::string("RAM ") + b + ": checking which programs differ...");
            std::string err;
            int written = 0;
            bool ok = vti::cc::restore_ram(broker, bank, lib->dumps, [&](int done, int total) {
                status(std::string("Restoring RAM ") + b + ": " + std::to_string(done) + " / " +
                                         std::to_string(total) + " written and read back");
            }, written, err, &stop_);
            if (ok)
                status(std::string("RAM ") + b + " restored from " + name + ": " +
                                         std::to_string(written) + " programs written" +
                                         (written ? "." : " (all were the same already)."));
            else
                status(std::string("RAM ") + b + " not restored completely (" + std::to_string(written) +
                                         " written): " + err);
        });
    }

    void choose_folder()
    {
        auto sel = owned(CNewFileSelector::create(frame_, CNewFileSelector::kSelectDirectory));
        if (!sel)
            return;
        sel->setTitle("Backup folder");
        if (!sel->run([this](CNewFileSelector *s) {
                if (s->getNumSelectedFiles())
                    set(&Shared::folder, s->getSelectedFile(0));
            }))
            no_dialog();
    }

    void export_library()
    {
        auto open = owned(CNewFileSelector::create(frame_, CNewFileSelector::kSelectFile));
        if (!open)
            return;
        open->setTitle("Patch library to convert (.mid or .syx)");
        open->setInitialDirectory(vti::library_dir().c_str());
        open->addFileExtension(CFileExtension("Patch library", "mid"));
        open->addFileExtension(CFileExtension("SysEx file", "syx"));
        bool shown = open->run([this](CNewFileSelector *s) {
            if (!s->getNumSelectedFiles())
                return;
            std::string in = s->getSelectedFile(0);
            vti::PatchLibrary lib;
            std::string err;
            auto files = vti::AccessFiles::open("", err);
            if (!vti::load_library_file(in, lib, files ? files->default_page_b() : std::vector<uint8_t>()) ||
                lib.dumps.empty())
                return set(&Shared::status, "No Virus TI patches in " + in);
            bool to_syx = fs::path(in).extension() != ".syx";
            auto save = owned(CNewFileSelector::create(frame_, CNewFileSelector::kSelectSaveFile));
            if (!save)
                return;
            save->setTitle(to_syx ? "Save as .syx" : "Save as .mid library");
            save->setDefaultSaveName((fs::path(in).stem().string() + (to_syx ? ".syx" : ".mid")).c_str());
            save->setDefaultExtension(to_syx ? CFileExtension("SysEx file", "syx") : CFileExtension("Patch library", "mid"));
            bool saved = save->run([this, lib, to_syx](CNewFileSelector *s2) mutable {
                if (!s2->getNumSelectedFiles())
                    return;
                std::string out = s2->getSelectedFile(0), e;
                bool ok;
                if (to_syx) {
                    ok = vti::cc::write_syx(lib.dumps, out, e);
                } else {
                    if (lib.dumps.size() > 128)
                        lib.dumps.resize(128); // a library holds 128 patches
                    lib.path = out;
                    ok = vti::save_library(lib, e);
                }
                set(&Shared::status, ok ? std::to_string(lib.dumps.size()) + " patches saved: " + out
                                        : "Not saved: " + e);
            });
            if (!saved)
                no_dialog();
        });
        if (!shown)
            no_dialog();
    }
    void no_dialog()
    {
        // Linux: VSTGUI's file dialogs run zenity or kdialog
        set(&Shared::status, "No file dialog could be opened (Linux: install zenity or kdialog).");
    }
};

Application::Init gApp(std::make_unique<App>());

} // namespace
