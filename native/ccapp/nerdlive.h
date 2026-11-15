// nerdlive.h - Nerd mode's live view, shared by the plain page (nerdmode.cpp) and the page in the Access look
// (skinpage.cpp): a read-only observer connection to the broker (protocol 4). The broker's reader thread fills the
// lines (decoded traffic and the broker's log lines) and the status; the UI's timer reads them. It also writes the
// recording (cc/capture.h). Nothing here sends anything to the Virus.
#pragma once
#include "model/utf8.h"
#include "access/accessfiles.h"
#include "cc/capture.h"
#include "cc/decode.h"
#include "device/broker.h"
#include "model/remotedevice.h"
#include "vtiproto.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace tirevive {

// one line of the live view: time (the clock, "11:25:54.004", for traffic and broker lines alike), direction
// ("-> Virus", "<- Virus", "USB ctl", "broker"), endpoint ("ep06" or ""), the decoded text; clock lines (clock,
// Active Sensing, F5) can be hidden
struct LiveLine {
    std::string time, dir, ep, text;
    bool clock = false;
    std::string joined() const
    {
        std::string s = time.empty() ? "" : time + " ";
        s += dir;
        if (!ep.empty())
            s += " " + ep;
        return s + " " + text;
    }
};

class Live {
public:
    ~Live()
    {
        stop();
        stop_record();
    }
    // The full traffic log in a file (cc/capture.h): reconnects to get the broker's pre-roll too, and keeps running on
    // the other tabs, until stop_record().
    bool start_record(const std::string &path, const std::string &version)
    {
        stop();
        FILE *f = vti::u8fopen(path, "w");
        if (!f)
            return false;
        {
            std::lock_guard<std::mutex> l(mu_);
            char stamp[32];
            time_t now = time(nullptr);
            strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", localtime(&now));
            fprintf(f, "TI Revive capture, Control Center %s, started %s. Not in it: the USB audio stream.\n",
                    version.c_str(), stamp);
            rec_file_ = f;
        }
        start();
        return true;
    }
    void stop_record()
    {
        std::lock_guard<std::mutex> l(mu_);
        if (!rec_file_)
            return;
        if (cap_)
            cap_->summary();
        cap_.reset();
        fclose(rec_file_);
        rec_file_ = nullptr;
    }
    bool recording()
    {
        std::lock_guard<std::mutex> l(mu_);
        return rec_file_ != nullptr;
    }
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
    // the last `n` lines as text (newest last) and the status; `clock` = with the clock lines (the plain page)
    void snapshot(size_t n, bool clock, std::vector<std::string> &lines, std::string &status)
    {
        std::lock_guard<std::mutex> l(mu_);
        status = status_;
        for (auto it = lines_.rbegin(); it != lines_.rend() && lines.size() < n; ++it)
            if (clock || !it->clock)
                lines.push_back(it->joined());
        std::reverse(lines.begin(), lines.end());
    }
    // how many lines there are (with or without the clock lines), the status and how many came in since the start
    int count(bool clock, std::string &status, uint64_t &serial)
    {
        std::lock_guard<std::mutex> l(mu_);
        status = status_;
        serial = serial_;
        if (clock)
            return (int)lines_.size();
        int n = 0;
        for (auto &x : lines_)
            n += !x.clock;
        return n;
    }
    // only the lines on screen: `n` from index `first` (the page copied all 3000 four times a second and lagged)
    void window(bool clock, int first, int n, std::vector<LiveLine> &out)
    {
        std::lock_guard<std::mutex> l(mu_);
        out.clear();
        int i = 0;
        for (auto &x : lines_) {
            if (!clock && x.clock)
                continue;
            if (i >= first + n)
                break;
            if (i++ >= first)
                out.push_back(x);
        }
    }

private:
    std::atomic<bool> running_{false};
    std::thread thread_;
    std::mutex mu_, conn_;
    std::unique_ptr<vti::Broker> broker_;
    std::unique_ptr<vti::RemoteDevice> model_;
    std::unique_ptr<vti::cc::TrafficDecoder> dec_;
    FILE *rec_file_ = nullptr;                 // mu_
    std::unique_ptr<vti::cc::CaptureLog> cap_; // mu_
    std::deque<LiveLine> lines_;               // mu_
    uint64_t serial_ = 0;                      // mu_: lines added since the start
    int64_t clock_offset_ = INT64_MAX;         // mu_: local clock (ms) minus the broker's traffic counter (ms)
    std::string status_ = "Connecting to the TI Revive broker...";

    void join()
    {
        if (thread_.joinable())
            thread_.join();
    }
    void add(LiveLine &&line) // mu_ held
    {
        lines_.push_back(std::move(line));
        serial_++;
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
        bool replay = false;
        {
            std::lock_guard<std::mutex> l(mu_);
            if (rec_file_ && !cap_) {
                FILE *f = rec_file_;
                cap_ = std::make_unique<vti::cc::CaptureLog>(model_.get(), [f](const std::string &line) {
                    fprintf(f, "%s\n", line.c_str());
                    fflush(f);
                });
                replay = true;
            }
        }
        bool ok = b->open(err) && b->observe([this](uint16_t type, const uint8_t *d, size_t n) {
            std::lock_guard<std::mutex> l(mu_);
            if (cap_) {
                if (type == VTIMSG_OBS_TRAFFIC)
                    cap_->traffic(d, n);
                else if (type == VTIMSG_OBS_LOG)
                    cap_->log(std::string((const char *)d, n));
                else if (type == VTIMSG_OBS_STATUS)
                    cap_->status(std::string((const char *)d, n));
            }
            if (type == VTIMSG_OBS_STATUS) {
                status_ = pretty_status(std::string((const char *)d, n));
            } else if (type == VTIMSG_OBS_LOG) {
                // the broker's lines start with its clock ("11:22:54.004 "): into the time field
                std::string text((const char *)d, n), when;
                if (text.size() > 13 && text[2] == ':' && text[5] == ':' && text[8] == '.' && text[12] == ' ')
                    when = text.substr(0, 12), text = text.substr(13);
                add({when, "broker", "", text, false});
            } else if (type == VTIMSG_OBS_TRAFFIC) {
                dec_->feed(d, n, [this](vti::cc::TrafficLine &&t) {
                    static const char *dir[] = {"-> Virus", "<- Virus", "USB ctl"};
                    // the broker counts traffic in ms since its start; as clock time, like its log lines. The
                    // offset is the smallest (local clock - counter) seen: a line that came late does not shift it
                    const int64_t now = (int64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
                                            std::chrono::system_clock::now().time_since_epoch())
                                            .count();
                    clock_offset_ = std::min(clock_offset_, now - (int64_t)t.ms);
                    const int64_t at = clock_offset_ + (int64_t)t.ms;
                    const time_t secs = (time_t)(at / 1000);
                    struct tm lt;
#ifdef _WIN32
                    localtime_s(&lt, &secs);
#else
                    localtime_r(&secs, &lt);
#endif
                    char ts[24]; // by hand: GTK sets the locale's decimal comma
                    snprintf(ts, sizeof ts, "%02d:%02d:%02d.%03d", lt.tm_hour, lt.tm_min, lt.tm_sec, (int)(at % 1000));
                    char ep[8] = "";
                    if (t.dir != 2)
                        snprintf(ep, sizeof ep, "ep%02x", t.ep);
                    add({ts, dir[t.dir < 3 ? t.dir : 2], ep, t.text, t.kind == "clock"});
                });
            }
        }, err, replay);
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

} // namespace tirevive
