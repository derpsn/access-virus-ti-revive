// editgestures.h - edits made in the editor as host gestures (audit 2026-11-17 #10). A knob drag sent beginEdit,
// performEdit and endEdit for every changed value (up to 128 gestures per drag), so a host in Touch mode resumed
// reading its lane between the steps and the knob jumped. Like the original (E304: beginEdit on mouse down, endEdit on
// mouse up): between drag_begin() and drag_end() a parameter's gesture begins at its first change and stays open;
// an edit outside a drag (menu, reset, undo) is one whole gesture. The editor thread only.
#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

namespace vti {

class EditGestures {
public:
    void drag_begin() { dragging_ = true; }
    // begin(id), perform(id, value), end(id): the host calls (beginEdit, performEdit, endEdit)
    template <class B, class P, class E> void edit(uint32_t id, double value, B &&begin, P &&perform, E &&end)
    {
        const bool open = std::find(open_.begin(), open_.end(), id) != open_.end();
        if (!open)
            begin(id);
        perform(id, value);
        if (dragging_ && !open)
            open_.push_back(id);
        else if (!dragging_)
            end(id);
    }
    // mouse up or the editor closing: every gesture the drag began ends, in the order they began
    template <class E> void drag_end(E &&end)
    {
        for (uint32_t id : open_)
            end(id);
        open_.clear();
        dragging_ = false;
    }

private:
    bool dragging_ = false;
    std::vector<uint32_t> open_;
};

} // namespace vti
