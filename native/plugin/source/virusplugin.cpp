// virusplugin.cpp - see virusplugin.h.
#include "virusplugin.h"
#include "model/devicestatus.h"
#include <cstdarg>
#include <ctime>
#include <set>
#include "viruseditor.h"
#include "access/accessfiles.h"
#include "access/firmware.h"
#include "access/library.h"
#include "cc/diag.h"
#include "base/source/fstreamer.h"
#include "device/usbmidi.h"
#include "model/state.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace vti {

// The plug-in takes over the class id of the TI Revive plug-in it replaces (0.8.0), so projects saved with
// "Virus TI (TI Revive)" open with it: Windows/Linux the wrapper's id (the original's with byte 15 ^ 0x5A, vtiwrap.c),
// macOS the bridge's id (tirbridge_vst3.cpp). The other ids are declared compatible in moduleinfo.json
// (plugin/CMakeLists.txt). VTI_NATIVE_DROPIN: the original Access id (tests with projects of the original).
#if defined(VTI_NATIVE_DROPIN)
const FUID kVirusTIClassId(0xD9909FB4, 0x5F730543, 0xAC5397BE, 0x8958CD31);
#elif defined(__APPLE__)
const FUID kVirusTIClassId(0x7A1E5C3B, 0x4D2F4E61, 0x9B0C8A17, 0x2F64D5E9);
#else
const FUID kVirusTIClassId(0xB49F90D9, 0x735F4305, 0xAC5397BE, 0x8958CD31 ^ 0x5A);
#endif
// Hosts that ignore moduleinfo compatibility restore only the exact id (Ableton Live 11, E221): the same plug-in is
// registered a second time under the original's id (entry.cpp), so projects made with the original open with it.
// As a FUID (the words hosts store, e.g. Live's Uid Fields): D9909FB4 5F730543 ...; the wrapper's id above was made
// from the raw bytes (B4 9F 90 D9 ..., E221) and stays: projects saved with it exist
const FUID kOriginalClassId(0xD9909FB4, 0x5F730543, 0xAC5397BE, 0x8958CD31);

static bool env_debug()
{
    static const bool on = getenv("VTI_NATIVE_DEBUG") != nullptr; // read once (called on hot paths)
    return on;
}

static bool env_panellog()
{
    static const bool on = getenv("VTI_NATIVE_PANELLOG") != nullptr; // read once (called on hot paths)
    return on;
}

static bool env_no_ping() // R5 test: the old stamping without sync ping / marker
{
    static const bool on = getenv("VTI_NATIVE_NO_PING") != nullptr;
    return on;
}

static const char *const USB_IO_PATH = "Global USB/Virus Control USB IO Configuration";
static const char *const SYNC_INFO_PATH = "Global USB/Sync Information Event"; // global 0x54 from the Virus (E205)

// the USB IO configuration of this computer: VTI_IO_CONFIG (tests), else <plug-in folder>/usb-io, else 0
static int load_usb_io()
{
    int v = 0;
    if (const char *e = std::getenv("VTI_IO_CONFIG"))
        v = std::atoi(e);
    else if (FILE *f = std::fopen((vti::cc::plugin_dir() + "/usb-io").c_str(), "r")) {
        if (std::fscanf(f, "%d", &v) != 1)
            v = 0;
        std::fclose(f);
    }
    return v >= 0 && v <= 2 ? v : 0;
}

static bool env_stats()
{
    static const bool on = getenv("VTI_NATIVE_STATS") != nullptr; // read once (called on hot paths)
    return on;
}

static const uint8_t EP_PLUGIN_OUT = 0x06, CABLE_PLUGIN = 2, CABLE_TIME = 5;
static const ParamID MIDI_MAP_BASE = 0x100000; // + channel * 0x1000 + 0x80 aftertouch / 0x81 pitch bend
static std::string own_cache_dir();

// ---- DeviceOut ----------------------------------------------------------------------------------------------------

DeviceOut::DeviceOut() : worker_([this] { run(); }) {}

DeviceOut::~DeviceOut()
{
    stop_ = true;
    cv_.notify_all();
    broker_.close(); // wakes a worker stuck in a write to a broker that stopped reading (review 2026-10-19 M6)
    worker_.join();
    // again: the worker may have reconnected in between. The reader thread calls panel_, reply_ and reads audio_,
    // which are destroyed before broker_ (members go in reverse order), so it stops here (review M5)
    broker_.close();
    release_owner(); // a waiting instance takes over within ~2 s (start-up messages, state, a new sync ping)
}

static std::mutex g_owner_mu;
static const DeviceOut *g_owner; // the instance of this process that talks to the Virus

bool DeviceOut::claim_owner()
{
    std::lock_guard<std::mutex> g(g_owner_mu);
    if (!g_owner)
        g_owner = this;
    owner_ = g_owner == this;
    return owner_;
}

void DeviceOut::release_owner()
{
    std::lock_guard<std::mutex> g(g_owner_mu);
    if (g_owner == this)
        g_owner = nullptr;
    owner_ = false;
}

void DeviceOut::send(std::vector<uint8_t> msg)
{
    send(std::move(msg), CABLE_PLUGIN);
}

void DeviceOut::send(std::vector<uint8_t> msg, uint8_t cable)
{
    if (msg.empty()) // nothing to write, and the worker's write accounting (pending_send_) expects bytes
        return;
    if (blocked_to_virus(msg.data(), msg.size())) { // rule 2 (sendguard.h): never on the wire
        blocked_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    std::lock_guard<std::mutex> lk(mu_);
    queue_.push_back({std::move(msg), false, cable});
    queued_++;
    cv_.notify_one();
}

int DeviceOut::send_state(std::vector<std::vector<uint8_t>> records)
{
    // the records come from the host's project file: rule 2 and well-formed messages apply to them too (sendguard.h)
    if (size_t bad = drop_unsendable(records))
        log("Virus TI native: state: dropped %zu record(s) that rule 2 forbids or that are not one complete message\n",
            bad);
    std::lock_guard<std::mutex> lk(mu_);
    size_t before = queue_.size();
    queue_.erase(std::remove_if(queue_.begin(), queue_.end(), [](const Queued &q) { return q.state; }), queue_.end());
    int dropped = (int)(before - queue_.size());
    queued_ -= dropped;
    for (auto &r : records)
        queue_.push_back({std::move(r), true});
    queued_ += (int)records.size();
    cv_.notify_one();
    return dropped;
}

// The broker connection is lost (a failed write, or the broker went away; review 2026-10-19 H3: the second case was
// never noticed, so the plug-in stayed on "No Virus" until the project was reopened). A dump in pieces and the
// automation held for it are dropped (the state goes out again when the Virus is back; leftover pieces would go out
// without their header, review M2), then the loop opens the broker again.
void DeviceOut::lose_broker()
{
    drop_dump();
    close_audio();
    broker_.close();
    open_ = false;
}

// A dump in pieces and the automation held for it, when the Virus or the broker went away: the state goes out again
// when it is back, and the remaining pieces would go out without their header (review 2026-10-19 M2).
void DeviceOut::drop_dump()
{
    {
        std::lock_guard<std::mutex> lk(mu_);
        dump_ = DumpPacer();
        held_.take();
    }
    if (dump_pending_) {
        dump_pending_ = false;
        queued_--;
    }
}

// The Virus is gone or the broker write failed: readers on other threads (process(), the editor's diag report, the
// reader's klog) finish their read first, then the ring is unmapped (audiogate.h; review 2026-10-19 H1).
void DeviceOut::close_audio()
{
    audio_gate_.close();
    audio_.close();
}

bool DeviceOut::send_rt(const uint8_t *msg, size_t n, int64_t stamp)
{
    if (blocked_to_virus(msg, n)) { // rule 2 (sendguard.h); counted here, logged off the audio thread
        blocked_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
    size_t w = rt_w_.load(std::memory_order_relaxed);
    if (n > sizeof rt_[0].data || w - rt_r_.load(std::memory_order_acquire) >= RT_SLOTS) {
        // full (a runaway host while the budget holds the flood back): counted, and a lost note-off is answered with
        // all notes off by the worker - it would leave a note hanging (review 2026-10-19)
        rt_lost_.fetch_add(1, std::memory_order_relaxed);
        if (n >= 3 && ((msg[0] & 0xF0) == 0x80 || ((msg[0] & 0xF0) == 0x90 && msg[2] == 0)))
            rt_lost_off_.store(true, std::memory_order_relaxed);
        return false;
    }
    Slot &s = rt_[w % RT_SLOTS];
    s.len = (uint8_t)n;
    s.stamp = stamp;
    memcpy(s.data, msg, n);
    rt_w_.store(w + 1, std::memory_order_release);
    return true;
}

// R5 diagnostic (VTI_NATIVE_KLOG): for each stamped note-on, the first onset in USB 1 after a quiet stretch, as
// K = onset frame - stamp (the Virus plays a stamped event K frames after its stamp; K is fixed per session) and as
// the delay after sending. The ring holds only ~370 ms, so the audio is scanned as it arrives (up to 2 s per note).
void DeviceOut::klog_check()
{
    if (!audio_ok())
        return;
    const int64_t newest = audio_.newest_frame();
    for (size_t k = 0; k < klog_stamps_.size();) {
        auto &e = klog_stamps_[k];
        int64_t from = std::max({e.scanned, e.stamp - 4000, newest - 16000});
        int n = (int)std::min<int64_t>(newest - from, 16000);
        bool found = false;
        if (n > 0) {
            std::vector<float> l(n), r(n), a(n), b(n);
            audio_.read_at(l.data(), r.data(), a.data(), b.data(), n, from);
            for (int i = 0; i < n && !found; i++) {
                float v = std::fabs(l[i]) + std::fabs(r[i]);
                if (v < 0.004f)
                    e.quiet++;
                else if (v > 0.03f && e.quiet >= 2000) {
                    int64_t on = from + i;
                    log("klog: K %lld (stamp %lld, sent at newest %lld = stamp + %lld; sound %.1f ms after sending)\n",
                        (long long)(on - e.stamp), (long long)e.stamp, (long long)e.sent,
                        (long long)(e.sent - e.stamp), (on - e.sent) / 44.1);
                    found = true;
                } else if (v >= 0.004f)
                    e.quiet = 0;
            }
            e.scanned = from + n;
        }
        if (!found && e.scanned > e.stamp + 88200)
            log("klog: no onset within 2 s of stamp %lld (sent at newest %lld)\n", (long long)e.stamp, (long long)e.sent);
        if (found || e.scanned > e.stamp + 88200)
            klog_stamps_.erase(klog_stamps_.begin() + (long)k);
        else
            k++;
    }
}

void DeviceOut::log(const char *fmt, ...)
{
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    fputs(line, stderr);
    if (diag_on_.load(std::memory_order_acquire))
        if (FILE *f = fopen(diag_path_, "a")) {
            fprintf(f, "%ld %s", (long)std::time(nullptr), line);
            fclose(f);
        }
}

void DeviceOut::scan_audio()
{
    if (!audio_ok())
        return;
    int64_t newest = audio_.newest_frame();
    if (newest < 0)
        return;
    if (scanned_ < 0 || newest - scanned_ > 16384 - 2048)
        scanned_ = std::max<int64_t>(0, newest - 4096); // first call / fell behind: the ring holds 16384 frames
    int64_t from = marker_from_.load();
    // a frame is judged once the 66 after it are there: the marker check below reads frames f+2..f+65, which past
    // the newest frame are not written yet (review 2026-10-19: a marker could be judged on them)
    const int64_t until = newest - 66;
    while (scanned_ < until) {
        int n = (int)std::min<int64_t>(1024, until - scanned_);
        float l1[1024], r1[1024], l2[1024], r2[1024];
        audio_.read_at(l1, r1, l2, r2, n, scanned_);
        for (int i = 0; i < n; i++) {
            int64_t f = scanned_ + i;
            bool usb1 = l1[i] != 0 || r1[i] != 0, usb2 = l2[i] != 0 || r2[i] != 0;
            if (pings_out_.load() > 0 && from >= 0 && f >= from && f > marker_skip_ && usb2 && !usb1) {
                // the marker: one sample per channel, the rest of USB 2 silent around it
                float ll[64], rr[64], a2[64], b2[64];
                audio_.read_at(ll, rr, a2, b2, 64, f + 2);
                bool isolated = true;
                for (int k = 0; k < 64; k++)
                    isolated &= a2[k] == 0 && b2[k] == 0;
                if (isolated) {
                    marker = f; // the newest marker: the Virus reset its stamp clock here
                    pings_out_.fetch_sub(1);
                    log("Virus TI native: sync marker at frame %lld (ping at %lld, %lld frames later)\n", (long long)f,
                        (long long)from, (long long)(f - from));
                    marker_skip_ = f + 1; // its second channel follows one frame later
                }
            }
            if (usb1 || usb2)
                last_sound = f;
        }
        scanned_ += n;
    }
    if (pings_out_.load() > 0 && from >= 0 && newest > from + 5 * 44100) { // 5 s without a marker
        pings_out_ = 0;
        if (marker.load() < 0) {
            marker = -2;
            log("Virus TI native: no sync marker - time stamps relative to the ring\n");
        }
    }
}

// the Virus unplugged / switched off and back while the broker connection stays (user test): stop the audio, and on
// its return let the plug-in send the start-up messages and its state again; the audio start retries (worker thread)
void DeviceOut::check_presence()
{
    // another program's plug-in has the Virus (broker state 2) / gave it up: the same page as a second instance
    if (bool elsewhere = broker_.in_use_elsewhere(); elsewhere != elsewhere_seen_) {
        elsewhere_seen_ = elsewhere;
        log(elsewhere ? "Virus TI native: another program's plug-in uses the Virus - waiting\n"
                      : "Virus TI native: the other program gave up the Virus\n");
        if (on_waiting_ && !wait_reported_)
            on_waiting_(elsewhere);
    }
    auto lost = [this] {
        drop_dump();
        close_audio();
    };
    auto arrived = [this] {
        // a new audio stream counts its frames from 0 again: the scan positions of the old one would keep
        // scan_audio idle (and the sync marker unfound) until the new stream passed them - ~19 s (user test)
        scanned_ = -1;
        marker_skip_ = -1;
        last_sound = -1;
        pings_out_ = 0; // a ping of the old stream gets no marker in the new one (its 5 s timeout fired too early)
        generation++;
        // the Virus' OS version for PATCH UTILITY > INFO, read like the original at plug-in start, before the
        // memory sync: the broker's fixed read-only flash sequence (flashinfo.h, E026/E040), ~2 s once per open
        if (!elsewhere_seen_) {
            std::vector<uint8_t> fl;
            int n = broker_.flash_info(fl);
            std::string os = n > 0 ? tios_from_flash(fl) : std::string();
            log("Virus TI native: flash info %d bytes, OS \"%s\"\n", n, os.c_str());
            std::lock_guard<std::mutex> g(info_mu_);
            tios_ = os;
        }
    };
    const bool present = broker_.device_present(); // the flag before the count (the reader counts first)
    switch (presence_.update(present, broker_.device_arrivals())) {
    case vti::Presence::same:
        break;
    case vti::Presence::gone: {
        bool was = present_seen_ == 1;
        present_seen_ = 0;
        lost();
        if (device_ && was)
            device_(false);
        break;
    }
    case vti::Presence::back:
        present_seen_ = 1;
        arrived();
        if (device_)
            device_(true);
        break;
    case vti::Presence::bounced:
        // gone and back while this thread was not looking (E212: a Linux wake, the broker reopened the Virus 0.27 s
        // later): its audio stream is a new one that nobody started - a reconnect like a replug
        log("Virus TI native: the Virus was gone for a moment - reconnecting\n");
        lost();
        if (device_)
            device_(false);
        arrived();
        if (device_)
            device_(true);
        break;
    }
    report_status();
}

void DeviceOut::report_status()
{
    // "Virus unavailable" also for an instance waiting for another one of this process: it has no broker connection
    // yet, so its presence is unknown (E181; since 0.8.18 the page came only from the broker and stayed normal)
    const bool elsewhere = elsewhere_seen_ || wait_reported_;
    if (status_change_.update(present_seen_, elsewhere) && status_)
        status_(present_seen_ == 1, elsewhere);
}

void DeviceOut::run()
{
    while (!stop_ && !go_) // until the plug-in is activated (see start())
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (const char *e = getenv("VTI_NATIVE_MIDI_RATE")) // bytes/s of stamped traffic, 0 = unlimited (tests only)
        budget_.rate = atof(e);
    while (!stop_) {
        if (!open_) {
            std::string err;
            if (!rx_set_) {
                auto parser = std::make_shared<UsbMidiParser>([this](uint8_t ep, int cable, const std::vector<uint8_t> &m) {
                    if (klog_ && cable != 2 && cable != 3) { // R5: what else does the Virus send (time stamps?)
                        fprintf(stderr, "klog rx ep %02x cable %d:", ep, cable);
                        for (auto x : m)
                            fprintf(stderr, " %02x", x);
                        AudioGate::Use ring(audio_gate_); // the reader thread: the worker may unmap meanwhile
                        fprintf(stderr, " (newest %lld)\n", (long long)(ring ? audio_.newest_frame() : -1));
                    }
                    if (ep == 0x86 && cable == 2 && panel_)
                        panel_(m);
                    else if (((ep == 0x86 && cable == 3) || (ep == 0x85 && cable == 1)) && reply_ && !m.empty() &&
                             m[0] == 0xF0)
                        reply_(m);
                });
                broker_.on_rx([parser](uint8_t ep, const uint8_t *d, size_t len) { parser->feed(ep, d, len); });
                rx_set_ = true;
            }
            if (!owner_ && !claim_owner()) {
                if (!wait_reported_) {
                    wait_reported_ = true;
                    log("Virus TI native: another instance of the plug-in uses the Virus - waiting\n");
                    if (on_waiting_)
                        on_waiting_(true);
                    report_status();
                }
                for (int i = 0; i < 200 && !stop_; i++) { // keep the state current, try again in 2 s
                    // notes played meanwhile are dropped: sent at a takeover they would sound late, a full ring would
                    // lose their note-offs
                    rt_r_.store(rt_w_.load(std::memory_order_acquire), std::memory_order_release);
                    if (idle_)
                        idle_();
                    std::unique_lock<std::mutex> lk(mu_);
                    cv_.wait_for(lk, std::chrono::milliseconds(10), [this] { return stop_.load(); });
                }
                continue;
            }
            if (wait_reported_) {
                wait_reported_ = false;
                log("Virus TI native: the other instance is gone - this one takes over the Virus\n");
                if (on_waiting_)
                    on_waiting_(false);
            }
            broker_.set_name("TI Revive Virus Control");
            open_ = broker_.open(err);
            if (open_ && !audio_ok()) { // the owner starts the Virus audio stream (Linux: shm ring)
#ifdef __APPLE__
                int r = 1; // macOS: the plug-in reads the Virus' Core Audio input itself (the broker carries MIDI)
#else
                int r = broker_.audio(true);
#endif
                if (r == 1 && audio_.open())
                    audio_gate_.open();
                fprintf(stderr, "Virus TI native: audio %s (%d)\n", audio_ok() ? "streaming" : "not available", r);
            }
            if (!open_) {
                fprintf(stderr, "Virus TI native: %s\n", err.c_str());
                if (present_seen_ < 0) { // no broker at all (its socket stopped, E179 handover): the "No Virus TI can
                    present_seen_ = 0;   // be found" page instead of none (user, REAPER, round 0.8.20)
                    report_status();
                }
                for (int i = 0; i < 200 && !stop_; i++) { // keep the state current while there is no broker
                    if (idle_)
                        idle_();
                    std::unique_lock<std::mutex> lk(mu_);
                    cv_.wait_for(lk, std::chrono::milliseconds(10), [this] { return stop_.load(); });
                }
                continue;
            }
        }
        // no Virus (switched off, or another program has it): keep the queued messages (start-up, state) until it is
        // there - written into the void, a Virus switched on later never got them; events from process() (notes)
        // are dropped, they would play late
        if (!broker_.device_present()) {
            if (!broker_.connected()) { // the broker itself went away (crash, update, restart): open it again above
                log("Virus TI native: the connection to the TI Revive broker ended - reconnecting\n");
                lose_broker();
                continue;
            }
            rt_r_.store(rt_w_.load(std::memory_order_acquire), std::memory_order_release);
            if (idle_)
                idle_();
            check_presence();
            std::unique_lock<std::mutex> lk(mu_);
            cv_.wait_for(lk, std::chrono::milliseconds(10), [this] { return stop_.load(); });
            continue;
        }
        std::vector<uint8_t> msg, piece; // piece: the next part of a dump in pieces (dumppacer.h)
        bool piece_last = false;
        int64_t piece_wait_ns = 0;
        int64_t stamp = -1;
        uint8_t cable = CABLE_PLUGIN; // queued remote template records: 3 (E199)
        {
            std::unique_lock<std::mutex> lk(mu_);
            // process() wakes it once per block with events (wake_rt); the timeout is only a backstop and runs the idle
            // work (parameter drain) - before: a 1 ms poll, 1000 wake-ups/s even without anything to send
            cv_.wait_for(lk, std::chrono::milliseconds(5), [this] {
                return stop_ || !queue_.empty() || rt_r_.load() != rt_w_.load(std::memory_order_acquire);
            });
            if (stop_)
                break;
            size_t r = rt_r_.load(std::memory_order_relaxed);
            // events from process() first, but after 8 in a row one queued message (state, dumps, sync requests),
            // else dense MIDI starves the queue (stress test: queue stuck, bank sync timed out)
            // after a multi dump the Virus sets up its 16 parts and drops what arrives meanwhile (the following part 1
            // dump got lost in 2 of 5 start-ups): queued messages wait until it is done; events go on
            auto now = std::chrono::steady_clock::now();
            bool held = now < queue_hold_until_;
            // stamped traffic within the budget (sendbudget.h): a flood waits here, a few blocks at most
            const int64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
            const bool budget_ok = budget_.available(now_ns) >= 8;
            // after 8 events the queue side's turn, only when it can go now (a due piece, a queued message not held)
            const bool q_ready = queue_ready(dump_, now_ns, queue_.empty(), held);
            const bool rt = r != rt_w_.load(std::memory_order_acquire) && rt_run_.events_first(q_ready) && budget_ok;
            // a dump in pieces (dumppacer.h): the DAW's events go between the pieces, queued messages wait for its end
            // the piece is written below, after the regular work of each round (idle work, audio scan, stall watch,
            // presence): skipping that for a whole state's dumps (~2 s) made the stall watch see a sleep (E272)
            if (dump_.active() && !rt) {
                if (dump_.next(now_ns, piece)) {
                    piece_last = !dump_.active();
                    rt_run_.turn(); // the queue's turn while it waits for the dump (EventRun, dumppacer.h)
                } else
                    piece_wait_ns = std::max<int64_t>(dump_.wait_ns(now_ns), 1);
            } else {
                if ((held || !budget_ok) && !rt && queue_.empty()) {
                    lk.unlock();
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    continue;
                }
                if (held && !rt) {
                    lk.unlock();
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    continue;
                }
                if (rt) {
                    const Slot &sl = rt_[r % RT_SLOTS];
                    msg.assign(sl.data, sl.data + sl.len);
                    stamp = sl.stamp;
                    rt_r_.store(r + 1, std::memory_order_release);
                    rt_run_.event();
                    cable = dump_.event_cable(CABLE_PLUGIN); // cable 5 while a dump is in pieces (dumppacer.h)
                    if (dump_.active() && HeldParams::held(msg.data(), msg.size())) { // automation: after the dump
                        held_.add(msg);
                        msg.clear();
                        stamp = -1;
                    }
                } else if (!queue_.empty()) {
                    rt_run_.turn();
                    msg = std::move(queue_.front().msg);
                    cable = queued_cable(msg.data(), msg.size(), queue_.front().cable); // dumps: cable 3 (cablerule.h)
                    queue_.pop_front();
                    if (cable == 3 && msg.size() > 3 * (DumpPacer::PIECE / 4)) { // a dump: in pieces from the next round
                        auto pk = usbmidi_pack(msg, cable);
                        dump_.start(pk, std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count());
                        dump_pending_ = true;
                        msg.clear(); // its pieces go out from the next round on
                    } else
                        pending_send_ = true; // counted down after the write, so busy() covers the transfer
                }
            }
        }
        if (idle_)
            idle_();
        scan_audio();
        // the stream stood still while time went on and runs on (macOS sleep pauses Core Audio, E207): the Virus'
        // stamp clock did not stop, so the anchor is off by the pause - a new timeline and sync ping, as after a
        // reconnect (process() re-anchors on the generation change)
        if (audio_ok() && stall_.update(audio_.newest_frame(),
                                       std::chrono::duration_cast<std::chrono::milliseconds>(
                                           std::chrono::steady_clock::now().time_since_epoch()).count())) {
            log("Virus TI native: the Virus' audio stood still (sleep?) - set-up, state and a new timeline again\n");
            generation++;
            if (resume_) // after a Windows sleep the Virus answered sync pings but played no notes until this (E210)
                resume_();
        }
        check_presence();
        if ((env_stats() || diag_on_.load(std::memory_order_acquire)) && std::chrono::steady_clock::now() >= stats_next_) { // endurance tests
            stats_next_ = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            int64_t newest = audio_ok() ? audio_.newest_frame() : -1, pos = audio_ok() ? audio_.position() : -1;
            log("stats: audio %d drops %lld filled %lld distance %lld queued %d | marker %lld aligned blocks %lld, read "
                "margin min %lld, timeline steps %lld, last sound %lld frames ago | host frames %lld / virus frames "
                "%lld, anchors %lld, err %lld..%lld\n",
                (int)audio_ok(), (long long)(audio_ok() ? audio_.drops() : -1),
                (long long)(audio_ok() ? audio_.filled() : -1),
                (long long)(newest >= 0 && pos >= 0 ? newest - pos : -1), queued_.load(), (long long)marker.load(),
                (long long)aligned_blocks.exchange(0), (long long)read_margin_min.exchange(INT64_MAX),
                (long long)tl_steps.exchange(0), (long long)(newest - last_sound.load()), (long long)host_frames.exchange(0),
                (long long)(stats_newest_ >= 0 ? newest - stats_newest_ : -1), (long long)anchors.exchange(0),
                (long long)err_min.exchange(INT64_MAX), (long long)err_max.exchange(INT64_MIN));
            stats_newest_ = newest;
            int pk[6];
            if (audio_ok() && audio_.raw_peaks(44100, pk)) // the last second per raw channel (0/1 in, 2/3 USB 1, 4/5 USB 2)
                log("stats: raw peaks %d %d | %d %d | %d %d\n", pk[0], pk[1], pk[2], pk[3], pk[4], pk[5]);
            int64_t sc_f = sc_frames.exchange(0), sc_y = sc_yields.exchange(0);
            if (sc_f || sc_y)
                log("stats: sidechain to the Virus %lld frames, input peak %d, refills %lld, rate %+lld ppm, queued "
                    "%lld..%lld, paused for another writer %lld\n",
                    (long long)sc_f, sc_in_peak.exchange(0), (long long)sc_refills.exchange(0),
                    (long long)sc_corr_ppm.load(), (long long)sc_queued_min.exchange(INT64_MAX),
                    (long long)sc_queued_max.exchange(INT64_MIN), (long long)sc_y);
        }
        // the broker may refuse the audio start right after another session ended (stress test: every second
        // quick re-open had no sound): retry every 500 ms while connected without audio
        if (!audio_ok() && present_seen_ == 1 && std::chrono::steady_clock::now() >= audio_retry_) {
            audio_retry_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
#ifdef __APPLE__
            int r = 1;
#else
            int r = broker_.audio(true);
#endif
            if (r == 1 && audio_.open())
                audio_gate_.open();
            if (audio_ok())
                fprintf(stderr, "Virus TI native: audio streaming (retry)\n");
        }
        if (int b = blocked_.load(std::memory_order_relaxed); b != blocked_logged_) {
            log("Virus TI native: dropped %d message(s) rule 2 forbids (multi header 72 xx 00-03 or F4; sendguard.h)\n",
                b - blocked_logged_);
            blocked_logged_ = b;
        }
        if (int lost = rt_lost_.load(std::memory_order_relaxed); lost != rt_lost_logged_) {
            log("Virus TI native: %d event(s) lost, the queue from process() was full\n", lost - rt_lost_logged_);
            rt_lost_logged_ = lost;
        }
        if (rt_lost_off_.exchange(false, std::memory_order_relaxed))
            for (uint8_t ch = 0; ch < 16; ch++) { // a note-off was among them: nothing may keep sounding
                send({(uint8_t)(0xB0 | ch), 64, 0});
                send({(uint8_t)(0xB0 | ch), 123, 0});
            }
        if (!piece.empty()) { // a piece of a dump (dumppacer.h)
            bool wok = broker_.write(EP_PLUGIN_OUT, piece.data(), piece.size());
            if (piece_last && dump_pending_) {
                dump_pending_ = false;
                queued_--;
            }
            if (piece_last && !held_.empty()) { // automation held during the dump goes first now (cable 2)
                auto held = held_.take();
                std::lock_guard<std::mutex> lk(mu_);
                for (auto it = held.rbegin(); it != held.rend(); ++it) {
                    queue_.push_front({std::move(*it), false, CABLE_PLUGIN});
                    queued_++;
                }
            }
            if (!wok)
                lose_broker();
            continue;
        }
        if (piece_wait_ns > 0) { // the next piece is not due yet and nothing else is waiting
            std::this_thread::sleep_for(std::chrono::nanoseconds(std::min<int64_t>(piece_wait_ns, 2000000)));
            continue;
        }
        if (msg.empty())
            continue;
        // channel messages and clock ticks carry a time stamp: F5 lo hi on cable 5 = the frame on the Virus timeline
        // (docs/protocol.md "timing"); process() stamps its events (and, since 0.8.27, its automation: stamprule.h),
        // other threads use the latest timeline position
        pk_.clear();
        StampRun stamps; // one F5 per stamp value in this write (stamprule.h)
        auto pack = [&](const std::vector<uint8_t> &m, int64_t st) {
            bool from_process = st >= 0;
            if (st < 0)
                st = timeline.load();
            if (st < 0 && audio_ok())
                st = audio_.newest_frame();
            int64_t raw = st;
            bool ping = m.size() == 11 && m[6] == 0x73 && m[8] == 0x22;
            if (int64_t mk = marker.load(); st >= 0 && mk >= 0 && !ping)
                st = st + lead.load() - mk; // ring frame st + lead on the Virus' stamp clock (0 at the marker)
            if (env_debug())
                fprintf(stderr, "send %02x stamp %lld newest %lld\n", m[0], (long long)st,
                        (long long)(audio_ok() ? audio_.newest_frame() : -1));
            if (st >= 0 && needs_stamp(m.data(), m.size(), from_process) && (ping || stamps.fresh(st))) {
                auto t = usbmidi_pack({0xF5, (uint8_t)(st & 0x7F), (uint8_t)((st >> 7) & 0x7F)}, CABLE_TIME);
                pk_.insert(pk_.end(), t.begin(), t.end());
            }
            auto ev = usbmidi_pack(m, cable);
            pk_.insert(pk_.end(), ev.begin(), ev.end());
            if (klog_ && st >= 0 && m.size() == 3 && (m[0] & 0xF0) == 0x90 && m[2] && klog_stamps_.size() < 64)
                klog_stamps_.push_back({raw, audio_ok() ? audio_.newest_frame() : -1, 0, 0});
        };
        pack(msg, stamp);
        // the rest of a block's events in the same write (one broker message instead of one per event); the
        // fairness rule stays: after 8 in a row the queue gets its turn in the next round
        if (stamp >= 0 && !pending_send_) {
            std::lock_guard<std::mutex> lk(mu_);
            // one write holds at most the budget's balance (sendbudget.h): the rest of a flood goes in later rounds
            const auto now = std::chrono::steady_clock::now();
            const int64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
            const size_t cap = std::min<size_t>(3072, (size_t)std::max(0, budget_.available(now_ns)));
            const bool q_ready = queue_ready(dump_, now_ns, queue_.empty(), now < queue_hold_until_);
            for (size_t r = rt_r_.load(std::memory_order_relaxed);
                 r != rt_w_.load(std::memory_order_acquire) && pk_.size() < cap && rt_run_.events_first(q_ready);
                 r = rt_r_.load(std::memory_order_relaxed)) {
                const Slot &sl = rt_[r % RT_SLOTS];
                msg.assign(sl.data, sl.data + sl.len);
                int64_t st = sl.stamp;
                rt_r_.store(r + 1, std::memory_order_release);
                rt_run_.event();
                if (dump_.active() && HeldParams::held(msg.data(), msg.size())) { // automation: after the dump
                    held_.add(msg);
                    continue;
                }
                pack(msg, st);
            }
        }
        bool ok = broker_.write(EP_PLUGIN_OUT, pk_.data(), pk_.size());
        if (stamp >= 0) // stamped traffic counts against the budget; queued SysEx does not (sendbudget.h, E266)
            budget_.take((int)pk_.size());
        if (msg.size() > 7 && msg[0] == 0xF0 && msg[6] == 0x11) { // a multi dump (11 00 00 ...)
            static const int pause = getenv("VTI_NATIVE_MULTI_PAUSE") ? atoi(getenv("VTI_NATIVE_MULTI_PAUSE")) : 250;
            queue_hold_until_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(pause);
        }
        if (klog_)
            klog_check();
        if (pending_send_) {
            pending_send_ = false;
            queued_--;
        }
        if (!ok)
            lose_broker();
    }
}

// ---- VirusPlugin ----------------------------------------------------------------------------------------------------

tresult PLUGIN_API VirusPlugin::initialize(FUnknown *context)
{
    tresult r = SingleComponentEffect::initialize(context);
    if (r != kResultOk)
        return r;
    // buses as the original: audio in "Dummy In" + "Sidechain", event in "Midi In" 16 ch, audio out "Out 1".."Out 3"
    addAudioInput(STR16("Dummy In"), SpeakerArr::kStereo);
    addAudioInput(STR16("Sidechain"), SpeakerArr::kStereo, BusTypes::kAux, 0);
    addEventInput(STR16("Midi In"), 16);
    addAudioOutput(STR16("Out 1"), SpeakerArr::kStereo);
    addAudioOutput(STR16("Out 2"), SpeakerArr::kStereo, BusTypes::kMain, 0);
    addAudioOutput(STR16("Out 3"), SpeakerArr::kStereo, BusTypes::kMain, 0);

    std::string err;
    files_ = AccessFiles::open("", err);
    auto &files = files_;
    std::vector<AutomationEntry> autom;
    if (!files || !dev_.load(files->get("remotedevice.xml"), err) || !load_automation(files->get("automation.xml"), autom, err)) {
        // without the user's Access installation there is nothing to show; the host still gets a valid instance
        fprintf(stderr, "Virus TI native: %s\n", err.c_str());
        return kResultOk;
    }
    single_template_ = dev_.message("VirusTI_Singledump");
    ui_state_["VirusControl/InitialSyncProgress"] = 127; // exist before the sync thread updates them
    ui_state_["VirusControl/RecallProgress"] = 127;
    load_banks();
    load_remote_templates();
    if (ed_.load(files->get("editordevice.xml"), err) && layout_.load(files->get("editor.xml"), err))
        cond_ = std::make_unique<Conditions>(dev_, ed_);
    else
        fprintf(stderr, "Virus TI native: no editor (%s)\n", err.c_str());
    // a new instance's state like the original's: empty record, the fixed multi, 16 parts = the init patch,
    // Output Control 2/3 globals, 14 controllers x 16 channels (formats.md)
    auto multi = files->init_multi();
    auto init = files->init_single();
    if (single_template_ && init.size() == 524) { // which parameters a part's single dump carries
        std::map<std::string, int> v;
        std::string e;
        auto d = init;
        RemoteDevice::upgrade_single(d);
        if (dev_.decode_dump(*single_template_, d, v, e))
            for (auto &kv : v)
                dump_paths_.insert(kv.first);
    }
    if (multi.size() == 267 && init.size() == 524) {
        RemoteDevice::upgrade_single(init);
        default_.push_back({});
        auto smulti = multi; // the original's state keeps the multi with bytes 25..40 and the checksum zeroed
        std::fill(smulti.begin() + 25, smulti.begin() + 41, 0);
        smulti[265] = 0;
        default_.push_back(smulti);
        for (int p = 0; p < 16; p++) {
            auto d = init;
            d[7] = 0, d[8] = (uint8_t)p;
            unsigned a = 0;
            for (int i = 5; i < 265; i++)
                a += d[i];
            d[265] = a & 0x7F;
            for (int i = 265; i < 522; i++)
                a += d[i];
            d[522] = a & 0x7F;
            default_.push_back(d);
        }
        for (int i = 0; i < 2; i++)
            default_.push_back({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x73, 0x00, 0x2D, 0x02, 0xF7});
        static const uint8_t ccs[14][2] = {{1, 0},  {2, 0},   {3, 0},  {4, 0},  {6, 0},  {7, 100}, {8, 64},
                                           {9, 0},  {11, 127}, {12, 0}, {13, 0}, {14, 0}, {15, 0},   {16, 0}};
        for (auto &cc : ccs)
            for (int ch = 0; ch < 16; ch++)
                default_.push_back({(uint8_t)(0xB0 | ch), cc[0], cc[1]});
    }
    // the part dumps in the current format (E174: the Virus would convert old ones and reset the newer fields)
    upgrade_state_dumps(default_);
    for (auto &e : autom) {
        char title[128], units[16];
        snprintf(title, sizeof title, "[%d] %s", e.part, automation_title(dev_, e).c_str());
        snprintf(units, sizeof units, "Part %02d/", e.part);
        parameters.addParameter(UString128(title), UString128(units), 0, 0.0, ParameterInfo::kCanAutomate, e.id);
        params_[(ParamID)e.id] = {e.path, e.part - 1};
        ids_[{e.path, e.part - 1}] = (ParamID)e.id;
    }
    for (int ch = 0; ch < 16; ch++) {
        parameters.addParameter(STR16("Aftertouch"), STR16("Aftertouch"), 0, 0.0, ParameterInfo::kCanAutomate,
                                MIDI_MAP_BASE + ch * 0x1000 + 0x80);
        parameters.addParameter(STR16("Pitchbend"), STR16("Pitchbend"), 0, 0.5, ParameterInfo::kCanAutomate,
                                MIDI_MAP_BASE + ch * 0x1000 + 0x81);
    }
    out_ = std::make_unique<DeviceOut>();
    out_->lead = lead();
    if (diag_enabled())
        out_->set_diag_path(own_cache_dir() + "/../diag.log");
    out_->on_panel([this](const std::vector<uint8_t> &m) { panel_message(m); });
    out_->on_idle([this] {
        if (int lay = sc_layout_.load(std::memory_order_relaxed); lay >= 0 && lay != sc_layout_seen_) {
            sc_layout_seen_ = lay; // process() stores it, the worker logs a change once
            const int layout = lay / 4, n_in = layout / 10000, bus = layout / 100 % 100 - 1, ch = layout % 100;
            out_->log("Virus TI native: sidechain from input bus %d of %d passed, %d channel(s) (active: %d %d)\n",
                      bus, n_in, ch, lay / 2 % 2, lay % 2);
        }
        drain_param_updates();
        load_reported_presets();
        report_offline();
        tick_sync_alert();
        diag_report(); // also with the editor closed (host rate and block sizes, E187)
    });
    sync_ = std::make_unique<MemorySync>([this](const std::vector<uint8_t> &m) {
        out_->send(m);
        return true;
    });
    out_->on_reply([this](const std::vector<uint8_t> &m) { sync_->feed(m); });
    out_->on_waiting([this](bool waiting) { // worker thread
        // the original's page for a Virus that another program holds (SystemStatus/Error 2 = "VDM not Master":
        // "Virus unavailable"), plus the LCD
        if (waiting)
            set_lcd("Virus in use", "by another plug-in", std::string(), std::string());
        else
            set_lcd("Virus connected", std::string(), std::string(), std::string());
    });
    out_->on_status([this](bool present, bool elsewhere) { // worker thread
        // the original's status pages: "No Virus TI can be found" (3), "Virus unavailable" (2), none (0)
        const DeviceStatus st = device_status(present, elsewhere);
        bool changed = false;
        for (const char *p : {"SystemStatus/Error", "Internal System Info/USB Communication",
                              "Internal System Info/MIDI Communication", "Internal System Info/Audio Communication"})
            changed = ui_state_set(p, device_status_value(st, p)) || changed;
        if (changed)
            ui_dirty_ = true;
    });
    out_->on_device([this](bool present) { // worker thread
        out_->log("Virus TI native: device %s\n", present ? (device_seen_ ? "reconnected" : "connected") : "disconnected");
        if (!present) {
            // not ready until the return's probe: a new audio stream can start before the state goes out again (a
            // broker reconnect: 3 s of flash read in between), and an anchor and sync ping on it would go unanswered
            ready_ = false;
            set_lcd("Virus disconnected", std::string(), std::string(), std::string());
            return;
        }
        if (!device_seen_) { // the first connection: the start-up messages are queued (held until now)
            device_seen_ = true;
            // the start-up check gave up (no Virus then: switched on later, or another program had it): wait for the
            // Virus now like after a reconnect
            if (absent_at_start_)
                after_reconnect();
            return;
        }
        set_lcd("Virus reconnected", std::string(), std::string(), std::string());
        resend_to_virus();
    });
    out_->on_resume([this] { resend_to_virus(); }); // worker thread
    usb_io_ = load_usb_io();
    // the original's start-up: GlobalInit (remote mode, cable numbers, clock sync, ...) + ParamInit, before any state
    for (auto &m : startup_messages())
        out_->send(m);
    return kResultOk;
}

void VirusPlugin::set_usb_io(int value, bool save)
{
    if (value < 0 || value > 2)
        return;
    const int old = usb_io_.exchange(value);
    if (save && value != old)
        if (FILE *f = std::fopen((vti::cc::plugin_dir() + "/usb-io").c_str(), "w")) {
            std::fprintf(f, "%d\n", value);
            std::fclose(f);
        }
    if (value != old && out_)
        out_->log("Virus TI native: USB IO configuration %d (%s)\n", value,
                  value == 1   ? "3 Outs / No Input: Out 3 = USB 3"
                  : value == 2 ? "3 Outs / 1 Input: Out 3 = USB 3 (the plug-in sends no audio to the input)"
                               : "2 Outs / No Input");
    ui_dirty_ = true;
}

// GlobalInit + ParamInit, with this computer's USB IO configuration instead of GlobalInit's 0 - the original sends
// its stored "IO Configuration" the same way on every connection (0x18007ec30)
std::vector<std::vector<uint8_t>> VirusPlugin::startup_messages() const
{
    auto msgs = dev_.init_messages();
    for (auto &m : msgs)
        if (m.size() == 11 && m[6] == 0x73 && m[7] == 0x00 && m[8] == 0x09)
            m[9] = (uint8_t)usb_io_.load();
    return msgs;
}

tresult PLUGIN_API VirusPlugin::terminate()
{
    stop_warm_ = true;
    if (sync_)
        sync_->cancel(); // a request in flight ends now, not after its timeout (review 2026-10-19 M6)
    if (sync_thread_.joinable())
        sync_thread_.join();
    {
        std::lock_guard<std::mutex> g(download_mu_);
        if (download_thread_.joinable())
            download_thread_.join();
    }
    {
        // the worker starts a reconnect thread in after_reconnect(): under the same lock, and not after stop_warm_,
        // so none starts after this join (a joinable std::thread destroyed = std::terminate; review M6)
        std::lock_guard<std::mutex> g(reconnect_mu_);
        if (reconnect_thread_.joinable())
            reconnect_thread_.join();
    }
    out_.reset();
    return SingleComponentEffect::terminate();
}

tresult PLUGIN_API VirusPlugin::setActive(TBool state)
{
    if (state && out_)
        out_->start(); // the first activation connects to the Virus (a plug-in scan never gets here)
    if (out_)
        out_->log("Virus TI native: setActive(%d), state loaded %d\n", (int)state, (int)state_loaded_);
    if (!state && out_ && out_->connected()) { // nothing keeps sounding after the plug-in stops (removed, closed)
        for (uint8_t ch = 0; ch < 16; ch++) {
            out_->send({(uint8_t)(0xB0 | ch), 64, 0});  // sustain pedal up
            out_->send({(uint8_t)(0xB0 | ch), 123, 0}); // all notes off
        }
        for (int i = 0; i < 50 && out_->busy(); i++) // ~32 messages, a few ms; at most 250 ms
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (state && sync_ && !sync_started_.exchange(true)) {
        // the "Starting up" page from the first moment (the original shows it once, right away), not only after the
        // start-up messages; wait_virus_ready fills its ring and hides it when the Virus is ready
        set_recall_progress(0);
        syncing_ = true;
        starting_ = true;
        sync_thread_ = std::thread([this] {
            sync_banks();
            warm_categories();
        });
    }
    if (state && !state_loaded_ && !default_.empty()) { // new instance: the original's default project
        {
            std::lock_guard<std::mutex> lk(state_mu_);
            state_ = default_;
        }
        state_loaded_ = true;
        apply_state_to_params();
        send_state();
    }
    return SingleComponentEffect::setActive(state);
}

tresult PLUGIN_API VirusPlugin::activateBus(MediaType type, BusDirection dir, int32 index, TBool state)
{
    if (type == kAudio && dir == kInput && index >= 0 && index < 2) {
        in_active_[index] = state != 0;
        if (out_)
            out_->log("Virus TI native: input bus %d (%s) %s\n", (int)index, index ? "Sidechain" : "Dummy In",
                      state ? "activated" : "deactivated");
    }
    return SingleComponentEffect::activateBus(type, dir, index, state);
}

tresult PLUGIN_API VirusPlugin::setBusArrangements(SpeakerArrangement *inputs, int32 numIns, SpeakerArrangement *outputs,
                                                   int32 numOuts)
{
    for (int32 i = 0; i < numIns; i++)
        if (inputs[i] != SpeakerArr::kStereo)
            return kResultFalse; // the host keeps our stereo buses (getBusArrangement)
    for (int32 i = 0; i < numOuts; i++)
        if (outputs[i] != SpeakerArr::kStereo)
            return kResultFalse;
    return SingleComponentEffect::setBusArrangements(inputs, numIns, outputs, numOuts);
}

tresult PLUGIN_API VirusPlugin::setupProcessing(ProcessSetup &setup)
{
    block_ = setup.maxSamplesPerBlock;
    rate_ = setup.sampleRate;
    if (resampling()) { // the Virus runs at 44.1 kHz; other host rates get its audio resampled
        const double step = 44100.0 / rate_;
        resampler_.setup(step);
        for (auto &b : rs_src_)
            b.assign((size_t)(block_ * step * 1.01) + 2 * Resampler::TAPS + 8, 0.0f);
        rs_valid_ = false;
    }
    sc_.setup(rate_, std::max(block_, 1));
    sc_buf_.assign((size_t)sc_.max_out() * 2, 0);
    sc_expect_ = sc_seen_ = -1;
    sc_hold_ = 0;
    return SingleComponentEffect::setupProcessing(setup);
}

tresult PLUGIN_API VirusPlugin::canProcessSampleSize(int32 s) { return s == kSample32 ? kResultTrue : kResultFalse; }

tresult PLUGIN_API VirusPlugin::getMidiControllerAssignment(int32 bus, int16 channel, CtrlNumber ctl, ParamID &id)
{
    if (bus != 0 || channel < 0 || channel > 15 || (ctl != kAfterTouch && ctl != kPitchBend))
        return kResultFalse;
    id = MIDI_MAP_BASE + channel * 0x1000 + (ctl == kAfterTouch ? 0x80 : 0x81);
    return kResultTrue;
}

// the part slot of a decoded message: globals and "part none" parameters (selected part, ...) are part -1
int VirusPlugin::state_part(const std::string &path, int part) const
{
    const Parameter *p = dev_.find(path);
    return part > 15 || (p && p->part == "none") ? -1 : part;
}

void VirusPlugin::set_part_value(int part, const std::string &path, int value)
{
    {
        std::lock_guard<std::mutex> lk(values_mu_);
        part_values_[part][path] = value;
        if (changed_.size() < 256)
            changed_.insert(path);
        else
            changed_overflow_ = true;
    }
    values_dirty_ = true; // a partial repaint of the controls showing it (was a full repaint per change)
    if (!single_template_)
        return;
    std::lock_guard<std::mutex> lk(state_mu_);
    if (state_.empty())
        state_ = default_;
    if (part >= 0 && part < 16 && dump_paths_.count(path)) { // inside the part's single dump
        for (auto &r : state_)
            if (is_part_dump(r, part))
                dev_.update_dump(*single_template_, r, {{path, value}});
        return;
    }
    // everything else (selected part, part volume/pan, globals) lives as single messages in the state: replace the
    // message of this parameter, else add one - otherwise a patch load or project reload brought the old value back
    // (user test: part 3 selected, load a patch -> back on part 1)
    auto msg = dev_.encode(path, value, part < 0 ? 0 : part);
    if (msg.empty())
        return;
    // index (path, part) -> record, rebuilt when the record list was replaced or grew (patch / project load); a
    // linear decode of all ~240 records per change cost ~45 us - with automation thousands of times per second
    if (state_index_data_ != state_.data() || state_index_size_ != state_.size()) {
        state_index_.clear();
        for (size_t i = 0; i < state_.size(); i++) {
            const auto &r = state_[i];
            if (r.empty() || r.size() == 524 || r.size() == 267 || (r.size() > 1 && r[1] == 0x7D))
                continue;
            for (auto &c : dev_.decode(r))
                state_index_[c.path + '#' + std::to_string(state_part(c.path, c.part))] = i;
        }
        state_index_data_ = state_.data();
        state_index_size_ = state_.size();
    }
    // the same part slot as the index (globals: -1); with the raw part a global reported by the Virus (part 16 or
    // its SysEx part byte) was never found and appended again on every load/save cycle (+45 bytes, issue #5)
    std::string key = path + '#' + std::to_string(state_part(path, part));
    auto it = state_index_.find(key);
    if (it != state_index_.end() && it->second < state_.size()) {
        state_[it->second] = msg;
        return;
    }
    state_.push_back(msg);
    state_index_[key] = state_.size() - 1;
    state_index_data_ = state_.data();
    state_index_size_ = state_.size();
}

static std::string own_cache_dir();

bool VirusPlugin::diag_enabled()
{
    static int on = -1;
    if (on < 0) {
        std::error_code ec;
        on = std::filesystem::exists(own_cache_dir() + "/../diag.on", ec) ? 1 : 0;
    }
    return on == 1;
}

void VirusPlugin::diag_report()
{
    // called by the editor's timer and the device worker: one of them writes every 10 s
    const int64_t now = now_ms();
    int64_t due = diag_next_ms_.load();
    if (!diag_enabled() || now < due || !diag_next_ms_.compare_exchange_strong(due, now + 10000))
        return;
    long long drops = -1; // the editor's timer or the worker: read under the audio gate (audiogate.h)
    if (out_)
        if (AudioGate::Use ring(out_->audio_gate()); ring)
            drops = (long long)out_->audio().drops();
    if (FILE *f = fopen((own_cache_dir() + "/../diag.log").c_str(), "a")) {
        fprintf(f, "%ld s: draws full %ld partial %ld, draw time %ld ms, dirty ticks %ld, process calls %ld max %ld us,"
                   " over block time %ld, audio %d drops %lld, queued %d, host %.0f Hz block %d (largest %ld)\n",
                (long)std::time(nullptr), diag.full_draws.exchange(0), diag.part_draws.exchange(0),
                diag.draw_us.exchange(0) / 1000, diag.dirty_ticks.exchange(0), diag.process_calls.exchange(0),
                diag.process_max_us.exchange(0), diag.process_over.exchange(0),
                out_ ? (int)out_->audio_ready() : -1, drops,
                out_ ? (int)out_->busy() : -1, rate_, block_, diag.block_max.exchange(0));
        fclose(f);
    }
}

tresult PLUGIN_API VirusPlugin::process(ProcessData &data)
{
    struct ProcessTimer { // field diagnostics: duration of every process() call
        VirusPlugin *p;
        int samples;
        std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
        ~ProcessTimer()
        {
            long us = (long)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count();
            p->diag.process_calls++;
            long m = p->diag.process_max_us.load();
            while (us > m && !p->diag.process_max_us.compare_exchange_weak(m, us)) {
            }
            if (us > (long)(samples * 1000000.0 / (p->rate_ > 0 ? p->rate_ : 44100)))
                p->diag.process_over++;
            long b = p->diag.block_max.load();
            while (samples > b && !p->diag.block_max.compare_exchange_weak(b, samples)) {
            }
        }
    } process_timer{this, data.numSamples};
    host_playing_.store(data.processContext && (data.processContext->state & ProcessContext::kPlaying),
                        std::memory_order_relaxed); // the sync error page: shown while playing, cleared by Play
    if ((data.processContext && (data.processContext->state & ProcessContext::kPlaying)) ||
        (data.inputEvents && data.inputEvents->getEventCount() > 0))
        last_play_ms_.store(now_ms(), std::memory_order_relaxed);
    // the Virus timeline: block start = newest ring frame - S, continued by the block size, re-anchored on drift
    const int S = timing_s();
    const double ratio = 44100.0 / rate_; // Virus frames per host sample
    int64_t newest = ring_newest();
    if (out_ && out_->generation.load() != generation_seen_) { // the Virus came back: a new audio stream
        generation_seen_ = out_->generation.load();
        tl_valid_ = false;
        stream_start_ = -1;
    }
    int64_t avail = newest; // the ring's newest frame for the reads; offline, `newest` becomes the paced position
    wait_offline(data, ratio, newest, avail); // offline render: at the Virus' pace
    if (newest >= 0) {
        // anchor once the stream has run for ~0.5 s (the first blocks arrive in bursts), then follow drift gently:
        // one frame per block, re-anchor (new song position) only on a large jump (DAW or USB stall)
        if (stream_start_ < 0)
            stream_start_ = newest;
        int64_t target = newest - S, err = target - tl_;
        // in Virus frames, the fractional rest carried over (rounding per block showed 440625 instead of 441000 / 10 s at 48 kHz)
        stat_acc_ += data.numSamples * ratio;
        int64_t whole = (int64_t)stat_acc_;
        stat_acc_ -= (double)whole;
        out_->host_frames.fetch_add(whole, std::memory_order_relaxed);
        if (tl_valid_) {
            if (err < out_->err_min.load(std::memory_order_relaxed))
                out_->err_min.store(err, std::memory_order_relaxed);
            if (err > out_->err_max.load(std::memory_order_relaxed))
                out_->err_max.store(err, std::memory_order_relaxed);
        }
        if (!tl_valid_ && (out_->busy() || !ready_.load(std::memory_order_relaxed)))
            stream_start_ = newest; // wait until the Virus has worked through the start-up messages and the state
        // the sync ping goes with the anchor: like the original, after 1 s of silence on USB 1/2 so its marker can be
        // found (at most 6 s later; without a marker the stamps stay relative to the ring)
        bool quiet = out_->last_sound.load() < newest - 44100;
        bool anchor_now = !tl_valid_ ? newest - stream_start_ > 22050 && (quiet || newest - stream_start_ > 6 * 44100)
                                     : std::llabs(err) > 8192;
        // no marker (the Virus was not silent): ping again once it is quiet and the host is not playing (3 tries)
        if (tl_valid_ && !anchor_now && out_->marker.load() == -2 && !out_->calibrating() && marker_retries_ < 3 && quiet &&
            now_ms() - last_play_ms_.load(std::memory_order_relaxed) > 2000 && !env_no_ping()) {
            marker_retries_++;
            anchor_now = true;
        }
        if (anchor_now) {
            if (env_stats())
                fprintf(stderr, "timeline %s: err %lld newest %lld block %d\n", tl_valid_ ? "re-anchor" : "anchor",
                        (long long)err, (long long)newest, data.numSamples);
            tl_ = target;
            next_tick_ = (double)tl_;
            bool new_stream = !tl_valid_;
            if (new_stream)
                marker_retries_ = 0; // a new stream (start, the Virus came back)
            out_->anchors.fetch_add(1, std::memory_order_relaxed);
            tl_valid_ = true;
            anchored_ = true; // FE + song position at the new anchor, like the original's start
            rs_valid_ = false; // the resampled output jumps with the timeline, like the direct one (E279)
            if (!env_no_ping())
                out_->calibrate(newest, new_stream); // the marker follows the ping sent in this block
        } else if (tl_valid_ && out_->marker.load() >= 0 && std::llabs(err) > 512) {
            // with the marker the Virus' stamp clock is tied to the ring, not to our anchor: jump back to real time
            // (hosts that process ahead in bursts - REAPER's anticipative FX - or stall) without a new FE/F2/ping
            tl_ += err;
            next_tick_ += (double)err;
            out_->anchors.fetch_add(1, std::memory_order_relaxed);
            // the resampled output (host rate != 44.1 kHz) jumps too: steering at 0.2 % took >10 s for the ~1000
            // frames after a render in REAPER - notes late, then catching up (user, L3; E279)
            rs_valid_ = false;
        } else if (tl_valid_ && std::llabs(err) > 256) {
            tl_ += err > 0 ? 1 : -1;
            next_tick_ += err > 0 ? 1 : -1;
            out_->tl_steps.fetch_add(1, std::memory_order_relaxed);
        }
    }
    struct Ev {
        int64_t stamp;
        uint8_t n, m[16];
    };
    // automation takes at most 96 slots and clock/bend/aftertouch a few dozen, so notes always keep 100+ (a dropped
    // note-off would hang a note)
    constexpr int EV_MAX = 256;
    Ev evs[EV_MAX];
    int nev = 0;
    auto add = [&](int64_t stamp, const uint8_t *m, size_t n) {
        if (nev < EV_MAX && n <= 16) {
            evs[nev].stamp = stamp, evs[nev].n = (uint8_t)n;
            memcpy(evs[nev].m, m, n);
            nev++;
        }
    };
    // Virus frames of this block: exact on average (a fractional rest carries over), so other host rates do not drift
    tl_acc_ += data.numSamples * ratio;
    int64_t block_frames = (int64_t)std::floor(tl_acc_ + 1e-9);
    tl_acc_ -= (double)block_frames;
    if (anchored_) {
        anchored_ = false;
        // the Virus anchors its timeline on the song position's arrival: stamping it M frames early puts every later
        // event M frames into the Virus' future, so the stamps are honoured (constant latency) instead of "late"
        static const int M = getenv("VTI_NATIVE_M") ? atoi(getenv("VTI_NATIVE_M")) : 1024;
        static const uint8_t fe = 0xFE;
        add(tl_ - M, &fe, 1);
        // and then every 300 ms (ActiveSensing): the DSP releases every voice ~1 s after the last FE it saw (E237)
        fe_.reset(tl_ - M + fe_.period);
        int spp = 0; // song position (sixteenths) from the host, else 0
        if (data.processContext && (data.processContext->state & ProcessContext::kProjectTimeMusicValid))
            spp = (int)std::max(0.0, data.processContext->projectTimeMusic * 4.0);
        uint8_t f2[3] = {0xF2, (uint8_t)(spp & 0x7F), (uint8_t)((spp >> 7) & 0x7F)};
        add(tl_ - M, f2, 3);
        if (!env_no_ping()) { // the original's sync ping (73 00 22 00), stamped 3072 like the original: the Virus
            // answers with a one-sample marker on USB 2 where its stamp clock is 0 (DeviceOut::scan_audio)
            static const uint8_t ping[11] = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x73, 0x00, 0x22, 0x00, 0xF7};
            add(3072, ping, 11);
        }
    }
    // Play and loop restarts (transport.h): like the original, the song position, the +1 clock tick and the per-part
    // reset at the block's time, so the arpeggiator and clock-synced LFOs restart with the bar
    if (tl_valid_ && data.processContext && (data.processContext->state & ProcessContext::kProjectTimeMusicValid)) {
        const bool playing = (data.processContext->state & ProcessContext::kPlaying) != 0;
        const double tempo_qn = (data.processContext->state & ProcessContext::kTempoValid) ? data.processContext->tempo
                                                                                          : 120.0;
        const double ppq = data.processContext->projectTimeMusic;
        if (transport_.update(playing, ppq, (double)block_frames / 44100.0 * tempo_qn / 60.0)) {
            // F2 and the +1 tick; the original's 16 part messages (6F <part> 7F 00) are left out: on the device they
            // delayed the next note by 29-37 ms, and they only concern parts that use the Atomizer (E280)
            uint8_t f2[3];
            transport_f2((int)std::max(0.0, std::floor(ppq * 4.0 + 1e-6)), f2);
            add(tl_, f2, 3);
            add(tl_, TRANSPORT_TICK, 11);
        }
    }
    if (tl_valid_) { // MIDI clock at the host tempo (120 BPM without a process context), 24 ticks per quarter
        double tempo = clock_tempo(data.processContext && (data.processContext->state & ProcessContext::kTempoValid)
                                       ? data.processContext->tempo
                                       : 120.0); // 0 or negative made this loop endless (hostmidi.h)
        double per_tick = 44100.0 * 60.0 / (tempo * 24.0);
        static const uint8_t f8 = 0xF8;
        while (next_tick_ < (double)(tl_ + block_frames)) {
            add((int64_t)std::lround(next_tick_), &f8, 1);
            next_tick_ += per_tick;
        }
        static const uint8_t fe = 0xFE; // Active Sensing every 300 ms, like the original (E025); see keepalive.h
        fe_.run(tl_ + block_frames, [&](int64_t t) { add(t, &fe, 1); });
    }
    auto at = [&](int32 offset) { return tl_valid_ ? tl_ + (int64_t)std::lround(offset * ratio) : -1; };
    if (data.inputParameterChanges && out_) {
        for (int32 i = 0; i < data.inputParameterChanges->getParameterCount(); i++) {
            IParamValueQueue *q = data.inputParameterChanges->getParameterData(i);
            int32 offset;
            ParamValue v;
            if (!q || q->getPointCount() <= 0 || q->getPoint(q->getPointCount() - 1, offset, v) != kResultOk)
                continue;
            ParamID id = q->getParameterId();
            if (id >= MIDI_MAP_BASE) { // channel aftertouch / pitch bend of channel (id - base) / 0x1000
                uint8_t ch = (uint8_t)(((id - MIDI_MAP_BASE) >> 12) & 15);
                if ((id & 0xFFF) == 0x80) {
                    uint8_t m[2] = {(uint8_t)(0xD0 | ch), data7(v)}; // data bytes always < 0x80 (hostmidi.h)
                    add(at(offset), m, 2);
                } else {
                    int pb = bend14(v);
                    uint8_t m[3] = {(uint8_t)(0xE0 | ch), (uint8_t)(pb & 0x7F), (uint8_t)(pb >> 7)};
                    add(at(offset), m, 3);
                }
                continue;
            }
            auto it = params_.find(id);
            if (it == params_.end())
                continue;
            if (gestures_.echo(id, v, now_ms())) // our own edit of a knob moved on the Virus, handed back: not again
                continue;
            int value = (int)std::lround(v * 127); // the original's automation scale (docs/formats.md)
            if (const Parameter *p = dev_.find(it->second.path))
                value = std::min(std::max(value, p->min), p->max);
            // coalesce: only the latest value per parameter waits for the budget below
            if (it->second.pending >= 0 || automation_.push(id))
                it->second.pending = value;
            size_t w = pu_w_.load(std::memory_order_relaxed); // part dump update: off the audio thread
            if (w - pu_r_.load(std::memory_order_acquire) < PU_SLOTS) {
                pu_[w % PU_SLOTS] = {id, value};
                pu_w_.store(w + 1, std::memory_order_release);
            }
        }
    }
    // send waiting automation within the budget, oldest first (at the block start)
    automation_.refill(data.numSamples, rate_);
    if (out_)
        automation_.drain(96 - nev, [&](uint32_t id) {
            auto it = params_.find(id);
            if (it == params_.end() || it->second.pending < 0)
                return false;
            int value = it->second.pending;
            it->second.pending = -1;
            uint8_t m[16];
            size_t n = dev_.encode_into(it->second.path, value, it->second.part, m, sizeof m);
            if (n)
                add(at(0), m, n);
            return true;
        });
    if (data.inputEvents && out_) {
        for (int32 i = 0; i < data.inputEvents->getEventCount(); i++) {
            Event e;
            if (data.inputEvents->getEvent(i, e) != kResultOk)
                continue;
            if (e.type == Event::kNoteOnEvent && starting_.load(std::memory_order_relaxed))
                continue; // the Virus is still busy with the start-up messages and the state (notes would be late)
            if (e.type == Event::kNoteOnEvent) {
                last_note_ms_.store(now_ms(), std::memory_order_relaxed);
                uint8_t m[3] = {(uint8_t)(0x90 | (e.noteOn.channel & 15)), note7(e.noteOn.pitch),
                                std::max<uint8_t>(1, data7(e.noteOn.velocity))};
                add(at(e.sampleOffset), m, 3);
                auto &mt = meter_[e.noteOn.channel & 15]; // MIDI activity meter: the highest velocity, decays in tick_meters
                if (mt.load(std::memory_order_relaxed) < m[2])
                    mt.store(m[2], std::memory_order_relaxed);
            } else if (e.type == Event::kNoteOffEvent) {
                uint8_t m[3] = {(uint8_t)(0x80 | (e.noteOff.channel & 15)), note7(e.noteOff.pitch),
                                data7(e.noteOff.velocity)};
                add(at(e.sampleOffset), m, 3);
            }
        }
    }
    // to the Virus in time order (clock ticks and events share the timeline); stable: at the same stamp automation
    // stays before the notes, so a note starts with the new values (insertion sort: stable, no allocation on the
    // audio thread, and the events come nearly in order)
    for (int i = 1; i < nev; i++)
        for (int j = i; j > 0 && evs[j].stamp < evs[j - 1].stamp; j--)
            std::swap(evs[j], evs[j - 1]);
    for (int i = 0; out_ && i < nev; i++)
        out_->send_rt(evs[i].m, evs[i].n, evs[i].stamp);
    if (out_ && nev > 0)
        out_->wake_rt();
    int64_t block_start = tl_;
    if (tl_valid_) {
        tl_ += block_frames;
        if (out_)
            out_->timeline = tl_;
    }
    // device -> host: knob moves on the Virus as output parameter changes (the host updates the controller)
    if (data.outputParameterChanges && pending_mu_.try_lock()) {
        for (auto &pc : pending_) {
            int32 idx;
            if (IParamValueQueue *q = data.outputParameterChanges->addParameterData(pc.first, idx))
                q->addPoint(0, pc.second, idx);
        }
        pending_.clear();
        pending_mu_.unlock();
    }
    // audio: Out 1 = USB 1, Out 2 = USB 2 of the Virus (broker ring), Out 3 = USB 3 while the USB IO configuration
    // has 3 outputs (raw channels 0/1 in both "3 Outs" modes, E158/E164), else silent
    for (int32 b = 0; b < data.numOutputs; b++)
        for (int32 c = 0; c < data.outputs[b].numChannels; c++)
            if (data.outputs[b].channelBuffers32 && data.outputs[b].channelBuffers32[c])
                memset(data.outputs[b].channelBuffers32[c], 0, sizeof(float) * data.numSamples);
    // the ring stays mapped while this block reads it and feeds the sidechain (audiogate.h); never waits for the worker
    static AudioGate no_device;
    AudioGate::Use ring(out_ ? out_->audio_gate() : no_device);
    if (ring && data.numOutputs >= 2 && data.outputs[0].numChannels >= 2 &&
        data.outputs[1].numChannels >= 2 && data.numSamples > 0) {
        float **o1 = data.outputs[0].channelBuffers32, **o2 = data.outputs[1].channelBuffers32;
        float **o3 = usb_io_.load(std::memory_order_relaxed) >= 1 && data.numOutputs >= 3 &&
                             data.outputs[2].numChannels >= 2 && data.outputs[2].channelBuffers32 &&
                             data.outputs[2].channelBuffers32[0] && data.outputs[2].channelBuffers32[1]
                         ? data.outputs[2].channelBuffers32
                         : nullptr;
        float *l3 = o3 ? o3[0] : nullptr, *r3 = o3 ? o3[1] : nullptr;
        // a host may pass a bus with null channel pointers (an inactive output): every one is checked (review M8)
        const bool stereo = o1 && o2 && o1[0] && o1[1] && o2[0] && o2[1];
        if (stereo && resampling())
            read_resampled(o1, o2, o3, data.numSamples, ratio, avail,
                           tl_valid_ && out_->marker.load() >= 0
                               ? block_start + lead() - latency_frames()
                               : newest - std::max<int64_t>(2 * block_frames + 256, 768) - Resampler::HALF);
        else if (stereo)
        {
            if (tl_valid_ && out_->marker.load() >= 0) { // stamp-aligned: an event of this block sounds latency() later
                int64_t pos = block_start + lead() - latency_frames();
                if (pos + data.numSamples > avail) // never read frames that are not there yet (silence, clicks)
                    pos = avail - data.numSamples;
                out_->audio().read_at(o1[0], o1[1], o2[0], o2[1], data.numSamples, pos, l3, r3);
                int64_t margin = avail - (pos + data.numSamples); // < 0: frames not there yet (silence)
                if (margin < out_->read_margin_min.load(std::memory_order_relaxed))
                    out_->read_margin_min.store(margin, std::memory_order_relaxed);
                out_->aligned_blocks.fetch_add(1, std::memory_order_relaxed);
            } else
                // at least 768 frames: with 64-sample blocks 2 * 64 + 256 gave a ring re-anchor (click) every ~30 s
                out_->audio().read(o1[0], o1[1], o2[0], o2[1], data.numSamples, std::max(2 * block_ + 256, 768), l3,
                                   r3);
        }
        send_sidechain(data);
    }
    return kResultOk;
}

int64_t VirusPlugin::ring_newest()
{
    if (!out_)
        return -1;
    AudioGate::Use ring(out_->audio_gate());
    return ring ? out_->audio().newest_frame() : -1;
}

// Offline render (export, freeze, offline bounce): the host calls process() as fast as it can, the Virus plays in real
// time. The block waits until the Virus has made its audio (OfflinePacer), and the timeline runs on the paced frame:
// the ring frame seen after a sleep is up to a timer tick (Windows ~15 ms) later than needed and would look like a
// timeline jump. A block smaller than the ring's step runs up to a step early (OfflinePacer::early, E315); `avail`
// is the ring's newest frame then, for the reads. Real-time blocks never wait; a Virus that stops streaming ends the
// wait after 1 s (silence, no hang).
void VirusPlugin::wait_offline(ProcessData &data, double ratio, int64_t &newest, int64_t &avail)
{
    const bool offline = data.processMode == kOffline; // per block: the Audio Unit wrapper sets it only here
    if (offline != offline_) {
        offline_ = offline;
        pacer_.reset();
        offline_start_waited_ = false;
        offline_now_.store(offline, std::memory_order_relaxed);
    }
    if (!offline) {
        pacer_.observe(newest); // the ring's step, known when a render starts (E316)
        return;
    }
    // a render right after the project opened (a freeze, an export at once): wait once, at most 20 s, until the Virus
    // has taken the start-up messages and its audio runs - else the render raced through silence and the start-up
    // dropped the first notes. The broker connection may not be up yet in the first block: up to 3 s for the Virus to
    // show up; a missing Virus or one held by another program renders silence, as before.
    if (!offline_start_waited_ && out_) {
        offline_start_waited_ = true;
        const auto t0 = std::chrono::steady_clock::now();
        auto waiting = [&] {
            const auto t = std::chrono::steady_clock::now() - t0;
            if (out_->waiting() || t > std::chrono::seconds(20))
                return false;
            if (!out_->connected())
                return t < std::chrono::seconds(3);
            return !out_->audio_ready() || !ready_.load(std::memory_order_relaxed) || out_->busy();
        };
        bool waited = false;
        while (waiting()) {
            waited = true;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        newest = avail = ring_newest();
        if (out_->generation.load() != generation_seen_) { // the Virus connected meanwhile: its stream starts here
            generation_seen_ = out_->generation.load();
            tl_valid_ = false;
            stream_start_ = -1;
        }
        // the stream ran steadily and the Virus stayed silent meanwhile: the timeline anchors in this block (the sync
        // ping goes out now, the marker follows ~1 s later) instead of after 0.5 s of render - or 6 s when the
        // render's first notes keep the Virus from being quiet
        if (waited && !tl_valid_ && newest >= 0)
            stream_start_ = newest - 22051;
    }
    if (newest < 0)
        return;
    const double frames = data.numSamples * ratio;
    const int64_t need = pacer_.wait_until(newest, frames);
    if (need < 0)
        return;
    const int64_t early = pacer_.early(frames);
    const int64_t release = need - early;
    if (early > offline_early_max_.load(std::memory_order_relaxed))
        offline_early_max_.store((int)early, std::memory_order_relaxed);
    const auto give_up = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    int64_t f = newest;
    for (; f < release; f = ring_newest()) {
        if (f < 0 || std::chrono::steady_clock::now() > give_up) { // the stream stopped: no pacing until it is back
            pacer_.reset();
            offline_timeouts_.fetch_add(1, std::memory_order_relaxed);
            newest = avail = f;
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    newest = need;
    avail = f;
}

// worker thread: the original's sync error page (SyncAlert, E205) - the transport, Live Mode and its 50 ms tick; the
// page is the editor state "Info Panel/Page" (the skin shows "A sync error occurred. Press Stop and Play to clear.")
void VirusPlugin::tick_sync_alert()
{
    const int64_t now = now_ms();
    std::lock_guard<std::mutex> lk(sync_alert_mu_);
    sync_alert_.play(host_playing_.load(std::memory_order_relaxed));
    sync_alert_.live_mode(ui_value("VirusControl/Live Mode", -1) == 1);
    if (sync_tick_ms_ == 0 || now - sync_tick_ms_ > 1000)
        sync_tick_ms_ = now; // first call, or the worker paused: no burst of ticks
    for (; sync_tick_ms_ + 50 <= now; sync_tick_ms_ += 50)
        sync_alert_.tick();
    if (sync_alert_.take_change()) {
        if (ui_state_set("Info Panel/Page", sync_alert_.page()))
            ui_dirty_ = true;
        out_->log(sync_alert_.page() ? "Virus TI native: sync error shown (%d late events since Play, %d in total)\n"
                                     : "Virus TI native: sync error cleared by Play (%d, %d)\n",
                  sync_alert_.since_play(), sync_alert_.total());
    }
    if (sync_alert_.total() != sync_logged_ && now - sync_log_ms_ >= 10000) { // field diagnostics, at most every 10 s
        sync_logged_ = sync_alert_.total();
        sync_log_ms_ = now;
        out_->log("Virus TI native: %d sync reports from the Virus so far (%d since Play)\n", sync_alert_.total(),
                  sync_alert_.since_play());
    }
}

// worker thread: an offline render in the log and on the LCD, and waits that gave up
void VirusPlugin::report_offline()
{
    const bool now = offline_now_.load(std::memory_order_relaxed);
    if (now != offline_reported_) {
        offline_reported_ = now;
        if (now)
            out_->log("Virus TI native: offline render, paced to the Virus (real-time speed)\n");
        else // the ring's step against the block size: > 0 where small blocks ran early instead of in a burst
            out_->log("Virus TI native: offline render ended (blocks up to %d frames early)\n",
                      offline_early_max_.exchange(0, std::memory_order_relaxed));
        if (now)
            set_lcd("Offline render", "at real-time speed", std::string(), std::string());
    }
    const int t = offline_timeouts_.load(std::memory_order_relaxed);
    if (t != offline_timeouts_reported_) {
        offline_timeouts_reported_ = t;
        out_->log("Virus TI native: offline render: no audio from the Virus for 1 s (%d times), not paced\n", t);
    }
}

// "3 Outs / 1 Input": the Sidechain bus (input bus 1) to the Virus' USB input, as the original does (E167: its VST3
// shell passes only the Sidechain bus on, "Dummy In" is never read; silence without buffers), through a
// rate-controlled converter (SidechainSender) at any host rate. The ring has one writer: if the write position
// moves without us (Windows: the ASIO driver plays the DAW to the Virus), we stay out for 2 s and start again.
void VirusPlugin::send_sidechain(ProcessData &data)
{
    const int n = data.numSamples;
    if (usb_io_.load(std::memory_order_relaxed) != 2 || n <= 0 || n > block_ || sc_buf_.empty()) {
        sc_expect_ = sc_seen_ = -1;
        return;
    }
    AudioIn &audio = out_->audio();
    const int64_t w0 = audio.out_written();
    if (w0 < 0)
        return;
    const bool foreign = sc_expect_ >= 0 ? w0 != sc_expect_ : sc_seen_ >= 0 && w0 != sc_seen_;
    sc_seen_ = w0;
    if (foreign || sc_hold_ > 0) {
        if (foreign) {
            sc_hold_ = (int64_t)(2 * rate_);
            out_->sc_yields.fetch_add(1, std::memory_order_relaxed);
        } else
            sc_hold_ -= n;
        if (sc_hold_ <= 0)
            sc_.reset(); // the next block starts the stream again
        sc_expect_ = -1;
        return;
    }
    // the Sidechain bus: at its declared place, or among the active buses only (hosts that pass only those); a host
    // that activates only the main input (Ableton) delivers the sidechain there
    const float *l = nullptr, *r = nullptr;
    const int bus = sidechain_source_bus(in_active_, data.numInputs); // Ableton: the main input (E172)
    const int ch = bus >= 0 && data.inputs[bus].channelBuffers32 ? data.inputs[bus].numChannels : 0;
    if (ch >= 1) {
        l = data.inputs[bus].channelBuffers32[0];
        r = ch >= 2 ? data.inputs[bus].channelBuffers32[1] : l; // mono: both sides
        if (!r) // a null second channel pointer (review M8)
            r = l;
    }
    // a change of the host's bus layout, logged by the worker (no formatting or file I/O here: review M4)
    sc_layout_.store(((data.numInputs * 10000 + (bus + 1) * 100 + ch) * 2 + (int)in_active_[0]) * 2 + (int)in_active_[1],
                     std::memory_order_relaxed);
    if (int pk = (int)(sidechain_peak(l, r, n) * 32767.0f); pk > out_->sc_in_peak.load(std::memory_order_relaxed))
        out_->sc_in_peak.store(pk, std::memory_order_relaxed);
    const int64_t q = audio.out_queued();
    if (q < out_->sc_queued_min.load(std::memory_order_relaxed))
        out_->sc_queued_min.store(q, std::memory_order_relaxed);
    if (q > out_->sc_queued_max.load(std::memory_order_relaxed))
        out_->sc_queued_max.store(q, std::memory_order_relaxed);
    const int64_t refills = sc_.refills();
    const int w = sc_.process(l, r, n, q, sc_buf_.data());
    audio.write_out(sc_buf_.data(), w);
    sc_expect_ = w0 + w;
    out_->sc_frames.fetch_add(w, std::memory_order_relaxed);
    out_->sc_refills.fetch_add(sc_.refills() - refills, std::memory_order_relaxed);
    out_->sc_corr_ppm.store(std::llround(sc_.correction() * 1e6), std::memory_order_relaxed);
}

// host rate != 44.1 kHz: n output samples from the Virus frame `target` on, resampled. The read position moves on
// smoothly and steers towards the target by at most 0.2 % in speed while the error is small (the timeline's drift
// steps), so the audio has no clicks; on a jump of the timeline it jumps too.
void VirusPlugin::read_resampled(float **o1, float **o2, float **o3, int n, double ratio, int64_t newest,
                                 int64_t target)
{
    double err = (double)target - rs_pos_;
    // a jump beyond 128 frames (~3 ms): the timeline moved (anchor, marker, a burst) - follow it at once like the
    // direct path; steering is for the timeline's drift steps of one frame per block (E280: 2048 left the first
    // notes after a start ~9 ms late for ~7 s)
    if (!rs_valid_ || std::fabs(err) > 128) {
        rs_pos_ = (double)target;
        rs_valid_ = true;
        err = 0;
    }
    double adj = err / (64.0 * std::max(1.0, n * ratio)); // closes the error over ~64 blocks
    adj = std::max(-0.002, std::min(0.002, adj));
    const double step = ratio * (1.0 + adj);
    double end = rs_pos_ + (n - 1) * step + Resampler::HALF; // never read frames that are not there yet
    if (end > (double)(newest - 1))
        rs_pos_ -= end - (double)(newest - 1);
    int64_t first;
    int count;
    Resampler::frames_needed(rs_pos_, step, n, first, count);
    if (count > (int)rs_src_[0].size()) { // a block larger than announced: read the beginning only (silence after)
        count = (int)rs_src_[0].size();
        n = std::max(0, std::min(n, (int)((count - 2 * Resampler::HALF) / step)));
    }
    out_->audio().read_at(rs_src_[0].data(), rs_src_[1].data(), rs_src_[2].data(), rs_src_[3].data(), count, first,
                          o3 ? rs_src_[4].data() : nullptr, o3 ? rs_src_[5].data() : nullptr);
    float *dst[6] = {o1[0], o1[1], o2[0], o2[1], o3 ? o3[0] : nullptr, o3 ? o3[1] : nullptr};
    for (int c = 0; c < (o3 ? 6 : 4); c++)
        resampler_.process(rs_src_[c].data(), first, rs_pos_, step, dst[c], n);
    int64_t margin = newest - (int64_t)std::ceil(rs_pos_ + (n - 1) * step + Resampler::HALF);
    if (margin < out_->read_margin_min.load(std::memory_order_relaxed))
        out_->read_margin_min.store(margin, std::memory_order_relaxed);
    out_->aligned_blocks.fetch_add(1, std::memory_order_relaxed);
    rs_pos_ += n * step;
}

void VirusPlugin::drain_param_updates()
{
    // at most every 5 ms, and per parameter only the latest value (automation arrives up to every block for each
    // lane - the editor and the state only need the newest)
    auto now = std::chrono::steady_clock::now();
    if (now < drain_next_)
        return;
    drain_next_ = now + std::chrono::milliseconds(5);
    latest_.clear();
    for (size_t r = pu_r_.load(std::memory_order_relaxed); r != pu_w_.load(std::memory_order_acquire); r++) {
        const ParamUpdate u = pu_[r % PU_SLOTS];
        pu_r_.store(r + 1, std::memory_order_release);
        latest_[u.id] = u.value;
    }
    for (auto &kv : latest_) {
        auto it = params_.find(kv.first);
        if (it != params_.end())
            set_part_value(it->second.part, it->second.path, kv.second);
    }
}

IPlugView *PLUGIN_API VirusPlugin::createView(FIDString name)
{
    if (FIDStringsEqual(name, ViewType::kEditor) && files_ && cond_)
        return new VirusEditor(this);
    return nullptr;
}

std::string VirusPlugin::softknob_target(const std::string &path, int part)
{
    static const std::string prefix = "Softknobs/Softknob ";
    if (path.size() <= prefix.size() || path.compare(0, prefix.size(), prefix) != 0)
        return {};
    // only real device parameters come back, so a target is never a soft knob itself (no recursion)
    return dev_.softknob_parameter(ui_value("Soft Knobs/Destination " + path.substr(prefix.size()), part));
}

int VirusPlugin::ui_value(const std::string &path, int part)
{
    if (path == USB_IO_PATH)
        return usb_io_.load(); // this computer's setting, not the project's (menu, "Third USB Active" conditions)
    if (std::string t = softknob_target(path, part); !t.empty())
        return ui_value(t, part); // the soft knob shows its target's value (original: 0x1800a2890)
    int browser_bank;
    // the original's internal browser parameters: browser N shows RAM bank N until changed
    if (path.rfind("Browser ", 0) == 0 && path.find("/Selected Bank") != std::string::npos &&
        !ui_state_get(path, browser_bank))
        return path[8] - '0';
    if (int sel; path == "Remote Templates/Selected Template" && !ui_state_get(path, sel))
        return -1; // REMOTE page: nothing selected until a click, like the original (E193)
    if (path == "Remote Templates/MIDI Channel Behaviour") { // the selected template's flag (E196)
        int sel = ui_value("Remote Templates/Selected Template", -1);
        std::lock_guard<std::mutex> lk(remote_mu_);
        return remote_.channel_enabled(sel < 0 ? 0 : sel) ? 1 : 0;
    }
    const Parameter *param = cond_ ? cond_->find(path) : nullptr;
    if (param && param->part == "none")
        part = -1; // global parameters (Panel Part, globals) are not per part
    if (param && !dev_.find(path) && param->part == "all") { // per-part editor values and meta parameters
        std::lock_guard<std::mutex> lk(values_mu_);
        auto &store = param->meta ? meta_vals_ : ed_part_;
        auto it = store.find({path, part < 0 ? 0 : part});
        return it != store.end() ? it->second : param->def;
    }
    if (int v; ui_state_get(path, v))
        return v;
    // stored values outside the parameter's range are shown clamped like the original, mapped lists' values as they
    // are (RemoteDevice::shown)
    auto clamp = [this, &path, param](int v) {
        if (dev_.find(path))
            return dev_.shown(path, v);
        return param && param->max > param->min ? std::max(param->min, std::min(param->max, v)) : v;
    };
    {
        std::lock_guard<std::mutex> lk(values_mu_);
        for (int p : {part, -1}) {
            auto pv = part_values_.find(p);
            if (pv != part_values_.end()) {
                auto v = pv->second.find(path);
                if (v != pv->second.end())
                    return clamp(v->second);
            }
        }
    }
    return cond_ ? cond_->default_value(path) : 0;
}

std::string VirusPlugin::live_text(const std::string &param)
{
    if (param.rfind("Remote Templates/", 0) == 0) { // REMOTE page (E196); before values_mu_ (ui_value locks it)
        int sel = ui_value("Remote Templates/Selected Template", -1), knob = ui_value("Remote Templates/Selected Knob", -1);
        std::lock_guard<std::mutex> lk(remote_mu_);
        if (std::string r = remote_text(param, remote_, sel, knob); !r.empty())
            return r;
    }
    if (param.rfind("Browser ", 0) == 0 && param.find("/Selected Bank") != std::string::npos) // the bank menu
        return bank_title(ui_value(param, -1));
    if (param == "Repository Actions/Category Filter") {
        int c = ui_value(param, -1);
        if (c == 0)
            return "";
        auto vl = dev_.valuelists.find("Val Name Categories");
        return vl != dev_.valuelists.end() ? category_name_() : std::to_string(c);
    }
    if (param == "Repository Actions/Search")
        return search();
    std::lock_guard<std::mutex> lk(values_mu_);
    if (param == "VirusControl/Display Parameter")
        return lcd_param_;
    if (param == "VirusControl/Display Value")
        return lcd_value_;
    if (param == "VirusControl/Display Part")
        return lcd_part_;
    if (param == "VirusControl/Display Original Value")
        return lcd_orig_;
    sysinfo_.broker = out_ ? out_->broker_version() : std::string(); // the broker's INFO message ("" from older ones)
    sysinfo_.tios = out_ ? out_->tios() : std::string();             // read from the Virus' flash at connect
    return system_info_text(param, sysinfo_);   // the INFO box's versions, "" for anything else
}

void VirusPlugin::ui_set(const std::string &path, int part, int value)
{
    if (path == USB_IO_PATH) { // a hardware setting of this computer (original: registry), sent at once
        set_usb_io(value, true);
        if (out_)
            if (auto msg = dev_.encode(path, usb_io_.load(), 0); !msg.empty())
                out_->send(msg);
        return;
    }
    if (std::string t = softknob_target(path, part); !t.empty()) {
        ui_set(t, part, value); // turning a soft knob edits its target (original: 0x1800a2840)
        return;
    }
    if (path == "Remote Templates/MIDI Channel Behaviour") { // REMOTE page: the template's flag, saved and sent
        int sel = ui_value("Remote Templates/Selected Template", -1);
        std::vector<uint8_t> rec;
        {
            std::lock_guard<std::mutex> lk(remote_mu_);
            remote_.set_channel_enabled(sel < 0 ? 0 : sel, value != 0);
            save_remote_templates();
            rec = remote_edit_record(path, remote_, sel, 0);
        }
        send_remote({rec});
        ui_dirty_ = true;
        return;
    }
    if (!dev_.find(path) && !ed_.find(path)) { // the original's internal parameters (browser state)
        if (path == "Repository Actions/Category Filter") { // the original shows it on the LCD like a parameter
            std::string before = category_name_();
            ui_state_set(path, value);
            std::string name = path;
            std::replace(name.begin(), name.end(), '/', ' ');
            set_lcd(name, category_name_(), "Part " + std::to_string(ui_value(layout_.frame_parameter, -1) + 1), before);
            update_results();
            return;
        }
        ui_state_set(path, value);
        ui_dirty_ = true;
        return;
    }
    if (const Parameter *p = meta_depth_ ? nullptr : dev_.find(path)) { // LCD: the edited parameter (not linked ones)
        auto text = [&](int val) {
            std::string v = std::to_string(val);
            for (const RemoteDevice *d : {&dev_, &ed_}) {
                auto vl = d->valuelists.find(p->valuelist);
                if (vl != d->valuelists.end())
                    if (const Valuelist::Entry *en = vl->second.find(val))
                        if (!en->text.empty())
                            v = en->text;
            }
            return v;
        };
        std::string before = text(ui_value(path, part));
        set_lcd(p->displayname.empty() ? path : p->displayname, text(value),
                p->part == "none" ? std::string() : "Part " + std::to_string(part + 1), before);
    }
    if (const Parameter *p = cond_ ? cond_->find(path) : nullptr)
        if (p->part == "none")
            part = -1;
    if (const Parameter *ep = ed_.find(path); ep && !dev_.find(path)) {
        if (ep->meta) { // e.g. the LFO clock menus: set the meta value and pass it to its clients
            std::lock_guard<std::recursive_mutex> mk(meta_mu_);
            meta_set(*ep, part < 0 ? 0 : part, std::max(ep->min, std::min(ep->max, value)), std::string(), true);
            meta_retrigger(part < 0 ? 0 : part, true);
        } else if (ep->part == "all") { // per-part editor values (FX switches, link buttons, helpers)
            set_editor_value(path, part, value, true);
            std::lock_guard<std::recursive_mutex> mk(meta_mu_);
            meta_source(path, part < 0 ? 0 : part, value, true);
        } else
            ui_state_set(path, value); // editor state (page selection, ...)
        ui_dirty_ = true;
        return;
    }
    auto id = ids_.find({path, part});
    if (id != ids_.end()) { // automatable: through the host, which hands it to process()
        ParamValue n = value / 127.0;
        beginEdit(id->second);
        setParamNormalized(id->second, n);
        performEdit(id->second, n);
        endEdit(id->second);
    } else if (out_) { // not automatable (menus, globals): straight to the Virus
        auto msg = dev_.encode(path, value, part < 0 ? 0 : part);
        if (!msg.empty())
            out_->send(msg);
    }
    set_part_value(part, path, value);
    if (env_debug())
        fprintf(stderr, "ui_set %s part %d = %d -> parts 0..2: %d %d %d\n", path.c_str(), part, value,
                ui_value(path, 0), ui_value(path, 1), ui_value(path, 2));
    if (part >= 0) { // linked parameters (filter link, FX switches, LFO clocks)
        std::lock_guard<std::recursive_mutex> mk(meta_mu_);
        meta_source(path, part, value, true);
    }
}

// ---- meta parameters (editordevice.xml): a source client sets the meta value (client value -> meta through the
// maps or (v - b) / a), the meta passes it to its trigger clients (maps or a * meta + b) while its condition holds;
// "retrigger" metas fire again when their condition becomes true (FX switch on/off, LFO clock switch).
// full = false (changes reported by the Virus, loaded states): only editor-side clients are updated.

void VirusPlugin::set_editor_value(const std::string &path, int part, int value, bool persist)
{
    part = part < 0 ? 0 : part;
    {
        std::lock_guard<std::mutex> lk(values_mu_);
        ed_part_[{path, part}] = value;
        if (changed_.size() < 256)
            changed_.insert(path);
        else
            changed_overflow_ = true;
        values_dirty_ = true;
    }
    // persist the user's own settings (link buttons, clicked in the editor); values the meta engine derives from
    // the patch (Filter Link from Filters/Select, FX switches) are not stored
    const Parameter *p = ed_.find(path);
    if (!persist || !p || p->group != "VirusControl Links")
        return;
    std::lock_guard<std::mutex> lk(state_mu_);
    if (state_.empty())
        state_ = default_;
    auto rec = editor_record(path, part, value);
    for (auto &r : state_) {
        std::string rp;
        int rpart, rv;
        if (parse_editor_record(r, rp, rpart, rv) && rp == path && rpart == part) {
            r = rec;
            return;
        }
    }
    state_.push_back(rec);
}

bool VirusPlugin::meta_cond(const Parameter &m, int part)
{
    if (m.condition.empty() || !cond_)
        return true;
    return cond_->eval(m.condition, [this](const std::string &p, int pt) { return ui_value(p, pt); }, part);
}

void VirusPlugin::meta_set(const Parameter &m, int part, int mv, const std::string &except, bool full)
{
    {
        std::lock_guard<std::mutex> lk(values_mu_);
        meta_vals_[{m.path, part}] = mv;
    }
    if (meta_depth_ > 6 || !meta_cond(m, part))
        return;
    meta_depth_++;
    for (auto &c : m.clients) {
        if (c.notrigger || c.path == except)
            continue;
        const Parameter *cp = cond_ ? cond_->find(c.path) : nullptr;
        int cv = meta_to_client(c, mv);
        if (cp)
            cv = std::max(cp->min, std::min(cp->max, cv));
        if (ui_value(c.path, part) == cv)
            continue;
        if (dev_.find(c.path)) {
            if (full)
                ui_set(c.path, part, cv); // to the Virus (and on to further metas)
            else
                set_part_value(part, c.path, cv);
        } else if (cp && cp->meta) {
            meta_set(*cp, part, cv, std::string(), full);
        } else {
            set_editor_value(c.path, part, cv);
            meta_source(c.path, part, cv, full);
        }
    }
    meta_depth_--;
}

void VirusPlugin::meta_source(const std::string &path, int part, int value, bool full)
{
    if (meta_depth_ > 6)
        return;
    meta_index();
    auto src = meta_sources_.find(path);
    if (src != meta_sources_.end())
        for (auto [m, c] : src->second) {
            if (!m->catch_always && !meta_cond(*m, part))
                continue;
            int mv;
            if (!client_to_meta(*c, value, mv))
                continue;
            mv = std::max(m->min, std::min(m->max, mv));
            meta_set(*m, part, mv, path, full);
        }
    meta_retrigger(part, full);
}

// path -> (meta, client) of the source clients, and the retrigger metas (meta_mu_; built once - meta_source ran
// through all editor-device parameters on every change before)
void VirusPlugin::meta_index()
{
    if (meta_indexed_)
        return;
    for (auto &m : ed_.parameters) {
        if (!m.meta)
            continue;
        for (auto &c : m.clients)
            if (!c.nosource)
                meta_sources_[c.path].push_back({&m, &c});
        if (m.retrigger)
            meta_retriggers_.push_back(&m);
    }
    meta_indexed_ = true;
}

void VirusPlugin::meta_retrigger(int part, bool full)
{
    meta_index();
    for (const Parameter *mp : meta_retriggers_) {
        const Parameter &m = *mp;
        bool now = meta_cond(m, part), before;
        {
            std::lock_guard<std::mutex> lk(values_mu_);
            auto it = meta_cond_.find({m.path, part});
            before = it == meta_cond_.end() ? now : it->second;
            meta_cond_[{m.path, part}] = now;
        }
        if (now && !before)
            meta_set(m, part, ui_value(m.path, part), std::string(), full);
    }
}

// after loading a state: meta values from their first source client, passed to the editor-side clients only
void VirusPlugin::meta_init()
{
    std::lock_guard<std::recursive_mutex> mk(meta_mu_);
    for (int part = 0; part < 16; part++) {
        for (auto &m : ed_.parameters) {
            if (!m.meta || (!m.catch_always && !meta_cond(m, part))) // e.g. "Trigger On" keeps its default (64)
                continue;
            for (auto &c : m.clients) {
                int mv;
                if (c.nosource || !client_to_meta(c, ui_value(c.path, part), mv))
                    continue;
                meta_set(m, part, std::max(m.min, std::min(m.max, mv)), c.path, false);
                break;
            }
        }
        for (auto &m : ed_.parameters) // the retrigger metas start from the loaded state's conditions
            if (m.meta && m.retrigger) {
                bool c = meta_cond(m, part);
                std::lock_guard<std::mutex> lk(values_mu_);
                meta_cond_[{m.path, part}] = c;
            }
    }
}

// our own bank cache: Linux ~/.local/share/virus-ti-native/cache, Mac ~/Library/Application Support/virus-ti-native/
// cache, Windows %LOCALAPPDATA%\virus-ti-native\cache
static std::string own_cache_dir()
{
#ifdef _WIN32
    if (const char *la = getenv("LOCALAPPDATA"))
        return std::string(la) + "/virus-ti-native/cache";
#elif defined(__APPLE__)
    if (const char *home = getenv("HOME"))
        return std::string(home) + "/Library/Application Support/virus-ti-native/cache";
#else
    if (const char *home = getenv("HOME"))
        return std::string(home) + "/.local/share/virus-ti-native/cache";
#endif
    return {};
}

// REMOTE page templates: our RemoteSets file (the original's format, E196); without one, once, the original's cache
// on Windows ({common_appdata} = ProgramData), read only. Missing both: the original's defaults.
void VirusPlugin::load_remote_templates()
{
    std::string d = own_cache_dir();
    if (d.empty())
        return;
    remote_path_ = d + "/RemoteSets";
    std::lock_guard<std::mutex> lk(remote_mu_);
    if (remote_.load_file(remote_path_))
        return;
#ifdef _WIN32
    if (const char *pd = getenv("ProgramData"))
        remote_.load_file(std::string(pd) + "/Access Music/Virus TI/Cache/RemoteSets");
#endif
}

void VirusPlugin::save_remote_templates()
{
    if (remote_path_.empty())
        return;
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(remote_path_).parent_path(), ec);
    if (!remote_.save_file(remote_path_))
        fprintf(stderr, "Virus TI native: cannot write %s\n", remote_path_.c_str());
}

bool VirusPlugin::set_remote_text(const std::string &param, const std::string &text)
{
    int sel = ui_value("Remote Templates/Selected Template", -1), knob = ui_value("Remote Templates/Selected Knob", -1);
    std::vector<uint8_t> rec;
    {
        std::lock_guard<std::mutex> lk(remote_mu_);
        if (!remote_set_text(param, remote_, sel, knob, text))
            return false;
        save_remote_templates();
        rec = remote_edit_record(param, remote_, sel, knob);
    }
    send_remote({rec});
    ui_dirty_ = true;
    return true;
}

void VirusPlugin::send_remote(const std::vector<std::vector<uint8_t>> &records)
{
    if (!out_)
        return;
    for (auto &r : records)
        if (!r.empty())
            out_->send(r, 3); // the original's cable for template records (E199)
}

void VirusPlugin::choose_remote_template(int t)
{
    if (t < 0 || t >= RemoteTemplates::kTemplates)
        return;
    const int was = ui_value("Remote Templates/Selected Template", -1);
    ui_set("Remote Templates/Selected Template", -1, t);
    std::vector<std::vector<uint8_t>> records;
    std::string name, before;
    {
        std::lock_guard<std::mutex> lk(remote_mu_);
        records = remote_.template_records(t);
        name = remote_.name(t), before = remote_.name(was < 0 ? 0 : was);
    }
    // the LCD like the original's (E199 screenshot): "Remote Templates Selected Temp(late)" / the name, the one before
    // in brackets (both as the 16-character field), the part
    name.resize(16, ' '), before.resize(16, ' ');
    set_lcd("Remote Templates Selected Template", name,
            "Part " + std::to_string(ui_value(layout_.frame_parameter, -1) + 1), before);
    // "Global Patch Select/Remote Patch" = t first (Bank F global, 73 00 6C t), then the template (E199)
    if (out_)
        if (auto msg = dev_.encode("Global Patch Select/Remote Patch", t, 0); !msg.empty())
            out_->send(msg);
    send_remote(records);
}

static std::string remote_template_dir()
{
    std::string lib = library_dir(); // .../Access Music/Virus TI/Patches
    return lib.empty() ? std::string() : (std::filesystem::path(lib).parent_path() / "Remote Templates").string();
}

std::vector<std::pair<std::string, std::string>> VirusPlugin::remote_template_folders() const
{
    std::vector<std::pair<std::string, std::string>> f = {{"Personal Libraries", library_dir()},
                                                         {"Personal Remote Templates", remote_template_dir()}};
#ifdef __APPLE__
    // the Mac installer puts the factory templates here, not into the Patches folder (E200); the menu name is ours
    // (how the original's Mac menu calls this place is not checked yet)
    f.push_back({"Remote Templates", "/Library/Application Support/Access Music/Virus TI/Remote Templates"});
#endif
    return f;
}

bool VirusPlugin::load_remote_template(const std::string &path)
{
    std::vector<uint8_t> d;
    if (FILE *f = fopen(path.c_str(), "rb")) {
        uint8_t buf[4096];
        for (size_t n; (n = fread(buf, 1, sizeof buf, f)) > 0;)
            d.insert(d.end(), buf, buf + n);
        fclose(f);
    }
    int sel = ui_value("Remote Templates/Selected Template", -1);
    std::vector<std::vector<uint8_t>> records;
    {
        std::lock_guard<std::mutex> lk(remote_mu_);
        if (remote_.load_template_file(d, sel < 0 ? 0 : sel) == 0)
            return false;
        save_remote_templates();
        records = remote_.template_records(sel < 0 ? 0 : sel);
    }
    send_remote(records); // the original sends the loaded template's records (E200)
    ui_dirty_ = true;
    return true;
}

bool VirusPlugin::save_remote_template(const std::string &file)
{
    namespace fs = std::filesystem;
    const fs::path dir = remote_template_dir();
    if (file.empty() && dir.empty())
        return false;
    int sel = ui_value("Remote Templates/Selected Template", -1);
    std::vector<uint8_t> d;
    fs::path path = file;
    {
        std::lock_guard<std::mutex> lk(remote_mu_);
        d = remote_.template_file(sel < 0 ? 0 : sel);
        if (file.empty()) { // a new file named after the template, never over an existing one
            std::string name = remote_.name(sel < 0 ? 0 : sel);
            for (auto &c : name)
                if (strchr("/\\:*?\"<>|", c))
                    c = '_';
            if (name.empty())
                name = "Template";
            path = dir / (name + ".vrt");
            std::error_code ec;
            for (int n = 2; fs::exists(path, ec); n++)
                path = dir / (name + " " + std::to_string(n) + ".vrt");
        }
    }
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    const std::string tmp = path.string() + ".tmp";
    FILE *f = fopen(tmp.c_str(), "wb");
    if (!f)
        return false;
    bool ok = fwrite(d.data(), 1, d.size(), f) == d.size();
    ok = fclose(f) == 0 && ok;
    if (ok)
        fs::rename(tmp, path, ec);
    if (!ok || ec) {
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

std::vector<KnobTemplate> VirusPlugin::knob_templates()
{
    std::lock_guard<std::mutex> lk(remote_mu_);
    if (!knob_templates_read_ && files_) {
        knob_templates_ = files_->knob_templates(); // ~30 ms: only when the menu opens the first time
        knob_templates_read_ = true;
    }
    return knob_templates_;
}

// The Virus' RAM banks: our cache (tools/virussync, later the plug-in's own sync), else the original's Access cache
// (read only) - Windows/Wine: ProgramData/Access Music/Virus TI/Cache/<device>, Mac: /Library/Application Support/...
void VirusPlugin::load_banks()
{
    // the original's three places (E307): on the Mac the factory libraries are in "Public Libraries"
    for (auto &root : library_roots())
        for (auto &lib : load_libraries(root.dir, files_ ? files_->default_page_b() : std::vector<uint8_t>())) {
            banks_[LIB_BASE + (int)lib_names_.size()] = lib.dumps;
            lib_names_.push_back(lib.name);
            lib_paths_.push_back(lib.path);
            lib_roots_.push_back(root.menu);
        }
    // like the original: only the plug-in's own cache; without one the first start syncs the whole RAM
    std::string d = own_cache_dir();
    if (d.empty())
        return;
    for (int b = 0; b < 4; b++) {
        Bank bank;
        if (load_bank_file(d + "/RAM " + (char)('A' + b), bank))
            banks_[b].assign(bank.begin(), bank.end());
    }
}

void VirusPlugin::load_rom_banks()
{
    // the model decides the firmware image: bcdDevice 0x100 = TI (Snow if the product string says so), 0x300 = TI2
    VirusModel model = VirusModel::TI2;
    uint8_t dd[18] = {0};
    if (out_ && out_->control(0x80, 6, 0x0100, 0, dd, sizeof dd) >= 14) {
        uint16_t bcd = (uint16_t)(dd[12] | dd[13] << 8);
        std::string product;
        uint8_t sd[256] = {0};
        int n = dd[15] ? out_->control(0x80, 6, (uint16_t)(0x0300 | dd[15]), 0x0409, sd, 255) : -1;
        for (int i = 2; i + 1 < n; i += 2)
            product += (char)sd[i];
        model = bcd >= 0x300 ? VirusModel::TI2 : product.find("Snow") != std::string::npos ? VirusModel::Snow : VirusModel::TI;
        model_ = model == VirusModel::TI2 ? 0 : model == VirusModel::TI ? 1 : 2;
        fprintf(stderr, "Virus TI native: device bcdDevice %04x \"%s\"\n", bcd, product.c_str());
    }
    std::string err;
    auto roms = vti::load_rom_banks(firmware_path(files_ ? files_->source() : std::string()), model, err);
    if (roms.empty()) {
        fprintf(stderr, "Virus TI native: no ROM banks (%s)\n", err.c_str());
        return;
    }
    std::lock_guard<std::mutex> lk(banks_mu_);
    for (size_t i = 0; i < roms.size(); i++)
        banks_[4 + (int)i] = std::move(roms[i]);
    ui_dirty_ = true;
}

void VirusPlugin::set_sync_progress(int v)
{
    const std::string path = "VirusControl/InitialSyncProgress";
    if (ui_state_set(path, v))
        ui_dirty_ = true;
}

void VirusPlugin::set_start_pages(int recall, int sync)
{
    {
        std::lock_guard<std::mutex> lk(ui_state_mu_);
        ui_state_["VirusControl/RecallProgress"] = recall;
        ui_state_["VirusControl/InitialSyncProgress"] = sync;
    }
    ui_dirty_ = true;
}

void VirusPlugin::set_recall_progress(int v)
{
    const std::string path = "VirusControl/RecallProgress";
    if (ui_state_set(path, v))
        ui_dirty_ = true;
}

bool VirusPlugin::wait_until_quiet()
{
    // playing has priority: no dump request while notes arrive (the Virus delays notes behind requests, E144). After
    // 30 s of pauses in total a short gap is enough for the next request, so constant playing cannot block the sync.
    bool shown = false;
    const int64_t t0 = now_ms();
    auto gap = [&] { return sync_paused_ms_ + (now_ms() - t0) >= 30000 ? 300 : 1500; };
    while (!stop_warm_ && now_ms() - last_note_ms_.load(std::memory_order_relaxed) < gap()) {
        if (!shown) {
            if (ui_value("VirusControl/InitialSyncProgress", -1) < 127) // only during a real sync, not a bank check
                set_lcd("Bank sync paused (playing)", std::string(), std::string(), std::string());
            sync_paused_ = true;
            ui_dirty_ = true; // the note on the sync page
            shown = true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    // a dump in pieces (dumppacer.h): a request between its pieces would land in the host CPU's dump parse
    while (!stop_warm_ && out_ && out_->dumping())
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    if (shown) {
        sync_paused_ = false;
        ui_dirty_ = true;
        sync_paused_ms_ += now_ms() - t0;
        if (out_)
            out_->log("Virus TI native: bank sync paused %.1f s for notes (%.0f s in total)\n", (now_ms() - t0) / 1000.0,
                      sync_paused_ms_ / 1000.0);
    }
    return !stop_warm_;
}

void VirusPlugin::wait_virus_ready()
{
    // Our queue empties long before the Virus has digested it: after a takeover (530 queued messages) notes sent 4 s
    // later still played 1.8 s late and the first sync ping went unanswered (E148). The Virus handles its MIDI input
    // in order (E144), so a request queued behind the start-up messages and the state is answered only when they are
    // done: the single dump request of part 1's edit buffer (30 00 00, read-only, as the bank sync and E145 use it).
    const int64_t t0 = now_ms();
    while (!stop_warm_ && out_ && (!out_->connected() || out_->waiting()) && now_ms() - t0 < 4000)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (!stop_warm_ && out_ && out_->waiting()) {
        // another instance owns the Virus (issue #6): this one starts up when it takes over, with the same start
        // page and note hold as a first start
        set_recall_progress(127); // the "Virus unavailable" page instead of the start pages
        while (!stop_warm_ && out_->waiting())
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        set_recall_progress(0);
        const int64_t t1 = now_ms();
        while (!stop_warm_ && !out_->connected() && now_ms() - t1 < 4000)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (stop_warm_ || !out_ || !out_->connected()) { // no Virus: nothing to wait for (when it comes: after_reconnect)
        absent_at_start_ = !stop_warm_ && out_;
        ready_ = true;
        set_recall_progress(127);
        return;
    }
    // the ring of the "Starting up" page follows the start-up messages and the state on their way out
    RecallMeter meter;
    set_recall_progress(meter.update(out_->queued()));
    while (!stop_warm_ && out_->connected() && out_->busy()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        set_recall_progress(meter.update(out_->queued()));
    }
    set_recall_progress(RecallMeter::SENT);
    probe_virus_ready();
    ready_ = true;
    // process() anchors now and sends the ping (0.5 s of stream, 1 s of silence); notes stay held until its marker
    // is found, so the first notes are stamped on the Virus' clock (at most 3 s, else stamps stay ring-relative)
    const int64_t tm = now_ms();
    while (!stop_warm_ && out_->marker.load() < 0 && now_ms() - tm < 3000)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    if (sync_expected_)
        set_start_pages(127, 0); // straight to "Syncing memory"
    else
        set_recall_progress(127);
}

bool VirusPlugin::probe_virus_ready()
{
    const int64_t tp = now_ms();
    // one try: a second request behind a slow first one would only lengthen the Virus' backlog
    Dump r = sync_->transact({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x30, 0x00, 0x00, 0xF7}, {0x10, 0x00, 0x00}, 10000, 1);
    out_->log("Virus TI native: Virus ready after %.1f s%s\n", (now_ms() - tp) / 1000.0,
              r.empty() ? " (no reply to the probe)" : "");
    return !r.empty();
}

// The Virus is back (sender thread, after the start-up messages and the state were queued): until it has worked
// through them, notes are held and the timeline does not anchor - else notes play up to 1.6 s late and the sync ping
// goes unanswered (E144, E148), as before 0.8.6 at a first start. A reconnect during a running probe lets it run again.
// The Virus came back or the stream woke up from a sleep: it gets what it got at the first start, then the readiness
// probe holds notes until it has worked through it (a reconnect showed this works; a sleep needs it too, E210)
void VirusPlugin::resend_to_virus()
{
    for (auto &m : startup_messages())
        out_->send(m);
    send_state();
    after_reconnect();
}

void VirusPlugin::after_reconnect()
{
    if (!sync_)
        return;
    std::lock_guard<std::mutex> g(reconnect_mu_); // terminate() joins under it
    if (stop_warm_)
        return;
    ready_ = false;
    starting_ = true;
    if (out_)
        out_->marker = -1; // the old marker belongs to the stream before: the wait below is for the new one
    reconnect_again_ = true;
    if (reconnect_busy_.exchange(true))
        return; // the running probe goes once more
    if (reconnect_thread_.joinable())
        reconnect_thread_.join(); // the previous one has finished (busy was false)
    reconnect_thread_ = std::thread([this] {
        do {
            while (!stop_warm_ && reconnect_again_.exchange(false))
                probe_virus_ready();
            ready_ = true;
            // process() anchors and pings now; notes wait for its marker like at a first start (at most 3 s)
            const int64_t tm = now_ms();
            while (!stop_warm_ && out_ && out_->marker.load() < 0 && now_ms() - tm < 3000)
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            starting_ = false;
            reconnect_busy_ = false;
        } while (!stop_warm_ && reconnect_again_.load() && !reconnect_busy_.exchange(true));
    });
}

void VirusPlugin::sync_banks()
{
    // until the Virus is ready it works through the start-up messages and the state and plays notes up to 1.6 s late
    // (E144), so note-ons are held back; from then on notes play, and the sync pauses while they arrive (dump requests
    // delay the Virus' notes by ~130 ms and the sync stalls - measured 2026-10-14)
    // a RAM bank without a cache file is read in full: its "Syncing memory" page follows "Starting up" directly,
    // without the editor in between (user, L4)
    bool sync_expected = false;
    {
        std::lock_guard<std::mutex> lk(banks_mu_);
        for (int b = 0; b < 4; b++) { // no cache file, or one an interrupted sync left with gaps
            auto it = banks_.find(b);
            if (it == banks_.end() || it->second.size() < 128 ||
                std::any_of(it->second.begin(), it->second.end(), [](const std::vector<uint8_t> &p) { return p.empty(); }))
                sync_expected = true;
        }
    }
    sync_expected_ = sync_expected; // wait_virus_ready switches from "Starting up" to "Syncing" in one step
    wait_virus_ready();
    starting_ = false;
    if (stop_warm_ || !out_ || !out_->connected()) { // no Virus: no sync, the editor as usual
        syncing_ = false;
        set_sync_progress(127);
        return;
    }
    std::string dir = own_cache_dir();
    if (!dir.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
    }
    load_rom_banks();
    sync_->before_request = [this] { return wait_until_quiet(); };
    const int64_t sync_t0 = now_ms();
    // like the original: its page "Syncing memory with Virus" (VirusControl/InitialSyncProgress 0..126) covers the
    // editor while banks are fetched; notes still play. Only when a patch is really fetched (startprogress.h): the
    // check of a current cache shows nothing. The original keeps "synchronising" on the Virus display while it
    // syncs (74 01 7E 07 about once a second) - also only then
    SyncPage page;
    int result = 0; // the last bank's sync_bank result: < 0 = the sync stopped there
    if (sync_expected)
        page.expect();
    std::atomic<bool> rise{true}, fetching{page.shown()};
    std::thread riser([this, &rise, &fetching] {
        while (rise) {
            if (fetching)
                out_->send({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x74, 0x01, 0x7E, 0x07, 0xF7});
            for (int i = 0; i < 10 && rise; i++)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });
    for (int b = 0; b < 4; b++) {
        Bank bank;
        {
            std::lock_guard<std::mutex> lk(banks_mu_);
            auto it = banks_.find(b);
            if (it != banks_.end())
                for (size_t p = 0; p < 128 && p < it->second.size(); p++)
                    bank[p] = it->second[p];
        }
        int n = sync_->sync_bank(b + 1, bank, [this, b, &bank, sync_t0, &page, &fetching](int p, bool fetched) {
            set_sync_progress(page.progress(b * 128 + p + 1, fetched));
            fetching = page.shown();
            // remaining time at the pace so far (pauses for notes not counted): the original's start page promises
            // "a couple of seconds", but a first sync takes 8-9 minutes (issue #5)
            int done = b * 128 + p + 1;
            int64_t worked = now_ms() - sync_t0 - sync_paused_ms_;
            if (done >= 8 && worked > 0)
                sync_eta_s_ = (int)((512 - done) * worked / done / 1000);
            if (page.shown()) {
                std::lock_guard<std::mutex> lk(values_mu_); // progress on the LCD
                lcd_param_ = "Synchronizing " + bank_name(b);
                lcd_value_ = std::to_string(p + 1) + " / 128";
                lcd_part_.clear();
                lcd_orig_.clear();
            }
            if (fetched) {
                std::lock_guard<std::mutex> lk(banks_mu_);
                auto &v = banks_[b];
                v.resize(128);
                v[p] = bank[p];
                ui_dirty_ = true; // a new patch name in the browser
            } else
                lcd_dirty_ = true; // only the progress on the LCD (a full repaint costs ~77 ms in REAPER)
        }, &stop_warm_);
        out_->log("Virus TI native: RAM %c sync: %d fetched\n", 'A' + b, n); // also diag.log: the sync's duration
        if (n == -2 && !dir.empty()) // closed while syncing: keep what was fetched, the next start continues
            save_bank_file(dir + "/RAM " + (char)('A' + b), bank);
        result = n;
        if (n < 0)
            break;
        {
            std::lock_guard<std::mutex> lk(banks_mu_);
            banks_[b].assign(bank.begin(), bank.end());
            if (n > 0)
                drop_.forget(); // the Virus' RAM changed elsewhere: an undo could write over it
        }
        if (!dir.empty())
            save_bank_file(dir + "/RAM " + (char)('A' + b), bank);
        ui_dirty_ = true;
    }
    rise = false;
    riser.join();
    syncing_ = false;
    sync_eta_s_ = -1;
    set_sync_progress(127);
    out_->log("Virus TI native: bank sync %s in %.1f s\n", sync_end(result), (now_ms() - sync_t0) / 1000.0);
    if (page.shown()) {
        std::lock_guard<std::mutex> lk(values_mu_);
        lcd_param_ = result < 0 ? "" : "Synchronization done";
        lcd_value_.clear();
        lcd_orig_.clear();
    }
    ui_dirty_ = true;
}

std::string VirusPlugin::bank_patch(int bank, int program)
{
    std::lock_guard<std::mutex> lk(banks_mu_);
    auto b = banks_.find(bank);
    if (b == banks_.end() || program < 0 || program >= (int)b->second.size() || b->second[program].size() != 524)
        return {};
    return std::string(b->second[program].begin() + 249, b->second[program].begin() + 259);
    // (library banks hold fewer than 128 patches: the rest of the browser stays empty)
}

bool VirusPlugin::load_patch(int bank, int program, int part)
{
    std::vector<uint8_t> d;
    {
        std::lock_guard<std::mutex> lk(banks_mu_);
        auto b = banks_.find(bank);
        if (b == banks_.end() || program < 0 || program >= (int)b->second.size() || b->second[program].size() != 524 ||
            part < 0 || part > 15)
            return false;
        d = b->second[program];
    }
    return load_dump(d, part);
}

std::vector<uint8_t> VirusPlugin::part_dump(int part)
{
    std::lock_guard<std::mutex> lk(state_mu_);
    for (auto &r : state_.empty() ? default_ : state_)
        if (is_part_dump(r, part))
            return r;
    return {};
}

bool VirusPlugin::load_patch_remembered(int bank, int program, int part)
{
    std::vector<uint8_t> before = part_dump(part), d;
    {
        std::lock_guard<std::mutex> lk(banks_mu_);
        auto b = banks_.find(bank);
        if (b == banks_.end() || program < 0 || program >= (int)b->second.size())
            return false;
        d = b->second[program];
    }
    if (!load_patch(bank, program, part))
        return false;
    std::lock_guard<std::mutex> lk(history_mu_);
    history_.loaded(part, before, d);
    return true;
}

bool VirusPlugin::load_recent(int part, int index)
{
    std::vector<uint8_t> d, current = part_dump(part);
    {
        std::lock_guard<std::mutex> lk(history_mu_);
        d = history_.take_recent(part, index, current);
    }
    return load_dump(d, part);
}

bool VirusPlugin::revert_patch(int part)
{
    std::vector<uint8_t> d, current = part_dump(part);
    {
        std::lock_guard<std::mutex> lk(history_mu_);
        d = history_.revert(part, current);
    }
    return load_dump(d, part);
}

std::string VirusPlugin::revert_name(int part)
{
    std::lock_guard<std::mutex> lk(history_mu_);
    return history_.revert_name(part);
}

std::vector<std::string> VirusPlugin::recent_names(int part)
{
    std::lock_guard<std::mutex> lk(history_mu_);
    return history_.recent_names(part);
}

// a part's patch onto another part (drag and drop between the part strips, like the original)
bool VirusPlugin::copy_part(int from, int to)
{
    if (from == to || from < 0 || from > 15 || to < 0 || to > 15)
        return false;
    std::vector<uint8_t> d;
    {
        std::lock_guard<std::mutex> lk(state_mu_);
        if (state_.empty())
            state_ = default_;
        for (auto &r : state_)
            if (is_part_dump(r, from))
                d = r;
    }
    return d.size() == 524 && load_dump(d, to);
}

bool VirusPlugin::load_dump(std::vector<uint8_t> d, int part)
{
    if (d.size() != 524 || part < 0 || part > 15)
        return false;
    RemoteDevice::upgrade_single(d);
    d[7] = 0, d[8] = (uint8_t)part; // edit buffer of the part
    unsigned a = 0;
    for (int i = 5; i < 265; i++)
        a += d[i];
    d[265] = a & 0x7F;
    for (int i = 265; i < 522; i++)
        a += d[i];
    d[522] = a & 0x7F;
    {
        std::lock_guard<std::mutex> lk(state_mu_);
        if (state_.empty())
            state_ = default_;
        for (auto &r : state_)
            if (is_part_dump(r, part))
                r = d;
    }
    if (out_)
        out_->send(d);
    apply_state_to_params(); // parameters + editor values of the new patch
    return true;
}

bool VirusPlugin::rename_part(int part, const std::string &name)
{
    std::vector<uint8_t> d;
    {
        std::lock_guard<std::mutex> lk(state_mu_);
        if (state_.empty())
            state_ = default_;
        for (auto &r : state_)
            if (is_part_dump(r, part)) {
                for (int i = 0; i < 10; i++) { // printable ASCII, padded with spaces
                    char ch = i < (int)name.size() ? name[i] : ' ';
                    r[249 + i] = (uint8_t)(ch >= 32 && ch < 127 ? ch : ' ');
                }
                unsigned a = 0;
                for (int i = 5; i < 265; i++)
                    a += r[i];
                r[265] = a & 0x7F;
                for (int i = 265; i < 522; i++)
                    a += r[i];
                r[522] = a & 0x7F;
                d = r;
            }
    }
    if (d.empty())
        return false;
    if (out_)
        out_->send(d);
    apply_state_to_params();
    return true;
}

bool VirusPlugin::store_patch(int part, int bank, int program)
{
    if (bank < 0 || bank > 3 || program < 0 || program > 127)
        return false;
    std::vector<uint8_t> d;
    {
        std::lock_guard<std::mutex> lk(state_mu_);
        if (state_.empty())
            state_ = default_;
        for (auto &r : state_)
            if (is_part_dump(r, part))
                d = r;
    }
    if (d.empty())
        return false;
    d[7] = (uint8_t)(bank + 1), d[8] = (uint8_t)program; // a dump into RAM bank 1..4 = store
    unsigned a = 0;
    for (int i = 5; i < 265; i++)
        a += d[i];
    d[265] = a & 0x7F;
    for (int i = 265; i < 522; i++)
        a += d[i];
    d[522] = a & 0x7F;
    if (out_)
        out_->send(d);
    Bank cache;
    {
        std::lock_guard<std::mutex> lk(banks_mu_);
        auto &b = banks_[bank];
        b.resize(128);
        b[program] = d;
        for (int p = 0; p < 128; p++)
            cache[p] = b[p];
        drop_.forget(); // an undo of an earlier drop must not write over this store
    }
    std::string dir = own_cache_dir();
    if (!dir.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        save_bank_file(dir + "/RAM " + (char)('A' + bank), cache);
    }
    ui_dirty_ = true;
    return true;
}

bool VirusPlugin::drop_to_ram(int src_bank, const std::vector<int> &programs, int bank, int slot)
{
    if (syncing_.load() || downloading_.load()) { // a sync/download writes its read of the bank back: it would undo the drop's cache
        if (out_)
            out_->log("Virus TI native: drop onto RAM %c refused, bank sync running\n", 'A' + std::max(0, bank));
        return false;
    }
    std::vector<BankDrop::Write> w;
    {
        std::lock_guard<std::mutex> lk(banks_mu_);
        auto src = banks_.find(src_bank), dst = banks_.find(bank);
        if (src == banks_.end() || dst == banks_.end())
            return false;
        std::vector<BankDrop::Patch> sources;
        for (int p : programs)
            if (p >= 0 && p < (int)src->second.size() && !src->second[p].empty()) {
                sources.push_back(src->second[p]);
                RemoteDevice::upgrade_single(sources.back()); // older library formats, as a load does
            }
        w = drop_.drop(sources, bank, slot, dst->second);
    }
    if (w.empty()) {
        if (out_)
            out_->log("Virus TI native: drop of %d patch(es) onto RAM %c %d refused (RAM bank not synced?)\n",
                      (int)programs.size(), 'A' + std::max(0, std::min(3, bank)), slot + 1);
        return false;
    }
    if (out_)
        out_->log("Virus TI native: %d patch(es) dropped onto RAM %c from %d\n", (int)w.size(), 'A' + bank, slot + 1);
    return send_ram_writes(w);
}

bool VirusPlugin::swap_in_ram(int bank, const std::vector<int> &programs, int slot)
{
    if (syncing_.load() || downloading_.load()) { // see drop_to_ram
        if (out_)
            out_->log("Virus TI native: swap in RAM %c refused, bank sync running\n", 'A' + std::max(0, bank));
        return false;
    }
    std::vector<BankDrop::Write> w;
    {
        std::lock_guard<std::mutex> lk(banks_mu_);
        auto b = banks_.find(bank);
        if (b == banks_.end())
            return false;
        w = drop_.swap(programs, bank, slot, b->second);
    }
    if (w.empty()) {
        if (out_)
            out_->log("Virus TI native: swap of %d slot(s) in RAM %c onto %d refused\n", (int)programs.size(),
                      'A' + std::max(0, std::min(3, bank)), slot + 1);
        return false;
    }
    if (out_)
        out_->log("Virus TI native: %d slot(s) swapped in RAM %c with %d..\n", (int)programs.size(), 'A' + bank, slot + 1);
    return send_ram_writes(w);
}

// one dump to the slot with the new name, like the original (E297 (D)); its undo is not known: an earlier drop's
// undo is forgotten as after a SAVE
bool VirusPlugin::rename_in_ram(int bank, int program, const std::string &name)
{
    if (bank < 0 || bank > 3 || program < 0 || program > 127 || syncing_.load() || downloading_.load())
        return false;
    BankDrop::Patch d;
    {
        std::lock_guard<std::mutex> lk(banks_mu_);
        auto b = banks_.find(bank);
        if (b == banks_.end() || program >= (int)b->second.size())
            return false;
        d = BankDrop::renamed(b->second[program], name);
        if (d.empty())
            return false;
        BankDrop::address(d, bank, program);
        drop_.forget();
    }
    if (out_)
        out_->log("Virus TI native: RAM %c %d renamed\n", 'A' + bank, program + 1);
    return send_ram_writes({{bank, program, std::move(d)}});
}

bool VirusPlugin::save_library_file(int library)
{
    PatchLibrary lib;
    {
        std::lock_guard<std::mutex> lk(banks_mu_);
        if (library < 0 || library >= (int)lib_paths_.size())
            return false;
        lib.name = lib_names_[library], lib.path = lib_paths_[library], lib.dumps = banks_[LIB_BASE + library];
    }
    std::string err;
    bool ok = save_library(lib, err);
    if (!ok && out_)
        out_->log("Virus TI native: %s\n", err.c_str());
    return ok;
}

bool VirusPlugin::edit_library(int library, const char *what,
                               const std::function<LibraryEdit::Lib(const LibraryEdit::Lib &)> &op)
{
    {
        std::lock_guard<std::mutex> lk(banks_mu_);
        if (library < 0 || library >= (int)lib_paths_.size())
            return false;
        auto &cur = banks_[LIB_BASE + library];
        LibraryEdit::Lib after = op(cur);
        if (after.empty()) {
            if (out_)
                out_->log("Virus TI native: %s in library %d refused\n", what, library + 1);
            return false;
        }
        drop_.remember_library(library, cur, after);
        cur = std::move(after);
    }
    if (out_)
        out_->log("Virus TI native: %s in library %d\n", what, library + 1);
    bool ok = save_library_file(library);
    ui_dirty_ = true;
    return ok;
}

bool VirusPlugin::drop_to_library(int src_bank, const std::vector<int> &programs, int library, int slot)
{
    std::vector<BankDrop::Patch> sources;
    {
        std::lock_guard<std::mutex> lk(banks_mu_);
        auto src = banks_.find(src_bank);
        if (src == banks_.end())
            return false;
        for (int p : programs)
            if (p >= 0 && p < (int)src->second.size() && !src->second[p].empty()) {
                sources.push_back(src->second[p]);
                RemoteDevice::upgrade_single(sources.back());
            }
    }
    return edit_library(library, "patch(es) dropped", [&](const LibraryEdit::Lib &l) {
        return LibraryEdit::drop(l, sources, slot);
    });
}

bool VirusPlugin::swap_in_library(int library, const std::vector<int> &programs, int slot)
{
    return edit_library(library, "swap", [&](const LibraryEdit::Lib &l) { return LibraryEdit::swap(l, programs, slot); });
}

bool VirusPlugin::delete_from_library(int library, const std::vector<int> &programs)
{
    return edit_library(library, "delete", [&](const LibraryEdit::Lib &l) { return LibraryEdit::remove(l, programs); });
}

bool VirusPlugin::sort_library(int library)
{
    return edit_library(library, "sort", [](const LibraryEdit::Lib &l) { return LibraryEdit::sort(l); });
}

bool VirusPlugin::rename_in_library(int library, int program, const std::string &name)
{
    return edit_library(library, "rename", [&](const LibraryEdit::Lib &l) { return LibraryEdit::rename(l, program, name); });
}

bool VirusPlugin::sort_ram_bank(int bank)
{
    if (syncing_.load() || downloading_.load())
        return false;
    std::vector<BankDrop::Write> w;
    {
        std::lock_guard<std::mutex> lk(banks_mu_);
        auto b = banks_.find(bank);
        if (b == banks_.end())
            return false;
        w = drop_.sort_ram(bank, b->second);
    }
    if (out_)
        out_->log("Virus TI native: RAM %c sorted, %d slot(s) written\n", 'A' + std::max(0, std::min(3, bank)), (int)w.size());
    return w.empty() || send_ram_writes(w);
}

// the original (E301): the bank's entries are emptied, then filled one by one as the Virus answers (~1 s each); the
// bank sync's request path with its pause while notes play. Stopped (plug-in closing, no reply): the entries not read
// again keep their cached patches
bool VirusPlugin::download_bank(int bank)
{
    if (bank < 0 || bank > 3 || !sync_ || !out_ || !out_->connected() || syncing_.load())
        return false;
    std::lock_guard<std::mutex> g(download_mu_);
    if (downloading_.exchange(true))
        return false;
    if (download_thread_.joinable())
        download_thread_.join(); // the previous one has ended (downloading_ was false)
    download_thread_ = std::thread([this, bank] {
        Bank old, cache;
        {
            std::lock_guard<std::mutex> lk(banks_mu_);
            auto &b = banks_[bank];
            b.resize(128);
            for (int p = 0; p < 128; p++)
                old[p] = b[p], b[p].clear();
            drop_.forget(); // the bank is read again: an undo could write over it
        }
        // our LCD shows the progress and the time left (the original shows it only on the Virus; user wish)
        const int64_t t0 = now_ms();
        auto lcd = [this, bank](const std::string &param, const std::string &value) {
            std::lock_guard<std::mutex> lk(values_mu_);
            lcd_param_ = param, lcd_value_ = value;
            lcd_part_.clear(), lcd_orig_.clear();
            lcd_dirty_ = true;
        };
        const std::string what = "Downloading " + bank_name(bank);
        lcd(what, download_progress(0, 0));
        ui_dirty_ = true;
        if (out_)
            out_->log("Virus TI native: download of RAM %c started\n", 'A' + bank);
        int n = sync_->read_bank(bank + 1, cache, [&](int p) {
            {
                std::lock_guard<std::mutex> lk(banks_mu_);
                banks_[bank][p] = cache[p];
            }
            lcd(what, download_progress(p + 1, now_ms() - t0));
            ui_dirty_ = true;
        }, &stop_warm_);
        lcd(n == 128 ? "Download done" : "Download stopped", "");
        {
            std::lock_guard<std::mutex> lk(banks_mu_);
            auto &b = banks_[bank];
            for (int p = 0; p < 128; p++)
                if (cache[p].empty())
                    b[p] = old[p];
                else
                    old[p] = cache[p];
        }
        std::string dir = own_cache_dir();
        if (!dir.empty())
            save_bank_file(dir + "/RAM " + (char)('A' + bank), old);
        if (out_)
            out_->log("Virus TI native: download of RAM %c %s\n", 'A' + bank, n == 128 ? "done" : "stopped");
        ui_dirty_ = true;
        downloading_ = false;
    });
    return true;
}

void VirusPlugin::editor_opened()
{
    editors_open_++;
}

void VirusPlugin::editor_closed()
{
    if (--editors_open_ <= 0) {
        editors_open_ = 0;
        gestures_.flush([this](uint32_t id) { endEdit(id); });
    }
}

void VirusPlugin::tick_gestures()
{
    gestures_.tick(
        now_ms(), [this](uint32_t id) { beginEdit(id); },
        [this](uint32_t id, double v) {
            setParamNormalized(id, v);
            performEdit(id, v);
        },
        [this](uint32_t id) { endEdit(id); });
}

bool VirusPlugin::can_undo_drop()
{
    std::lock_guard<std::mutex> lk(banks_mu_);
    return drop_.can_undo();
}

bool VirusPlugin::undo_drop()
{
    std::vector<BankDrop::Write> w;
    int library = -1;
    {
        std::lock_guard<std::mutex> lk(banks_mu_);
        library = drop_.undo_library();
        if (library >= 0 && library < (int)lib_paths_.size())
            banks_[LIB_BASE + library] = drop_.take_library_undo();
        else
            w = drop_.take_undo();
    }
    if (library >= 0) { // a library operation was the last one (E298 (L4))
        if (out_)
            out_->log("Virus TI native: undo of the last operation in library %d\n", library + 1);
        bool ok = save_library_file(library);
        ui_dirty_ = true;
        return ok;
    }
    if (w.empty())
        return false;
    if (out_)
        out_->log("Virus TI native: undo of the last operation, %d slot(s) of RAM %c\n", (int)w.size(), 'A' + w[0].bank);
    return send_ram_writes(w);
}

// one single dump per slot, in order (the original: one after the other, no reply, E294); the dump pacer spaces
// them on cable 3 like any dump. Then the cache and its file, like a SAVE.
bool VirusPlugin::send_ram_writes(const std::vector<BankDrop::Write> &w)
{
    if (out_)
        for (auto &x : w)
            out_->send(x.dump);
    std::set<int> touched;
    std::map<int, Bank> caches;
    {
        std::lock_guard<std::mutex> lk(banks_mu_);
        for (auto &x : w) {
            auto &b = banks_[x.bank];
            b.resize(128);
            b[x.slot] = x.dump;
            touched.insert(x.bank);
        }
        for (int bank : touched)
            for (int p = 0; p < 128; p++)
                caches[bank][p] = banks_[bank][p];
    }
    std::string dir = own_cache_dir();
    if (!dir.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        for (auto &c : caches)
            save_bank_file(dir + "/RAM " + (char)('A' + c.first), c.second);
    }
    ui_dirty_ = true;
    return true;
}

bool VirusPlugin::create_library(const std::string &chosen)
{
    const std::string path = library_file_name(chosen);
    std::string err;
    if (!write_empty_library(path, err)) {
        fprintf(stderr, "Virus TI native: %s\n", err.c_str());
        return false;
    }
    {
        std::lock_guard<std::mutex> lk(banks_mu_);
        auto it = std::find(lib_paths_.begin(), lib_paths_.end(), path);
        if (it != lib_paths_.end()) { // replaced a listed library: now empty
            banks_[LIB_BASE + (int)(it - lib_paths_.begin())].clear();
        } else { // listed by its file name, as the original adds it (wherever the file was saved)
            banks_[LIB_BASE + (int)lib_names_.size()].clear();
            lib_names_.push_back(std::filesystem::path(path).stem().string());
            lib_paths_.push_back(path);
            lib_roots_.push_back("Personal Libraries");
        }
    }
    ui_dirty_ = true;
    return true;
}

bool VirusPlugin::store_to_library(int part, int library, int slot)
{
    std::vector<uint8_t> d;
    {
        std::lock_guard<std::mutex> lk(state_mu_);
        if (state_.empty())
            state_ = default_;
        for (auto &r : state_)
            if (is_part_dump(r, part))
                d = r;
    }
    if (d.empty())
        return false;
    PatchLibrary lib;
    {
        std::lock_guard<std::mutex> lk(banks_mu_);
        if (library < 0 || library >= (int)lib_paths_.size())
            return false;
        auto &b = banks_[LIB_BASE + library];
        if (slot < 0 || slot > (int)b.size() || slot >= 128)
            return false;
        if (slot == (int)b.size())
            b.push_back(d);
        else
            b[slot] = d;
        drop_.forget(); // an undo of an earlier operation must not write over this SAVE
        lib.name = lib_names_[library], lib.path = lib_paths_[library], lib.dumps = b;
    }
    std::string err;
    bool ok = save_library(lib, err);
    if (!ok)
        fprintf(stderr, "Virus TI native: %s\n", err.c_str());
    ui_dirty_ = true;
    return ok;
}

int VirusPlugin::tick_meters(int step)
{
    int changed = 0;
    for (int ch = 0; ch < 16; ch++) {
        int v = meter_[ch].load(std::memory_order_relaxed);
        if (v != meter_shown_[ch])
            changed |= 1 << ch;
        meter_shown_[ch] = v;
        if (v > 0) // the original lowers every channel by 1 per editor idle tick
            meter_[ch].compare_exchange_strong(v, std::max(0, v - step), std::memory_order_relaxed);
    }
    return changed;
}

// the LCD: "[original value]" is the value the parameter had when its editing started (a new parameter on the LCD)
void VirusPlugin::set_lcd(const std::string &param, const std::string &value, const std::string &part,
                          const std::string &before)
{
    std::lock_guard<std::mutex> lk(values_mu_);
    if (param != lcd_param_ || part != lcd_part_)
        lcd_orig_ = "[" + before + "]";
    lcd_param_ = param, lcd_value_ = value, lcd_part_ = part;
    lcd_dirty_ = true; // only the LCD (value changes set ui_dirty_ themselves)
}

void VirusPlugin::set_search(const std::string &s)
{
    {
        std::lock_guard<std::mutex> lk(values_mu_);
        search_ = s;
    }
    update_results();
}

// like the original (repository "Search Results", search://): a search text and/or a category collect the matching
// patches of all banks, sorted by name (byte order, duplicates kept), in pages of 128 shown in the upper browser
// (browser 0); cleared, browser 0 returns to the bank it showed before
void VirusPlugin::update_results()
{
    int cat = ui_value("Repository Actions/Category Filter", -1);
    std::string q = search();
    bool active = cat != 0 || !q.empty();
    std::vector<int> ids;
    {
        std::lock_guard<std::mutex> lk(banks_mu_);
        for (auto &kv : banks_)
            if (kv.first != SEARCH_BANK)
                ids.push_back(kv.first);
    }
    // copies of the same sound in several banks count once: key = page A without its first two bytes (dump bytes
    // 11..264; checked against the original's results: RAM + ROM + all libraries, 70 piano patches, same doubles)
    std::vector<std::vector<uint8_t>> found;
    std::set<std::string> seen;
    for (int b = 0; active && b < (int)ids.size(); b++)
        for (int p = 0; p < 128; p++) {
            if (!patch_matches(ids[b], p, cat, q))
                continue;
            std::lock_guard<std::mutex> lk(banks_mu_);
            auto it = banks_.find(ids[b]);
            if (it == banks_.end() || p >= (int)it->second.size() || it->second[p].size() != 524)
                continue;
            const auto &d = it->second[p];
            if (seen.insert(std::string(d.begin() + 11, d.begin() + 265)).second)
                found.push_back(d);
        }
    std::stable_sort(found.begin(), found.end(), [](const std::vector<uint8_t> &a, const std::vector<uint8_t> &b) {
        return std::lexicographical_compare(a.begin() + 249, a.begin() + 259, b.begin() + 249, b.begin() + 259);
    });
    {
        std::lock_guard<std::mutex> lk(banks_mu_);
        results_ = std::move(found);
        results_page_ = 0;
        show_results_page();
        if (!active)
            banks_.erase(SEARCH_BANK);
    }
    const std::string b0 = "Browser 0/Selected Bank";
    int shown = ui_value(b0, -1);
    if (active && shown != SEARCH_BANK) {
        results_return_ = shown;
        ui_state_set(b0, SEARCH_BANK);
    } else if (!active && shown == SEARCH_BANK)
        ui_state_set(b0, results_return_);
    ui_dirty_ = true;
}

void VirusPlugin::show_results_page() // banks_mu_ held
{
    int n = (int)results_.size(), from = results_page_ * 128;
    banks_[SEARCH_BANK].assign(results_.begin() + std::min(n, from), results_.begin() + std::min(n, from + 128));
}

bool VirusPlugin::results_step(int dir)
{
    std::lock_guard<std::mutex> lk(banks_mu_);
    int pages = std::max(1, ((int)results_.size() + 127) / 128), p = results_page_ + dir;
    if (p < 0 || p >= pages)
        return false;
    results_page_ = p;
    show_results_page();
    ui_dirty_ = true;
    return true;
}

std::string VirusPlugin::results_title()
{
    int cat = ui_value("Repository Actions/Category Filter", -1);
    std::string q = search(), t;
    if (!q.empty() && cat)
        t = "Name contains \"" + q + "\" and category is \"" + category_name_() + "\"";
    else if (!q.empty())
        t = "Name contains \"" + q + "\"";
    else
        t = "Category is \"" + category_name_() + "\"";
    std::lock_guard<std::mutex> lk(banks_mu_);
    int pages = std::max(1, ((int)results_.size() + 127) / 128);
    return "Search Results/" + t + " (page " + std::to_string(results_page_ + 1) + " of " + std::to_string(pages) + ")";
}

// decode the categories of all stored patches in the background (after the bank sync), so the first category
// filter does not stall the editor (~11000 patches with the factory backups)
void VirusPlugin::warm_categories()
{
    std::vector<int> ids;
    {
        std::lock_guard<std::mutex> lk(banks_mu_);
        for (auto &kv : banks_)
            if (kv.first != SEARCH_BANK)
                ids.push_back(kv.first);
    }
    auto t0 = std::chrono::steady_clock::now();
    for (int b : ids)
        for (int p = 0; p < 128 && !stop_warm_; p++)
            patch_matches(b, p, 1, std::string());
    if (env_debug())
        fprintf(stderr, "Virus TI native: categories of %zu banks decoded in %.0f ms\n", ids.size(),
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
}

std::string VirusPlugin::category_name_()
{
    int cat = ui_value("Repository Actions/Category Filter", -1);
    auto vl = ed_.valuelists.find("Val Name Category Filter");
    if (vl != ed_.valuelists.end())
        for (auto &en : vl->second.entries)
            if (en.index == cat)
                return en.text;
    return std::to_string(cat);
}

bool VirusPlugin::patch_matches(int bank, int program, int cat, const std::string &q)
{
    std::vector<uint8_t> d;
    {
        std::lock_guard<std::mutex> lk(banks_mu_);
        auto b = banks_.find(bank);
        if (b == banks_.end() || program >= (int)b->second.size())
            return false;
        d = b->second[program];
    }
    if (d.size() != 524)
        return false;
    if (cat && single_template_) { // categories of a stored patch, decoded once per dump (checksum)
        std::array<int, 3> c{-1, 0, 0};
        {
            std::lock_guard<std::mutex> lk(values_mu_);
            auto it = cat_cache_.find({bank, program});
            if (it != cat_cache_.end() && it->second[0] == d[522])
                c = it->second;
        }
        if (c[0] < 0) {
            std::map<std::string, int> v;
            std::string err;
            dev_.decode_dump(*single_template_, d, v, err);
            c = {d[522], v["Categories/Category 1"], v["Categories/Category 2"]};
            std::lock_guard<std::mutex> lk(values_mu_);
            cat_cache_[{bank, program}] = c;
        }
        // the filter menu lists the categories alphabetically ("Val Name Category Filter": index -> stored value)
        int want = cat;
        auto vl = ed_.valuelists.find("Val Name Category Filter");
        if (vl != ed_.valuelists.end())
            for (auto &en : vl->second.entries)
                if (en.index == cat && en.value >= 0)
                    want = en.value;
        if (c[1] != want && c[2] != want)
            return false;
    }
    if (!q.empty()) {
        std::string name(d.begin() + 249, d.begin() + 259), a = name, b = q;
        for (auto &ch : a)
            ch = (char)tolower(ch);
        for (auto &ch : b)
            ch = (char)tolower(ch);
        if (a.find(b) == std::string::npos)
            return false;
    }
    return true;
}

std::string VirusPlugin::patch_name(int part)
{
    std::string n;
    std::lock_guard<std::mutex> lk(values_mu_);
    auto p = part_values_.find(part);
    if (p == part_values_.end())
        return n;
    for (int i = 1; i <= 10; i++) {
        char k[48];
        snprintf(k, sizeof k, "Single Patch Name/Character %02d", i);
        auto c = p->second.find(k);
        n += c == p->second.end() ? ' ' : (char)c->second;
    }
    while (!n.empty() && n.back() == ' ')
        n.pop_back();
    return n;
}

void VirusPlugin::panel_message(const std::vector<uint8_t> &msg)
{
    if (env_panellog()) {
        static int n;
        if (n++ < 40) {
            fprintf(stderr, "panel:");
            for (auto b : msg)
                fprintf(stderr, " %02x", b);
            fprintf(stderr, "\n");
        }
    }
    // one controller can stand for several parameters (CC 40: Filter 1 Cutoff / Vocoder Carrier Center Frequency):
    // only the one whose condition holds now, else Ableton's Configure mode added both (round 0.8.29 W3)
    auto holds = [this](const std::string &path, int part) {
        const Parameter *p = cond_ ? cond_->find(path) : nullptr;
        return !p || meta_cond(*p, part);
    };
    for (auto &c : PanelFollow::reports_by_condition(dev_.decode(msg), holds)) {
        // a soft knob turned on the Virus: in plug-in mode it only reports the knob's value and leaves the sound alone
        // (E152, user test in REAPER: no change until the host sends it). Like the original (0x180081980 -> the soft
        // knob's setter 0x1800a2840 -> the target's setter): the panel part's destination parameter takes the value
        // unchanged and is sent to the Virus; clamped to its range so nothing out of range goes out
        if (int knob = PanelFollow::softknob_report(c.path)) {
            const int part = ui_value("Multi Parameters/Panel Part", -1);
            const std::string target = softknob_target("Softknobs/Softknob " + std::to_string(knob), part);
            if (target.empty() || part < 0 || part > 15)
                continue;
            int v = c.value;
            if (const Parameter *p = dev_.find(target))
                v = std::max(p->min, std::min(p->max, v));
            c = {target, part, v};
            if (out_)
                if (auto m = dev_.encode(target, v, part); !m.empty())
                    out_->send(m);
        }
        if (c.path == USB_IO_PATH) { // changed on the Virus: keep it as this computer's setting
            set_usb_io(c.value, true);
            continue;
        }
        if (c.path == SYNC_INFO_PATH) { // an event reached the Virus too late for its time stamp (E205)
            if (c.value == 1) {
                std::lock_guard<std::mutex> lk(sync_alert_mu_);
                sync_alert_.event();
            }
            continue;
        }
        if (c.part > 15)
            continue; // globals are not plug-in parameters
        // a preset selected on the Virus: it reports the part's Bank / Patch and then waits for the host to send the
        // patch into the edit buffer (E033, E152; else the part keeps its old sound). Only after the start-up check
        // and only for a real change, so a project's own patch is never replaced while it loads.
        if (ready_ && PanelFollow::is_preset_path(c.path) && ui_value(c.path, c.part) != c.value)
            follow_.preset_report(c.path, c.part, PanelFollow::Clock::now());
        set_part_value(c.part, c.path, c.value);
        {
            std::lock_guard<std::recursive_mutex> mk(meta_mu_);
            meta_source(c.path, c.part, c.value, false); // e.g. the FX switches follow the send knobs on the Virus
        }
        auto it = ids_.find({c.path, c.part});
        if (it != ids_.end() && editors_open_.load() > 0 && !PanelFollow::is_preset_path(c.path)) {
            // like the original: a knob moved on the Virus becomes a host edit (begin/perform/end, E304), which
            // Ableton Live's Configure mode picks up (#10); patch selections do not (the original's loads do not)
            gestures_.device_change(it->second, c.value / 127.0, now_ms());
        } else if (it != ids_.end()) {
            // the latest value per parameter: bounded by the parameter count even when the host does not call
            // process() for a while (review 2026-10-19: it grew with every knob move)
            std::lock_guard<std::mutex> lk(pending_mu_);
            auto same = std::find_if(pending_.begin(), pending_.end(), [&](const auto &pc) { return pc.first == it->second; });
            if (same != pending_.end())
                same->second = c.value / 127.0;
            else
                pending_.push_back({it->second, c.value / 127.0});
        }
    }
}

// sender thread (idle hook): the presets the Virus reported (panel_message) once it has been quiet for 150 ms, like the
// original (0x180081980 marks the part, its timer 0x18007ec30 calls 0x180083ae0): the patch from the bank cache goes
// into the part's edit buffer, then the part's known controller positions (mod wheel, ...) follow. A patch missing
// from the cache is left alone: the Virus does not load it itself in plug-in mode (E157).
void VirusPlugin::load_reported_presets()
{
    const unsigned parts = follow_.presets_due(PanelFollow::Clock::now());
    for (int part = 0; parts && part < 16; part++)
        if (parts & (1u << part)) {
            int bank = ui_value("Multi Part Parameter/Bank", part), patch = ui_value("Multi Part Parameter/Patch", part);
            // the controllers before the load: only positions the plug-in knows are sent again
            std::vector<std::pair<std::string, int>> ctl;
            {
                std::lock_guard<std::mutex> lk(values_mu_);
                auto pv = part_values_.find(part);
                if (pv != part_values_.end())
                    for (auto &p : PanelFollow::restored_controllers())
                        if (auto v = pv->second.find(p); v != pv->second.end())
                            ctl.push_back({p, v->second});
            }
            const bool ok = load_patch(bank, patch, part);
            if (ok && out_)
                for (auto &c : ctl)
                    if (auto m = dev_.encode(c.first, c.second, part); !m.empty())
                        out_->send(m);
            if (ok) {
                if (out_)
                    out_->log("Virus TI native: preset chosen on the Virus, part %d bank %d patch %d: loaded from the "
                              "bank cache\n", part + 1, bank, patch);
                continue;
            }
            // Not in the cache (no firmware.bin for the ROM banks, RAM not synced yet): left alone. Reading the edit
            // buffer back does not help - in plug-in mode the Virus changes only the name and origin there, not the
            // sound (E157: read-back = the old sound data with the new name)
            if (out_)
                out_->log("Virus TI native: preset chosen on the Virus, part %d bank %d patch %d: not in the bank cache "
                          "(%s), left alone\n", part + 1, bank, patch,
                          bank >= 4 ? "ROM banks come from firmware.bin next to Virus Control" : "RAM bank not synced yet");
        }
}

void VirusPlugin::apply_state_to_params()
{
    if (!single_template_)
        return;
    std::map<int, std::map<std::string, int>> parts;
    std::vector<std::pair<std::pair<std::string, int>, int>> editor_vals;
    {
        std::lock_guard<std::mutex> lk(state_mu_);
        for (auto &r : state_) {
            bool dump = false;
            for (int p = 0; p < 16; p++)
                if (is_part_dump(r, p)) {
                    std::string err;
                    dev_.decode_dump(*single_template_, r, parts[p], err);
                    dump = true;
                }
            std::string ep;
            int epart, ev;
            if (!dump && parse_editor_record(r, ep, epart, ev)) { // editor-only settings (link buttons)
                editor_vals.push_back({{ep, epart}, ev});
                continue;
            }
            if (!dump) // controllers and parameter SysEx of the state (volume, pan, globals)
                for (auto &c : dev_.decode(r))
                    parts[state_part(c.path, c.part)][c.path] = c.value;
        }
    }
    {
        std::lock_guard<std::mutex> lk(values_mu_);
        part_values_ = parts;
        for (auto &kv : ed_part_) // link buttons come from the state (default off)
            if (const Parameter *p = ed_.find(kv.first.first); p && p->group == "VirusControl Links")
                kv.second = p->def;
        for (auto &e : editor_vals)
            ed_part_[e.first] = e.second;
    }
    ui_dirty_ = true;
    for (auto &kv : params_) {
        auto part = parts.find(kv.second.part);
        if (part == parts.end())
            continue;
        auto v = part->second.find(kv.second.path);
        if (v != part->second.end())
            setParamNormalized(kv.first, v->second / 127.0);
    }
    meta_init();
}

// field diagnostics (diag.log): the patch name of part 1 in a state's records
static std::string part1_name(const std::vector<std::vector<uint8_t>> &recs)
{
    for (auto &r : recs)
        if (r.size() == 524 && r[0] == 0xF0 && r[6] == 0x10 && r[7] == 0 && r[8] == 0)
            return std::string(r.begin() + 249, r.begin() + 259);
    return "?";
}

void VirusPlugin::send_state()
{
    if (!out_)
        return;
    std::lock_guard<std::mutex> lk(state_mu_);
    std::vector<std::vector<uint8_t>> recs;
    for (auto &r : state_)
        if (!r.empty() && !(r.size() > 2 && r[0] == 0xF0 && r[1] == 0x7D)) { // not our editor-only records
            recs.push_back(r);
            auto &m = recs.back(); // a project's USB IO configuration: this computer's setting wins
            if (m.size() == 11 && m[0] == 0xF0 && m[6] == 0x73 && m[7] == 0x00 && m[8] == 0x09)
                m[9] = (uint8_t)usb_io_.load();
        }
    const int n = (int)recs.size();
    const int dropped = out_->send_state(std::move(recs));
    out_->log("Virus TI native: state sent (%d messages, part 1 \"%s\", device %s)%s\n", n, part1_name(state_).c_str(),
              out_->connected() ? "connected" : "not yet connected",
              dropped ? (" - replaces " + std::to_string(dropped) + " queued records of the previous state").c_str() : "");
}

tresult PLUGIN_API VirusPlugin::setState(IBStream *state)
{
    std::vector<uint8_t> d;
    char buf[4096];
    int32 got = 0;
    while (state->read(buf, sizeof buf, &got) == kResultOk && got > 0)
        d.insert(d.end(), buf, buf + got);
    auto recs = parse_container(d);
    if (out_)
        out_->log("Virus TI native: setState %zu bytes, %zu records, part 1 \"%s\"\n", d.size(), recs.size(),
                  part1_name(recs).c_str());
    if (recs.empty())
        return kResultFalse;
    // part dumps of an older format: upgraded as the Virus does on receipt, so the editor shows what it plays and
    // later edits of the newer fields (Input Mode, unison, envelopes 3/4 ...) reach it (E174)
    if (int up = upgrade_state_dumps(recs); up && out_)
        out_->log("Virus TI native: %d part dump(s) of the project upgraded to the current format\n", up);
    {
        std::lock_guard<std::mutex> lk(state_mu_);
        state_ = std::move(recs);
    }
    state_loaded_ = true;
    apply_state_to_params();
    send_state();
    return kResultOk;
}

tresult PLUGIN_API VirusPlugin::getState(IBStream *state)
{
    std::vector<uint8_t> d;
    {
        std::lock_guard<std::mutex> lk(state_mu_);
        d = build_container(state_.empty() ? default_ : state_);
    }
    int32 done = 0;
    return state->write(d.data(), (int32)d.size(), &done);
}

} // namespace vti
