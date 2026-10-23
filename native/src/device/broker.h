// broker.h - connection to the TI Revive broker (src/broker, protocol vtiproto.h), which owns the
// Virus TI's USB interfaces on every platform. The rewrite talks to the device only through it.
#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace vti {

class Broker {
public:
    using RxHandler = std::function<void(uint8_t ep, const uint8_t *data, size_t len)>;
    using StateHandler = std::function<void(bool connected)>;
    // observer messages (VTIMSG_OBS_TRAFFIC / _LOG / _STATUS, protocol 4)
    using ObserveHandler = std::function<void(uint16_t type, const uint8_t *data, size_t len)>;

    ~Broker();
    // Connect (POSIX: Unix socket /tmp/vtibroker-<uid>.sock; Windows: named pipe \\.\pipe\vtibroker), say HELLO, start the reader.
    bool open(std::string &error);
    void close();
    bool device_present() const { return present_; }
    // the connection to the broker is up (its reader runs): false after the broker went away (crash, update, restart)
    // - device_present() is false then too, but only this says that a new open() is needed
    bool connected() const { return !reader_done_; }
    // how often the Virus became present (STATE 1 after anything else): a gone-and-back faster than the caller polls
    // device_present() still changes it (E212: the broker reopened the Virus 0.27 s after a wake)
    uint32_t device_arrivals() const { return arrivals_; }
    // the broker's release version from its INFO message ("" from older brokers): PATCH UTILITY > INFO (E189)
    std::string broker_version() const
    {
        std::lock_guard<std::mutex> g(info_mu_);
        return broker_version_;
    }
    // the Virus is there, but another program's plug-in has it (state 2): wait, like a second instance (E166)
    bool in_use_elsewhere() const { return elsewhere_; }
    // shown in the broker's status for observers ("TI Revive Virus Control", "tirevive-cc", ...); before open()
    void set_name(const std::string &name) { name_ = name; }
    // POSIX: another socket path instead of /tmp/vtibroker-<uid>.sock (unit tests with a fake broker); "" = the
    // broker's own. Set before open(); only code sets it, never the environment.
    void set_endpoint(const std::string &path) { endpoint_ = path; }
    // Become a read-only observer (after open): the broker refuses our writes and sends a copy of the traffic,
    // its log lines and a status line once a second to `h`. False if the broker does not confirm (older than
    // protocol 4: it would have kept us as the owner of the Virus, so the connection is closed).
    bool observe(ObserveHandler h, std::string &error);

    void on_rx(RxHandler h) { rx_ = std::move(h); }
    void on_state(StateHandler h) { state_ = std::move(h); }

    bool write(uint8_t ep, const uint8_t *data, size_t len);
    // USB control request through the broker; returns bytes transferred or -1. `data` in/out; a reply never writes
    // more than len bytes into it.
    int control(uint8_t request_type, uint8_t request, uint16_t value, uint16_t index, uint8_t *data, uint16_t len,
                int timeout_ms = 3000);

    // Start/stop the Virus audio stream (only the owner may). Linux: 1 = streaming into the POSIX shm ring
    // (vtiaudioshm.h, VTI_AUDIO_SHM_LINUX), 0 = no audio function, -1 = error / not the owner.
    // `rate` = the Virus rate the plug-in wants (44100/48000, protocol 5; older brokers ignore it and stream 44.1 kHz).
    // The broker may keep another one (not the owner, debounce): the real rate is in the shared ring.
    int audio(bool on, int timeout_ms = 3000, uint32_t rate = 44100);

    // The Virus' identification blocks (model, OS version), read by the broker with the original driver's fixed
    // read-only flash sequence (flashinfo.h, E026/E040; owner only, cached per device open). Returns the byte count
    // (blocks in `out`) or -1 (also at once from a broker without the INFO message: older than 0.8.21). Takes about
    // 2 s the first time.
    int flash_info(std::vector<uint8_t> &out, int timeout_ms = 8000);

    // control replies received and not yet taken (unit tests)
    size_t pending_replies() const
    {
        std::lock_guard<std::mutex> g(ctl_);
        return replies_.size();
    }

private:
    intptr_t fd_ = -1; // POSIX socket / Windows pipe HANDLE (overlapped)
    int audio_reply_ = 0;
    bool audio_replied_ = false;
    int flash_reply_ = 0;
    bool flash_replied_ = false;
    std::vector<uint8_t> flash_;
    uint32_t observe_reply_ = 0;
    std::string name_, endpoint_, broker_version_;
    mutable std::mutex info_mu_;
    ObserveHandler observe_;
    std::thread reader_;
    std::mutex wr_;
    mutable std::mutex ctl_;
    std::condition_variable ctl_cv_;
    std::map<uint32_t, std::vector<uint8_t>> replies_; // seq -> i32 result + data
    std::set<uint32_t> waiting_;                       // seqs a control() still waits for
    uint32_t seq_ = 0;
    std::atomic<bool> present_{false}, elsewhere_{false}, stop_{false}, reader_done_{true};
    std::atomic<uint32_t> arrivals_{0};
    RxHandler rx_;
    StateHandler state_;
    bool send(uint16_t type, const void *a, size_t alen, const void *b = nullptr, size_t blen = 0);
    void read_loop();
};

} // namespace vti
