// undohistory.h - Undo/Redo of editor edits (TI Revive's own addition; the original has none): parameter changes
// (old and new value per part) or a patch load into a part (the part's dump before and after). The changes of one
// control in a row merge into one step while they come less than kMergeMs apart - a knob drag is one step - and a
// step that ends where it started is dropped. An action that sets many parameters at once (the ARP page's pattern
// actions, the randomizer) is one step: group_begin .. group_end. A new edit clears the redo steps; the oldest steps
// go beyond kMaxSteps. Session only, like "Revert to" (not in the project). Header only. Not thread safe: the caller
// locks.
#pragma once
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace vti {

struct UndoStep {
    enum Kind { Param, Patch } kind = Param;
    struct Change {
        std::string path; // the device parameter
        int part = 0;     // -1: a parameter of no part
        int before = 0, after = 0;
    };
    std::vector<Change> changes;                  // Param: undo takes them back last first, redo in order
    int part = 0;                                 // Patch: the part
    std::vector<uint8_t> dump_before, dump_after; // Patch: the part's single dump
    std::string label;                            // for the menu: "Cutoff (Part 1)"
    int64_t t_ms = 0;                             // the last change merged into it
};

class UndoHistory {
public:
    static constexpr int kMaxSteps = 200;
    static constexpr int64_t kMergeMs = 1000;

    void param(const std::string &path, int part, int before, int after, const std::string &label, int64_t now_ms)
    {
        redo_.clear();
        if (grouping_) {
            for (auto &c : group_.changes)
                if (c.path == path && c.part == part) {
                    c.after = after;
                    return;
                }
            group_.changes.push_back({path, part, before, after});
            return;
        }
        if (!undo_.empty() && can_merge_ && undo_.back().kind == UndoStep::Param && undo_.back().changes.size() == 1 &&
            undo_.back().changes[0].path == path && undo_.back().changes[0].part == part &&
            now_ms - undo_.back().t_ms < kMergeMs) {
            UndoStep &s = undo_.back();
            s.changes[0].after = after, s.t_ms = now_ms;
            if (s.changes[0].after == s.changes[0].before) { // dragged back to where it started
                undo_.pop_back();
                can_merge_ = false;
            }
            return;
        }
        if (before == after)
            return;
        UndoStep s;
        s.kind = UndoStep::Param, s.changes.push_back({path, part, before, after}), s.label = label, s.t_ms = now_ms;
        push(std::move(s));
    }

    // the param() calls up to group_end are one step with this label
    void group_begin(const std::string &label)
    {
        grouping_ = true;
        group_ = UndoStep();
        group_.kind = UndoStep::Param, group_.label = label;
    }
    void group_end(int64_t now_ms)
    {
        if (!grouping_)
            return;
        grouping_ = false;
        UndoStep s = std::move(group_);
        std::vector<UndoStep::Change> changed;
        for (auto &c : s.changes)
            if (c.before != c.after)
                changed.push_back(c);
        if (changed.empty())
            return;
        s.changes = std::move(changed), s.t_ms = now_ms;
        push(std::move(s));
        can_merge_ = false;
    }

    void patch(int part, std::vector<uint8_t> before, std::vector<uint8_t> after, const std::string &label, int64_t now_ms)
    {
        redo_.clear();
        if (before == after)
            return;
        UndoStep s;
        s.kind = UndoStep::Patch, s.part = part, s.dump_before = std::move(before), s.dump_after = std::move(after);
        s.label = label, s.t_ms = now_ms;
        push(std::move(s));
        can_merge_ = false;
    }

    bool can_undo() const { return !undo_.empty(); }
    bool can_redo() const { return !redo_.empty(); }
    std::string undo_label() const { return undo_.empty() ? std::string() : undo_.back().label; }
    std::string redo_label() const { return redo_.empty() ? std::string() : redo_.back().label; }

    // the step to take back (apply its "before"); false when there is none
    bool undo(UndoStep &out)
    {
        if (undo_.empty())
            return false;
        out = undo_.back();
        redo_.push_back(std::move(undo_.back()));
        undo_.pop_back();
        can_merge_ = false;
        return true;
    }
    // the step to do again (apply its "after"); false when there is none
    bool redo(UndoStep &out)
    {
        if (redo_.empty())
            return false;
        out = redo_.back();
        undo_.push_back(std::move(redo_.back()));
        redo_.pop_back();
        can_merge_ = false;
        return true;
    }
    void clear()
    {
        undo_.clear(), redo_.clear();
        grouping_ = can_merge_ = false;
    }

private:
    std::deque<UndoStep> undo_, redo_;
    bool can_merge_ = false; // the last step is a fresh edit (not one brought back by redo, not before an undo)
    bool grouping_ = false;
    UndoStep group_;

    void push(UndoStep s)
    {
        undo_.push_back(std::move(s));
        if ((int)undo_.size() > kMaxSteps)
            undo_.pop_front();
        can_merge_ = true;
    }
};

} // namespace vti
