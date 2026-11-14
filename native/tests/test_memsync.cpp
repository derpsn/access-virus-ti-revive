// MemorySync against a fake Virus (the embedded constructor: requests go to a callback, replies come back via feed):
// request/reply matching, bank info, which programs a bank sync fetches, cancelling, and the cache file format
#include "check.h"
#include "device/memsync.h"
#include "model/remotedevice.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <thread>
#ifndef _WIN32
#include <csignal>
#include <sys/resource.h>
#endif

using namespace vti;
using Bytes = std::vector<uint8_t>;

// a single dump as the Virus sends it (524 bytes, 30 bank program -> 10 bank program); the content depends on `seed`
static Dump fake_single(int bank, int program, int seed)
{
    Dump d(524, 0);
    const uint8_t head[9] = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x10, (uint8_t)bank, (uint8_t)program};
    std::copy(head, head + 9, d.begin());
    for (size_t i = 9; i < 523; i++)
        d[i] = (uint8_t)((i * 7 + seed * 13) & 0x7F);
    d[523] = 0xF7;
    return d;
}

// answers bank info (39 bank -> 14 bank, 128 fingerprints) and single dump requests from `ram`, synchronously
struct FakeVirus {
    std::vector<Bank> ram = std::vector<Bank>(5);
    MemorySync *sync = nullptr;
    std::vector<Bytes> requests;
    int corrupt = -1; // this program's dump comes back with a different fingerprint than bank info says
    int corrupt_times = -1; // how many times it does; -1: always
    int short_info = 0;     // this many bank info replies come back cut short (a packet lost on the way); -1: always
    std::function<bool(const Bytes &)> send()
    {
        return [this](const Bytes &m) {
            requests.push_back(m);
            if (m.size() < 9)
                return true;
            int bank = m[7];
            if (m[6] == 0x39) {
                if (short_info != 0) {
                    if (short_info > 0)
                        short_info--;
                    sync->feed({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x14, (uint8_t)bank, 0x00, 0x01, 0xF7});
                    return true;
                }
                Bytes r = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x14, (uint8_t)bank, 0x00};
                for (int p = 0; p < 128; p++) {
                    int fp = RemoteDevice::single_fingerprint(ram[bank][p]);
                    r.push_back((uint8_t)(fp & 0x7F));
                    r.push_back((uint8_t)(fp >> 7 & 0x7F));
                }
                r.push_back(0x00); // checksum position (not checked by bank_info)
                r.push_back(0xF7);
                sync->feed(r);
            } else if (m[6] == 0x30) {
                int p = m[8];
                const bool bad = p == corrupt && corrupt_times != 0;
                if (bad && corrupt_times > 0)
                    corrupt_times--;
                sync->feed(bad ? fake_single(bank, p, 999) : ram[bank][p]);
            }
            return true;
        };
    }
};

TEST(memsync_transact_matches_reply)
{
    MemorySync *ms = nullptr;
    MemorySync sync([&](const Bytes &) {
        ms->feed({0xF0, 0x7E, 0x00, 0x06, 0x02, 0xF7});                         // not an Access SysEx: dropped
        ms->feed({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x14, 0x01, 0x00, 0xF7}); // another reply: not matched
        ms->feed({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x10, 0x00, 0x00, 0x42, 0xF7});
        return true;
    });
    ms = &sync;
    Dump r = sync.transact({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x30, 0x00, 0x00, 0xF7}, {0x10, 0x00, 0x00}, 500, 1);
    CHECK_EQ(r.size(), 11u);
    CHECK(r.size() == 11 && r[9] == 0x42);
}

TEST(memsync_transact_times_out_without_reply)
{
    MemorySync sync([](const Bytes &) { return true; });
    auto t0 = std::chrono::steady_clock::now();
    Dump r = sync.transact({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x30, 0x00, 0x00, 0xF7}, {0x10, 0x00, 0x00}, 100, 1);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    CHECK(r.empty());
    CHECK(ms >= 100); // one try waits its timeout, then the drain pause for a late reply (1.5 s)
    CHECK(ms < 2500);
}

TEST(memsync_transact_failed_send)
{
    int sends = 0;
    MemorySync sync([&](const Bytes &) {
        sends++;
        return false;
    });
    CHECK(sync.transact({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x30, 0x00, 0x00, 0xF7}, {0x10}, 1000, 3).empty());
    CHECK_EQ(sends, 1); // no retries when the message cannot even be queued
}

TEST(memsync_transact_late_reply)
{
    // the readiness probe's case: the Virus answers only after it worked through what was queued before
    MemorySync *ms = nullptr;
    std::thread late;
    MemorySync sync([&](const Bytes &) {
        late = std::thread([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            ms->feed(fake_single(0, 0, 1));
        });
        return true;
    });
    ms = &sync;
    Dump r = sync.transact({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x30, 0x00, 0x00, 0xF7}, {0x10, 0x00, 0x00}, 2000, 1);
    late.join();
    CHECK_EQ(r.size(), 524u);
}

TEST(memsync_bank_info_and_single)
{
    FakeVirus v;
    MemorySync sync(v.send());
    v.sync = &sync;
    for (int p = 0; p < 128; p++)
        v.ram[2][p] = fake_single(2, p, p);
    auto info = sync.bank_info(2);
    CHECK_EQ(info.size(), 128u);
    bool same = info.size() == 128;
    for (int p = 0; same && p < 128; p++)
        same = info[p] == RemoteDevice::single_fingerprint(v.ram[2][p]);
    CHECK(same);
    CHECK(sync.single(2, 5) == v.ram[2][5]);
    CHECK(v.requests.size() == 2 && v.requests[0][6] == 0x39 && v.requests[1][6] == 0x30 && v.requests[1][8] == 5);
}

TEST(memsync_sync_bank_fetches_only_stale)
{
    FakeVirus v;
    MemorySync sync(v.send());
    v.sync = &sync;
    Bank cache;
    for (int p = 0; p < 128; p++) {
        v.ram[1][p] = fake_single(1, p, p);
        cache[p] = v.ram[1][p];
    }
    cache[3] = fake_single(1, 3, 500); // changed on the Virus
    cache[100].clear();                // not cached yet
    int progress_calls = 0, stale_calls = 0;
    int n = sync.sync_bank(1, cache, [&](int, bool stale) {
        progress_calls++;
        stale_calls += stale;
    });
    CHECK_EQ(n, 2);
    CHECK_EQ(progress_calls, 128);
    CHECK_EQ(stale_calls, 2);
    CHECK(cache[3] == v.ram[1][3] && cache[100] == v.ram[1][100]);
    CHECK_EQ(v.requests.size(), 6u); // display message (E321) + bank info + 2 x (syncing message + single dump)
    // up to date now: only the display message and the bank info
    v.requests.clear();
    CHECK_EQ(sync.sync_bank(1, cache), 0);
    CHECK_EQ(v.requests.size(), 2u);
}

TEST(memsync_sync_bank_cancel_and_errors)
{
    FakeVirus v;
    MemorySync sync(v.send());
    v.sync = &sync;
    for (int p = 0; p < 128; p++)
        v.ram[1][p] = fake_single(1, p, p);
    Bank cache; // empty: every program is stale
    std::atomic<bool> stop{true};
    CHECK_EQ(sync.sync_bank(1, cache, nullptr, &stop), -2);
    sync.before_request = [] { return false; }; // e.g. the plug-in is closing
    CHECK_EQ(sync.sync_bank(1, cache), -2);
    int calls = 0;
    sync.before_request = [&] { return ++calls < 3; }; // bank info + 1 program, then cancelled
    CHECK_EQ(sync.sync_bank(1, cache), -2);
    CHECK(cache[0] == v.ram[1][0] && cache[1].empty());
    sync.before_request = nullptr;
    v.corrupt = 2; // a reply that does not match the bank info: the sync stops instead of caching it
    CHECK_EQ(sync.sync_bank(1, cache), -1);
    CHECK(cache[2].empty());
}

// One lost packet must not end a sync: seen on the simulator's bridge (S048), not reported on a real unit. single()
// already retried a dump of the wrong size; bank info did not, and a dump with the wrong fingerprint ended the sync.
TEST(memsync_bank_info_retries_a_wrong_size_reply)
{
    FakeVirus v;
    MemorySync sync(v.send());
    v.sync = &sync;
    v.short_info = 1;
    CHECK_EQ(sync.bank_info(2).size(), 128u);
    CHECK_EQ(v.requests.size(), 2u);
}

TEST(memsync_bank_info_gives_up_on_wrong_sizes)
{
    FakeVirus v;
    MemorySync sync(v.send());
    v.sync = &sync;
    v.short_info = -1;
    CHECK(sync.bank_info(2).empty());
    CHECK_EQ(v.requests.size(), 3u); // bounded, like single()
}

// #34: a "bank sync failed" log could not tell cut-short replies (lost packets) from no reply at all (2026-11-14)
TEST(memsync_logs_wrong_size_replies)
{
    FakeVirus v;
    MemorySync sync(v.send());
    v.sync = &sync;
    std::vector<std::string> lines;
    sync.log = [&](const std::string &s) { lines.push_back(s); };
    v.short_info = 1;
    CHECK_EQ(sync.bank_info(2).size(), 128u);
    CHECK_EQ(lines.size(), 1u);
    if (!lines.empty())
        CHECK(lines[0] == std::string("bank info RAM B: reply 11 bytes, expected 267 (try 1 of 3)"));

    lines.clear();
    v.ram[2][5] = fake_single(2, 5, 5);
    CHECK_EQ(sync.single(2, 5).size(), 524u);
    CHECK(lines.empty()); // a good reply logs nothing
}

TEST(memsync_logs_no_reply_but_not_a_cancel)
{
    std::vector<std::string> lines;
    MemorySync sync([](const Bytes &) { return false; }); // empty at once, like a reply that never comes
    sync.log = [&](const std::string &s) { lines.push_back(s); };
    CHECK(sync.single(3, 7).empty());
    CHECK_EQ(lines.size(), 1u);
    if (!lines.empty())
        CHECK(lines[0] == std::string("single RAM C 7: no reply"));
    lines.clear();
    sync.cancel();
    CHECK(sync.bank_info(1).empty());
    CHECK(lines.empty()); // the plug-in closing is not a Virus that did not answer
}

TEST(memsync_sync_bank_survives_one_wrong_size_bank_info)
{
    FakeVirus v;
    MemorySync sync(v.send());
    v.sync = &sync;
    Bank cache;
    for (int p = 0; p < 128; p++)
        cache[p] = v.ram[1][p] = fake_single(1, p, p);
    v.short_info = 1;
    CHECK_EQ(sync.sync_bank(1, cache), 0);
}

TEST(memsync_sync_bank_retries_a_wrong_fingerprint)
{
    FakeVirus v;
    MemorySync sync(v.send());
    v.sync = &sync;
    for (int p = 0; p < 128; p++)
        v.ram[1][p] = fake_single(1, p, p);
    Bank cache;
    for (int p = 0; p < 128; p++)
        cache[p] = v.ram[1][p];
    cache[2] = fake_single(1, 2, 500); // only this one is stale
    v.corrupt = 2;
    v.corrupt_times = 1;
    CHECK_EQ(sync.sync_bank(1, cache), 1);
    CHECK(cache[2] == v.ram[1][2]);
    CHECK_EQ(v.requests.size(), 6u); // display message + bank info + the dump twice, each after its syncing message
}

TEST(memsync_sync_bank_gives_up_on_a_wrong_fingerprint)
{
    FakeVirus v;
    MemorySync sync(v.send());
    v.sync = &sync;
    for (int p = 0; p < 128; p++)
        v.ram[1][p] = fake_single(1, p, p);
    Bank cache;
    for (int p = 0; p < 128; p++)
        cache[p] = v.ram[1][p];
    const Dump old = fake_single(1, 2, 500);
    cache[2] = old;
    v.corrupt = 2; // always: never cached
    CHECK_EQ(sync.sync_bank(1, cache), -1);
    CHECK(cache[2] == old);
    CHECK_EQ(v.requests.size(), 8u); // display message + bank info + 3 tries (each with its syncing message), bounded
}

TEST(memsync_bank_file_round_trip)
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "vtitests-memsync";
    fs::create_directories(dir);
    const std::string path = (dir / "bank.bin").string();
    Bank out;
    for (int p = 0; p < 128; p += 3)
        out[p] = fake_single(1, p, p);
    CHECK(save_bank_file(path, out));
    CHECK(!fs::exists(path + ".tmp"));
    CHECK(save_bank_file(path, out)); // replaces an existing file
    Bank in;
    CHECK(load_bank_file(path, in));
    CHECK(in == out);
    CHECK(fs::file_size(path) == 8 + 128 * 4 + 43 * 524u);

    Bank none;
    CHECK(!load_bank_file((dir / "missing.bin").string(), none));
    auto write = [&](const std::string &name, const Bytes &b) {
        FILE *f = fopen((dir / name).string().c_str(), "wb");
        fwrite(b.data(), 1, b.size(), f);
        fclose(f);
        return (dir / name).string();
    };
    CHECK(!load_bank_file(write("header.bin", {'X', 'I', 'D', 'I', 0, 1, 8, 0}), none));
    CHECK(!load_bank_file(write("huge.bin", {'M', 'I', 'D', 'I', 0, 1, 8, 0, 0x00, 0x01, 0x00, 0x00}), none));
    CHECK(!load_bank_file(write("short.bin", {'M', 'I', 'D', 'I', 0, 1, 8, 0, 0, 0, 2, 0x0C, 0xF0}), none));
    fs::remove_all(dir);
}

// Two threads with requests at once (review 2026-10-19, hardware test of the broker reconnect): the bank sync was
// still running when the readiness probe went out, and the probe waited its full 10 s ("Virus ready after 11.5 s
// (no reply to the probe)"). The Virus does not answer requests that overlap: of 16 single dump requests sent back to
// back only the first (truncated) and the last were answered (E145). The fake Virus here answers only the latest
// open request. Requests go one at a time now, so both get their reply.
TEST(memsync_concurrent_transacts_both_get_their_reply)
{
    MemorySync *self = nullptr;
    std::mutex mu;
    std::vector<std::thread> answers;
    int latest = 0; // requests seen; a reply goes out only if no newer request came meanwhile
    MemorySync sync([&](const Bytes &req) {
        std::lock_guard<std::mutex> g(mu);
        const int n = ++latest;
        answers.emplace_back([&self, &mu, &latest, n, req] {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            {
                std::lock_guard<std::mutex> g2(mu);
                if (latest != n) // overlapped by a newer request: this one is lost (E145)
                    return;
            }
            self->feed(fake_single(req[7], req[8], 1));
        });
        return true;
    });
    self = &sync;
    Dump a, b;
    std::thread probe([&] { a = sync.transact({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x30, 0x00, 0x00, 0xF7}, {0x10, 0x00, 0x00}, 1000, 1); });
    std::this_thread::sleep_for(std::chrono::milliseconds(30)); // the probe's request is out, its reply not yet
    std::thread bank([&] { b = sync.transact({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x30, 0x01, 0x05, 0xF7}, {0x10, 0x01, 0x05}, 1000, 1); });
    probe.join();
    bank.join();
    std::vector<std::thread> done;
    {
        std::lock_guard<std::mutex> g(mu);
        done.swap(answers); // joined outside: they take mu themselves
    }
    for (auto &t : done)
        t.join();
    CHECK(a.size() == 524 && a[7] == 0 && a[8] == 0);
    CHECK(b.size() == 524 && b[7] == 1 && b[8] == 5);
}

// The plug-in's terminate() waited for a request in flight (review 2026-10-19 M6): the readiness probe up to 10 s, a
// bank sync request 3 x 3 s plus 1.5 s pauses. cancel() ends the waiting at once, also for later requests.
TEST(memsync_cancel_ends_a_waiting_request_at_once)
{
    MemorySync sync([](const Bytes &) { return true; }); // the Virus never answers
    std::thread stopper([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        sync.cancel();
    });
    auto t0 = std::chrono::steady_clock::now();
    Dump r = sync.transact({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x30, 0x00, 0x00, 0xF7}, {0x10, 0x00, 0x00}, 10000, 3);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    stopper.join();
    CHECK(r.empty());
    CHECK(ms < 1000);
    t0 = std::chrono::steady_clock::now();
    CHECK(sync.transact({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x39, 0x01, 0xF7}, {0x14, 0x01}, 10000, 3).empty());
    CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(100));
}

// "Download entire bank from Virus TI" like the original (E301): all 128 programs in order, each request preceded by
// the display message 74 01 7E 04 (Actions/Rise Message 4), no bank info, every program read again even if cached
TEST(memsync_sync_bank_keeps_the_configuring_message_up)
{
    // E321: the original sends 74 01 7E 07 (Rise Message 7: "Configuring Virus TI") right before every bank info
    // request, whether anything is fetched or not - the Virus shows it while the banks are checked
    FakeVirus v;
    MemorySync sync(v.send());
    v.sync = &sync;
    Bank cache;
    for (int p = 0; p < 128; p++)
        cache[p] = v.ram[1][p] = fake_single(1, p, p);
    CHECK_EQ(sync.sync_bank(1, cache), 0);
    const Bytes rise = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x74, 0x01, 0x7E, 0x07, 0xF7};
    CHECK_EQ(v.requests.size(), 2u);
    CHECK(v.requests[0] == rise);
    CHECK(v.requests.size() > 1 && v.requests[1][6] == 0x39);
}

// the original's start-up sync on the wire (captures/orig-rom-a-fetch-20261101b): 74 01 7E 07 only right before the
// bank info; every program it fetches gets 74 01 7E 04 (the Virus shows its syncing message) right before its
// 30 bank program, also each retry. Nothing else in between: no repeated "Configuring" while patches come in.
TEST(memsync_sync_bank_shows_syncing_for_each_fetch)
{
    FakeVirus v;
    MemorySync sync(v.send());
    v.sync = &sync;
    Bank cache;
    for (int p = 0; p < 128; p++)
        cache[p] = v.ram[1][p] = fake_single(1, p, p);
    cache[3] = fake_single(1, 3, 500);
    cache[100].clear();
    v.corrupt = 100; // one wrong dump first: asked again
    v.corrupt_times = 1;
    CHECK_EQ(sync.sync_bank(1, cache), 2);
    const Bytes configuring = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x74, 0x01, 0x7E, 0x07, 0xF7};
    const Bytes syncing = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x74, 0x01, 0x7E, 0x04, 0xF7};
    CHECK_EQ(v.requests.size(), 8u); // 7E 07, bank info, 7E 04 + #3, 7E 04 + #100, 7E 04 + #100 again
    CHECK(v.requests.size() == 8 && v.requests[0] == configuring && v.requests[1][6] == 0x39);
    for (size_t i = 2; i + 1 < v.requests.size(); i += 2) {
        CHECK(v.requests[i] == syncing);
        CHECK(v.requests[i + 1][6] == 0x30);
    }
    CHECK(v.requests.size() == 8 && v.requests[3][8] == 3 && v.requests[5][8] == 100 && v.requests[7][8] == 100);
}

TEST(memsync_read_bank_reads_all)
{
    FakeVirus v;
    MemorySync sync(v.send());
    v.sync = &sync;
    Bank cache;
    for (int p = 0; p < 128; p++) {
        v.ram[2][p] = fake_single(2, p, p + 7);
        cache[p] = fake_single(2, p, p + 7); // already current: read anyway
    }
    cache[5] = fake_single(2, 5, 999);
    std::vector<int> order;
    int n = sync.read_bank(2, cache, [&](int p) { order.push_back(p); });
    CHECK_EQ(n, 128);
    CHECK_EQ(order.size(), 128u);
    CHECK(order.front() == 0 && order.back() == 127);
    CHECK(cache[5] == v.ram[2][5]);
    CHECK_EQ(v.requests.size(), 256u);
    const Bytes rise = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x74, 0x01, 0x7E, 0x04, 0xF7};
    CHECK(v.requests[0] == rise);
    CHECK((v.requests[1] == Bytes{0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x30, 0x02, 0x00, 0xF7}));
    CHECK(v.requests[254] == rise && v.requests[255][8] == 127);
}

// cancelled or a request without reply: stops, what was read stays, the rest of `cache` is untouched
TEST(memsync_read_bank_stops)
{
    FakeVirus v;
    MemorySync sync(v.send());
    v.sync = &sync;
    Bank cache;
    for (int p = 0; p < 128; p++) {
        v.ram[1][p] = fake_single(1, p, p);
        cache[p] = fake_single(1, p, 500);
    }
    int calls = 0;
    sync.before_request = [&] { return ++calls <= 3; }; // 3 programs, then the plug-in closes
    CHECK_EQ(sync.read_bank(1, cache), -2);
    CHECK(cache[2] == v.ram[1][2] && cache[3] == fake_single(1, 3, 500));
    sync.before_request = nullptr;
    std::atomic<bool> stop{true};
    CHECK_EQ(sync.read_bank(1, cache, nullptr, &stop), -2);
}

// E314: Live closed the plug-in 3 s into its bank sync; terminate() cancels the request in flight, which came back
// empty like a request without reply, and the log said "bank sync failed". A cancelled request is a stop (-2).
TEST(memsync_cancel_during_a_request_is_a_stop)
{
    for (int mode = 0; mode < 2; mode++) { // 0: sync_bank, 1: read_bank
        MemorySync sync([](const Bytes &) { return true; }); // the Virus never answers
        std::thread stopper([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            sync.cancel();
        });
        Bank cache;
        const int r = mode == 0 ? sync.sync_bank(1, cache) : sync.read_bank(1, cache);
        stopper.join();
        CHECK_EQ(r, -2);
    }
    // a request that just gets no reply is still a failure
    MemorySync quiet([](const Bytes &) { return false; }); // the send fails: nothing reaches the Virus
    Bank cache;
    CHECK_EQ(quiet.sync_bank(1, cache), -1);
    CHECK_EQ(quiet.read_bank(1, cache), -1);
}

// a user ROM bank (E381/E382): the original reads the programs it has no data for, each after the display message
// 74 01 7E 04, with 30 <5+i> pp - one at a time, no bank info for ROM banks. Programs already cached are not read.
TEST(memsync_read_missing_reads_only_empty_slots)
{
    FakeVirus v;
    v.ram.resize(31);
    MemorySync sync(v.send());
    v.sync = &sync;
    Bank cache;
    for (int p = 0; p < 128; p++)
        v.ram[30][p] = fake_single(30, p, p + 3); // ROM Z = bank 30
    cache[0] = v.ram[30][0];
    cache[64] = v.ram[30][64];
    std::vector<int> order;
    int n = sync.read_missing(30, cache, [&](int p) { order.push_back(p); });
    CHECK_EQ(n, 126);
    CHECK_EQ(order.size(), 126u);
    CHECK(order.front() == 1 && order.back() == 127);
    for (int p = 0; p < 128; p++)
        CHECK(cache[p] == v.ram[30][p]);
    CHECK_EQ(v.requests.size(), 252u); // 126 x (display message + request), no bank info
    const Bytes rise = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x74, 0x01, 0x7E, 0x04, 0xF7};
    CHECK(v.requests[0] == rise);
    CHECK((v.requests[1] == Bytes{0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x30, 0x1E, 0x01, 0xF7}));
    for (auto &r : v.requests)
        CHECK(r[6] != 0x39);
}

TEST(memsync_read_missing_full_cache_sends_nothing)
{
    FakeVirus v;
    MemorySync sync(v.send());
    v.sync = &sync;
    Bank cache;
    for (int p = 0; p < 128; p++)
        cache[p] = fake_single(5, p, p);
    CHECK_EQ(sync.read_missing(5, cache), 0);
    CHECK(v.requests.empty());
}

// stopped (closed, notes) or no reply: what was read stays, the next start continues with the rest
TEST(memsync_read_missing_stops)
{
    FakeVirus v;
    v.ram.resize(6);
    MemorySync sync(v.send());
    v.sync = &sync;
    Bank cache;
    for (int p = 0; p < 128; p++)
        v.ram[5][p] = fake_single(5, p, p);
    int calls = 0;
    sync.before_request = [&] { return ++calls <= 3; };
    CHECK_EQ(sync.read_missing(5, cache), -2);
    CHECK(cache[2] == v.ram[5][2] && cache[3].empty());
    sync.before_request = nullptr;
    std::atomic<bool> stop{true};
    CHECK_EQ(sync.read_missing(5, cache, nullptr, &stop), -2);
}

// Only an Access SysEx (F0 00 20 33) longer than 8 bytes is a reply: other manufacturers' SysEx and cut-short Access
// messages whose bytes 6.. happen to look like the reply must not be taken for it (the plug-in feeds every SysEx from
// the Virus in, also MIDI-port traffic)
TEST(memsync_transact_skips_lookalike_messages)
{
    MemorySync *ms = nullptr;
    const Bytes good = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x14, 0x01, 0x00, 0x55, 0xF7};
    MemorySync sync([&](const Bytes &) {
        ms->feed({0xF1, 0x00, 0x20, 0x33, 0x01, 0x10, 0x14, 0x01, 0x00, 0xF7}); // not a SysEx start
        ms->feed({0xF0, 0x01, 0x20, 0x33, 0x01, 0x10, 0x14, 0x01, 0x00, 0xF7}); // another manufacturer (each byte)
        ms->feed({0xF0, 0x00, 0x21, 0x33, 0x01, 0x10, 0x14, 0x01, 0x00, 0xF7});
        ms->feed({0xF0, 0x00, 0x20, 0x34, 0x01, 0x10, 0x14, 0x01, 0x00, 0xF7});
        ms->feed({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x14, 0x01}); // 8 bytes: cut short
        ms->feed(good);
        return true;
    });
    ms = &sync;
    Dump r = sync.transact({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x39, 0x01, 0xF7}, {0x14, 0x01}, 500, 1);
    CHECK(r == good);
}

// the match is a prefix of bytes 6..: a reply that ends right after the matched bytes still counts
TEST(memsync_transact_reply_just_long_enough)
{
    MemorySync *ms = nullptr;
    const Bytes nine = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x10, 0x00, 0x05};
    MemorySync sync([&](const Bytes &) {
        ms->feed(nine);
        return true;
    });
    ms = &sync;
    CHECK(sync.transact({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x30, 0x00, 0x05, 0xF7}, {0x10, 0x00, 0x05}, 300, 1) ==
          nine);
}

// a single dump of the wrong size is asked for again, but bounded: 3 requests, then single() gives up (each extra
// request costs ~1 s on the Virus and delays its notes)
TEST(memsync_single_gives_up_after_three_wrong_sizes)
{
    MemorySync *ms = nullptr;
    int requests = 0;
    MemorySync sync([&](const Bytes &m) {
        requests++;
        ms->feed({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x10, m[7], m[8], 0x00, 0xF7}); // cut short
        return true;
    });
    ms = &sync;
    CHECK(sync.single(1, 7).empty());
    CHECK_EQ(requests, 3);
}

// a ROM bank has 128 programs (0..127): read_missing must never ask for a program number 128 (0x80 is not even a
// valid data byte in a SysEx). The bank sits in front of an empty dump so that a read one past its end would find an
// empty slot and ask for it.
TEST(memsync_read_missing_never_asks_past_program_127)
{
    struct {
        Bank bank;
        Dump after;
    } cache;
    MemorySync *ms = nullptr;
    int singles = 0, bad = 0;
    MemorySync sync([&](const Bytes &m) {
        if (m.size() >= 9 && m[6] == 0x30) {
            singles++;
            bad += m[8] > 0x7F;
            ms->feed(fake_single(m[7], m[8], 1));
        }
        return true;
    });
    ms = &sync;
    CHECK_EQ(sync.read_missing(5, cache.bank), 128);
    CHECK_EQ(singles, 128);
    CHECK_EQ(bad, 0);
}

namespace {
struct TempDir {
    std::filesystem::path dir;
    explicit TempDir(const char *name) : dir(std::filesystem::temp_directory_path() / name)
    {
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
    }
    ~TempDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
    std::string write(const std::string &name, const Bytes &b) const
    {
        FILE *f = fopen((dir / name).string().c_str(), "wb");
        fwrite(b.data(), 1, b.size(), f);
        fclose(f);
        return (dir / name).string();
    }
};
} // namespace

// a cache file holds exactly 128 programs: whatever follows them is not read (and cannot make the load fail), and a
// dump of up to 4096 bytes is accepted (only longer ones are taken as a broken file)
TEST(memsync_bank_file_limits)
{
    TempDir t("vtitests-memsync-limits");
    Bytes full = {'M', 'I', 'D', 'I', 0, 1, 8, 0};
    for (int p = 0; p < 128; p++) {
        Dump d = fake_single(1, p, p);
        full.insert(full.end(), {0x00, 0x00, 0x02, 0x0C});
        full.insert(full.end(), d.begin(), d.end());
    }
    full.insert(full.end(), {0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x02}); // trailing bytes after program 128
    Bank in;
    CHECK(load_bank_file(t.write("trailing.bin", full), in));
    CHECK(in[127] == fake_single(1, 127, 127));

    Bytes big(12 + 4096, 0x11);
    const uint8_t head[12] = {'M', 'I', 'D', 'I', 0, 1, 8, 0, 0x00, 0x00, 0x10, 0x00}; // one dump of 4096 bytes
    std::copy(head, head + 12, big.begin());
    Bank b;
    CHECK(load_bank_file(t.write("4096.bin", big), b));
    CHECK_EQ(b[0].size(), 4096u);
}

// a save that cannot be done reports it, and an existing cache file at the path is never left half-written
TEST(memsync_bank_file_save_errors)
{
    TempDir t("vtitests-memsync-save");
    Bank bank;
    bank[0] = fake_single(1, 0, 0);
    CHECK(!save_bank_file((t.dir / "no-such-folder" / "bank.bin").string(), bank)); // the temp file cannot be made
    // the rename at the end fails (a non-empty folder is in the way): the folder stays as it was
    const std::filesystem::path in_way = t.dir / "bank.bin";
    std::filesystem::create_directories(in_way);
    t.write("bank.bin/keep", {1});
    CHECK(!save_bank_file(in_way.string(), bank));
    CHECK(std::filesystem::is_directory(in_way) && std::filesystem::exists(in_way / "keep"));
#ifndef _WIN32
    // the disk is full when the buffered data is written at fclose (the writes before it all succeed): simulated
    // with a file size limit of 100 bytes (SIGXFSZ ignored, so the write fails with EFBIG instead)
    {
        Bank empty; // 8 + 128 x 4 = 520 bytes: all in stdio's buffer until fclose
        const std::string path = (t.dir / "full.bin").string();
        const std::string old = t.write("full.bin", {'o', 'l', 'd'});
        struct rlimit was;
        getrlimit(RLIMIT_FSIZE, &was);
        void (*was_sig)(int) = signal(SIGXFSZ, SIG_IGN);
        struct rlimit small = was;
        small.rlim_cur = 100;
        setrlimit(RLIMIT_FSIZE, &small);
        const bool ok = save_bank_file(path, empty);
        setrlimit(RLIMIT_FSIZE, &was);
        signal(SIGXFSZ, was_sig);
        CHECK(!ok);
        CHECK_EQ(std::filesystem::file_size(old), 3u); // the old file is kept
    }
#endif
}
