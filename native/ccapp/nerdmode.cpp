// nerdmode.cpp - see nerdmode.h.
#include "nerdmode.h"
#include "access/accessfiles.h"
#include "cc/decode.h"
#include "cc/diag.h"
#include "device/broker.h"
#include "model/remotedevice.h"
#include "vtiproto.h"
#include "vstgui/lib/cfileselector.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/controls/cbuttons.h"
#include "vstgui/lib/controls/coptionmenu.h"
#include "vstgui/lib/controls/ctextlabel.h"
#include "vstgui/lib/controls/icontrollistener.h"
#include "vstgui/lib/cvstguitimer.h"
#include <algorithm>
#include <atomic>
#include <map>
#include <memory>
#include <ctime>
#include <deque>
#include <mutex>
#include <thread>

using namespace VSTGUI;

namespace tirevive {
namespace {

const CColor kBack(30, 30, 30), kText(220, 220, 220), kDim(150, 150, 150), kAccent(240, 170, 60), kGreen(10, 110, 30);
constexpr CCoord W = kNerdWidth, H = kNerdHeight, M = 16, kLine = 15;
enum Tag { kBack_ = 1, kLiveTab, kPluginTab, kBrokerTab, kPause, kClock, kPluginLog, kSave, kOptions };
enum Page { kLive, kPluginPage, kBrokerPage };

// The live view: a read-only observer connection to the broker (protocol 4) while the "Live" tab is shown. The
// broker's reader thread fills the lines; the UI timer shows them.
class Live {
public:
    ~Live() { stop(); }
    void start()
    {
        if (running_.exchange(true))
            return;
        join();
        thread_ = std::thread([this] { connect(); });
    }
    void stop()
    {
        running_ = false;
        join();
        std::lock_guard<std::mutex> l(conn_);
        broker_.reset();
    }
    // the lines to show (newest last) and the status line; `clock` = with clock / Active Sensing / F5 lines
    void snapshot(size_t n, bool clock, std::vector<std::string> &lines, std::string &status)
    {
        std::lock_guard<std::mutex> l(mu_);
        status = status_;
        for (auto it = lines_.rbegin(); it != lines_.rend() && lines.size() < n; ++it)
            if (clock || !it->second)
                lines.push_back(it->first);
        std::reverse(lines.begin(), lines.end());
    }

private:
    std::atomic<bool> running_{false};
    std::thread thread_;
    std::mutex mu_, conn_;
    std::unique_ptr<vti::Broker> broker_;
    std::unique_ptr<vti::RemoteDevice> model_;
    std::unique_ptr<vti::cc::TrafficDecoder> dec_;
    std::deque<std::pair<std::string, bool>> lines_; // text, is a clock line
    std::string status_ = "Connecting to the TI Revive broker...";

    void join()
    {
        if (thread_.joinable())
            thread_.join();
    }
    void add(const std::string &line, bool clock) // mu_ held
    {
        lines_.emplace_back(line, clock);
        if (lines_.size() > 3000)
            lines_.pop_front();
    }
    static std::string pretty_status(const std::string &raw)
    {
        // "device=1<TAB>owner=...<TAB>..." -> one readable line
        std::map<std::string, std::string> f;
        size_t i = 0;
        while (i <= raw.size()) {
            size_t e = raw.find('\t', i);
            std::string kv = raw.substr(i, e == std::string::npos ? std::string::npos : e - i);
            size_t eq = kv.find('=');
            if (eq != std::string::npos)
                f[kv.substr(0, eq)] = kv.substr(eq + 1);
            if (e == std::string::npos)
                break;
            i = e + 1;
        }
        return std::string("Virus: ") + (f["device"] == "1" ? "connected" : "not connected") + "   Owner: " +
               (f["owner"] == "-" ? "nobody" : f["owner"]) + "   Audio: " + (f["audio"] == "1" ? "streaming" : "off") +
               "   Packets to / from the Virus: " + f["to_device"] + " / " + f["from_device"] + "   Dropped here: " +
               f["dropped"] + "\nClients: " + f["clients"];
    }
    void connect()
    {
        if (!model_) { // parameter names from the user's Access files, if they are there
            std::string err;
            auto files = vti::AccessFiles::open("", err);
            auto m = std::make_unique<vti::RemoteDevice>();
            if (files && m->load(files->get("remotedevice.xml"), err))
                model_ = std::move(m);
            dec_ = std::make_unique<vti::cc::TrafficDecoder>(model_.get());
        }
        auto b = std::make_unique<vti::Broker>();
        b->set_name("TI Revive Control Center (Nerd mode)");
        std::string err;
        bool ok = b->open(err) && b->observe([this](uint16_t type, const uint8_t *d, size_t n) {
            std::lock_guard<std::mutex> l(mu_);
            if (type == VTIMSG_OBS_STATUS)
                status_ = pretty_status(std::string((const char *)d, n));
            else if (type == VTIMSG_OBS_LOG)
                add("broker  " + std::string((const char *)d, n), false);
            else if (type == VTIMSG_OBS_TRAFFIC)
                dec_->feed(d, n, [this](vti::cc::TrafficLine &&t) {
                    static const char *dir[] = {"-> Virus", "<- Virus", "USB ctl "};
                    char ts[24]; // by hand: GTK sets the locale's decimal comma
                    snprintf(ts, sizeof ts, "%4u.%03u ", t.ms / 1000, t.ms % 1000);
                    add(ts + std::string(dir[t.dir]) + " " + t.text, t.kind == "clock");
                });
        }, err);
        std::lock_guard<std::mutex> l(mu_);
        if (!ok) {
            status_ = "No live view: " + err;
            running_ = false;
            return;
        }
        status_ = "Connected (read-only observer), waiting for the broker's status...";
        std::lock_guard<std::mutex> lc(conn_);
        broker_ = std::move(b);
    }
};

#if defined(_WIN32)
const char *kMono = "Consolas";
#elif defined(__APPLE__)
const char *kMono = "Menlo";
#else
const char *kMono = "Monospace";
#endif

class NerdPage : public CViewContainer, public IControlListener {
public:
    NerdPage(std::string version, std::function<void()> back)
        : CViewContainer(CRect(0, 0, W, H)), version_(std::move(version)), back_(std::move(back))
    {
        setBackgroundColor(kBack);
        build();
    }
    ~NerdPage() noexcept override
    {
        if (timer_)
            timer_->stop();
    }

    void activate(bool on)
    {
        setVisible(on);
        active_ = on;
        if (!on) {
            timer_ = nullptr;
            live_.stop();
            return;
        }
        if (page_ == kLive)
            live_.start();
        timer_ = makeOwned<CVSTGUITimer>([this](CVSTGUITimer *) { refresh(); }, 250, true);
        refresh(true);
    }

    void valueChanged(CControl *c) override
    {
        if (c->getValue() < 0.5f)
            return;
        switch (c->getTag()) {
        case kBack_: back_(); return;
        case kLiveTab: page_ = kLive; live_.start(); break;
        case kPluginTab: page_ = kPluginPage; live_.stop(); break;
        case kBrokerTab: page_ = kBrokerPage; live_.stop(); break;
        case kPause: paused_ = !paused_; break;
        case kClock: show_clock_ = !show_clock_; break;
        case kPluginLog: {
            std::string err;
            if (!vti::cc::set_plugin_log(!vti::cc::plugin_log_enabled(), err))
                message_ = err;
            else
                message_ = vti::cc::plugin_log_enabled()
                               ? "Plug-in log on: it starts with the next start of the plug-in (reload it or restart the DAW)."
                               : "Plug-in log off (from the next start of the plug-in).";
            break;
        }
        case kSave: save(); break;
        case kOptions: options_menu(c); return;
        }
        refresh(true);
    }

private:
    std::string version_;
    std::function<void()> back_;
    SharedPointer<CVSTGUITimer> timer_;
    CTextButton *tabs_[3] = {}, *pause_ = nullptr, *clock_ = nullptr, *log_switch_ = nullptr;
    CMultiLineTextLabel *where_ = nullptr;
    CTextLabel *state_ = nullptr;
    CMultiLineTextLabel *text_ = nullptr;
    std::string message_;
    int page_ = kLive, ticks_ = 0;
    bool paused_ = false, show_clock_ = false, active_ = false;
    Live live_;

    CTextLabel *label(const CRect &r, const CColor &color, CFontRef font)
    {
        auto *l = new CTextLabel(r, "");
        l->setFontColor(color);
        l->setFont(font);
        l->setHoriAlign(kLeftText);
        l->setTransparency(true);
        l->setTextTruncateMode(CTextLabel::kTruncateHead);
        addView(l);
        return l;
    }
    // A/B switches against the original (diag.h plugin_options): a checked entry = its flag file exists; a click
    // flips it. Like the log switch, the plug-in reads them when an instance starts
    void options_menu(CControl *from)
    {
        auto *menu = new COptionMenu();
        for (auto &o : vti::cc::plugin_options())
            menu->addEntry(o.label)->setChecked(vti::cc::plugin_option(o.name));
        CPoint where(from->getViewSize().left, from->getViewSize().bottom);
        menu->popup(getFrame(), where, [this](COptionMenu *m) {
            const int i = m->getLastResult();
            const auto &opts = vti::cc::plugin_options();
            if (i < 0 || i >= (int)opts.size())
                return;
            std::string err;
            const bool on = !vti::cc::plugin_option(opts[(size_t)i].name);
            if (!vti::cc::set_plugin_option(opts[(size_t)i].name, on, err))
                message_ = err;
            else
                message_ = std::string(opts[(size_t)i].label) + (on ? ": on" : ": off") +
                           " - from the next start of the plug-in (reload it or restart the DAW).";
            refresh(true);
        });
        menu->forget();
    }
    CTextButton *button(CCoord x, CCoord y, CCoord w, int tag, const char *title)
    {
        auto *b = new CTextButton(CRect(x, y, x + w, y + 26), this, tag, title);
        addView(b);
        return b;
    }

    void build()
    {
        auto title = makeOwned<CFontDesc>(*kSystemFont);
        title->setSize(16);
        title->setStyle(kBoldFace);
        label(CRect(M, 10, W - M - 200, 34), kText, title)->setText("Nerd mode: what TI Revive is doing");
        button(W - M - 190, 10, 90, kOptions, "Options...");
        button(W - M - 90, 10, 90, kBack_, "Back");
        tabs_[0] = button(M, 44, 80, kLiveTab, "Live");
        tabs_[1] = button(M + 86, 44, 100, kPluginTab, "Plug-in log");
        tabs_[2] = button(M + 192, 44, 100, kBrokerTab, "Broker log");
        pause_ = button(M + 304, 44, 70, kPause, "Pause");
        clock_ = button(M + 380, 44, 110, kClock, "");
        log_switch_ = button(W - M - 330, 44, 160, kPluginLog, "");
        button(W - M - 160, 44, 160, kSave, "Save logs...");
        where_ = new CMultiLineTextLabel(CRect(M, 76, W - M, 108));
        where_->setFontColor(kDim);
        where_->setFont(kNormalFontSmall);
        where_->setHoriAlign(kLeftText);
        where_->setTransparency(true);
        addView(where_);
        state_ = label(CRect(M, H - 26, W - M, H - 8), kAccent, kNormalFontSmall);
        auto mono = makeOwned<CFontDesc>(kMono, 11);
        text_ = new CMultiLineTextLabel(CRect(M, 110, W - M, H - 32));
        text_->setFontColor(kText);
        text_->setFont(mono);
        text_->setHoriAlign(kLeftText);
        text_->setBackColor(CColor(18, 18, 18));
        text_->setStyle(CTextLabel::kNoFrame);
        text_->setLineLayout(CMultiLineTextLabel::LineLayout::truncate);
        text_->setTextInset({6, 4});
        addView(text_);
    }

    void refresh(bool force = false)
    {
        bool on = vti::cc::plugin_log_enabled();
        log_switch_->setTitle(on ? "Plug-in log: ON" : "Plug-in log: OFF");
        log_switch_->setTextColor(on ? kGreen : kText);
        for (int i = 0; i < 3; i++)
            tabs_[i]->setAlphaValue(page_ == i ? 1.f : 0.55f);
        pause_->setTitle(paused_ ? "Resume" : "Pause");
        clock_->setTitle(show_clock_ ? "Clock: shown" : "Clock: hidden");
        clock_->setVisible(page_ == kLive);
        state_->setText(message_.c_str());
        const size_t n = (size_t)((H - 32 - 110 - 8) / kLine);
        std::vector<std::string> lines;
        std::string where;
        if (page_ == kLive) {
            live_.snapshot(n, show_clock_, lines, where);
        } else {
            where = page_ == kBrokerPage ? "Broker log: " + vti::cc::broker_log_location()
                                         : "Plug-in log: " + vti::cc::plugin_log_path() +
                                               (on ? "" : "  (off: switch it on to see what the plug-in does)");
            if (++ticks_ % 4 && !force) // the log files once a second
                return;
        }
        where_->setText(where.c_str());
        if (paused_ && !force)
            return;
        if (page_ == kBrokerPage)
            lines = vti::cc::broker_log_tail(n);
        else if (page_ == kPluginPage)
            lines = vti::cc::tail_file(vti::cc::plugin_log_path(), n);
        std::string t;
        for (auto &l : lines)
            t += l + "\n";
        if (lines.empty())
            t = page_ == kLive      ? "(nothing yet: start the plug-in or use the Control Center to see the traffic)"
                : page_ == kBrokerPage ? "(no broker log yet: it is written while a program uses the Virus)"
                                       : "(no plug-in log yet)";
        text_->setText(t.c_str());
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
        bool shown = sel->run([this](CNewFileSelector *s) {
            if (!s->getNumSelectedFiles())
                return;
            std::string out = s->getSelectedFile(0), err;
            message_ = vti::cc::collect_logs(out, version_, err)
                           ? "Saved (home folder and user name replaced; please look through it before attaching): " + out
                           : "Not saved: " + err;
            refresh(true);
        });
        if (!shown)
            message_ = "No file dialog could be opened (Linux: install zenity or kdialog).";
    }
};

} // namespace

CViewContainer *make_nerd_page(const std::string &version, std::function<void()> back)
{
    auto *p = new NerdPage(version, std::move(back));
    p->setVisible(false);
    return p;
}

void set_nerd_page_active(CViewContainer *page, bool on)
{
    if (auto *p = dynamic_cast<NerdPage *>(page))
        p->activate(on);
}

} // namespace tirevive
