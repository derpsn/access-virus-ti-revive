// setuptips.h - the setup page of the plug-in (user, 2026-11-05, #30): what a user of this DAW should check, with the real
// numbers of this setup. It opens by itself the first time for a DAW and for a warning not shown before, and it is in the
// "?" menu. Our own words. Every point was checked against TI Revive or says that it is Access' advice for the original
// (E406, E408): the manuals' list cannot be copied, e.g. offline bounces work here. Built from plain state, no drawing.
#pragma once
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>
#include <vector>

namespace vti {

enum class TipsHost { Logic, GarageBand, Live, Cubase, Reaper, FlStudio, Other };

inline std::string tips_lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

// vst_name: IHostApplication::getName. In an Audio Unit that is only Steinberg's wrapper, so the process' name decides.
inline TipsHost tips_host(const std::string &vst_name, const std::string &process)
{
    const auto is = [](const std::string &s, const char *what) { return s.find(what) != std::string::npos; };
    for (const std::string &n : {tips_lower(vst_name), tips_lower(process)}) {
        if (is(n, "logic"))
            return TipsHost::Logic;
        if (is(n, "garageband"))
            return TipsHost::GarageBand;
        if (is(n, "ableton") || n == "live")
            return TipsHost::Live;
        if (is(n, "cubase") || is(n, "nuendo"))
            return TipsHost::Cubase;
        if (is(n, "reaper"))
            return TipsHost::Reaper;
        if (is(n, "fl studio") || is(n, "fl64"))
            return TipsHost::FlStudio;
    }
    return TipsHost::Other;
}

struct TipsFacts {
    TipsHost host = TipsHost::Other;
    double host_rate = 44100;
    double virus_rate = 44100;
    int largest_block = 256; // what the host announced; in an Audio Unit MaximumFramesPerSlice, not the I/O buffer (E401)
    double delay_ms = 0;     // the plug-in latency it reports now
    bool mac = false;        // the MIDI input "Virus TI Synth" exists only there
};

struct Tip {
    std::string key;
    bool warn;
    std::string text;
};

struct TipsPage {
    std::string title;
    std::string slug; // of the DAW, in the "seen" file
    std::vector<Tip> tips;
};

inline TipsPage setup_tips(const TipsFacts &f)
{
    TipsPage p;
    const auto ms = [](double v) {
        char b[32];
        snprintf(b, sizeof b, "%.0f ms", v);
        return std::string(b);
    };
    const auto add = [&](const char *key, std::string text, bool warn = false) { p.tips.push_back({key, warn, std::move(text)}); };
    const std::string delay = ms(f.delay_ms);
    switch (f.host) {
    case TipsHost::Logic: p.title = "Setup tips for Logic Pro"; p.slug = "logic"; break;
    case TipsHost::GarageBand: p.title = "Setup tips for GarageBand"; p.slug = "garageband"; break;
    case TipsHost::Live: p.title = "Setup tips for Ableton Live"; p.slug = "live"; break;
    case TipsHost::Cubase: p.title = "Setup tips for Cubase and Nuendo"; p.slug = "cubase"; break;
    case TipsHost::Reaper: p.title = "Setup tips for REAPER"; p.slug = "reaper"; break;
    case TipsHost::FlStudio: p.title = "Setup tips for FL Studio"; p.slug = "flstudio"; break;
    default: p.title = "Setup tips for your DAW"; p.slug = "other"; break;
    }
    // what is wrong here comes first
    if (f.largest_block > 512) {
        const std::string blocks = std::to_string(f.largest_block) + " samples, which adds " +
                                   ms((f.largest_block - 256) * 1000.0 / f.virus_rate) + " here. ";
        if (f.host == TipsHost::Logic)
            add("large-block", "Logic may ask for blocks of up to " + blocks + "Set Process Buffer Range to Small.", true);
        else
            add("large-block", "Your DAW may ask for blocks of up to " + blocks + "A smaller audio buffer avoids that.", true);
    }
    if (f.host_rate < 44099 || (f.host_rate > 44101 && f.host_rate < 47999) || f.host_rate > 48001) {
        char b[160];
        snprintf(b, sizeof b, "The project runs at %.0f Hz. The Virus runs at 44.1 or 48 kHz, so TI Revive converts, which adds a little delay. A project at 44.1 or 48 kHz avoids that.", f.host_rate);
        add("rate", b, true);
    }
    add("delay", "Delay now: " + delay + ". TI Revive reports it, so playback stays in time. Keys you play live are heard that late too; a faster path for playing is being worked on.");
    switch (f.host) {
    case TipsHost::Logic:
        add("logic-range", "Settings > Audio > Devices: Process Buffer Range Small. TI Revive's delay follows the largest block Logic may ask for.");
        add("compensation", "Settings > Audio > General: Plug-in Latency Compensation must not be Off, or the Virus sounds " + delay + " late against other tracks.");
        add("logic-low-latency", "Keep Low Latency Mode off.");
        add("logic-parts", "Several parts: one instance plays them all. In the New Tracks dialog choose Multi-timbral; each MIDI channel plays one part.");
        add("logic-cycle", "Settings > General > Cycle: Smooth Cycle Algorithm, pre-processing 1/96 (Access' advice for the original).");
        break;
    case TipsHost::GarageBand:
        add("compensation", "TI Revive tells GarageBand about its delay; GarageBand has no setting for this.");
        break;
    case TipsHost::Live:
        add("compensation", "Options menu: Delay Compensation on, or the Virus sounds " + delay + " late against other tracks.");
        add("live-parts", "Several parts: one instance plays them all. Point other MIDI tracks at its track with MIDI To, one channel each, and arm them to play.");
        if (f.mac)
            add("live-keys", "A Virus with keys: choose the MIDI input Virus TI Synth under MIDI From on an armed track.");
        break;
    case TipsHost::FlStudio:
        add("compensation", "Keep plug-in delay compensation on, or the Virus sounds " + delay + " late against other tracks.");
        add("fl-wrapper", "In the plug-in's wrapper settings, Processing: Process inactive inputs and outputs on, Use fixed size buffers off (Access' advice for the original).");
        break;
    default:
        add("compensation", "Keep plug-in delay compensation on, or the Virus sounds " + delay + " late against other tracks.");
        add("parts", "Several parts: one instance plays them all. Send each part's notes on its own MIDI channel.");
        break;
    }
    add("buffer", "An audio buffer of 128 or 256 samples works well. Below 256 only your DAW's own part gets shorter, TI Revive's does not.");
    add("start", "Leave the first bar empty: a note right at the start of playback can come late.");
    return p;
}

// "logic/large-block": a point once read stays read, per DAW
inline std::string tips_seen_key(const TipsPage &p, const Tip &t) { return p.slug + "/" + t.key; }

// the page opens by itself when it has a point not read before
inline bool tips_open_at_start(const TipsPage &p, const std::vector<std::string> &seen)
{
    for (const Tip &t : p.tips)
        if (std::find(seen.begin(), seen.end(), tips_seen_key(p, t)) == seen.end())
            return true;
    return false;
}

inline std::vector<std::string> tips_seen_after(const TipsPage &p, std::vector<std::string> seen)
{
    for (const Tip &t : p.tips) {
        std::string k = tips_seen_key(p, t);
        if (std::find(seen.begin(), seen.end(), k) == seen.end())
            seen.push_back(std::move(k));
    }
    return seen;
}

inline std::string tips_seen_text(const std::vector<std::string> &seen)
{
    std::string s;
    for (const std::string &k : seen)
        s += k + "\n";
    return s;
}

inline std::vector<std::string> tips_seen_parse(const std::string &text)
{
    std::vector<std::string> out;
    std::string line;
    for (char c : text) {
        if (c == '\n' || c == '\r') {
            if (!line.empty())
                out.push_back(line);
            line.clear();
        } else
            line += c;
    }
    if (!line.empty())
        out.push_back(line);
    return out;
}

} // namespace vti
