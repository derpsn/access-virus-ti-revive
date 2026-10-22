// sidechain.h - host audio towards the Virus' USB input ("3 Outs / 1 Input"), as the original does it (E167): the
// host's frames go through a sample-rate converter into the 2-channel ring the broker plays to the Virus at the
// Virus' clock. The converter's rate follows the ring's fill level (the original drives its CSampleRateConverter the
// same way, 0x1800aa1c0 / 0x1800a8f60), so the DAW's clock and the Virus' clock may differ and the host may run at
// any rate. Audio thread only; no allocation after setup().
#pragma once
#include "device/resampler.h"
#include <cstdint>
#include <vector>

namespace vti {

class SidechainSender {
public:
    // frames kept queued in the ring before each block: at least the broker's prefill (512), and with a block on top
    // below its limit (2048, it drops beyond)
    static constexpr int TARGET = 512;

    void setup(double host_rate, int max_block, int virus_rate = 44100); // the Virus at 44.1 or 48 kHz (E323)
    void reset(); // the stream starts again (e.g. after another writer had the ring)

    // n host frames (l/r may be null = silence); queued = frames in the ring right before this block. Writes up to
    // max_out() 16-bit stereo frames to out and returns their number.
    int process(const float *l, const float *r, int n, int64_t queued, int16_t *out);
    int max_out() const { return max_out_; }

    // diagnostics: rate correction (relative), refills (start, the broker ran dry)
    double correction() const { return corr_; }
    int64_t refills() const { return refills_; }

private:
    Resampler rs_;
    double nominal_ = 1;      // host frames per Virus frame at equal clocks (host rate / Virus rate)
    int max_block_ = 0, max_out_ = 0, cap_ = 0;
    std::vector<float> hist_[2]; // host frames from absolute frame first_ on (cnt_ of them)
    int64_t first_ = 0;
    int cnt_ = 0;
    double pos_ = 0;           // absolute host-frame position of the next Virus frame
    double avg_ = TARGET;      // the ring's fill level, averaged over ~32 blocks (single readings jump: USB bursts)
    double integ_ = 0, corr_ = 0;
    bool started_ = false;
    int64_t refills_ = 0;
    std::vector<float> tmp_[2]; // converter output before int16
};

// Which of the input buses a host passed to process() is the sidechain. declared = the plug-in's input buses,
// active[i] = their activation by the host (activateBus), passed = ProcessData::numInputs, aux = the sidechain's
// declared index. A host that passes all declared buses has it at its declared place; a host that passes only the
// active ones (some instrument hosts) has it after the active buses declared before it. -1: not passed.
int sidechain_bus_index(int declared, const bool *active, int passed, int aux);

// The bus the sidechain comes in on, for the plug-in's two input buses (0 = "Dummy In" main, 1 = "Sidechain" aux):
// the Sidechain bus if the host activated it (REAPER); the main input if the host activated only that (Ableton Live
// delivers an instrument's sidechain there, E172). Indices as passed to process() (see sidechain_bus_index).
int sidechain_source_bus(const bool active[2], int passed);

// the highest absolute sample of l and r (either may be null) over n frames: the log's sidechain input level
float sidechain_peak(const float *l, const float *r, int n);

} // namespace vti
