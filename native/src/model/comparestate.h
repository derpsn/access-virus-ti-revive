// comparestate.h - Compare (TI Revive's own addition; the Virus has a compare on its panel, the original plug-in
// none): a part switches between its edited sound and its reference - the patch as it was last loaded, or as the
// project brought it - and back. While a part compares, its reference stays (the compare's own load is no new
// patch); an edit or a load ends the compare, and the edit is handed back so the caller can keep it as an undo step.
// Patches are 524-byte single dumps. Header only. Not thread safe: the caller locks.
#pragma once
#include "model/rambackup.h"
#include <map>
#include <vector>

namespace vti {

class CompareState {
public:
    using Patch = std::vector<uint8_t>;

    // a patch arrived in the part (a load, the project): the new reference - not while the part compares
    void set_reference(int part, const Patch &d)
    {
        if (!active(part))
            ref_[part] = d;
    }
    Patch reference(int part) const
    {
        auto it = ref_.find(part);
        return it == ref_.end() ? Patch() : it->second;
    }
    bool active(int part) const { return edited_.count(part) != 0; }
    // the edited sound of a comparing part (empty when it does not compare): what the project keeps
    Patch edited(int part) const
    {
        auto it = edited_.find(part);
        return it == edited_.end() ? Patch() : it->second;
    }

    // compare on: `edited` = the part's sound now; `to_load` = the reference to put into the part. False when there
    // is nothing to compare (no reference, or the sound is the reference)
    bool start(int part, const Patch &edited, Patch &to_load)
    {
        auto it = ref_.find(part);
        if (active(part) || it == ref_.end() || RamBackup::same_sound(it->second, edited))
            return false;
        edited_[part] = edited;
        to_load = it->second;
        return true;
    }
    // compare off: `edited` = the sound to put back into the part
    bool stop(int part, Patch &edited) { return take(part, edited); }
    // an edit or a load while the part compares: the compare ends (the part keeps the reference with the new edit);
    // `edited` = the sound it had, for an undo step
    bool interrupted(int part, Patch &edited) { return take(part, edited); }

    void clear() { ref_.clear(), edited_.clear(); }

private:
    std::map<int, Patch> ref_, edited_;

    bool take(int part, Patch &edited)
    {
        auto it = edited_.find(part);
        if (it == edited_.end())
            return false;
        edited = std::move(it->second);
        edited_.erase(it);
        return true;
    }
};

} // namespace vti
