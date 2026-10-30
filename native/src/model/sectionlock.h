// sectionlock.h - section locking of the original Virus Control (E378, issue #21). A locked section of a part keeps its
// parameters when a patch arrives for that part: the original writes the part's current values of the locked
// sections' children into the incoming single dump, marks it with flags byte 10 = 0x20 and sends that one dump (no
// extra parameter messages). The sections and their children come from editordevice.xml (ParameterSet "Locks",
// type="section", <Child>); the locks themselves are editor values per part, kept for the session only (the original
// clears them when a project loads and never stores them). Header only.
#pragma once
#include "model/remotedevice.h"
#include <algorithm>
#include <functional>
#include <set>
#include <string>
#include <vector>

namespace vti {

struct SectionLock {
    // the "Presets" submenu of the original's header menu, in its order (0x180084c90)
    enum Preset { LOCK_OSC, LOCK_FILTER, LOCK_LFO, LOCK_MATRIX, LOCK_ARP, LOCK_FX, PRESET_COUNT };
    static constexpr int FLAGS_BYTE = 10;    // single dump: Patchmanagement/Flags
    static constexpr uint8_t MERGED = 0x20;  // the original's flags value for a merged dump

    static const char *preset_name(int p)
    {
        static const char *names[] = {"Lock OSC", "Lock Filter", "Lock LFO", "Lock Matrix", "Lock Arpeggiator",
                                      "Lock FX"};
        return p >= 0 && p < PRESET_COUNT ? names[p] : "";
    }

    // the section parameters ("Locks/Oscillator 1", ...) in file order
    static std::vector<std::string> sections(const RemoteDevice &ed)
    {
        std::vector<std::string> out;
        for (auto &p : ed.parameters)
            if (p.type == "section")
                out.push_back(p.path);
        return out;
    }

    // a preset locks the sections of its page, which the page tab's "Locks/Page X Lockings" condition lists (the
    // original's fixed lists are the same, E378); "Lock FX" covers both FX pages
    static std::vector<std::string> preset_sections(const RemoteDevice &ed, int p)
    {
        static const std::vector<std::vector<const char *>> pages = {
            {"Locks/Page OSC Lockings"},    {"Locks/Page Filter Lockings"},      {"Locks/Page LFO Lockings"},
            {"Locks/Page Matrix Lockings"}, {"Locks/Page Arpeggiator Lockings"},
            {"Locks/Page FX Lockings", "Locks/Page FX 2 Lockings"}};
        std::vector<std::string> out;
        if (p < 0 || p >= PRESET_COUNT)
            return out;
        for (const char *name : pages[(size_t)p]) {
            auto range = ed.conditions.equal_range(name);
            for (auto it = range.first; it != range.second; ++it)
                for (auto &t : it->second.tests)
                    if (std::find(out.begin(), out.end(), t.param) == out.end())
                        out.push_back(t.param);
        }
        return out;
    }

    // "Lock all Sections" (1) / "Unlock all Sections" (0) for one part: every section with that value
    static std::vector<std::pair<std::string, int>> all_values(const RemoteDevice &ed, int v)
    {
        std::vector<std::pair<std::string, int>> out;
        for (auto &s : sections(ed))
            out.push_back({s, v});
        return out;
    }

    // a preset replaces the part's locks (E378: the original unlocks all, then locks the page's sections)
    static std::vector<std::pair<std::string, int>> preset_values(const RemoteDevice &ed, int p)
    {
        if (p < 0 || p >= PRESET_COUNT)
            return {};
        const auto on = preset_sections(ed, p);
        auto out = all_values(ed, 0);
        for (auto &kv : out)
            kv.second = std::find(on.begin(), on.end(), kv.first) != on.end() ? 1 : 0;
        return out;
    }

    // the parameters the locked sections keep (each once, in section order)
    static std::vector<std::string> locked_children(const RemoteDevice &ed,
                                                    const std::function<bool(const std::string &)> &locked)
    {
        std::vector<std::string> out;
        std::set<std::string> seen;
        for (auto &p : ed.parameters)
            if (p.type == "section" && locked(p.path))
                for (auto &c : p.children)
                    if (seen.insert(c).second)
                        out.push_back(c);
        return out;
    }

    // `incoming` (a single dump for the part) with the `children` fields of `current` (the part's dump now): the
    // template's bytes of each child are copied (masked bits only, so a shared byte keeps the other parameter's
    // bits), then flags = MERGED and the checksums are recomputed. False and `incoming` unchanged when no child is a
    // field of the dump or the sizes do not fit.
    static bool merge(const RemoteDevice &dev, const MidiMessage &tpl, const std::vector<uint8_t> &current,
                      std::vector<uint8_t> &incoming, const std::vector<std::string> &children)
    {
        const size_t n = RemoteDevice::dump_size(tpl);
        if (current.size() != n || incoming.size() != n || n <= (size_t)FLAGS_BYTE)
            return false;
        std::vector<uint8_t> out = incoming;
        bool any = false;
        for (auto &c : children) {
            auto f = tpl.field_bytes.find(c);
            if (f == tpl.field_bytes.end())
                continue;
            for (size_t i : f->second) {
                const MessageByte &b = tpl.bytes[i];
                const int mask = b.mask >= 0 ? b.mask : 0x7F;
                out[(size_t)b.index] = (uint8_t)((out[(size_t)b.index] & ~mask) | (current[(size_t)b.index] & mask));
                any = true;
            }
        }
        if (!any)
            return false;
        out[FLAGS_BYTE] = MERGED;
        dev.update_dump(tpl, out, {}); // no values: only the checksums
        incoming = out;
        return true;
    }
};

} // namespace vti
