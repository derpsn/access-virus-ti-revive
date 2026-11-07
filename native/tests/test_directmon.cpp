// The parts section's D button (Direct Monitoring), as in the original (E449): the part's two Output Select to Out1 L+R
// and back.
#include "check.h"
#include "device/directmon.h"

#include <vector>

using namespace vti;
using Set = DirectMonitor::Set;

static bool same(const std::vector<Set> &a, const std::vector<Set> &b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); i++)
        if (a[i].part != b[i].part || a[i].which != b[i].which || a[i].value != b[i].value)
            return false;
    return true;
}

TEST(directmon_on_sets_both_outputs_to_out1)
{
    DirectMonitor d;
    CHECK_EQ(d.part(), -1);
    CHECK(same(d.on(2, 10, 12), {{2, 0, DirectMonitor::OUT1_LR}, {2, 1, DirectMonitor::OUT1_LR}}));
    CHECK_EQ(d.part(), 2);
    CHECK(d.on(2, 1, 1).empty()); // already on: nothing again, the saved values stay
}

TEST(directmon_off_gives_the_outputs_back)
{
    DirectMonitor d;
    d.on(2, 10, 12);
    CHECK(d.off(5).empty()); // not this part
    CHECK(same(d.off(2), {{2, 0, 10}, {2, 1, 12}}));
    CHECK_EQ(d.part(), -1);
    CHECK(d.off(2).empty());
}

TEST(directmon_one_part_at_a_time)
{
    // another part takes over: the first one gets its outputs back first
    DirectMonitor d;
    d.on(0, 10, 11);
    CHECK(same(d.on(3, 13, 14), {{0, 0, 10}, {0, 1, 11}, {3, 0, DirectMonitor::OUT1_LR}, {3, 1, DirectMonitor::OUT1_LR}}));
    CHECK_EQ(d.part(), 3);
    CHECK(same(d.off(3), {{3, 0, 13}, {3, 1, 14}}));
}

TEST(directmon_a_change_by_hand_ends_it_without_restoring)
{
    DirectMonitor d;
    d.on(4, 10, 12);
    CHECK(!d.changed(4, 0, DirectMonitor::OUT1_LR)); // our own setting coming back as a report
    CHECK(!d.changed(1, 0, 9));                       // another part
    CHECK_EQ(d.part(), 4);
    CHECK(d.changed(4, 1, 15)); // the user picked another output: D goes off
    CHECK_EQ(d.part(), -1);
    CHECK(d.off(4).empty()); // nothing left to give back: the user's choice stands
}

TEST(directmon_out_of_range_part_is_ignored)
{
    DirectMonitor d;
    CHECK(d.on(-1, 10, 10).empty());
    CHECK(d.on(16, 10, 10).empty());
    CHECK_EQ(d.part(), -1);
}
