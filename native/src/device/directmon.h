// directmon.h - the parts section's D button, Direct Monitoring, as in the original (E449; listener 0x180081980): it
// sends no message of its own. On, it remembers the part's two Output Select parameters (Control2 and Control3, with and
// without the third USB output; "pr" names on the Snow) and puts both on Out1 L+R, so the part sounds straight from the
// Virus' analog output; off, they go back. One part at a time: another part taking over gives the first its outputs
// back. A change of those outputs by hand ends it and keeps the user's choice. Not stored with the project, like the
// original. The analog hold for a part below Output Select 9 comes from outputdelay.h, as for any analog part (E344).
// Plain logic, no locks: the plug-in calls it under its own mutex.
#pragma once
#include <vector>

namespace vti {

class DirectMonitor {
public:
    static constexpr int OUT1_LR = 1; // "Out1 L+R" in the Output Select lists
    struct Set {
        int part;
        int which; // 0 = Output Select Control2, 1 = Control3
        int value;
    };

    int part() const { return part_; }

    // `out2`, `out3`: the part's current Output Select values
    std::vector<Set> on(int part, int out2, int out3)
    {
        std::vector<Set> s;
        if (part < 0 || part > 15 || part == part_)
            return s;
        s = off(part_);
        part_ = part, saved_[0] = out2, saved_[1] = out3;
        s.push_back({part, 0, OUT1_LR});
        s.push_back({part, 1, OUT1_LR});
        return s;
    }

    std::vector<Set> off(int part)
    {
        if (part < 0 || part != part_)
            return {};
        part_ = -1;
        return {{part, 0, saved_[0]}, {part, 1, saved_[1]}};
    }

    // an Output Select of `part` changed (the editor, the Virus' panel, a report): true when that ends Direct Monitoring
    bool changed(int part, int which, int value)
    {
        (void)which;
        if (part != part_ || part_ < 0 || value == OUT1_LR)
            return false;
        part_ = -1;
        return true;
    }

private:
    int part_ = -1;
    int saved_[2] = {0, 0};
};

} // namespace vti
