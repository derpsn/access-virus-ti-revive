// memsync.cpp - see memsync.h.
#include "memsync.h"
#include "../model/remotedevice.h"
#include "usbmidi.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>
#include <cstdio>
#include <memory>
#include <filesystem>

namespace vti {

static const uint8_t EP_PLUGIN_OUT = 0x06, CABLE_PLUGIN = 2;
static const size_t SINGLE_SIZE = 524, BANKINFO_SIZE = 267;

bool load_bank_file(const std::string &path, Bank &bank)
{
    FILE *f = fopen(path.c_str(), "rb");
    if (!f)
        return false;
    uint8_t hdr[8];
    bool ok = fread(hdr, 1, 8, f) == 8 && !memcmp(hdr, "MIDI", 4);
    for (int p = 0; ok && p < 128; p++) {
        uint8_t l[4];
        if (fread(l, 1, 4, f) != 4)
            break;
        size_t n = (size_t)l[0] << 24 | l[1] << 16 | l[2] << 8 | l[3];
        if (n > 4096) {
            ok = false;
            break;
        }
        bank[p].resize(n);
        if (fread(bank[p].data(), 1, n, f) != n)
            ok = false;
    }
    fclose(f);
    return ok;
}

bool save_bank_file(const std::string &path, const Bank &bank)
{
    std::string tmp = path + ".tmp";
    FILE *f = fopen(tmp.c_str(), "wb");
    if (!f)
        return false;
    static const uint8_t hdr[8] = {'M', 'I', 'D', 'I', 0x00, 0x01, 0x08, 0x00};
    bool ok = fwrite(hdr, 1, 8, f) == 8;
    for (auto &d : bank) {
        uint8_t l[4] = {(uint8_t)(d.size() >> 24), (uint8_t)(d.size() >> 16), (uint8_t)(d.size() >> 8), (uint8_t)d.size()};
        ok = ok && fwrite(l, 1, 4, f) == 4 && fwrite(d.data(), 1, d.size(), f) == d.size();
    }
    ok = fclose(f) == 0 && ok;
    // std::filesystem::rename replaces an existing file on Windows too (C rename() fails there)
    std::error_code ec;
    if (ok)
        std::filesystem::rename(tmp, path, ec);
    return ok && !ec;
}

MemorySync::MemorySync(Broker &broker, int device_id) : broker_(&broker), dev_(device_id)
{
    send_ = [this](const std::vector<uint8_t> &m) {
        auto pk = usbmidi_pack(m, CABLE_PLUGIN);
        return broker_->write(EP_PLUGIN_OUT, pk.data(), pk.size());
    };
    // replies come on EP 0x86 cable 3 in a plug-in session, otherwise on EP 0x85 cable 1 (MIDI port)
    auto parser = std::make_shared<UsbMidiParser>([this](uint8_t, int, const std::vector<uint8_t> &m) { feed(m); });
    broker_->on_rx([parser](uint8_t ep, const uint8_t *d, size_t len) { parser->feed(ep, d, len); });
}

MemorySync::MemorySync(std::function<bool(const std::vector<uint8_t> &)> send, int device_id)
    : send_(std::move(send)), dev_(device_id)
{
}

MemorySync::~MemorySync()
{
    if (broker_)
        broker_->on_rx(nullptr);
}

void MemorySync::feed(const std::vector<uint8_t> &m)
{
    if (m.size() > 8 && m[0] == 0xF0 && m[1] == 0x00 && m[2] == 0x20 && m[3] == 0x33) {
        std::lock_guard<std::mutex> lk(mu_);
        inbox_.push_back(m);
        cv_.notify_all();
    }
}

Dump MemorySync::transact(const Dump &request, const std::vector<uint8_t> &match, int timeout_ms, int tries)
{
    // one request at a time, also across threads: the readiness probe after a reconnect met a bank sync still
    // running, the Virus answered only the newer request (E145) and the probe waited 10 s (review 2026-10-19)
    std::lock_guard<std::mutex> one(tx_mu_);
    for (int t = 0; t < tries; t++) {
        {
            std::lock_guard<std::mutex> lk(mu_);
            if (cancelled_)
                return {};
            inbox_.clear();
        }
        if (!send_(request))
            return {};
        std::unique_lock<std::mutex> lk(mu_);
        auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        while (true) {
            for (auto &m : inbox_)
                if (m.size() >= 6 + match.size() && std::equal(match.begin(), match.end(), m.begin() + 6))
                    return m;
            if (cancelled_ || cv_.wait_until(lk, until) == std::cv_status::timeout || cancelled_)
                break;
        }
        // let a late/truncated reply drain before the next attempt (overlapping requests confuse the Virus)
        if (cv_.wait_for(lk, std::chrono::milliseconds(1500), [this] { return cancelled_; }))
            return {};
    }
    return {};
}

void MemorySync::cancel()
{
    std::lock_guard<std::mutex> lk(mu_);
    cancelled_ = true;
    cv_.notify_all();
}

// an empty reply after cancel() is the plug-in closing, not a Virus that did not answer (E314: "bank sync failed"
// when Live closed the plug-in during the sync)
bool MemorySync::was_cancelled()
{
    std::lock_guard<std::mutex> lk(mu_);
    return cancelled_;
}

std::vector<int> MemorySync::bank_info(int bank)
{
    Dump r = transact({0xF0, 0x00, 0x20, 0x33, 0x01, (uint8_t)dev_, 0x39, (uint8_t)bank, 0xF7}, {0x14, (uint8_t)bank});
    if (r.size() != BANKINFO_SIZE)
        return {};
    std::vector<int> v(128);
    for (int p = 0; p < 128; p++)
        v[p] = r[9 + 2 * p] | r[10 + 2 * p] << 7;
    return v;
}

Dump MemorySync::single(int bank, int program)
{
    for (int t = 0; t < 3; t++) {
        Dump r = transact({0xF0, 0x00, 0x20, 0x33, 0x01, (uint8_t)dev_, 0x30, (uint8_t)bank, (uint8_t)program, 0xF7},
                          {0x10, (uint8_t)bank, (uint8_t)program});
        if (r.size() == SINGLE_SIZE)
            return r;
        if (r.empty())
            break;
    }
    return {};
}

int MemorySync::sync_bank(int bank, Bank &cache, const std::function<void(int, bool)> &progress,
                          const std::atomic<bool> *stop)
{
    if (before_request && !before_request())
        return -2;
    auto info = bank_info(bank);
    if (info.empty())
        return was_cancelled() ? -2 : -1;
    int fetched = 0;
    for (int p = 0; p < 128; p++) {
        if (stop && *stop)
            return -2;
        bool stale = RemoteDevice::single_fingerprint(cache[p]) != info[p];
        if (stale) {
            if (before_request && !before_request())
                return -2;
            Dump d = single(bank, p);
            if (d.empty() && was_cancelled())
                return -2;
            if (d.empty() || RemoteDevice::single_fingerprint(d) != info[p])
                return -1;
            cache[p] = std::move(d);
            fetched++;
        }
        if (progress)
            progress(p, stale);
    }
    return fetched;
}

int MemorySync::read_bank(int bank, Bank &cache, const std::function<void(int)> &progress, const std::atomic<bool> *stop)
{
    const std::vector<uint8_t> rise = {0xF0, 0x00, 0x20, 0x33, 0x01, (uint8_t)dev_, 0x74, 0x01, 0x7E, 0x04, 0xF7};
    for (int p = 0; p < 128; p++) {
        if ((stop && *stop) || (before_request && !before_request()))
            return -2;
        send_(rise);
        Dump d = single(bank, p);
        if (d.empty())
            return was_cancelled() ? -2 : -1;
        cache[p] = std::move(d);
        if (progress)
            progress(p);
    }
    return 128;
}

} // namespace vti
