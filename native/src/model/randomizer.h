// randomizer.h - the patch randomizer (TI Revive's own addition, asked for in #19; the original has none). The
// parameters of a part's sections (the section lock's sections, E378) that are not locked move by up to `amount` of
// their range; a list parameter (a menu) picks another entry with that chance. Locked sections stay - lock what you
// want to keep - and so do parameters outside the single dump (editor metas), parameters in no section (Transpose,
// Key Mode) and what would silence the patch or start it playing: the patch volume, the input and vocoder sections
// (the part could listen to the audio input) and the arpeggiator (the ARP page has its own random actions). The
// caller writes the values into the part's single dump: one dump to the Virus, like a patch load. Header only.
#pragma once
#include "model/remotedevice.h"
#include <cctype>
#include <cmath>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace vti {

struct Randomizer {
    struct Target {
        std::string path;
        int min = 0, max = 127;
        std::vector<int> choices; // a list parameter's values; empty = a number range
    };
    struct Amount {
        const char *name;
        int percent;
    };
    static constexpr Amount kAmounts[] = {{"Subtle", 10}, {"Medium", 25}, {"Strong", 50}};

    // sections and parameters that stay (see above)
    static bool kept_section(const std::string &s)
    {
        return s == "Locks/FX Input" || s == "Locks/FX Vocoder" || s.rfind("Locks/Arpeggiator", 0) == 0;
    }
    static bool kept_param(const std::string &p) { return p == "Patch Common/Volume"; }

    // a value text that is a number ("12", "+63", "-64", "50 %"): a list of only such texts only displays a range
    static bool number_text(const std::string &t)
    {
        size_t i = 0;
        while (i < t.size() && t[i] == ' ')
            i++;
        if (i < t.size() && (t[i] == '+' || t[i] == '-'))
            i++;
        const size_t digits = i;
        while (i < t.size() && (std::isdigit((unsigned char)t[i]) || t[i] == '.'))
            i++;
        if (i == digits)
            return false;
        while (i < t.size() && (t[i] == ' ' || t[i] == '%'))
            i++;
        return i == t.size();
    }

    // the parameters to randomize: the children of the sections of `ed` that are not locked (and not kept), each
    // once, that `dev` describes and `in_dump` says are fields of the single dump; a child of a locked section stays
    // even when another section lists it too
    static std::vector<Target> targets(const RemoteDevice &ed, const RemoteDevice &dev,
                                       const std::function<bool(const std::string &)> &locked,
                                       const std::function<bool(const std::string &)> &in_dump)
    {
        std::set<std::string> held, seen;
        for (auto &s : ed.parameters)
            if (s.type == "section" && (locked(s.path) || kept_section(s.path)))
                held.insert(s.children.begin(), s.children.end());
        std::vector<Target> out;
        for (auto &s : ed.parameters) {
            if (s.type != "section" || locked(s.path) || kept_section(s.path))
                continue;
            for (auto &c : s.children) {
                if (held.count(c) || kept_param(c) || !in_dump(c) || !seen.insert(c).second)
                    continue;
                const Parameter *p = dev.find(c);
                if (!p || p->max <= p->min)
                    continue;
                Target t;
                t.path = c, t.min = p->min, t.max = p->max;
                if (auto vl = dev.valuelists.find(p->valuelist); !p->valuelist.empty() && vl != dev.valuelists.end()) {
                    bool menu = false; // names (filter modes, wavetables); numbers only display the range
                    for (auto &e : vl->second.entries)
                        menu = menu || !number_text(e.text);
                    for (auto &e : vl->second.entries)
                        if (int v = vl->second.value_of(e); menu && v >= p->min && v <= p->max)
                            t.choices.push_back(v);
                }
                out.push_back(t);
            }
        }
        return out;
    }

    // the new values (`current` keys = target paths; targets without a current value are left out); `amount` 0..1,
    // `rnd` uniform in [0, 1)
    static std::map<std::string, int> values(const std::vector<Target> &targets, const std::map<std::string, int> &current,
                                             double amount, const std::function<double()> &rnd)
    {
        std::map<std::string, int> out;
        for (auto &t : targets) {
            auto it = current.find(t.path);
            if (it == current.end())
                continue;
            int v = it->second;
            if (!t.choices.empty()) {
                if (rnd() < amount)
                    v = t.choices[std::min(t.choices.size() - 1, (size_t)(rnd() * (double)t.choices.size()))];
            } else {
                const double span = (double)(t.max - t.min) * amount;
                v = (int)std::lround((double)v + (2.0 * rnd() - 1.0) * span);
            }
            out[t.path] = std::max(t.min, std::min(t.max, v));
        }
        return out;
    }
};

} // namespace vti
