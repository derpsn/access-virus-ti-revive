// SidechainSender: the host's audio towards the Virus at the Virus' clock (E167/E168). A simulated broker takes the
// frames in USB bursts at the Virus' rate (with a clock difference to the host); the ring must stay near its target
// without running dry, and the converted sine must be continuous (no dropped or repeated frames) at the right pitch.
#include "check.h"
#include "device/sidechain.h"
#include <cmath>

using namespace vti;

struct SimResult {
    int64_t underruns = 0, refills = 0;
    int64_t q_min = INT64_MAX, q_max = INT64_MIN; // before a host block, after the settling time
    double worst_jump = 0, expect_d2 = 0;         // second difference of the output sine vs. a clean sine's maximum
    double freq = 0;                              // cycles per Virus frame * the Virus' nominal rate
};

// host at host_rate with blocks of `block`, the Virus at `vrate` (44.1 or 48 kHz, E323) with its clock off by `ppm`,
// 1 kHz sine at -6 dBFS, `seconds` of time
static SimResult simulate(double host_rate, int block, double ppm, double seconds, double vrate = 44100)
{
    SidechainSender s;
    s.setup(host_rate, block, (int)vrate);
    std::vector<int16_t> out((size_t)s.max_out() * 2);
    std::vector<float> l((size_t)block), r((size_t)block);
    std::vector<float> played; // what the Virus played (left), after the settling time
    SimResult res;
    const double virus_rate = vrate * (1 + ppm * 1e-6), burst = 352; // 8 ms of USB transfers
    const double settle = 20;
    int64_t written = 0, consumed = 0;
    double next_burst = 0, phase = 0;
    std::vector<int16_t> ring; // frames written, consumed in order
    size_t ring_read = 0;
    const int blocks = (int)(seconds * host_rate / block);
    for (int b = 0; b < blocks; b++) {
        const double t = (double)b * block / host_rate;
        while (next_burst <= t) { // the broker takes a burst when it is due (silence if not enough is there)
            int64_t avail = written - consumed;
            if (avail < burst) {
                if (t > settle)
                    res.underruns++;
                consumed = written;
                ring_read = ring.size() / 2;
            } else {
                for (int i = 0; i < (int)burst; i++, ring_read++)
                    if (t > settle)
                        played.push_back(ring[ring_read * 2] / 32768.0f);
                consumed += (int64_t)burst;
            }
            next_burst += burst / virus_rate;
        }
        for (int i = 0; i < block; i++) {
            l[(size_t)i] = r[(size_t)i] = (float)(0.5 * std::sin(phase));
            phase += 2 * Resampler::PI * 1000.0 / host_rate;
        }
        const int64_t q = written - consumed;
        if (t > settle) {
            res.q_min = std::min(res.q_min, q);
            res.q_max = std::max(res.q_max, q);
        }
        int64_t before = s.refills();
        int w = s.process(l.data(), r.data(), block, q, out.data());
        if (t > settle)
            res.refills += s.refills() - before;
        ring.insert(ring.end(), out.begin(), out.begin() + 2 * w);
        written += w;
    }
    // the played sine: continuity and pitch (in the Virus' frames: the host's 1 kHz at the Virus' rate)
    int crossings = 0;
    size_t first = 0, last = 0;
    for (size_t i = 2; i < played.size(); i++) {
        double d2 = played[i] - 2 * played[i - 1] + played[i - 2];
        res.worst_jump = std::max(res.worst_jump, std::fabs(d2));
        if (played[i - 1] < 0 && played[i] >= 0) {
            if (!crossings)
                first = i;
            last = i;
            crossings++;
        }
    }
    res.freq = crossings > 1 ? (crossings - 1) * vrate / (double)(last - first) : 0;
    const double w0 = 2 * Resampler::PI * 1000.0 / vrate;
    res.expect_d2 = 0.5 * w0 * w0;
    return res;
}

static void check_case(double host_rate, int block, double ppm, double vrate = 44100)
{
    SimResult r = simulate(host_rate, block, ppm, 90, vrate);
    std::fprintf(stderr, "  %.0f Hz block %d, %+.0f ppm: underruns %lld refills %lld queued %lld..%lld jump %.5f "
                         "(clean %.5f) %.2f Hz\n",
                 host_rate, block, ppm, (long long)r.underruns, (long long)r.refills, (long long)r.q_min,
                 (long long)r.q_max, r.worst_jump, r.expect_d2, r.freq);
    CHECK(r.underruns == 0 && r.refills == 0);
    CHECK(r.q_min > 0 && r.q_max < 2048); // never dry, never beyond the broker's limit
    CHECK(r.worst_jump < 1.3 * r.expect_d2 + 4.0 / 32768); // no dropped or repeated frame
    // 1 kHz of the host's clock heard at the Virus' clock: 1000 / (1 + ppm) in the Virus' frames
    CHECK(std::fabs(r.freq - 1000.0 / (1 + ppm * 1e-6)) < 0.05);
}

TEST(sidechain_follows_the_virus_clock)
{
    check_case(44100, 512, 0);
    check_case(44100, 512, 300);   // the Virus faster than the DAW
    check_case(44100, 512, -300);  // slower
    check_case(44100, 64, 120);    // small blocks
    check_case(44100, 1024, -80);  // large blocks
}

TEST(sidechain_other_host_rates)
{
    check_case(48000, 512, 100);
    check_case(96000, 1024, -150);
    check_case(32000, 256, 50);
}

TEST(sidechain_virus_at_48k)
{
    // E323: the Virus runs at 48 kHz for 48 kHz projects (no conversion), 96 kHz is 2:1
    check_case(48000, 512, 100, 48000);
    check_case(48000, 128, -200, 48000);
    check_case(96000, 1024, -150, 48000);
}

TEST(sidechain_silence_and_restart)
{
    SidechainSender s;
    s.setup(44100, 256);
    std::vector<int16_t> out((size_t)s.max_out() * 2, 1);
    int w = s.process(nullptr, nullptr, 256, 0, out.data()); // no Sidechain buffers: silence, refilled to the target
    CHECK(w >= SidechainSender::TARGET && w <= s.max_out());
    bool silent = true;
    for (int i = 0; i < 2 * w; i++)
        silent = silent && out[(size_t)i] == 0;
    CHECK(silent);
    CHECK(s.refills() == 1);
    w = s.process(nullptr, nullptr, 256, 0, out.data()); // the broker ran dry: refill again
    CHECK(s.refills() == 2 && w >= SidechainSender::TARGET);
    s.reset();
    CHECK(s.correction() == 0);
    w = s.process(nullptr, nullptr, 99999, 600, out.data()); // an oversized block is cut to the announced size
    CHECK(w <= s.max_out());
}

TEST(sidechain_bus_lookup)
{
    // declared input buses: 0 = "Dummy In" (main), 1 = "Sidechain" (aux). Hosts pass either all declared buses or,
    // like some instrument hosts, only the active ones - the sidechain has to be found in both cases
    const bool both[2] = {true, true}, aux_only[2] = {false, true}, none[2] = {false, false};
    CHECK(sidechain_bus_index(2, both, 2, 1) == 1);     // all buses passed
    CHECK(sidechain_bus_index(2, aux_only, 2, 1) == 1); // all passed although the main bus is inactive
    CHECK(sidechain_bus_index(2, aux_only, 1, 1) == 0); // only the active (sidechain) bus passed
    CHECK(sidechain_bus_index(2, none, 2, 1) == 1);     // nothing activated: the declared place (silence if unused)
    CHECK(sidechain_bus_index(2, none, 0, 1) == -1);    // no buses passed at all
    CHECK(sidechain_bus_index(2, both, 1, 1) == -1);    // one bus passed while both are active: it is the main one
}

TEST(sidechain_input_peak)
{
    float a[4] = {0.1f, -0.7f, 0.3f, 0.0f}, b[4] = {0.2f, 0.25f, -0.9f, 0.0f};
    CHECK(std::fabs(sidechain_peak(a, b, 4) - 0.9f) < 1e-6f);
    CHECK(sidechain_peak(a, nullptr, 4) > 0.69f);
    CHECK(sidechain_peak(nullptr, nullptr, 4) == 0.0f);
}

TEST(sidechain_source_bus_choice)
{
    // the declared Sidechain bus when the host activated it (REAPER); a host that activates only the main input of an
    // instrument (Ableton Live: "active: 1 0" on macOS, E172) delivers its sidechain there - then the main input
    const bool both[2] = {true, true}, main_only[2] = {true, false}, aux_only[2] = {false, true}, none[2] = {false, false};
    CHECK(sidechain_source_bus(both, 2) == 1);
    CHECK(sidechain_source_bus(aux_only, 2) == 1);
    CHECK(sidechain_source_bus(aux_only, 1) == 0);  // only the active bus passed
    CHECK(sidechain_source_bus(main_only, 2) == 0); // Ableton
    CHECK(sidechain_source_bus(main_only, 1) == 0);
    CHECK(sidechain_source_bus(none, 2) == 1);      // nothing activated: the declared place (silence if unused)
    CHECK(sidechain_source_bus(none, 0) == -1);
}
