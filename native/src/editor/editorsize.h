// editorsize.h - the editor's free size: the window can be dragged to any size between kMinZoom and kMaxZoom of the
// original layout, always in the layout's proportions (the skin is one fixed picture: a wider window would only add
// empty space). Header only, no VSTGUI: viruseditor.cpp and the tests.
#pragma once
#include <algorithm>
#include <cmath>

namespace vti {

// the user's size (on top of the display scale): 50 % for a small screen, 300 % for a 4K screen at 100 % scaling;
// the size menu's steps lie in between
constexpr double kMinZoom = 0.5, kMaxZoom = 3.0;

inline double clamp_zoom(double z)
{
    return std::isfinite(z) ? std::min(kMaxZoom, std::max(kMinZoom, z)) : 1.0;
}

struct EditorSize {
    int w = 0, h = 0;
    double zoom = 1; // the user's size (without the display scale)
};

// the size for a window the user drags to `want_w` x `want_h` (pixels as the host counts them): the layout
// (`layout_w` x `layout_h`) times `display` (the host's display scale) times a user size in [kMinZoom, kMaxZoom].
// The edge the user moved decides: the one whose size is farther from the current one (`current`), so dragging only
// the bottom edge changes the size too; a corner follows the larger change.
inline EditorSize fit_editor_size(int want_w, int want_h, int layout_w, int layout_h, double display, double current)
{
    EditorSize s;
    if (layout_w <= 0 || layout_h <= 0)
        return s;
    if (!(display > 0))
        display = 1;
    const double bw = layout_w * display, bh = layout_h * display;
    const double zw = want_w / bw, zh = want_h / bh;
    s.zoom = clamp_zoom(std::fabs(zw - current) >= std::fabs(zh - current) ? zw : zh);
    s.w = (int)std::lround(bw * s.zoom);
    s.h = (int)std::lround(bh * s.zoom);
    return s;
}

// the user size a host's onSize means, if it did not ask checkSizeConstraint first: the largest that fits
inline double zoom_for_size(int w, int h, int layout_w, int layout_h, double display)
{
    if (layout_w <= 0 || layout_h <= 0 || w <= 0 || h <= 0)
        return 1.0;
    if (!(display > 0))
        display = 1;
    return clamp_zoom(std::min(w / (layout_w * display), h / (layout_h * display)));
}

} // namespace vti
