// Editor edits as host gestures (audit 2026-11-17 #10): a knob drag sent begin + perform + end for every changed
// value, up to 128 gestures per drag, so a host in Touch mode read its lane back between the steps. Like the original
// (E304: beginEdit on mouse down, endEdit on mouse up): one gesture per drag; an edit outside a drag stays one whole
// gesture; mouse up (or the editor closing) ends what the drag began.
#include "check.h"
#include "model/editgestures.h"
#include <string>
#include <vector>
using namespace vti;

namespace {
struct Log {
    std::vector<std::string> calls;
    void edit(EditGestures &g, uint32_t id, int v)
    {
        g.edit(
            id, v / 127.0, [&](uint32_t i) { calls.push_back("begin " + std::to_string(i)); },
            [&](uint32_t i, double x) { calls.push_back("perform " + std::to_string(i) + " " + std::to_string((int)(x * 127 + 0.5))); },
            [&](uint32_t i) { calls.push_back("end " + std::to_string(i)); });
    }
    void up(EditGestures &g)
    {
        g.drag_end([&](uint32_t i) { calls.push_back("end " + std::to_string(i)); });
    }
};
} // namespace

TEST(editgestures_one_gesture_per_drag)
{
    EditGestures g;
    Log l;
    g.drag_begin();
    l.edit(g, 7, 10);
    l.edit(g, 7, 11);
    l.edit(g, 7, 12);
    CHECK((l.calls == std::vector<std::string>{"begin 7", "perform 7 10", "perform 7 11", "perform 7 12"}));
    l.up(g);
    CHECK(l.calls.back() == "end 7");
    l.up(g); // a second mouse up: nothing open, nothing ends twice
    CHECK(l.calls.size() == 5);
}

TEST(editgestures_a_drag_over_two_parameters_ends_both)
{
    EditGestures g;
    Log l;
    g.drag_begin(); // e.g. an envelope graph handle: attack and decay at once
    l.edit(g, 3, 1);
    l.edit(g, 9, 2);
    l.edit(g, 3, 4);
    l.up(g);
    CHECK((l.calls == std::vector<std::string>{"begin 3", "perform 3 1", "begin 9", "perform 9 2", "perform 3 4", "end 3",
                                               "end 9"}));
}

TEST(editgestures_outside_a_drag_each_edit_is_whole)
{
    EditGestures g;
    Log l;
    l.edit(g, 5, 64); // a menu choice, a reset by double-click, undo
    CHECK((l.calls == std::vector<std::string>{"begin 5", "perform 5 64", "end 5"}));
    g.drag_begin();
    l.up(g); // a click without a change: no gesture at all
    CHECK(l.calls.size() == 3);
    l.edit(g, 5, 65); // after the mouse up: whole again
    CHECK(l.calls.size() == 6 && l.calls.back() == "end 5");
}
