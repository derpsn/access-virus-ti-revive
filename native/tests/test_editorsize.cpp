// test_editorsize.cpp - the editor's free size (editor/editorsize.h): proportions, limits, the edge that moved
#include "check.h"
#include "editor/editorsize.h"
#include <cmath>

using namespace vti;

static const int LW = 907, LH = 513; // the original layout

TEST(editorsize_keeps_proportions)
{
    // a corner dragged to 1200 x 900 at 100 %: the width moved more, the height follows the layout's proportions
    auto s = fit_editor_size(1200, 600, LW, LH, 1.0, 1.0);
    CHECK(std::fabs(s.zoom - 1200.0 / LW) < 1e-9);
    CHECK(s.w == 1200);
    CHECK(s.h == (int)std::lround(LH * 1200.0 / LW));
}

TEST(editorsize_bottom_edge_only)
{
    // only the bottom edge dragged (the width stays): the height decides
    auto s = fit_editor_size(LW, 1026, LW, LH, 1.0, 1.0);
    CHECK(std::fabs(s.zoom - 2.0) < 1e-9);
    CHECK(s.w == 2 * LW && s.h == 1026);
}

TEST(editorsize_limits)
{
    auto small = fit_editor_size(100, 50, LW, LH, 1.0, 1.0);
    CHECK(small.zoom == kMinZoom);
    CHECK(small.w == (int)std::lround(LW * kMinZoom));
    auto big = fit_editor_size(10000, 6000, LW, LH, 1.0, 1.0);
    CHECK(big.zoom == kMaxZoom);
    CHECK(big.h == (int)std::lround(LH * kMaxZoom));
}

TEST(editorsize_display_scale)
{
    // at a 150 % display the host counts physical pixels: 1361 px wide is the user's 100 %
    auto s = fit_editor_size(1361, 770, LW, LH, 1.5, 1.0);
    CHECK(std::fabs(s.zoom - 1.0) < 0.01);
    CHECK(s.w == 1361 && s.h == 770);
    CHECK(std::fabs(zoom_for_size(1361, 770, LW, LH, 1.5) - 1.0) < 0.01);
}

TEST(editorsize_unchecked_host_size)
{
    // a host that skips checkSizeConstraint: the largest size that fits, never outside the limits
    CHECK(std::fabs(zoom_for_size(1814, 2000, LW, LH, 1.0) - 2.0) < 1e-9);
    CHECK(zoom_for_size(0, 0, LW, LH, 1.0) == 1.0);
    CHECK(zoom_for_size(20000, 20000, LW, LH, 1.0) == kMaxZoom);
    CHECK(clamp_zoom(NAN) == 1.0);
}
