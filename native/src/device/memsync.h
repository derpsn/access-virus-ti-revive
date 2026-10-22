// memsync.h - the Virus' RAM banks in a local cache, kept current like the original plug-in does it (docs/protocol.md):
// per bank a bank-info request (39 -> 14: one 14-bit fingerprint per program), then single dump requests (30 -> 10)
// for the programs whose cached dump has a different fingerprint. The Virus answers one request at a time (~1 s each;
// overlapping requests make it truncate replies), so requests are strictly sequential.
//
// Cache files use the original's format, so an existing Access cache can be imported: "MIDI" 00 01 08 00, then
// 128 x (u32 big-endian length 524, single dump).
#pragma once
#include <atomic>
#include "broker.h"
#include <array>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace vti {

using Dump = std::vector<uint8_t>;
using Bank = std::array<Dump, 128>; // empty dump = not cached

bool load_bank_file(const std::string &path, Bank &bank);
bool save_bank_file(const std::string &path, const Bank &bank);

class MemorySync {
public:
    // standalone: owns the broker's receive handler (tools/virussync)
    MemorySync(Broker &broker, int device_id = 0x10);
    // embedded (the plug-in): `send` queues a SysEx to the Virus, replies are passed in with feed()
    explicit MemorySync(std::function<bool(const std::vector<uint8_t> &)> send, int device_id = 0x10);
    ~MemorySync();
    void feed(const std::vector<uint8_t> &sysex); // a SysEx from the Virus

    // One request/reply round trip: sends `request` (a SysEx), waits for a SysEx reply whose bytes 6..8 equal
    // `match` (prefix). Retries `tries` times with `timeout_ms` each. Empty result on failure.
    Dump transact(const Dump &request, const std::vector<uint8_t> &match, int timeout_ms = 3000, int tries = 3);
    // Ends a waiting transact() at once and makes every later one return empty right away (the plug-in's
    // terminate(): it waited up to 10 s for a request in flight, review 2026-10-19 M6).
    void cancel();

    // Bank info of RAM bank 1..4: 128 fingerprints, or empty.
    std::vector<int> bank_info(int bank);
    Dump single(int bank, int program);

    // Brings `cache` up to date for RAM bank 1..4; progress(program, fetched) is called per fetched program.
    // Returns the number of fetched programs, -1 on failure.
    // `stop` (optional) cancels between two requests, cancel() also during one: returns -2, `cache` keeps what was
    // fetched so far.
    int sync_bank(int bank, Bank &cache, const std::function<void(int, bool)> &progress = nullptr,
                  const std::atomic<bool> *stop = nullptr);
    // "Download entire bank from Virus TI" like the original (E301): RAM bank 1..4, all 128 programs in order, each
    // request after the display message 74 01 7E 04 (Actions/Rise Message 4: "syncing" on the Virus), cached or not.
    // progress(program) per program read. Returns 128, -1 when a request gets no reply, -2 when cancelled (like
    // sync_bank); `cache` keeps what was read, the rest unchanged.
    int read_bank(int bank, Bank &cache, const std::function<void(int)> &progress = nullptr,
                  const std::atomic<bool> *stop = nullptr);
    // called before every request: may block (e.g. while the host plays - the Virus delays notes while it answers
    // dump requests); returning false cancels like `stop`
    std::function<bool()> before_request;

private:
    bool was_cancelled();
    Broker *broker_ = nullptr;
    std::function<bool(const std::vector<uint8_t> &)> send_;
    int dev_;
    std::mutex mu_;
    std::mutex tx_mu_; // one request at a time: the Virus drops replies to requests that overlap (E145)
    std::condition_variable cv_;
    std::vector<Dump> inbox_;
    bool cancelled_ = false; // under mu_
};

} // namespace vti
