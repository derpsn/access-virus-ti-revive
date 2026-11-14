// markerscan.h - the sync-marker bookkeeping of DeviceOut::scan_audio (docs/protocol.md "Sync marker"): process()
// sends a ping and calls calibrate() with its ring frame; the worker's scan passes look for the Virus' answer, one
// non-zero sample per channel on USB 2 with USB 1 silent, and give a ping 5 s before the marker counts as not found.
// calibrate() runs on the audio thread, everything else on the worker; a ping can arrive in the middle of a pass
// (E497), so nothing here keeps the ping frame from the start of a pass.
#pragma once
#include <atomic>
#include <cstdint>

namespace vti {

class MarkerScan {
public:
    // the frame where the Virus reset its stamp clock (>= 0); -1 searching or unknown, -2 not found (stamps stay
    // relative to the ring)
    std::atomic<int64_t> marker{-2};

    // audio thread: a ping went out at ring frame `from`; a new audio stream voids the old marker
    void calibrate(int64_t from, bool new_stream)
    {
        from_ = from;
        pings_.fetch_add(1);
        if (new_stream || marker.load() < 0)
            marker = -1;
    }
    bool calibrating() const { return pings_.load() > 0; }

    // worker: a new audio stream (reconnect, restart, rate change): an old ping gets no marker in it
    void reset_stream()
    {
        skip_ = -1;
        pings_ = 0;
    }

    // worker, per frame of a scan pass: f could be the marker (a ping waits, f is at or after the NEWEST ping, past
    // the last marker, USB 2 only). The ping frame is read here, not once per pass (E497): a ping that process()
    // sends during the pass must not take a frame from before it as its marker.
    bool candidate(int64_t f, bool usb1, bool usb2) const
    {
        if (!usb2 || usb1 || pings_.load() <= 0)
            return false;
        const int64_t from = from_.load();
        return from >= 0 && f >= from && f > skip_;
    }
    // the caller checked that USB 2 is silent around f: the newest marker wins
    void take(int64_t f)
    {
        marker = f;
        pings_.fetch_sub(1);
        skip_ = f + 1; // its second channel follows one frame later
    }
    // worker, after a scan pass: newest = the ring's newest frame; true when the timeout found no marker at all
    // (marker now -2, worth a log line). E497: the timeout judges the NEWEST ping, read now, not a snapshot from the
    // pass's start, so a re-sync ping sent during the pass keeps its own 5 s. Order: the pings count first, then the
    // frame (calibrate() writes the frame first), and the count is cleared only if it did not change in between, so
    // a ping that lands at the very end survives too.
    bool end(int64_t newest, int rate)
    {
        int out = pings_.load();
        const int64_t from = from_.load();
        if (out > 0 && from >= 0 && newest > from + 5 * (int64_t)rate // 5 s without a marker
            && pings_.compare_exchange_strong(out, 0)) {
            if (marker.load() < 0) {
                marker = -2;
                return true;
            }
        }
        return false;
    }
    int64_t ping_frame() const { return from_.load(); }

private:
    std::atomic<int64_t> from_{-1};
    std::atomic<int> pings_{0}; // pings sent whose marker has not been seen yet
    int64_t skip_ = -1;         // worker: frames up to here belong to the last marker
};

} // namespace vti
