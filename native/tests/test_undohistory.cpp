// Undo/Redo of editor edits (model/undohistory.h, TI Revive's own addition; the original has none): parameter
// changes and patch loads per part, a knob drag as one step, redo cleared by a new edit, a bounded history.
// Hardware-free.
#include "check.h"
#include "model/undohistory.h"

using namespace vti;

static std::vector<uint8_t> dump(uint8_t v) { return std::vector<uint8_t>(524, v); }

TEST(undohistory_empty)
{
    UndoHistory h;
    CHECK(!h.can_undo() && !h.can_redo());
    CHECK(h.undo_label().empty() && h.redo_label().empty());
    UndoStep s;
    CHECK(!h.undo(s) && !h.redo(s));
}

TEST(undohistory_param_undo_redo)
{
    UndoHistory h;
    h.param("Filter/Cutoff", 0, 64, 90, "Cutoff (Part 1)", 1000);
    CHECK(h.can_undo() && !h.can_redo());
    CHECK(h.undo_label() == "Cutoff (Part 1)");
    UndoStep s;
    CHECK(h.undo(s));
    CHECK(s.kind == UndoStep::Param && s.changes.size() == 1);
    CHECK(s.changes[0].path == "Filter/Cutoff" && s.changes[0].part == 0 && s.changes[0].before == 64 &&
          s.changes[0].after == 90);
    CHECK(!h.can_undo() && h.can_redo());
    CHECK(h.redo_label() == "Cutoff (Part 1)");
    CHECK(h.redo(s));
    CHECK(s.changes[0].after == 90);
    CHECK(h.can_undo() && !h.can_redo());
}

TEST(undohistory_a_knob_drag_is_one_step)
{
    UndoHistory h;
    for (int v = 65, t = 1000; v <= 80; v++, t += 30)
        h.param("Filter/Cutoff", 0, v - 1, v, "Cutoff (Part 1)", t);
    UndoStep s;
    CHECK(h.undo(s));
    CHECK(s.changes.size() == 1 && s.changes[0].before == 64 && s.changes[0].after == 80);
    CHECK(!h.can_undo());
}

TEST(undohistory_pause_or_other_control_starts_a_new_step)
{
    UndoHistory h;
    h.param("Filter/Cutoff", 0, 64, 70, "Cutoff (Part 1)", 1000);
    h.param("Filter/Cutoff", 0, 70, 75, "Cutoff (Part 1)", 1000 + UndoHistory::kMergeMs + 1); // after a pause
    h.param("Filter/Cutoff", 1, 64, 10, "Cutoff (Part 2)", 3000);                           // another part
    h.param("Filter/Resonance", 1, 0, 10, "Resonance (Part 2)", 3010);                      // another control
    int n = 0;
    UndoStep s;
    while (h.undo(s))
        n++;
    CHECK(n == 4);
}

TEST(undohistory_drag_back_to_the_start_leaves_no_step)
{
    UndoHistory h;
    h.param("Filter/Cutoff", 0, 64, 70, "Cutoff (Part 1)", 1000);
    h.param("Filter/Cutoff", 0, 70, 64, "Cutoff (Part 1)", 1100);
    CHECK(!h.can_undo());
    h.param("Filter/Cutoff", 0, 64, 64, "Cutoff (Part 1)", 5000); // no change at all
    CHECK(!h.can_undo());
}

TEST(undohistory_new_edit_clears_redo)
{
    UndoHistory h;
    h.param("A", 0, 0, 1, "A", 1000);
    UndoStep s;
    h.undo(s);
    CHECK(h.can_redo());
    h.param("B", 0, 0, 1, "B", 5000);
    CHECK(!h.can_redo());
}

TEST(undohistory_merge_not_across_an_undo)
{
    UndoHistory h;
    h.param("A", 0, 0, 5, "A", 1000);
    h.param("B", 0, 0, 5, "B", 1010);
    UndoStep s;
    h.undo(s); // B back to 0
    h.param("A", 0, 5, 9, "A", 1020); // within the merge time of A's step, but A is not the last edit any more
    CHECK(h.undo(s) && s.changes[0].path == "A" && s.changes[0].before == 5 && s.changes[0].after == 9);
}

TEST(undohistory_patch_steps)
{
    UndoHistory h;
    h.patch(2, dump(1), dump(2), "Load watercop (Part 3)", 1000);
    h.patch(2, dump(2), dump(3), "Load vintage (Part 3)", 1010); // patch loads never merge
    UndoStep s;
    CHECK(h.undo_label() == "Load vintage (Part 3)");
    CHECK(h.undo(s) && s.kind == UndoStep::Patch && s.part == 2 && s.dump_before == dump(2) && s.dump_after == dump(3));
    CHECK(h.undo(s) && s.dump_before == dump(1));
    CHECK(!h.can_undo());
    h.patch(0, dump(4), dump(4), "Load same (Part 1)", 2000); // the same patch again: nothing to undo
    CHECK(!h.can_undo());
}

TEST(undohistory_bounded)
{
    UndoHistory h;
    for (int i = 0; i < UndoHistory::kMaxSteps + 50; i++)
        h.param("P" + std::to_string(i), 0, 0, 1, "x", i * 10000);
    int n = 0;
    UndoStep s;
    while (h.undo(s))
        n++;
    CHECK(n == UndoHistory::kMaxSteps);
    CHECK(s.changes[0].path == "P50"); // the oldest ones went first
}

TEST(undohistory_clear)
{
    UndoHistory h;
    h.param("A", 0, 0, 1, "A", 0);
    UndoStep s;
    h.param("B", 0, 0, 1, "B", 10000);
    h.undo(s);
    h.clear();
    CHECK(!h.can_undo() && !h.can_redo());
}

// an action that sets many parameters at once (the ARP page's pattern shift, the randomizer) is one step
TEST(undohistory_group_is_one_step)
{
    UndoHistory h;
    h.group_begin("Shift Pattern Left (Part 1)");
    h.param("Arp/Step 1", 0, 1, 0, "x", 1000);
    h.param("Arp/Step 2", 0, 0, 1, "x", 1000);
    h.param("Arp/Step 1", 0, 0, 2, "x", 1000); // the same control twice: its first before, its last after
    h.group_end(1000);
    CHECK(h.undo_label() == "Shift Pattern Left (Part 1)");
    UndoStep s;
    CHECK(h.undo(s) && s.changes.size() == 2);
    CHECK(s.changes[0].path == "Arp/Step 1" && s.changes[0].before == 1 && s.changes[0].after == 2);
    CHECK(s.changes[1].path == "Arp/Step 2" && s.changes[1].before == 0 && s.changes[1].after == 1);
    CHECK(!h.can_undo());
    // an edit right after a group does not merge into it
    h.group_begin("g");
    h.param("A", 0, 0, 1, "x", 2000);
    h.group_end(2000);
    h.param("A", 0, 1, 2, "A", 2010);
    int n = 0;
    while (h.undo(s))
        n++;
    CHECK(n == 2);
}

TEST(undohistory_group_without_change_leaves_no_step)
{
    UndoHistory h;
    h.group_begin("Reset (Part 1)");
    h.param("A", 0, 3, 3, "x", 1000);
    h.group_end(1000);
    CHECK(!h.can_undo());
    h.group_begin("nothing");
    h.group_end(1000);
    CHECK(!h.can_undo());
}

// setting a control to the value it has (a click on the current menu entry) changes nothing: Redo stays (review)
TEST(undohistory_no_change_keeps_redo)
{
    UndoHistory h;
    h.param("A", 0, 0, 1, "A", 1000);
    UndoStep s;
    h.undo(s);
    h.param("B", 0, 5, 5, "B", 5000);
    CHECK(h.can_redo());
    h.patch(0, dump(1), dump(1), "same", 6000);
    CHECK(h.can_redo());
}
