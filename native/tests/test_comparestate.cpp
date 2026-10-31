// Compare (model/comparestate.h, TI Revive's own addition): a part switches between its edited sound and the patch
// as it was loaded (its reference), and back; an edit or a load while comparing ends it, keeping the edit for an
// undo step. Hardware-free.
#include "check.h"
#include "model/comparestate.h"

using namespace vti;
using Patch = CompareState::Patch;

static Patch single(uint8_t sound)
{
    Patch d(524, 0);
    d[0] = 0xF0, d[1] = 0x00, d[2] = 0x20, d[3] = 0x33, d[4] = 0x01, d[5] = 0x10, d[6] = 0x10, d[523] = 0xF7;
    d[9] = 0x0C;
    for (int i = 11; i < 522; i++)
        d[i] = (uint8_t)((sound + i) & 0x7F);
    return d;
}

TEST(comparestate_nothing_to_compare_without_reference_or_edit)
{
    CompareState c;
    Patch out;
    CHECK(!c.start(0, single(1), out)); // no reference yet
    c.set_reference(0, single(1));
    CHECK(!c.start(0, single(1), out)); // not edited: the same sound
    CHECK(!c.active(0));
}

TEST(comparestate_start_and_back)
{
    CompareState c;
    c.set_reference(3, single(1));
    Patch to_load;
    CHECK(c.start(3, single(2), to_load));
    CHECK(c.active(3) && !c.active(2));
    CHECK(to_load == single(1)); // the reference goes into the part
    Patch edited;
    CHECK(c.stop(3, edited));
    CHECK(edited == single(2)); // the edit comes back
    CHECK(!c.active(3));
    CHECK(!c.stop(3, edited)); // not comparing any more
}

TEST(comparestate_edit_while_comparing_ends_it)
{
    CompareState c;
    c.set_reference(0, single(1));
    Patch p;
    c.start(0, single(2), p);
    Patch kept;
    CHECK(c.interrupted(0, kept)); // an edit now: compare ends, the edit is handed back for the undo history
    CHECK(kept == single(2) && !c.active(0));
    CHECK(!c.interrupted(0, kept)); // only once
}

TEST(comparestate_reference_follows_loads_but_not_while_comparing)
{
    CompareState c;
    c.set_reference(0, single(1));
    Patch p;
    c.start(0, single(2), p);
    c.set_reference(0, single(1)); // the compare's own load must not change it ...
    CHECK(c.reference(0) == single(1));
    c.stop(0, p);
    c.set_reference(0, single(5)); // ... a real load does
    CHECK(c.reference(0) == single(5));
    CHECK(c.reference(7).empty());
}

TEST(comparestate_clear)
{
    CompareState c;
    c.set_reference(0, single(1));
    Patch p;
    c.start(0, single(2), p);
    c.clear();
    CHECK(!c.active(0) && c.reference(0).empty());
}

// a project saved while a part compares keeps the edit, not the reference that is playing
TEST(comparestate_edited_for_the_project)
{
    CompareState c;
    c.set_reference(1, single(1));
    Patch e;
    CHECK(!c.edited(1, e));
    Patch p;
    c.start(1, single(2), p);
    CHECK(c.edited(1, e) && e == single(2));
}
