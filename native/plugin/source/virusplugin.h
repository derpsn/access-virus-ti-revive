// virusplugin.h - the native Virus TI plug-in (R3): single-component VST3 like the original, same parameter ids
// (automation.xml + 32 MIDI-mapping targets) and the same state format (docs/formats.md), talking to the Virus
// through the TI Revive broker. Audio and editor follow in later steps.
#pragma once
#include "access/accessfiles.h"
#include "device/audioin.h"
#include "editor/editormodel.h"
#include "editor/lcdhover.h"
#include "device/automation.h"
#include "device/broker.h"
#include "device/keepalive.h"
#include "device/memsync.h"
#include "device/sendbudget.h"
#include "device/cablerule.h"
#include "device/dumppacer.h"
#include "device/startprogress.h"
#include "device/transport.h"
#include "device/virusrate.h"
#include "device/audiogate.h"
#include "device/hostmidi.h"
#include "device/sendguard.h"
#include "device/stamprule.h"
#include "device/offlinepace.h"
#include "device/blockevents.h"
#include "device/outputdelay.h"
#include "device/timelinefollow.h"
#include "device/presencewatch.h"
#include "device/stallwatch.h"
#include "model/bankdrop.h"
#include "model/hostgestures.h"
#include "model/patchhistory.h"
#include "model/syncalert.h"
#include "device/resampler.h"
#include "device/sidechain.h"
#include "model/devicestatus.h"
#include "model/panelfollow.h"
#include "model/remotedevice.h"
#include "model/sysinfo.h"
#include "model/remotetemplates.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "public.sdk/source/vst/vstsinglecomponenteffect.h"
#include <array>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <set>
#include <chrono>
#include <thread>
#include <vector>

namespace vti {

// Original class ids (formats.md). The development build uses its own id (byte 15 XOR 0xA5) so it can sit next to
// the original; VTI_NATIVE_DROPIN=ON builds with the original id (loads projects saved with the original).
extern const Steinberg::FUID kVirusTIClassId;
extern const Steinberg::FUID kOriginalClassId; // the Access plug-in's id: a second class for its projects (E221)

// Messages to the Virus, sent by a worker thread (socket writes do not belong in process()).
class DeviceOut {
public:
    DeviceOut();
    ~DeviceOut();
    // Connect to the broker (and send what is queued) only from now on: the plug-in calls it on its first
    // setActive(true). A host's plug-in scan (initialize + terminate, no activation) never reaches the Virus.
    void start() { go_ = true; }
    void send(std::vector<uint8_t> msg); // a complete MIDI message (SysEx or channel message); not from process()
    // the same on another USB-MIDI cable of the plug-in endpoint: the original sends remote template records on
    // cable 3 (E199), everything else on cable 2
    void send(std::vector<uint8_t> msg, uint8_t cable);
    // a whole state upload: replaces the records of an earlier upload that are still queued (only the newest state
    // counts; a waiting instance got two before its takeover, E149). Returns how many queued records it dropped.
    int send_state(std::vector<std::vector<uint8_t>> records);
    // from process() only (single producer, lock-free): a message of up to 16 bytes; false if the ring is full
    // `stamp` = frame on the Virus timeline (F5 time stamp before the message), -1 = the latest timeline position
    bool send_rt(const uint8_t *msg, size_t n, int64_t stamp = -1);
    // events process() could not even collect (more than BlockEvents::MAX in one block): counted and logged by the
    // worker like a full queue; a note-off among them makes the worker send all notes off
    void collect_lost(int n, bool note_off)
    {
        ev_lost_.fetch_add(n, std::memory_order_relaxed);
        if (note_off)
            rt_lost_off_.store(true, std::memory_order_relaxed);
    }
    // after a block's send_rt calls: wakes the worker (no lock; one notify per block instead of a 1 ms poll)
    void wake_rt() { cv_.notify_one(); }
    std::atomic<int64_t> timeline{-1};
    bool busy() const { return queued_.load() > 0; } // non-realtime messages (state upload) still waiting
    int queued() const { return queued_.load(); } // start-up messages and state still to go (the start page's ring)
    bool dumping() const { return dump_pending_.load(); } // a dump in pieces: nothing else may go between them
    // USB control request through the broker (device descriptor / strings)
    int control(uint8_t rt, uint8_t req, uint16_t value, uint16_t index, uint8_t *data, uint16_t len)
    {
        return broker_.control(rt, req, value, index, data, len);
    } // the latest timeline position (process() block start), for unstamped messages
    // messages from the Virus' panel (EP 0x86 cable 2: knob moves as CC / poly pressure / parameter SysEx)
    void on_panel(std::function<void(const std::vector<uint8_t> &)> h) { panel_ = std::move(h); }
    // SysEx replies (dumps, bank info): EP 0x86 cable 3 in a plug-in session, EP 0x85 cable 1 otherwise
    void on_reply(std::function<void(const std::vector<uint8_t> &)> h) { reply_ = std::move(h); }
    // called by the worker every loop (<= 1 ms): non-realtime follow-up of process() (state updates)
    void on_idle(std::function<void()> h) { idle_ = std::move(h); }
    // the Virus went away (USB unplugged / switched off) or came back while the broker connection stays
    void on_device(std::function<void(bool present)> h) { device_ = std::move(h); }
    // the stream ran on after standing still (sleep, StallWatch): the Virus may have lost its set-up meanwhile (E210)
    void on_resume(std::function<void()> h) { resume_ = std::move(h); }
    // another instance in this process owns the Virus: this one waits (true), or takes over after the wait (false)
    void on_waiting(std::function<void(bool waiting)> h) { on_waiting_ = std::move(h); }
    // every change of (present, another program has it), from the first check on (the editor's status pages)
    void on_status(std::function<void(bool present, bool elsewhere)> h) { status_ = std::move(h); }
    // another instance has the Virus: in this process (claim_owner) or another program's plug-in (broker state 2)
    bool waiting() const { return wait_reported_.load() || broker_.in_use_elsewhere(); }
    std::atomic<int> generation{0}; // +1 on every return of the Virus (process() re-anchors its timeline)
    StallWatch stall_;              // worker: the stream stood still and runs on (sleep) -> generation + 1
    bool connected() const { return broker_.device_present(); }
    // read outside the worker only while holding an AudioGate::Use of audio_gate() (it may be unmapped otherwise)
    AudioIn &audio() { return audio_; }
    AudioGate &audio_gate() { return audio_gate_; }
    bool audio_ready() const { return audio_gate_.is_open(); }
    // R5 sync marker (docs/protocol.md "Sync marker"): after the sync ping the Virus puts one non-zero sample on USB 2
    // where its 14-bit time stamp clock is 0, so a stamp s plays at the ring frame marker + s (mod 16384).
    // process() calls calibrate() with the ring frame of its ping; the worker finds the marker (-1 = searching or
    // unknown, -2 = not found: stamps stay relative to the ring like before)
    // Every ping RESETS that clock, and the Virus can answer seconds late (Windows: start-up dumps still being
    // digested), so a ping waits up to 5 s for its marker and the newest marker always wins
    void calibrate(int64_t from, bool new_stream)
    {
        marker_from_ = from;
        pings_out_.fetch_add(1);
        if (new_stream || marker.load() < 0) // a new audio stream (start, the Virus came back): the old one is void
            marker = -1;
    }
    bool calibrating() const { return pings_out_.load() > 0; }
    std::atomic<int64_t> marker{-2};
    std::atomic<int> rate{44100}; // the Virus' sample rate (44100; 48000 when it follows the host, E323)
    // rate following (design-rate.md step 5): the Virus rate this instance wants (the host rate mapped like the
    // original, virusrate.h) - the audio start asks for it; request_rate() asks now and waits until the worker did
    // (true = the Virus runs at it). From setActive only, never from process(): it blocks up to timeout_ms.
    std::atomic<int> want_rate{44100};
    bool request_rate(int rate, int timeout_ms);
    std::atomic<int64_t> lead{0}; // frames from a timeline position to its sound in the ring (VirusPlugin::lead())
    // the newest ring frame that had sound on USB 1 / USB 2 (the ping waits for 1 s of silence, like the original)
    std::atomic<int64_t> last_sound{-1};
    // R5 diagnostics (VTI_NATIVE_STATS, every 10 s): stamp-aligned reads, their smallest distance to the newest frame,
    // timeline drift steps
    std::atomic<int64_t> aligned_blocks{0}, read_margin_min{INT64_MAX}, tl_steps{0};
    // process() calls/samples and timeline events (anticipative hosts call in bursts ahead of real time)
    std::atomic<int64_t> host_frames{0}, anchors{0}, err_min{INT64_MAX}, err_max{INT64_MIN};
    // the Sidechain bus towards the Virus' USB input ("3 Outs / 1 Input", E167): frames sent, refills (start, the
    // broker ran dry), pauses for another writer of the ring (the ASIO driver), the converter's rate correction
    // (ppm), the smallest/largest queue seen before a block
    std::atomic<int64_t> sc_frames{0}, sc_refills{0}, sc_yields{0}, sc_corr_ppm{0}, sc_queued_min{INT64_MAX},
        sc_queued_max{INT64_MIN};
    std::atomic<int> sc_in_peak{0}; // the highest sidechain input sample (x 32767) since the last stats line
    // R5 lines also go to this file (diag.on, field tests); set once after construction, read by the worker
    void set_diag_path(const std::string &p)
    {
        snprintf(diag_path_, sizeof diag_path_, "%s", p.c_str());
        diag_on_.store(true, std::memory_order_release);
        // klog.on next to diag.log also enables the R5 log (a DAW started from the desktop misses new env variables)
        std::string k = p.substr(0, p.find_last_of("/\\") + 1) + "klog.on";
        if (FILE *f = fopen(k.c_str(), "rb")) {
            fclose(f);
            klog_.store(true);
        }
    }
    void log(const char *fmt, ...);
    // the broker's release version from its INFO message ("" from older brokers): PATCH UTILITY > INFO (E189)
    std::string broker_version() const { return broker_.broker_version(); }
    // the Virus' OS version from its flash ("" until read, or from an older broker)
    std::string tios() const
    {
        std::lock_guard<std::mutex> g(info_mu_);
        return tios_;
    }

private:
    std::atomic<int> rate_req_{0};                       // a rate request for the worker (0 = none)
    std::atomic<unsigned> rate_req_seq_{0}, rate_done_seq_{0}; // requests made / handled
    mutable std::mutex info_mu_;
    std::string tios_;
    std::function<void(bool)> device_;
    std::function<void(bool)> on_waiting_;
    std::function<void(bool, bool)> status_;
    StatusChange status_change_; // worker: the editor's status page, reported on change
    // One instance per process talks to the Virus (README "One plug-in instance per synth"; issue #6). The Virus has
    // one stamp clock and every sync ping resets it, so a second instance (Ableton: duplicated track) shifted the
    // first one's time stamps for good. The Windows broker shares the Virus between all clients (the ASIO driver
    // runs in the same process), so the guard lives here; macOS/Linux brokers also give it to one client only.
    bool claim_owner();
    void release_owner();
    bool owner_ = false;         // worker: this instance holds the process-wide claim
    std::atomic<bool> wait_reported_{false}; // waiting for the other instance (set by the worker)
    int present_seen_ = -1; // worker: last device presence handled (-1 = unknown)
    vti::PresenceWatch presence_; // worker: gone / back / gone and back between two looks (E212)
    Broker broker_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::vector<uint8_t> pk_; // worker: the packets of one broker write
    std::atomic<bool> klog_{getenv("VTI_NATIVE_KLOG") != nullptr}; // R5 diagnostic, see klog_check; or klog.on
    struct KlogNote {
        int64_t stamp, sent, scanned; // stamp, newest frame when sent, ring frames scanned so far
        int quiet;                    // quiet frames before the scan position
    };
    std::vector<KlogNote> klog_stamps_;
    void klog_check();
    std::chrono::steady_clock::time_point queue_hold_until_{}; // queued messages wait (the Virus busy with a multi)
    char diag_path_[1024] = {};
    std::atomic<bool> diag_on_{false};
    void scan_audio(); // worker: silence tracking + marker search
    std::atomic<int64_t> marker_from_{-1};
    std::atomic<int> pings_out_{0}; // pings sent whose marker has not been seen yet
    int64_t marker_skip_ = -1;      // worker: frames up to here belong to the last marker
    int64_t scanned_ = -1; // ring frames up to here were looked at
    bool elsewhere_seen_ = false; // worker: broker state 2 seen (another program's plug-in has the Virus)
    struct Queued {
        std::vector<uint8_t> msg;
        bool state;        // part of a state upload (send_state)
        uint8_t cable = 2; // USB-MIDI cable
    };
    std::deque<Queued> queue_;
    std::atomic<int> queued_{0};
    struct Slot {
        uint8_t len, data[16];
        int64_t stamp;
    };
    static constexpr size_t RT_SLOTS = 1024;
    Slot rt_[RT_SLOTS];
    std::atomic<size_t> rt_w_{0}, rt_r_{0};
    EventRun rt_run_; // real-time messages sent in a row, the queue's turn after 8 (worker thread, dumppacer.h)
    DumpPacer dump_;            // a single dump on cable 3 in pieces, events between them on cable 5 (E270)
    HeldParams held_;           // automation that fell into a dump, sent when it is out (E274)
    std::atomic<bool> dump_pending_{false}; // the dump counts as queued (busy()) until its last piece is out
    SendBudget budget_; // bytes per second towards the DSP's 2046-byte stamp queue (E261)
    std::atomic<int> blocked_{0}; // messages rule 2 forbids, dropped (sendguard.h); logged by the worker
    std::atomic<int> rt_lost_{0};        // events process() could not queue (ring full); logged by the worker
    std::atomic<bool> rt_lost_off_{false}; // a note-off was among them: the worker sends all notes off
    int rt_lost_logged_ = 0;
    std::atomic<int> ev_lost_{0}; // events process() could not collect (collect_lost); logged by the worker
    int ev_lost_logged_ = 0;
    int blocked_logged_ = 0;
    std::atomic<bool> stop_{false};
    bool open_ = false;
    AudioIn audio_;
    AudioGate audio_gate_; // open while audio_ is mapped; closed (waiting for readers) before it is unmapped
    bool audio_ok() const { return audio_gate_.is_open(); }
    void close_audio();    // worker: no reader left, then unmap
    int start_audio();     // worker: the stream and its rate; the broker's answer (1 = streaming)
    void drop_dump();      // worker: the Virus or the broker went away mid-dump
    void lose_broker();    // worker: the broker connection is gone; drop the dump, close, reopen in the next round
    std::chrono::steady_clock::time_point audio_retry_{}; // next audio start attempt (worker thread)
    std::chrono::steady_clock::time_point stats_next_{};  // VTI_NATIVE_STATS output
    int64_t stats_newest_ = -1; // worker: the newest ring frame at the last stats line (per instance, issue #6)
    std::function<void(const std::vector<uint8_t> &)> panel_, reply_;
    std::function<void()> idle_, resume_;
    bool rx_set_ = false, pending_send_ = false;
    std::atomic<bool> go_{false};
    std::thread worker_;
    void run();
    void check_presence(); // worker: the Virus went away / came back
    void report_status();  // worker: the status page from presence, the other program and the other instance
};

class VirusPlugin : public Steinberg::Vst::SingleComponentEffect, public Steinberg::Vst::IMidiMapping {
public:
    static Steinberg::FUnknown *create(void *) { return (Steinberg::Vst::IAudioProcessor *)new VirusPlugin; }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown *context) override;
    Steinberg::tresult PLUGIN_API terminate() override;
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) override;
    Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup &setup) override;
    // the Sidechain input: which buses the host activates (some hosts then pass only those), stereo only like the
    // original's 2-channel input (a 4-pin offer made REAPER map the sidechain to unused pins, E171)
    Steinberg::tresult PLUGIN_API activateBus(Steinberg::Vst::MediaType type, Steinberg::Vst::BusDirection dir,
                                              Steinberg::int32 index, Steinberg::TBool state) override;
    Steinberg::tresult PLUGIN_API setBusArrangements(Steinberg::Vst::SpeakerArrangement *inputs, Steinberg::int32 numIns,
                                                     Steinberg::Vst::SpeakerArrangement *outputs,
                                                     Steinberg::int32 numOuts) override;
    Steinberg::tresult PLUGIN_API canProcessSampleSize(Steinberg::int32 symbolicSampleSize) override;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData &data) override;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream *state) override;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream *state) override;

    Steinberg::IPlugView *PLUGIN_API createView(Steinberg::FIDString name) override;

    // editor support (viruseditor.cpp)
    const AccessFiles &files() const { return *files_; }
    const EditorLayout &layout() const { return layout_; }
    // the editor's open and close in the log: REAPER's crash after a render needs the editor open (E287)
    void editor_event(const char *what)
    {
        if (out_)
            out_->log("Virus TI native: editor %s\n", what);
    }
    // the classic oscillator display's waves (E180), read from the binary on the first editor open, not at
    // initialize (plug-in scans stay fast); UI thread only
    const std::vector<float> &osc_waves()
    {
        if (!osc_waves_read_ && files_) {
            osc_waves_ = files_->osc_waves();
            osc_waves_read_ = true;
        }
        return osc_waves_;
    }
    const Conditions &conditions() const { return *cond_; }
    const RemoteDevice &device() const { return dev_; }
    const RemoteDevice &editor_device() const { return ed_; }
    int ui_value(const std::string &path, int part);              // device, controller or editor-state value
    void ui_set(const std::string &path, int part, int value);    // a change made in the editor
    void hover(const std::string &path, int part); // the control under the mouse on the LCD ("" = none, E353)
    std::string patch_name(int part);
    std::string bank_patch(int bank, int program); // stored patch name from the bank cache (RAM A..D)
    bool load_patch(int bank, int program, int part); // a stored patch into a part's edit buffer (and the state)
    // the same, chosen in the editor (LOAD menu, browser): remembered for "Revert to" / "Recently Used" (E220)
    bool load_patch_remembered(int bank, int program, int part);
    bool load_recent(int part, int index); // a "Recently Used" entry (the list keeps its order)
    bool revert_patch(int part);           // "Revert to": swaps the part's patch with the one before the last load
    std::string revert_name(int part);     // empty: no "Revert to" entry
    std::vector<std::string> recent_names(int part);
    bool copy_part(int from, int to);                    // a part's patch onto another part (drag and drop)
    bool load_dump(std::vector<uint8_t> d, int part);    // a single dump into a part's edit buffer
    bool rename_part(int part, const std::string &name); // the part's patch name (10 characters) -> dump + Virus
    bool store_patch(int part, int bank, int program);   // the part's patch into a RAM slot of the Virus (+ cache)
    // browser drag and drop onto a RAM slot of the other pane (E294): `programs` of `src_bank` into RAM `bank` from
    // `slot` on, like the original; false = nothing sent (no RAM target, not synced, bank sync running)
    bool drop_to_ram(int src_bank, const std::vector<int> &programs, int bank, int slot);
    // a drag within one RAM bank: `programs` swap places with the slots from `slot` on (E297); false = nothing sent
    bool swap_in_ram(int bank, const std::vector<int> &programs, int slot);
    bool rename_in_ram(int bank, int program, const std::string &name); // Alt+Click in the browser (E297 (D))
    bool sort_ram_bank(int bank);  // "Sort alphabetically" on RAM: only the changed slots, not undoable (E302)
    bool download_bank(int bank);  // "Download entire bank from Virus TI": all 128 read again in the background (E301)
    bool downloading() const { return downloading_.load(); }
    // personal libraries in the browser (E297, E298): files only, undoable like the RAM operations
    bool drop_to_library(int src_bank, const std::vector<int> &programs, int library, int slot);
    bool swap_in_library(int library, const std::vector<int> &programs, int slot);
    bool delete_from_library(int library, const std::vector<int> &programs);
    bool sort_library(int library);
    bool rename_in_library(int library, int program, const std::string &name);
    bool can_undo_drop();
    void editor_opened();  // the editor's open(): knob moves from the Virus become host edits
    void editor_closed();  // its close(): open edits end
    void tick_gestures();  // the editor's timer (UI thread): beginEdit / performEdit / endEdit
    bool undo_drop(); // "Undo last operation": writes back what the last drop overwrote
    // the part's patch into a personal library (slot = position, == size: append) - writes the library file
    bool store_to_library(int part, int library, int slot);
    // the original's "Create Empty Library" (E217): an empty library file at the path chosen in the save dialog
    // (".mid" added like the original), listed at once
    bool create_library(const std::string &chosen);
    // does a stored patch have category `cat` (filter menu index, 0 = any) and contain `q` in its name?
    bool patch_matches(int bank, int program, int cat, const std::string &q);
    static constexpr int SEARCH_BANK = 1000;             // "Search Results" (search://): the current page
    bool results_step(int dir);                          // previous / next results page (false: none)
    std::string results_title();
    void update_results();
    void warm_categories(); // background: fill the category cache
    std::atomic<bool> stop_warm_{false}; // terminate(): stops the bank sync and the category pass
    // the host plays (transport running or notes): steady_clock milliseconds of the last activity (audio thread)
    std::atomic<int64_t> last_play_ms_{0};
    std::atomic<int64_t> last_note_ms_{0}; // last note-on from the host (pauses the bank sync)
    int64_t sync_paused_ms_ = 0;           // bank sync thread only: time paused for notes so far
    std::atomic<bool> sync_paused_{false}; // the bank sync waits for a gap between notes
    std::atomic<int> sync_eta_s_{-1};       // see sync_eta()
    int marker_retries_ = 0; // process(): sync pings sent again after "no marker"
    static int64_t now_ms()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    bool wait_until_quiet(); // the bank sync's before_request: waits while notes arrive (playing has priority)
    void wait_virus_ready(); // the bank sync thread at start-up / takeover: probe, then the sync marker (see .cpp)
    void set_sync_progress(int v);   // the original's "Syncing memory" page: 0..126 shown, 127 hidden
    void set_recall_progress(int v); // its "Starting up" page with the ring (startprogress.h)
    // both at once: the layout shows "Starting up" only while InitialSyncProgress is 127 and "Syncing" only while
    // RecallProgress is 127 - set one after the other, the editor would show for a moment in between (user, L4)
    void set_start_pages(int recall, int sync);
    std::string category_name_(); // the selected category filter's name
    void set_search(const std::string &s);
    std::string search() { std::lock_guard<std::mutex> lk(values_mu_); return search_; }
    // device banks 0..3 RAM, 4.. ROM; personal libraries from bank id 100 on
    static constexpr int LIB_BASE = 100;
    int bank_count() // device banks only
    {
        std::lock_guard<std::mutex> lk(banks_mu_);
        int n = 4;
        for (auto &kv : banks_)
            if (kv.first < LIB_BASE)
                n = std::max(n, kv.first + 1);
        return n;
    }
    int library_count() { std::lock_guard<std::mutex> lk(banks_mu_); return (int)lib_names_.size(); }
    std::string library_name(int i) { std::lock_guard<std::mutex> lk(banks_mu_); return i < (int)lib_names_.size() ? lib_names_[i] : ""; }
    // the menu a library is listed in ("Personal Libraries", "User Libraries", "Public Libraries", E307)
    std::string library_root(int i) { std::lock_guard<std::mutex> lk(banks_mu_); return i < (int)lib_roots_.size() ? lib_roots_[i] : ""; }
    // the menus to show, in the original's order: "Personal Libraries" always (it has "Create Empty Library"), the
    // others when they hold a library
    std::vector<std::string> library_menus()
    {
        std::lock_guard<std::mutex> lk(banks_mu_);
        std::vector<std::string> m = {"Personal Libraries"};
        for (const char *r : {"User Libraries", "Public Libraries"})
            if (std::find(lib_roots_.begin(), lib_roots_.end(), r) != lib_roots_.end())
                m.push_back(r);
        return m;
    }
    int library_size(int i)
    {
        std::lock_guard<std::mutex> lk(banks_mu_);
        auto b = banks_.find(LIB_BASE + i);
        return b == banks_.end() ? 0 : (int)b->second.size();
    }
    std::string device_name() const // as the original shows it in the browser and the menus
    {
        return model_ == 1 ? "Virus TI (Virus TI Desktop)" : model_ == 2 ? "Virus TI Snow (Virus TI Snow)"
                                                                          : "Virus TI2 (Virus TI2 Desktop)";
    }
    std::string bank_title(int bank) // browser title
    {
        if (bank == SEARCH_BANK)
            return results_title();
        return bank >= LIB_BASE ? library_root(bank - LIB_BASE) + "/" + library_name(bank - LIB_BASE)
                                : device_name() + "/" + bank_name(bank);
    }
    static std::string bank_name(int bank) // 0..3 = RAM A..D, 4.. = ROM A..
    {
        return bank < 4 ? std::string("RAM ") + (char)('A' + bank) : std::string("ROM ") + (char)('A' + bank - 4);
    }
    bool ui_changed() { return ui_dirty_.exchange(false); }
    bool sync_paused() const { return syncing_.load() && sync_paused_.load(); } // the sync page's note
    // estimated seconds the bank sync still needs (-1: unknown or done); a first sync reads 512 programs at ~1 s each
    int sync_eta() const { return syncing_.load() ? sync_eta_s_.load() : -1; }
    bool lcd_changed() { return lcd_dirty_.exchange(false); } // only the LCD texts changed
    // field diagnostics (file ~/.local/share/virus-ti-native/diag.on enables a log every 10 s in diag.log)
    struct Diag {
        std::atomic<long> full_draws{0}, part_draws{0}, draw_us{0}, dirty_ticks{0}, process_calls{0};
        std::atomic<long> process_max_us{0}, process_over{0}; // over = longer than the block's real time
        std::atomic<long> block_max{0}; // largest block the host passed (E187: crackling at large buffers)
    } diag;
    std::atomic<int64_t> diag_next_ms_{0}; // diag_report: when the next line is due
    bool diag_enabled();
    void diag_report(); // editor timer and device worker
    // MIDI activity meters of the part strips (note-on velocity per MIDI channel, like the original's
    // "ShowMIDIActivityMeters"): tick_meters() decays them and returns the channels whose shown value changed (bits)
    int tick_meters(int step);
    bool meters_pending() const // a meter still to show or decay (the editor skips its idle preloading then)
    {
        for (int ch = 0; ch < 16; ch++)
            if (meter_shown_[ch] || meter_[ch].load(std::memory_order_relaxed))
                return true;
        return false;
    }
    int meter(int channel) const { return channel >= 0 && channel < 16 ? meter_shown_[channel] : 0; }
    std::string live_text(const std::string &param);
    std::string remote_name(int t) const // REMOTE page list
    {
        std::lock_guard<std::mutex> lk(remote_mu_);
        return remote_.name(t);
    }
    // REMOTE page: Template Name / Knob Name / MIDI String of the selected template and knob edited (false: no such
    // field); the templates are saved at once and the changed record goes to the Virus like the original's (E199)
    bool set_remote_text(const std::string &param, const std::string &text);
    // REMOTE page template files (*.vrt, E198) like the original's two places: "Personal Libraries" (the Patches
    // folder; the installer's factory templates are in its "Remote Templates") and "Personal Remote Templates"
    // (Documents/Access Music/Virus TI/Remote Templates, where new ones go): (menu name, folder). Loading puts a file
    // into the selected template and sends its records like the original (E200), saving writes the selected template
    // ("" = a new file named after the template in Personal Remote Templates) and sends nothing.
    std::vector<std::pair<std::string, std::string>> remote_template_folders() const;
    // REMOTE page: a double-click chooses template t like the original's (E199): "Remote Patch" = t, then the
    // template's 33 records; edits and LOAD send their records too (cable 3)
    void choose_remote_template(int t);
    bool load_remote_template(const std::string &path);
    bool save_remote_template(const std::string &path);
    // the Knob Template menu's entries, read from the user's plug-in binary on first use (E197); empty if not found
    std::vector<KnobTemplate> knob_templates();
    // LCD texts of the last edited parameter
    // "Softknobs/Softknob N" of a part: the parameter its "Soft Knobs/Destination N" points to (SoftknobMapping),
    // "" when unassigned or for other paths. The original's soft knob is a proxy of that parameter (E150).
    std::string softknob_target(const std::string &path, int part);

    // IMidiMapping: channel aftertouch / pitch bend -> the original's per-channel parameters
    Steinberg::tresult PLUGIN_API getMidiControllerAssignment(Steinberg::int32 busIndex, Steinberg::int16 channel,
                                                              Steinberg::Vst::CtrlNumber midiControllerNumber,
                                                              Steinberg::Vst::ParamID &id) override;

    OBJ_METHODS(VirusPlugin, SingleComponentEffect)
    DEFINE_INTERFACES
    DEF_INTERFACE(IMidiMapping)
    END_DEFINE_INTERFACES(SingleComponentEffect)
    REFCOUNT_METHODS(SingleComponentEffect)

private:
    struct Mapped {
        std::string path;
        int part = 0; // 0-based
        int pending = -1; // automation throttle (audio thread): the latest value waiting to be sent
    };
    // automation budget: a flood of parameter messages (stress test: 32 lanes per block, ~2750/s) hangs the
    // Virus' audio until a power cycle - changes are coalesced per parameter and sent at <= AUTOMATION_RATE/s
    static constexpr double AUTOMATION_RATE = 250; // 1000/s still hung together with 2750 notes/s
    AutomationQueue automation_{AUTOMATION_RATE};
    RemoteDevice dev_, ed_;
    std::unique_ptr<AccessFiles> files_;
    EditorLayout layout_;
    std::vector<float> osc_waves_;
    bool osc_waves_read_ = false;
    std::unique_ptr<Conditions> cond_;
    std::mutex values_mu_;
    std::map<int, std::map<std::string, int>> part_values_; // decoded part dumps + controllers, per part (-1 = global)
    std::map<int, std::map<std::string, int>> stored_values_; // the same as loaded (patch, project): the LCD's [value]
    std::map<std::string, int> ui_state_;                   // editor-only parameters (VirusControl/SelectedPage ...)
    std::mutex ui_state_mu_; // ui_state_ is used by the UI, worker (meta conditions) and sync threads
    bool ui_state_get(const std::string &path, int &v)
    {
        std::lock_guard<std::mutex> lk(ui_state_mu_);
        auto it = ui_state_.find(path);
        if (it == ui_state_.end())
            return false;
        v = it->second;
        return true;
    }
    bool ui_state_set(const std::string &path, int v) // false: unchanged
    {
        std::lock_guard<std::mutex> lk(ui_state_mu_);
        auto [it, added] = ui_state_.emplace(path, v);
        if (!added && it->second == v)
            return false;
        it->second = v;
        return true;
    }
    // meta parameters (editordevice.xml meta="true", e.g. filter link, FX switches, LFO clocks): per-part editor values
    // of "part all" editor parameters, meta values, last condition state of retrigger metas (values_mu_)
    std::map<std::pair<std::string, int>, int> ed_part_, meta_vals_;
    std::map<std::pair<std::string, int>, bool> meta_cond_;
    std::recursive_mutex meta_mu_;
    void meta_index();
    bool meta_indexed_ = false; // meta_mu_
    std::unordered_map<std::string, std::vector<std::pair<const Parameter *, const MetaClient *>>> meta_sources_;
    std::vector<const Parameter *> meta_retriggers_;
    int meta_depth_ = 0; // > 0 while the engine sets linked parameters (no LCD update for those)
    void meta_source(const std::string &path, int part, int value, bool full);
    void meta_set(const Parameter &m, int part, int mv, const std::string &except, bool full);
    void meta_retrigger(int part, bool full);
    void meta_init();
    bool meta_cond(const Parameter &m, int part);
    void set_editor_value(const std::string &path, int part, int value, bool persist = false);
    std::string lcd_param_, lcd_value_, lcd_part_, lcd_orig_; // the last edited parameter for the LCD
    LcdHover lcd_hover_; // values_mu_
    std::string value_text(const Parameter &p, int value) const; // a value as the LCD shows it (value list text)
    SystemInfo sysinfo_{VTI_NATIVE_VERSION, {}, {}}; // PATCH UTILITY > INFO (values_mu_): versions, Virus OS
    // PATCH UTILITY > REMOTE (E193, E196, E199): edited here, kept in our cache file and sent to the Virus like the
    // original's
    RemoteTemplates remote_;
    mutable std::mutex remote_mu_;
    std::string remote_path_; // our RemoteSets file
    std::vector<KnobTemplate> knob_templates_;
    bool knob_templates_read_ = false;
    void load_remote_templates();
    void save_remote_templates(); // remote_mu_ held
    void send_remote(const std::vector<std::vector<uint8_t>> &records); // to the Virus on cable 3 (not remote_mu_)
    void set_lcd(const std::string &param, const std::string &value, const std::string &part, const std::string &before);
    std::string search_;                                    // browser search text
    int results_return_ = 0;                                // browser 0's bank before the results
    std::vector<std::vector<uint8_t>> results_;             // all search results (banks_mu_)
    int results_page_ = 0;
    void show_results_page();
    std::map<std::pair<int, int>, std::array<int, 3>> cat_cache_; // (bank, program) -> checksum, category 1, 2
    std::atomic<bool> ui_dirty_{true}, lcd_dirty_{false};
    std::atomic<int> meter_[16] = {};
    int meter_shown_[16] = {}; // editor thread
    // automation values from process() for the part dumps (single producer / single consumer, lock-free)
    struct ParamUpdate {
        Steinberg::Vst::ParamID id;
        int value;
    };
    static constexpr size_t PU_SLOTS = 4096;
    ParamUpdate pu_[PU_SLOTS];
    std::atomic<size_t> pu_w_{0}, pu_r_{0};
    void drain_param_updates();
    // a preset chosen on the Virus itself (it reports the part's Bank / Patch): like the original, 150 ms after the
    // last report the stored patch from the bank cache goes into that part's edit buffer (sender thread, idle hook)
    vti::PanelFollow follow_;
    void load_reported_presets();
    std::vector<std::vector<uint8_t>> startup_messages() const; // GlobalInit + ParamInit (+ USB IO configuration)
    std::map<int, std::vector<std::vector<uint8_t>>> banks_; // RAM A..D (bank cache, see load_banks), ROM, libraries
    std::vector<std::string> lib_names_, lib_paths_, lib_roots_; // lib_roots_: the library's menu
    std::mutex banks_mu_;
    void load_banks();
    std::unique_ptr<MemorySync> sync_;
    std::thread sync_thread_;
    // the Virus came back (switched off and on, replugged): the readiness probe and note hold of a first start again
    // (worker thread: the sender thread itself must not wait for the probe's reply)
    std::thread reconnect_thread_;
    std::mutex reconnect_mu_; // after_reconnect() starts reconnect_thread_, terminate() joins it: never both at once
    std::atomic<bool> reconnect_busy_{false}, reconnect_again_{false};
    std::atomic<bool> absent_at_start_{false}; // the start-up check found no Virus: its first appearance waits too
    void after_reconnect();
    void resend_to_virus(); // start-up messages + state + readiness probe, after the Virus came back or woke up
    bool probe_virus_ready(); // the readiness probe (30 00 00 behind everything queued); false: no reply
    std::atomic<bool> sync_started_{false};
    void sync_banks(); // brings the bank cache up to date with the Virus (background thread)
    void load_rom_banks(); // ROM A.. from the user's firmware.bin for the connected model
    std::atomic<bool> syncing_{false};
    std::atomic<bool> downloading_{false}; // download_bank's thread is reading a bank
    std::thread download_thread_;
    std::mutex download_mu_; // starting / joining download_thread_
    std::atomic<bool> sync_expected_{false}; // a RAM bank has no cache file: the start-up reads it (sync page early)
    std::atomic<bool> starting_{false}; // start-up messages + state on their way: note-ons are held back
    // the Virus answered the readiness probe (wait_virus_ready): it has worked through the start-up messages and the
    // state, so the timeline may anchor and send the sync ping
    std::atomic<bool> ready_{false};
    std::atomic<int> model_{0}; // 0 = TI2, 1 = TI, 2 = Snow (from the device descriptor)
    std::map<Steinberg::Vst::ParamID, Mapped> params_;
    std::map<std::pair<std::string, int>, Steinberg::Vst::ParamID> ids_; // (path, part 0-based) -> id
    std::mutex pending_mu_;
    std::vector<std::pair<Steinberg::Vst::ParamID, double>> pending_; // device -> host, handed over in process()
    const MidiMessage *single_template_ = nullptr;
    std::vector<std::vector<uint8_t>> state_; // the state records (start-up messages), see formats.md
    std::mutex state_mu_;
    std::unique_ptr<DeviceOut> out_;
    int block_ = 512;
    // R5 timing with the sync marker: an event at timeline position t sounds at ring frame t + lead() (S behind the
    // newest frame + A to reach the Virus in time), and process() reads the ring latency_frames() behind that, so the
    // host gets it exactly latency_frames() late in every session (reported as the plug-in latency)
    // A: time for a stamped event to reach the Virus. Linux 1024 (exact down to 768); Windows needs more (win11 VM:
    // 2048 had late notes, 3072 exact for 16/16) - close to the original's reported 3840 + block
#ifdef _WIN32
    static constexpr int TIMING_A_DEFAULT = 3072;
#else
    static constexpr int TIMING_A_DEFAULT = 1024;
#endif
    static int timing_a()
    {
        static const int a = getenv("VTI_NATIVE_A") ? atoi(getenv("VTI_NATIVE_A")) : TIMING_A_DEFAULT;
        return a;
    }
    static int timing_s() { static const int s = getenv("VTI_NATIVE_S") ? atoi(getenv("VTI_NATIVE_S")) : 5000; return s; }
    int64_t lead() const { return timing_s() + timing_a(); }
    // the read position stays block + 640 - |timeline jitter (<= ~400)| behind the newest frame
    // (+ the resampler's look-ahead when the host does not run at the Virus' rate)
    int64_t latency_frames() const
    {
        return timing_a() + std::max(block_, 256) + 640 + (resampling() ? Resampler::HALF + 2 : 0);
    }
    int virus_rate() const { return out_ ? out_->rate.load(std::memory_order_relaxed) : 44100; }
    bool resampling() const { return std::fabs(rate_ - virus_rate()) > 0.5; }
    void read_resampled(float **o1, float **o2, float **o3, int n, double ratio, int64_t newest, int64_t target);
public:
    Steinberg::uint32 PLUGIN_API getLatencySamples() override
    {
        const auto l = (Steinberg::uint32)std::lround(latency_frames() * rate_ / virus_rate());
        latency_reported_ = (int64_t)l; // setActive tells the host when a rate switch changed it
        return l;
    }
private:
    double rate_ = 44100;
    // the Virus timeline (docs/protocol.md "timing"): frames of the synth, stamped on every clock tick and event
    int64_t tl_ = 0;
    bool tl_valid_ = false, anchored_ = false;
    double next_tick_ = 0;
    ActiveSensing fe_;  // FE every 300 ms on the timeline (the DSP's watchdog, E237)
    double tl_acc_ = 0; // fractional Virus frames of the timeline (host rate != the Virus' rate)
    TransportWatch transport_; // Play and loop restarts: the original's F2 + 73 00 23 01 + 6F <part> 7F 00 (E275)
    double stat_acc_ = 0; // the same for the host-frames statistic
    int64_t stream_start_ = -1;
    TimelineFollow tl_follow_; // after the anchor: jumps and drift steps from the error over time, not per block (E337)
    // offline render (export, freeze): each block waits for the Virus' audio; the worker logs mode changes and timeouts
    OfflinePacer pacer_;
    bool offline_ = false;                   // audio thread: the last block's mode
    bool offline_start_waited_ = false;      // audio thread: this render waited for a starting Virus once
    std::atomic<bool> offline_now_{false};   // for the worker's report
    bool offline_reported_ = false;          // worker
    std::atomic<int> offline_timeouts_{0};   // waits that gave up (the Virus stopped streaming)
    std::atomic<int> offline_early_max_{0};  // most frames a block ran before its paced position (E315), this render
    int offline_timeouts_reported_ = 0;      // worker
    void wait_offline(Steinberg::Vst::ProcessData &data, double ratio, int64_t &newest, int64_t &avail);
    int64_t ring_newest(); // the newest frame of the Virus' ring, -1 without one (read under the audio gate)
    void report_offline();
    // the original's sync error page (E205): events from the Virus (broker rx thread), ticks and transport (worker)
    SyncAlert sync_alert_;
    std::mutex history_mu_;
    PatchHistory history_; // per part: Revert to / Recently Used (E220), this session only
    BankDrop drop_;        // the last browser drop onto RAM, for its undo (banks_mu_)
    // knob moves on the Virus as host edits while an editor is open (its timer is the UI-thread tick; E304, #10);
    // without an editor they go to the host as output parameter changes (pending_), as before
    HostGestures gestures_;
    std::atomic<int> editors_open_{0};
    bool send_ram_writes(const std::vector<BankDrop::Write> &w); // dumps into RAM slots + cache + cache files
    // a library operation: `op` makes the library's new patches from the current ones (empty = refused); the old
    // ones are kept for the undo, the file is saved
    bool edit_library(int library, const char *what, const std::function<LibraryEdit::Lib(const LibraryEdit::Lib &)> &op);
    bool save_library_file(int library); // banks_ -> its file
    std::vector<uint8_t> part_dump(int part); // the part's patch as it is now (edits included)
    std::mutex sync_alert_mu_;
    std::atomic<bool> host_playing_{false}; // audio thread -> worker
    // analog parts held back by the latency like the original (outputdelay.h, E344); worker thread only
    OutputDelay out_delay_;
    bool od_ready_seen_ = false, od_playing_seen_ = false;
    std::chrono::steady_clock::time_point od_next_{};
    int part_output(int part);
    void send_output_delays();
    int64_t sync_tick_ms_ = 0, sync_log_ms_ = 0; // worker
    int sync_logged_ = 0;                        // worker
    void tick_sync_alert();
    // host rate != the Virus' rate: its audio through the resampler, read from a steered fractional position. One per
    // Virus rate (index vidx(): 0 = 44.1, 1 = 48 kHz), both built in setupProcessing: a switch outside setActive (an
    // older broker that stays at 44.1, a refusal, another program on macOS) costs process() only the choice
    static int vidx(int virus_rate) { return virus_rate == 48000 ? 1 : 0; }
    Resampler resampler_[2];
    int vrate_seen_ = 0;           // audio thread: the Virus rate of the last block (a change resets the converters)
    int64_t latency_reported_ = -1; // what getLatencySamples last told the host
    std::vector<float> rs_src_[6]; // USB 1 L/R, USB 2 L/R, USB 3 L/R
    // the Virus' USB IO configuration ("Global USB/Virus Control USB IO Configuration": 0 = 2 Outs / No Input,
    // 1 = 3 Outs / No Input, 2 = 3 Outs / 1 Input), kept per computer like the original's "IO Configuration"
    // setting (0x18007ec30, E158), not per project; Out 3 carries USB 3 while it is 1 or 2 (E164)
    std::atomic<int> usb_io_{0};
    void set_usb_io(int value, bool save);
    // "3 Outs / 1 Input": the Sidechain bus goes to the Virus' USB input, as in the original (E167)
    SidechainSender sc_[2];       // rate-controlled converter towards the Virus' clock, per Virus rate (audio thread)
    std::vector<int16_t> sc_buf_; // its 16-bit stereo output for the ring, sized in setupProcessing
    int64_t sc_expect_ = -1;      // the ring's write position after our last write (-1: we did not write)
    int64_t sc_seen_ = -1;        // the write position at the last block (another writer moves it)
    int64_t sc_hold_ = 0;         // host frames to stay out after another writer was seen
    bool in_active_[2] = {false, false}; // input buses "Dummy In", "Sidechain" activated by the host
    std::atomic<int> sc_layout_{-1}; // process(): the host's sidechain layout (numInputs, bus, channels, active flags)
    int sc_layout_seen_ = -1;        // worker: the last logged one - logged when it changes
    void send_sidechain(Steinberg::Vst::ProcessData &data);
    double rs_pos_ = 0;
    bool rs_valid_ = false;
    int generation_seen_ = 0; // DeviceOut::generation handled by process()
    bool device_seen_ = false; // the Virus was present once (worker thread)
    std::set<std::string> dump_paths_; // parameters stored in a part's single dump (the rest: single messages)
    std::unordered_map<std::string, size_t> state_index_; // "path#part" -> state record (state_mu_)
    const std::vector<uint8_t> *state_index_data_ = nullptr;
    size_t state_index_size_ = 0;
    std::chrono::steady_clock::time_point drain_next_{}; // worker: next automation drain
    std::map<Steinberg::Vst::ParamID, int> latest_;       // worker: coalesced automation values
    int state_part(const std::string &path, int part) const;
public:
    // parameters changed since the last call (the editor repaints linked controls while a knob is dragged)
    std::vector<std::string> take_changed(bool *overflow = nullptr)
    {
        std::lock_guard<std::mutex> lk(values_mu_);
        std::vector<std::string> v(changed_.begin(), changed_.end());
        changed_.clear();
        if (overflow)
            *overflow = changed_overflow_;
        changed_overflow_ = false;
        return v;
    }
    // only parameter values changed (automation, Virus panel): the editor repaints the controls that show them
    bool values_changed() { return values_dirty_.exchange(false); }

private:
    std::set<std::string> changed_; // values_mu_
    bool changed_overflow_ = false; // values_mu_: more than 256 changes, the editor repaints everything
    std::atomic<bool> values_dirty_{false};
    bool state_loaded_ = false;                // setState or the default state was applied
    std::vector<std::vector<uint8_t>> default_; // a new instance's state (from the user's plug-in binary)
    std::vector<uint8_t> init_multi_; // the fixed multi dump as the original sends it at start-up (E321)

    // `stored`: the parts whose values are now their stored patch for the LCD's [value] (E353): all (a project or the
    // default state), one (a patch loaded into it), or none (a rename)
    static constexpr int STORED_ALL = -1, STORED_NONE = -2;
    void apply_state_to_params(int stored = STORED_ALL);
    void panel_message(const std::vector<uint8_t> &msg);
    void send_state();
    void set_part_value(int part, const std::string &path, int value); // keeps the part dump in state_ current
};

} // namespace vti
