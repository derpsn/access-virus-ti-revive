// panelfollow.h - what the plug-in does when the Virus reports a change made on its own panel (E152), like the
// original Virus Control: a preset chosen on the Virus (it reports the part's Bank / Patch and waits for the host
// to send the patch into the edit buffer) and a soft knob turned on the Virus (it reports only the knob's value).
// The decisions live here, without the plug-in, so the tests can check them; the plug-in does the sending.
#pragma once
#include "remotedevice.h"
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace vti {

class PanelFollow {
public:
    using Clock = std::chrono::steady_clock;
    // the original loads a part's patch 150 ms after its last Bank / Patch report (timer 0x18007ec30): turning the
    // Virus' value knob through the presets loads only where it stops
    static constexpr std::chrono::milliseconds PRESET_QUIET{150};

    static bool is_preset_path(const std::string &path); // Multi Part Parameter/Bank or /Patch
    // a reported Bank / Patch change of `part` (0..15): marks it; false if it is no such report or out of range
    bool preset_report(const std::string &path, int part, Clock::time_point now);
    // the parts (bit per part) whose report is PRESET_QUIET old, and forgets them; 0 if none is due
    unsigned presets_due(Clock::time_point now);

    // a report that decodes to several parameters (the same controller, told apart by a condition: CC 40 = Filter 1
    // Cutoff with the vocoder off, Vocoder Carrier Center Frequency with it on): only those whose condition holds now
    // (`holds(path, part)`), like the original; if none holds, all (no value is lost). One parameter is always kept.
    template <class Holds>
    static std::vector<RemoteDevice::Change> reports_by_condition(const std::vector<RemoteDevice::Change> &changes, Holds holds)
    {
        if (changes.size() < 2)
            return changes;
        std::vector<RemoteDevice::Change> r;
        for (auto &c : changes)
            if (holds(c.path, c.part))
                r.push_back(c);
        return r.empty() ? changes : r;
    }

    // parameters that are not per part (part="none": Panel Part, the multi's own settings) keep their report as a global
    // (-1), where the editor reads and sets them: byte 7 of such a report is not their part - for Panel Part it is the
    // part selected before the switch (E342), so stored under it the editor never followed the Virus' part selection
    template <class IsGlobal>
    static void globals_without_part(std::vector<RemoteDevice::Change> &changes, IsGlobal is_global)
    {
        for (auto &c : changes)
            if (is_global(c.path))
                c.part = -1;
    }

    // "Global Soft Knob Configuration/Value Softknob0..2" -> soft knob 1..3, else 0
    static int softknob_report(const std::string &path);

    // the controllers the original sends again after a patch went into a part's edit buffer (0x180083ae0), so the
    // new patch starts with the current mod wheel, breath, ... positions
    static const std::vector<std::string> &restored_controllers();

    // a controller from the host (VST3 MIDI mapping) whose value the plug-in keeps for the channel: the "Internal MIDI
    // Standard Parameters" (mod wheel, breath, volume, expression, ...), like the ones the Virus reports; other
    // controllers change synth parameters of the patch
    static bool host_controller_kept(const std::string &path);

private:
    std::mutex mu_;
    unsigned parts_ = 0;
    Clock::time_point due_{};
};

} // namespace vti
