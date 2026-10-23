// audioin.h - the Virus' audio (USB 1 / USB 2) from the TI Revive broker's ring (vtiaudioshm.h). The broker streams
// once its owner asked for it (Broker::audio); the synth is the clock master, so the reader keeps a fixed distance
// behind the newest frame and corrects drift by dropping/repeating single frames (read()); the plug-in reads
// stamp-aligned positions (read_at()) and resamples itself at host rates other than 44.1 kHz.
#pragma once
#include <cstdint>

namespace vti {

class AudioIn {
public:
    static constexpr int IN_CHANNELS = 6; // ring order: analog in L/R, USB 1 L/R, USB 2 L/R
    ~AudioIn();
    // Linux: map /dev/shm/tirevive-audio-<uid> (or the POSIX shm `linux_name`: unit tests with their own ring);
    // Windows: the broker's mapping; macOS: Core Audio input, the device set to `rate` (44100/48000) when it differs
    bool open(const char *linux_name = nullptr, int rate = 44100);
    void close();
    bool streaming() const;
    // n frames into usb1 (L, R) and usb2 (L, R); missing data -> silence. `latency` = target distance in frames.
    // usb3l/usb3r (optional): raw channels 0/1, the third USB output while the Virus' USB IO configuration has 3
    // outputs (E158, E164; otherwise its analog inputs)
    void read(float *usb1l, float *usb1r, float *usb2l, float *usb2r, int n, int latency, float *usb3l = nullptr,
              float *usb3r = nullptr);
    int64_t drops() const { return drops_; }
    int64_t filled() const; // macOS: frames of silence put in for IO cycles Core Audio skipped (gapfill.h); else 0
    int64_t position() const { return pos_; } // next frame read() delivers (-1 before the first read)
    // n frames from the absolute ring frame `pos` (silence where not available)
    void read_at(float *usb1l, float *usb1r, float *usb2l, float *usb2r, int n, int64_t pos, float *usb3l = nullptr,
                 float *usb3r = nullptr);
    int64_t newest_frame() const; // synth frames produced so far (-1 if not streaming)
    // the Virus' current sample rate in the ring: 44100 or 48000 (44100 when not open or from an older broker, E323)
    int rate() const;
    // true once per rate switch of the broker (the ring's rate_gen counted up since the last call; the rate the ring
    // was opened with is none); `rate` = the new rate. A switch restarts the stream: re-anchor, new sync ping
    bool rate_changed(int &rate);
    // macOS: switch the Virus' Core Audio rate for our own (re)start, at most once per second; Linux/Windows: the broker
    // owns it (Broker::audio). Returns the rate in force afterwards. Blocks up to ~1 s on macOS (the device changes
    // its clock): never from the audio thread - the plug-in's resume / setActive only.
    int request_rate(int rate);
    // the highest absolute sample per raw channel (VTI_AUDIO_IN_CH, device order) over the newest `frames` frames:
    // diagnostics (which channels carry sound, e.g. the third USB output); false if not streaming
    bool raw_peaks(int frames, int peak[6]) const;
    // all 6 input channels (0/1 analog inputs, 2/3 USB 1, 4/5 USB 2) of n frames from ring frame `pos` as int16, frame
    // interleaved (6 per frame); frames not there (yet) are silence. For the ASIO driver.
    void read_raw_at(int16_t *dst, int n, int64_t pos) const;
    // output towards the synth (version 2 ring, one writer - the ASIO driver): append n stereo frames and publish them;
    // the broker plays them at the synth's rate (prefill 512, at most 2048 ahead). Frames queued and not played yet:
    void write_out(const int16_t *stereo, int n);
    int64_t out_queued() const;
    int64_t out_written() const; // the ring's write position (all writers): another writer moves it too

private:
    void *map_ = nullptr; // vti_audio_shm (vtiaudioshm.h)
    void *mac_ = nullptr; // macOS: the Core Audio reader that fills map_
    int64_t pos_ = -1, drops_ = 0;
    int64_t rate_gen_seen_ = -1; // -1: not looked at since open
};

} // namespace vti
