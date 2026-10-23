// sidechain.cpp - see sidechain.h.
#include "device/sidechain.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace vti {

// the rate control: proportional to the averaged fill error, plus an integral that learns the clock difference
// (typically well under 0.1 %). With KP the ring settles in a few seconds; the integral takes over within ~10 s.
static constexpr double KP = 4e-6;        // per frame of error
static constexpr double KI = 4e-12;       // per frame of error and host frame
static constexpr double MAX_INTEG = 0.005, MAX_CORR = 0.01;

void SidechainSender::setup(double host_rate, int max_block, int virus_rate)
{
    nominal_ = host_rate > 0 && virus_rate > 0 ? host_rate / virus_rate : 1.0;
    rs_.setup(nominal_); // band-limited for hosts above 44.1 kHz
    max_block_ = std::max(1, max_block);
    cap_ = max_block_ + 2 * Resampler::TAPS + 8;
    max_out_ = (int)(max_block_ / nominal_ * (1 + 2 * MAX_CORR)) + 4 + TARGET;
    for (int c = 0; c < 2; c++) {
        hist_[c].assign((size_t)cap_, 0.0f);
        tmp_[c].assign((size_t)max_out_, 0.0f);
    }
    refills_ = 0;
    reset();
}

void SidechainSender::reset()
{
    for (auto &h : hist_)
        std::fill(h.begin(), h.end(), 0.0f);
    first_ = 0;
    cnt_ = Resampler::TAPS; // silence before the first frame, so the filter has its history
    pos_ = Resampler::TAPS; // the first Virus frame: the host's first frame
    avg_ = TARGET;
    integ_ = corr_ = 0;
    started_ = false;
}

int SidechainSender::process(const float *l, const float *r, int n, int64_t queued, int16_t *out)
{
    if (hist_[0].empty())
        return 0;
    n = std::max(0, std::min(n, max_block_));
    const float *src[2] = {l, r};
    for (int c = 0; c < 2; c++) {
        float *h = hist_[c].data() + cnt_;
        if (src[c])
            memcpy(h, src[c], sizeof(float) * (size_t)n);
        else
            memset(h, 0, sizeof(float) * (size_t)n);
    }
    cnt_ += n;
    int w = 0;
    if (!started_ || queued < TARGET / 4) { // the start, or the broker ran dry: fill up to the target at once
        // (none if the ring is already full enough, e.g. a restart right after another writer)
        int pad = (int)std::max<int64_t>(0, TARGET - std::max<int64_t>(queued, 0));
        memset(out, 0, sizeof(int16_t) * 2 * (size_t)pad);
        w = pad;
        avg_ = TARGET;
        started_ = true;
        refills_++;
    } else {
        avg_ += ((double)queued - avg_) / 32.0;
    }
    const double e = avg_ - TARGET; // > 0: too full, take the host frames a little faster
    integ_ = std::max(-MAX_INTEG, std::min(MAX_INTEG, integ_ + e * KI * n));
    corr_ = std::max(-MAX_CORR, std::min(MAX_CORR, e * KP + integ_));
    const double step = nominal_ * (1 + corr_);
    // as many Virus frames as the host frames so far allow (the filter needs HALF frames after each position)
    const int64_t last = first_ + cnt_ - 1;
    const double room = (double)(last - Resampler::HALF) - pos_;
    int k = room >= 0 ? (int)std::floor(room / step) + 1 : 0;
    while (k > 0 && (int64_t)std::floor(pos_ + (k - 1) * step) + Resampler::HALF > last)
        k--;
    k = std::min(k, max_out_ - w);
    if (k > 0) {
        for (int c = 0; c < 2; c++)
            rs_.process(hist_[c].data(), first_, pos_, step, tmp_[c].data(), k);
        for (int i = 0; i < k; i++)
            for (int c = 0; c < 2; c++) {
                float v = std::max(-1.0f, std::min(1.0f, tmp_[c][(size_t)i]));
                out[2 * (w + i) + c] = (int16_t)std::lrint(v * 32767.0f);
            }
        pos_ += k * step;
    }
    // keep only the host frames the next block still needs
    const int64_t keep = (int64_t)std::floor(pos_) - Resampler::HALF + 1;
    const int drop = (int)std::min<int64_t>(std::max<int64_t>(keep - first_, 0), cnt_);
    if (drop > 0) {
        for (auto &h : hist_)
            memmove(h.data(), h.data() + drop, sizeof(float) * (size_t)(cnt_ - drop));
        cnt_ -= drop;
        first_ += drop;
    }
    return w + k;
}

int sidechain_bus_index(int declared, const bool *active, int passed, int aux)
{
    if (passed <= 0 || aux < 0 || aux >= declared)
        return -1;
    if (passed >= declared)
        return aux;
    if (!active[aux])
        return -1;
    int idx = 0;
    for (int i = 0; i < aux; i++)
        idx += active[i] ? 1 : 0;
    return idx < passed ? idx : -1;
}

int sidechain_source_bus(const bool active[2], int passed)
{
    if (!active[1] && active[0])
        return sidechain_bus_index(2, active, passed, 0);
    return sidechain_bus_index(2, active, passed, 1);
}

float sidechain_peak(const float *l, const float *r, int n)
{
    float p = 0;
    for (const float *s : {l, r})
        for (int i = 0; s && i < n; i++)
            p = std::max(p, std::fabs(s[i]));
    return p;
}

} // namespace vti
