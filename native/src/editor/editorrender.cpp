// editorrender.cpp - see editorrender.h.
#include "editorrender.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace vti {

static int frame_of(int v, int lo, int hi, int frames)
{
    if (hi <= lo || frames < 2)
        return 0;
    int f = (int)std::lround((double)(v - lo) / (hi - lo) * (frames - 1));
    return std::max(0, std::min(frames - 1, f));
}

std::vector<std::pair<int, std::string>> EditorRenderer::menu_entries(const std::string &param) const
{
    std::vector<std::pair<int, std::string>> out; // (value, text) in the list's menu order (index)
    const Parameter *p = cond_.find(param);
    if (!p)
        return out;
    for (const RemoteDevice *d : {&dev_, &ed_}) {
        auto vl = d->valuelists.find(p->valuelist);
        if (vl == d->valuelists.end())
            continue;
        std::vector<const Valuelist::Entry *> es;
        for (auto &e : vl->second.entries)
            es.push_back(&e);
        std::stable_sort(es.begin(), es.end(), [](auto *a, auto *b) { return a->index < b->index; });
        // min/max limit the menu position, not the value: like the original (issue #11), the mod matrix
        // destinations (max 121) list values up to 127 and leave out the "-reserved-" entries at positions 122..127
        for (auto *e : es)
            if (e->index >= p->min && e->index <= p->max && !e->text.empty())
                out.push_back({vl->second.value_of(*e), e->text});
        return out;
    }
    for (int v = p->min; v <= p->max; v++) // no list: plain numbers
        out.push_back({v, std::to_string(v)});
    return out;
}

int EditorRenderer::shown_value(const std::string &param, int v) const
{
    const Parameter *p = cond_.find(param);
    if (!p || p->max < p->min)
        return v;
    for (const RemoteDevice *d : {&dev_, &ed_}) {
        auto vl = d->valuelists.find(p->valuelist);
        if (vl != d->valuelists.end() && vl->second.mapped && vl->second.find(v))
            return v;
    }
    return std::max(p->min, std::min(p->max, v));
}

std::string EditorRenderer::value_text(const std::string &param, int v) const
{
    const Parameter *p = cond_.find(param);
    if (!p)
        return std::to_string(v);
    for (const RemoteDevice *d : {&dev_, &ed_}) {
        auto vl = d->valuelists.find(p->valuelist);
        if (vl != d->valuelists.end()) // an existing entry wins, even with empty text (live displays stay blank)
            if (const Valuelist::Entry *en = vl->second.find(v))
                // "-G" marks an entry the original shows greyed in its menus ("-GUSB3 L+R"): the text without it
                return en->text.rfind("-G", 0) == 0 ? en->text.substr(2) : en->text;
    }
    return std::to_string(v);
}

// bitmap font: vertical strip of 96 glyphs (ASCII 32..127), 6 pixels wide
void EditorRenderer::text(Canvas &c, const std::string &font, const std::string &str, int x, int y, int w, int h,
                          const std::string &align, double alpha)
{
    void *f = c.image(font);
    if (!f || str.empty())
        return;
    const int gw = 6, gh = c.image_height(f) / 96;
    c.frames(f, gh);
    int tw = (int)str.size() * gw;
    int tx = align.find("Left") != std::string::npos    ? x
             : align.find("Right") != std::string::npos ? x + w - tw
                                                        : x + (w - tw) / 2;
    int ty = align.find("Top") != std::string::npos      ? y
             : align.find("Bottom") != std::string::npos ? y + h - gh
                                                         : y + (h - gh - 1) / 2;
    for (size_t i = 0; i < str.size(); i++) {
        int ch = (unsigned char)str[i];
        if (ch < 32 || ch > 127)
            ch = '?';
        // glyph rectangle clipped to the element
        int gx = tx + (int)i * gw, x0 = std::max(gx, x), x1 = std::min(gx + gw, x + w);
        int y0 = std::max(ty, y), y1 = std::min(ty + gh, y + h);
        if (x1 > x0 && y1 > y0)
            c.blit(f, x0 - gx, (ch - 32) * gh + (y0 - ty), x0, y0, x1 - x0, y1 - y0, alpha);
    }
}

void EditorRenderer::image(Canvas &c, const std::string &name, void *img, int sy, int fh, int x, int y, int w, int h,
                           double alpha)
{
    auto t = layout_.tiles.find(name);
    int iw = c.image_width(img);
    c.frames(img, fh);
    if (t == layout_.tiles.end() || (iw == w && fh == h)) {
        c.blit(img, 0, sy, x, y, std::min(w, iw), std::min(h, fh), alpha);
        return;
    }
    const auto &in = t->second;
    // source columns/rows: [0,l) [l, iw-r) [iw-r, iw); destination middle filled by repeating the source middle
    int sx[3] = {0, in.left, iw - in.right}, sw[3] = {in.left, iw - in.left - in.right, in.right};
    int syy[3] = {0, in.top, fh - in.bottom}, sh[3] = {in.top, fh - in.top - in.bottom, in.bottom};
    int dx[3] = {x, x + in.left, x + w - in.right}, dw[3] = {in.left, w - in.left - in.right, in.right};
    int dy[3] = {y, y + in.top, y + h - in.bottom}, dh[3] = {in.top, h - in.top - in.bottom, in.bottom};
    for (int r = 0; r < 3; r++)
        for (int k = 0; k < 3; k++) {
            if (sw[k] <= 0 || sh[r] <= 0 || dw[k] <= 0 || dh[r] <= 0)
                continue;
            for (int oy = 0; oy < dh[r]; oy += sh[r])
                for (int ox = 0; ox < dw[k]; ox += sw[k])
                    c.blit(img, sx[k], sy + syy[r], dx[k] + ox, dy[r] + oy, std::min(sw[k], dw[k] - ox),
                           std::min(sh[r], dh[r] - oy), alpha);
        }
}

// the bank table of the browser page: 8 columns x 16 rows, the names measured on the original. The program numbers
// are not drawn: they are part of the page background (BG_Page_Browser) and show through the translucent table
// bitmap (alpha 102), as in the original (E299)
int EditorRenderer::browser_pane(const std::string &param)
{
    return param.rfind("Browser ", 0) == 0 ? std::max(0, atoi(param.c_str() + 8)) : 0;
}

void EditorRenderer::patch_browser(Canvas &c, const Element &e, int x, int y, int bank, int pane)
{
    const std::string &font = e.get("ControlFont"), &hfont = e.get("SelectedFont");
    auto hl = highlighted.find(bank);
    auto sel = selected.find(pane);
    const bool sel_here = sel != selected.end() && sel->second.first == bank;
    for (int p = 0; p < 128; p++) {
        int nx = x + 21 + (p / 16) * 84, ny = y + 1 + (p % 16) * 11;
        bool on = sel_here && sel->second.second.count(p);
        bool match = !browser_match || browser_match(bank, p);
        if (hl != highlighted.end() && hl->second.count(p) && !hfont.empty()) {
            // a drag's source and target (E298 (L7)): filled in the selection blue, the name in the white font
            c.fill(nx - 2, ny, 63, 11, Rgba{66, 111, 204, 255});
            text(c, hfont, bank_patch(bank, p), nx, ny, 60, 9, "Top Left", 1.0);
            continue;
        }
        if (on && pane == focused_pane) { // the list has the focus: filled, like a drag's entries
            c.fill(nx - 2, ny, 63, 11, Rgba{65, 111, 204, 255});
            text(c, hfont.empty() ? font : hfont, bank_patch(bank, p), nx, ny, 60, 9, "Top Left", 1.0);
            continue;
        }
        text(c, font, bank_patch(bank, p), nx, ny, 60, 9, "Top Left", match ? 1.0 : 0.3);
        if (on) { // without the focus the original frames each selected entry (1 px, measured colour)
            const Rgba fc{65, 111, 204, 255};
            double l = nx - 2.5, t = ny - 0.5, r = nx + 60.5, b = ny + 10.5;
            c.line(l, t, r, t, fc), c.line(r, t, r, b, fc), c.line(r, b, l, b, fc), c.line(l, b, l, t, fc);
        }
    }
}

// REMOTE page template list (the original's KRemoteTemplateTable, E193): 32 templates in 4 columns x 8 rows of
// 114 x 11 px; the number right-aligned in the darker column, the name after it; the selected one in SelectedFont
// (none while the selection is -1, the original's start)
void EditorRenderer::remote_browser(Canvas &c, const Element &e, int x, int y, int selected)
{
    const std::string &font = e.get("ControlFont"), &sel = e.get("SelectedFont");
    for (int t = 0; t < 32; t++) {
        int nx = x + (t / 8) * 114 + 18, ny = y + (t % 8) * 11;
        std::string num = std::to_string(t);
        int nw = 6 * (int)num.size();
        text(c, font, num, nx - nw - 2, ny, nw, 9, "Top Left", 1.0); // 2 px before the name (measured)
        text(c, t == selected && !sel.empty() ? sel : font, remote_name(t), nx, ny, 114 - 18, 9, "Top Left", 1.0);
    }
}

// ADSTR envelope display: stage geometry measured on the original (init patch; the original builds its stages in
// the control factory, not yet decompiled): x0 = 4, top = 3, bottom = h - 4 inside the element, 0.47 px per value
// unit for attack/decay/release, a hold segment of 62 px at sustain 127 .. 94 px at sustain 0, a ~29 px sustain-time
// segment (flat at 64), gradient fill under the curve, line in ForeColor, 7x7 handles, a marker at the slope end.
EditorRenderer::EnvGeom EditorRenderer::env_geom(const Element &e, int x, int y, int part) const
{
    const std::string &first = e.get("Parameter"); // "<Envelope>/Attack"
    std::string g = first.substr(0, first.rfind('/') + 1);
    int A = values(g + "Attack", part), D = values(g + "Decay", part), S = values(g + "Sustain", part);
    int T = values(g + "Sustain Time", part), R = values(g + "Release", part);
    EnvGeom q;
    const double k = 0.47;
    q.top = y + 3, q.bottom = y + e.h - 4, q.h = q.bottom - q.top;
    q.x0 = x + 4, q.x1 = q.x0 + A * k, q.x2 = q.x1 + 62 * std::pow(D / 127.0, 1.07);
    q.x3 = q.x2 + 62 + (127 - S) * 0.252, q.x4 = q.x3 + 29, q.x5 = q.x4 + R * 0.457;
    q.ys = q.bottom - S / 127.0 * q.h;
    q.yt = std::max(q.top, std::min(q.bottom, q.ys - (T - 64) / 63.0 * q.h * 0.5)); // slope: up for T > 64
    return q;
}

void EditorRenderer::envelope(Canvas &c, const Element &e, int x, int y, int part, double alpha)
{
    const EnvGeom q = env_geom(e, x, y, part);
    const double top = q.top, bottom = q.bottom, h = q.h, x0 = q.x0, x1 = q.x1, x2 = q.x2, x3 = q.x3, x4 = q.x4,
                 x5 = q.x5, ys = q.ys, yt = q.yt;
    // curve y for every column x0..x5
    auto curve = [&](double px) {
        if (px <= x1)
            return x1 > x0 ? bottom - (px - x0) / (x1 - x0) * h : top;
        if (px <= x2) {
            double t = (px - x1) / std::max(1.0, x2 - x1);
            return ys + (top - ys) * std::pow(1 - t, 3.0);
        }
        if (px <= x3)
            return ys;
        if (px <= x4)
            return ys + (yt - ys) * (px - x3) / (x4 - x3);
        double t = (px - x4) / std::max(1.0, x5 - x4);
        return bottom + (yt - bottom) * std::pow(1 - t, 3.0);
    };
    int cx0 = (int)std::floor(x0), cx1 = (int)std::ceil(x5);
    // gradient fill under the curve, row by row
    for (int row = (int)top; row < (int)bottom; row++) {
        int a = -1, b = -1;
        for (int px = cx0; px <= cx1; px++)
            if (curve(px) <= row) {
                if (a < 0)
                    a = px;
                b = px;
            }
        if (a < 0)
            continue;
        double f = (row - top) / h;
        Rgba col{(uint8_t)(133 + (38 - 133) * f), (uint8_t)(147 + (42 - 147) * f), (uint8_t)(167 + (48 - 167) * f),
                 (uint8_t)(255 * alpha)};
        c.fill(a, row, b - a + 1, 1, col);
    }
    Rgba fc = e.fore;
    fc.a = (uint8_t)(255 * alpha);
    double py = bottom;
    for (int px = cx0; px <= cx1; px++) {
        double cy = curve(px);
        c.line(px - 1 + 0.5, py + 0.5, px + 0.5, cy + 0.5, fc);
        py = cy;
    }
    c.line(x4 + 0.5, top + 0.5, x4 + 0.5, bottom + 0.5, fc); // slope end marker
    if (void *hd = c.image(e.get("NormalHandle"))) {
        int hw = c.image_width(hd), hh = c.image_height(hd);
        double pts[5][2] = {{x1, top}, {x2, ys}, {x3, ys}, {x4, yt}, {x5, bottom}};
        for (auto &pt : pts)
            c.blit(hd, 0, 0, (int)std::lround(pt[0]) - hw / 2, (int)std::lround(pt[1]) - hh / 2, hw, hh, alpha);
    }
}

// filter response displays (LP4/HP4/BP4/BS4Response): measured on the original - line (0,25,65) at the base level
// h/2 - 3, handle (NormalHandle) at x = 3 + cutoff/127 * (w - 17), slope 3 px per px; resonance lifts the handle
EditorRenderer::FilterGeom EditorRenderer::filter_geom(const Element &e, int x, int y, int part) const
{
    const std::string &cut = e.get("Parameter"); // "Filter N/Cutoff"
    std::string res = cut.substr(0, cut.rfind('/') + 1) + "Resonance";
    int cv = values(cut, part), rv = values(res, part);
    FilterGeom q;
    q.top = y + 4, q.base = y + e.h / 2 - 3;
    // the highpass handle sits 5 px right of the lowpass mapping, the bandpass 1 px (measured)
    q.x_off = e.type == "HP4Response" ? 5 : e.type == "BP4Response" ? 1 : 0;
    q.hx = x + 7 + cv / 127.0 * (e.w - 21) + q.x_off, q.hy = q.base - rv / 127.0 * (q.base - q.top);
    return q;
}

std::vector<EditorRenderer::GraphHandle> EditorRenderer::graph_handles(const Element &e, int x, int y, int part) const
{
    const std::string &p = e.get("Parameter");
    if (p.empty())
        return {};
    const std::string g = p.substr(0, p.rfind('/') + 1);
    if (e.type == "ADSTREnvelope") {
        const EnvGeom q = env_geom(e, x, y, part);
        return {{q.x1, q.top, g + "Attack", ""},
                {q.x2, q.ys, g + "Decay", g + "Sustain"},
                {q.x3, q.ys, "", g + "Sustain"},
                {q.x4, q.yt, "", g + "Sustain Time"},
                {q.x5, q.bottom, g + "Release", ""}};
    }
    if (e.type.size() == 11 && e.type.compare(3, 8, "Response") == 0) {
        const FilterGeom q = filter_geom(e, x, y, part);
        // the band stop has no vertical handle (E371); the cutoff may be "Offset for FilterLink" as bound
        return {{q.hx, q.hy, p, e.type == "BS4Response" ? "" : g + "Resonance"}};
    }
    return {};
}

std::vector<EditorRenderer::GraphSegment> EditorRenderer::graph_segments(const Element &e, int x, int y,
                                                                         int part) const
{
    const auto hs = graph_handles(e, x, y, part);
    if (hs.empty())
        return {};
    std::vector<GraphSegment> out;
    if (e.type == "ADSTREnvelope") { // segment i ends at handle i and grabs its parameters
        const EnvGeom q = env_geom(e, x, y, part);
        double px = q.x0, py = q.bottom;
        for (auto &h : hs) {
            out.push_back({px, py, h.x, h.y, h});
            px = h.x, py = h.y;
        }
        return out;
    }
    // the filters: the points of filter_response() in the handle's coordinates; the segment that rises to the
    // handle grabs both, the flat ones before it the cutoff, the ones after it nothing (the original's link table)
    const FilterGeom q = filter_geom(e, x, y, part);
    const double left = x + 3, right = x + e.w - 3, bottom = y + e.h - 1;
    const double drop = (bottom - q.base) / 3, hx = q.hx, hy = q.hy;
    const GraphHandle both = hs[0], cut{hx, hy, hs[0].xparam, ""}, none{hx, hy, "", ""};
    auto seg = [&](double a, double b, double c, double d, const GraphHandle &g) { out.push_back({a, b, c, d, g}); };
    if (e.type == "LP4Response") {
        seg(left, q.base, hx - 4, q.base, cut), seg(hx - 4, q.base, hx, hy, both);
        seg(hx, hy, std::min(right, hx + drop), bottom, none);
    } else if (e.type == "HP4Response") {
        seg(std::max(left, hx - drop), bottom, hx, hy, both), seg(hx, hy, hx + 4, q.base, none);
        seg(hx + 4, q.base, right, q.base, none);
    } else if (e.type == "BP4Response") {
        seg(std::max(left, hx - drop), bottom, hx, hy, both), seg(hx, hy, std::min(right, hx + drop), bottom, none);
    } else { // band stop: the handle has no vertical parameter, so both = cutoff
        seg(left, q.base, hx - drop / 2, q.base, cut), seg(hx - drop / 2, q.base, hx, bottom, cut);
        seg(hx, bottom, hx + drop / 2, q.base, none), seg(hx + drop / 2, q.base, right, q.base, none);
    }
    return out;
}

std::vector<EditorRenderer::GraphHandle> EditorRenderer::graph_grabs(const Element &e, int x, int y, int part) const
{
    auto v = graph_handles(e, x, y, part);
    for (auto &s : graph_segments(e, x, y, part))
        v.push_back(s.grab);
    return v;
}

int EditorRenderer::graph_grab(const Element &e, int x, int y, int part, int px, int py) const
{
    const auto hs = graph_handles(e, x, y, part);
    int found = -1;
    double best = 1e9;
    for (size_t i = 0; i < hs.size(); i++) {
        const double dx = std::abs(px - hs[i].x), dy = std::abs(py - hs[i].y);
        if (dx <= 5 && dy <= 5 && dx + dy < best)
            best = dx + dy, found = (int)i;
    }
    if (found >= 0)
        return found;
    const auto ss = graph_segments(e, x, y, part);
    for (size_t i = ss.size(); i-- > 0;) { // the later segment wins where boxes overlap (the original's order)
        const GraphSegment &s = ss[i];
        double x0 = std::min(s.x0, s.x1), x1 = std::max(s.x0, s.x1);
        double y0 = std::min(s.y0, s.y1), y1 = std::max(s.y0, s.y1);
        if (x1 - x0 < 4)
            x0 -= 2, x1 += 2;
        if (y1 - y0 < 4)
            y0 -= 2, y1 += 2;
        if (px >= x0 && px <= x1 && py >= y0 && py <= y1)
            return (int)(hs.size() + i);
    }
    return -1;
}

int EditorRenderer::graph_value(const Element &e, int x, int y, int part, const std::string &param, double hx,
                                double hy) const
{
    auto clamp = [](double v) { return (int)std::lround(std::max(0.0, std::min(127.0, v))); };
    const std::string name = param.substr(param.rfind('/') + 1);
    if (e.type == "ADSTREnvelope") {
        const EnvGeom q = env_geom(e, x, y, part);
        if (name == "Attack")
            return clamp((hx - q.x0) / 0.47);
        if (name == "Decay")
            return clamp(127 * std::pow(std::max(0.0, hx - q.x1) / 62, 1 / 1.07));
        if (name == "Sustain")
            return clamp((q.bottom - hy) / q.h * 127);
        if (name == "Sustain Time")
            return clamp(64 + (q.ys - hy) / (q.h * 0.5) * 63);
        if (name == "Release")
            return clamp((hx - q.x4) / 0.457);
        return 0;
    }
    const FilterGeom q = filter_geom(e, x, y, part);
    if (name == "Resonance")
        return clamp((q.base - hy) / (q.base - q.top) * 127);
    return clamp((hx - x - 7 - q.x_off) / (e.w - 21) * 127);
}

void EditorRenderer::filter_response(Canvas &c, const Element &e, int x, int y, int part)
{
    const FilterGeom q = filter_geom(e, x, y, part);
    const Rgba col{0, 25, 65, 255};
    double left = x + 3, right = x + e.w - 3, bottom = y + e.h - 1;
    double base = q.base, hx = q.hx - q.x_off, hy = q.hy;
    double drop = (bottom - base) / 3;
    // the curve sits 1 px left of and above the handle geometry (measured on the original)
    auto ln = [&](double a, double b, double c2, double d) { c.line(a - 0.5, b - 0.5, c2 - 0.5, d - 0.5, col); };
    if (e.type == "LP4Response") {
        ln(left, base, hx - 4, base), ln(hx - 4, base, hx, hy), ln(hx, hy, std::min(right, hx + drop), bottom);
    } else if (e.type == "HP4Response") {
        hx += 5; // the highpass handle sits 5 px right of the lowpass mapping (measured)
        ln(std::max(left, hx - drop), bottom, hx, hy), ln(hx, hy, hx + 4, base), ln(hx + 4, base, right, base);
    } else if (e.type == "BP4Response") { // centred on the handle, 1 px right of the lowpass mapping (measured)
        hx += 1;
        auto lc = [&](double a, double b, double c2, double d) { c.line(a + 0.5, b + 0.5, c2 + 0.5, d + 0.5, col); };
        lc(std::max(left, hx - drop), bottom, hx, hy), lc(hx, hy, std::min(right, hx + drop), bottom);
    } else { // BS4Response: flat with a notch at the cutoff
        ln(left, base, hx - drop / 2, base), ln(hx - drop / 2, base, hx, bottom), ln(hx, bottom, hx + drop / 2, base);
        ln(hx + drop / 2, base, right, base);
    }
    if (void *hd = c.image(e.get("NormalHandle")))
        c.blit(hd, 0, 0, (int)std::lround(hx) - c.image_width(hd) / 2, (int)std::lround(hy) - c.image_height(hd) / 2,
               c.image_width(hd), c.image_height(hd), 1.0);
}

// classic oscillator display: sine (shape 0) -> saw (64) -> pulse (127, width from Pulsewidth); area between the wave
// and the centre filled in ForeColor (182,200,228), wave line (91,100,114) - measured on the original
// HyperSaw display (measured on the original, 8 density/spread pairs): one 1-px line per saw in ForeColor, the centre
// first, then alternating right/left at floor(k * w) from x + w/2, w = 2 + spread * 2.625 / 64; the number of saws
// follows the density (0 -> 1, 32 -> 2, 64 -> 3, 96 -> 5, 112 -> 7, 127 -> 9, linear between); a fractional saw is
// a shorter line from the bottom (height = fraction * h)
// the original's table (constructor 0x180048b90, section ends at 0x180153da8): 2 lines up to Density 31, then one more
// per section; inside a section the last line grows from 0 in equal steps; 127 shows every line in full
std::pair<int, double> EditorRenderer::hypersaw_lines(int density)
{
    static const int ends[8] = {32, 64, 80, 96, 104, 112, 120, 128};
    density = std::max(0, std::min(127, density));
    if (density == 127)
        return {9, 1.0};
    int from = 0;
    for (int g = 0; g < 8; g++) {
        if (density < ends[g])
            return {g + 2, (double)(density - from) / (ends[g] - from)};
        from = ends[g];
    }
    return {9, 1.0};
}

double EditorRenderer::hypersaw_spacing(int w, double spread)
{
    return (w / 9.0 - 2) * spread + 2;
}

int EditorRenderer::hypersaw_x(int i, double spacing, int w, int left)
{
    double side = i % 2 ? spacing : -spacing; // line 1 right, 2 left, 3 right ...
    return (int)(((i + 1) >> 1) * side + w * 0.5 + left);
}

// the original (draw 0x180048c70): the background, then vertical lines in ForeColor from the bottom edge up, full
// height except the last one (its share from the table); dimmed to 37 % when inactive like the classic display
void EditorRenderer::osc_hypersaw(Canvas &c, const Element &e, int x, int y, int part)
{
    const std::string &sp = e.get("Parameter"); // "Oscillator N HyperSaw/DetuneSpread"
    const Parameter *p = cond_.find(sp);
    int lo = p ? p->min : 0, hi = p && p->max > lo ? p->max : 127;
    double spread = (double)(std::max(lo, std::min(hi, values(sp, part))) - lo) / (hi - lo);
    auto [lines, last] = hypersaw_lines(values(sp.substr(0, sp.rfind('/') + 1) + "Density", part));
    double s = hypersaw_spacing(e.w, spread);
    const int bottom = y + e.h;
    for (int i = 0; i < lines; i++) {
        double h = i == lines - 1 ? e.h * last : e.h;
        int top = (int)(bottom - h);
        if (bottom > top)
            c.fill(hypersaw_x(i, s, e.w, x), top, 1, bottom - top, e.fore);
    }
}

// arpeggiator user pattern (ArpEdit, measured on the original): 32 cells of 20 px; a step with its bit set shows a bar
// from the bottom, height = velocity in px, width from the step length (table below, right border at cell + r), filled
// with ForeColor at ~38 % and framed in (192,192,192); steps without the bit show nothing
int EditorRenderer::arp_step_width(int len)
{
    static const int lens[] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96, 112, 120, 127};
    static const int r[] = {0, 2, 3, 4, 5, 6, 7, 8, 10, 11, 12, 13, 14, 16, 18, 18};
    len = std::max(0, std::min(127, len));
    for (int i = 1; i < 16; i++)
        if (len <= lens[i])
            return (int)std::lround(r[i - 1] + (double)(len - lens[i - 1]) * (r[i] - r[i - 1]) / (lens[i] - lens[i - 1]));
    return 18;
}

void EditorRenderer::arp_step_geometry(const Element &e, int x, int y, int step, int &cell_x, int &bottom)
{
    cell_x = x + 1 + step * (e.w / 32); // measured: the first cell starts 1 px inside the element
    bottom = y + e.h - 1;
}

int EditorRenderer::arp_cell(const Element &e, int x, int px)
{
    return std::max(0, std::min(31, (px - x - 1) / std::max(1, e.w / 32)));
}

EditorRenderer::ArpGrab EditorRenderer::arp_grab(const Element &e, int x, int y, int part, int px, int py) const
{
    ArpGrab g;
    const int cell = std::max(1, e.w / 32);
    auto on = [&](int i) { return values("Arpeggiator User Pattern/Step " + std::to_string(i + 1) + " Bitfield", part) != 0; };
    for (int i = 0; i < 32; i++) {
        if (!on(i))
            continue;
        int span = 1;
        while (i + span < 32 && !on(i + span))
            span++;
        int cx, bottom;
        arp_step_geometry(e, x, y, i, cx, bottom);
        if (px < cx - 1 || px >= cx - 1 + span * cell)
            continue;
        const std::string st = "Arpeggiator User Pattern/Step " + std::to_string(i + 1) + " ";
        const int vel = std::max(0, std::min(127, values(st + "Velocity", part)));
        const int r = std::min(span * cell - 1, (int)std::lround(arp_step_width(values(st + "Length", part)) * (double)span));
        g.step = i, g.span = span;
        g.length = px >= cx + r - 8;
        g.velocity = py <= bottom - vel + 8;
        return g;
    }
    return g;
}

void EditorRenderer::arp_edit(Canvas &c, const Element &e, int x, int y, int part)
{
    const Rgba border{192, 192, 192, 255};
    const double a = 0.38;
    for (int i = 0; i < 32; i++) {
        std::string st = "Arpeggiator User Pattern/Step " + std::to_string(i + 1) + " ";
        if (!values(st + "Bitfield", part))
            continue;
        int vel = std::max(0, std::min(127, values(st + "Velocity", part)));
        // the note lasts until the next step that is on: the length applies to the step plus the following off steps
        int span = 1;
        while (i + span < 32 &&
               !values("Arpeggiator User Pattern/Step " + std::to_string(i + span + 1) + " Bitfield", part))
            span++;
        int r = std::min(span * 20 - 1, (int)std::lround(arp_step_width(values(st + "Length", part)) * (double)span));
        int cx, bottom;
        arp_step_geometry(e, x, y, i, cx, bottom);
        int top = bottom - vel;
        if (r > 0 && vel > 1) // translucent fill inside the frame
            c.fill(cx, top + 1, r, vel - 1, Rgba{e.fore.r, e.fore.g, e.fore.b, (uint8_t)std::lround(a * 255)});
        c.fill(cx - 1, top, r + 2, 1, border);    // top
        c.fill(cx - 1, bottom, r + 2, 1, border); // bottom
        c.fill(cx - 1, top, 1, vel + 1, border);  // left
        c.fill(cx + r, top, 1, vel + 1, border);  // right
    }
}

double EditorRenderer::osc_classic_value(int shape, int pulsewidth, int wave, const std::vector<float> &waves, int i)
{
    double s = std::max(0, std::min(127, shape)) / 127.0;
    double a = s < 0.5 ? 1 - 2 * s : 0, b = s < 0.5 ? 2 * s : 2 - 2 * s, c = s > 0.5 ? 2 * s - 1 : 0;
    int sw = (int)((std::max(0, std::min(127, pulsewidth)) / 127.0 + 1) * 0.5 * 64); // the pulse switches here
    int row = std::max(0, std::min(63, wave));
    double w = waves.size() == 64 * 64 ? waves[row * 64 + i] : std::sin(2 * 3.14159265358979 * i / 64);
    double v = a * w + b * (1 - i / 32.0) + (i < sw ? c : -c);
    return std::max(-1.0, std::min(1.0, v));
}

void EditorRenderer::osc_classic(Canvas &c, const Element &e, int x, int y, int part)
{
    const std::string &pwp = e.get("Parameter"); // "Oscillator N/Pulsewidth"
    std::string g = pwp.substr(0, pwp.rfind('/') + 1);
    int sh = values(g + "Shape", part), pwv = values(pwp, part), ws = values(g + "WaveSelect", part);
    // measured on the original: steps of 1 / (w - 2) per column, centre y + (h - 2) / 2 and amplitude (h - 2) / 2, the fill above the centre stops one
    // row short of it
    double mid = y + (e.h - 2) / 2.0, amp = (e.h - 2) / 2.0;
    const int w = e.w - 1;
    double sv[64];
    for (int i = 0; i < 64; i++)
        sv[i] = osc_classic_value(sh, pwv, ws, osc_waves, i);
    // the original draws a polygon through its 64 samples over the period: column t of it lies between two samples
    auto wave = [&](double t) {
        double k = std::max(0.0, t) * 64;
        int k0 = std::min(63, (int)k), k1 = std::min(63, k0 + 1);
        return sv[k0] + (sv[k1] - sv[k0]) * (k - k0 > 1 ? 1 : k - k0);
    };
    Rgba fillc{182, 200, 228, 255}, linec{91, 100, 114, 255};
    // one curve pixel per column at floor(y) (the original's 2:1 saw shows pixel pairs), fill between curve and centre
    int midrow = (int)std::lround(mid), r = midrow, prev_ry = midrow;
    for (int i = 0; i <= w; i++) { // the curve runs one column past the closing edge
        double t = (double)i / (w - 1), yy = mid - wave(t) * amp;
        int ry = (int)std::floor(yy);
        if (i < w) { // the extra column carries only the curve pixel
            r = ry;
            if (r < midrow)
                c.fill(x + i, r + 1, 1, midrow - r - 1, fillc);
            else if (r > midrow)
                c.fill(x + i, midrow, 1, r - midrow, fillc);
        }
        c.fill(x + i, ry, 1, 1, linec);
        if (i > 0 && std::abs(ry - prev_ry) > 1) // steep parts (pulse edges): connect vertically
            c.fill(x + i, std::min(ry, prev_ry) + 1, 1, std::abs(ry - prev_ry) - 1, linec);
        prev_ry = ry;
    }
    if (r > midrow) // closing edge
        c.fill(x + w - 1, midrow, 1, r - midrow, linec);
    else if (r < midrow)
        c.fill(x + w - 1, r, 1, midrow - r, linec);
}

bool EditorRenderer::param_active(const std::string &param, int part) const
{
    const Parameter *p = cond_.find(param);
    if (p && !p->condition.empty() && !cond_.eval(p->condition, values, part))
        return false;
    if (param.rfind("Softknobs/Softknob ", 0) == 0 && values("Soft Knobs/Destination " + param.substr(19), part) == 0)
        return false;
    return true;
}

// what keeps a parameter inactive: the parameters its condition tests (empty while it is active)
std::vector<std::string> EditorRenderer::inactive_inputs(const std::string &param, int part) const
{
    if (param_active(param, part))
        return {};
    if (param.rfind("Softknobs/Softknob ", 0) == 0)
        return {"Soft Knobs/Destination " + param.substr(19)};
    const Parameter *p = cond_.find(param);
    return p ? cond_.inputs(p->condition) : std::vector<std::string>();
}

// an element's own part: controls with Part >= 0 are bound to that part (the part strips: Part 0..15); containers'
// Part is not passed on - the original's page and frame containers carry Part="0" while their controls follow the
// selected part (user report: OSC sync of part 1 shown for part 2)
int EditorRenderer::own_part(const Element &e)
{
    return e.type == "Container" ? -1 : e.part();
}

std::vector<const Element *> EditorRenderer::static_signature() const
{
    std::vector<const Element *> out;
    std::function<void(const Element &, int)> walk = [&](const Element &e, int inherited) {
        int pt = own_part(e) >= 0 ? own_part(e) : inherited >= 0 ? inherited : part;
        if (!cond_.eval(e.get("Condition"), values, pt))
            return;
        if (e.get("Parameter").empty() && !e.get("Condition").empty())
            out.push_back(&e);
        for (auto &ch : e.children)
            walk(ch, own_part(e) >= 0 ? own_part(e) : inherited);
    };
    for (auto &e : layout_.elements)
        walk(e, -1);
    return out;
}

void EditorRenderer::dep_read(const std::string &path)
{
    if (dep_cond_)
        deps->cond.insert(path);
    else if (dep_rect_[2] > 0) {
        auto &v = deps->rects[path];
        if (v.empty() || v.back() != dep_rect_)
            v.push_back(dep_rect_);
    }
}

void EditorRenderer::render(Canvas &c)
{
    if (layer == 1)
        static_key.clear();
    ValueFn plain;
    if (deps) { // record the reads (conditions and drawing helpers all go through `values`)
        plain = values;
        values = [this, &plain](const std::string &path, int pt) {
            dep_read(path);
            return plain(path, pt);
        };
        dep_cond_ = false, dep_rect_ = {};
    }
    for (auto &e : layout_.elements)
        draw(c, e, 0, 0, -1);
    if (deps)
        values = plain;
}

void EditorRenderer::draw(Canvas &c, const Element &e, int ox, int oy, int inherited)
{
    // Part -1 = the enclosing patch container's part, else the current part
    int pt = own_part(e) >= 0 ? own_part(e) : inherited >= 0 ? inherited : part;
    dep_cond_ = true;
    bool shown = cond_.eval(e.get("Condition"), values, pt);
    dep_cond_ = false;
    if (!shown)
        return;
    int x = ox + e.x, y = oy + e.y;
    if (e.w > 0 && e.h > 0 && (x >= clip_x1 || y >= clip_y1 || x + e.w <= clip_x0 || y + e.h <= clip_y0))
        return; // outside the repaint area (children lie inside their parent)
    const std::string &param = e.get("Parameter");
    // layers: 1 = static (no parameter: backgrounds, panels, labels), 2 = dynamic (controls with a value); the
    // editor caches layer 1 and draws only layer 2 per repaint
    bool skip_self = (layer == 1 && !param.empty()) || (layer == 2 && param.empty());
    if (layer == 1 && param.empty() && !e.get("Condition").empty())
        static_key.push_back(&e); // visible conditional static elements: the cache's identity
    if (!skip_self) {
    dep_rect_ = {x, y, e.w, e.h};
    const std::string target = proxy_target && !param.empty() ? proxy_target(param, pt) : std::string();
    const std::string &vparam = target.empty() ? param : target; // read through `values`: a repaint dependency
    const Parameter *p = param.empty() ? nullptr : cond_.find(vparam);
    int v = param.empty() ? 0 : values(vparam, pt);
    // inactive controls (the parameter's own condition is false; soft knobs without destination) are translucent
    const bool inactive = !param.empty() && !param_active(param, pt);
    double alpha = inactive ? disabled_alpha : 1.0;
    int lo = p ? p->min : 0, hi = p ? p->max : 127;
    if (p)
        v = shown_value(vparam, v);

    void *normal = c.image(e.get("NormalBack"));
    // inactive controls with a DisabledBack strip use it at full opacity (the translucent "_Trs" look)
    bool use_disabled = alpha < 1.0 && normal && c.image(e.get("DisabledBack"));
    if (use_disabled) {
        normal = c.image(e.get("DisabledBack"));
        alpha = 1.0;
    }
    if (&e == pressed && c.image(e.get("PressedBack")))
        normal = c.image(e.get("PressedBack"));
    if (normal && e.w > 0 && e.h > 0) {
        int frames = std::max(1, c.image_height(normal) / e.h), index = 0;
        if (e.type == "VUSlider") { // MIDI activity of the part's channel (level strip) + volume handle
            void *hd = c.image(e.get("NormalHandle"));
            int hw = hd ? c.image_width(hd) : 0;
            int pos = hi > lo ? (int)std::lround((double)(v - lo) / (hi - lo) * (e.w - hw)) : 0;
            int act = meter ? meter(pt) : 0; // the plug-in's fixed multi: part n listens on channel n
            c.frames(normal, e.h);
            c.blit(normal, 0, frame_of(act, 0, 127, frames) * e.h, x, y, e.w, e.h, alpha);
            if (hd)
                c.blit(hd, 0, 0, x + pos, y + (e.h - c.image_height(hd)) / 2, hw, c.image_height(hd), alpha);
        } else {
            if (e.type.find("Knob") != std::string::npos || e.type.find("Slider") != std::string::npos)
                index = frame_of(v, lo, hi, frames);
            else if (e.type == "RadioButton") // 2-frame strips: 0 = off, 1 = selected
                index = frames >= 2 && v == atoi(e.get("OnValue").c_str()) ? 1 : 0;
            else if (e.type == "Button" || e.type == "KickButton" || e.type == "DblClkButton") // section headers:
                index = frames >= 2 && v ? 1 : 0; // frame 1 = locked (red, E378)
            const std::string &nm = use_disabled ? e.get("DisabledBack")
                                    : (&e == pressed && c.image(e.get("PressedBack"))) ? e.get("PressedBack")
                                                                                       : e.get("NormalBack");
            image(c, nm, normal, index * std::min(e.h, c.image_height(normal)), std::min(e.h, c.image_height(normal)), x, y,
                  e.w, e.h, alpha);
        }
    } else if (e.get("Transparent") == "false" && e.back.a)
        c.fill(x, y, e.w, e.h, e.back);

    bool display = (e.type.size() == 11 && e.type.compare(3, 8, "Response") == 0) || e.type == "OscClassicShape" ||
                   e.type == "OscHypersawShape" || e.type == "ArpEdit" || e.type == "ADSTREnvelope";
    if (display)
        c.push_clip(x, y, e.w, e.h);
    if (e.type.size() == 11 && e.type.compare(3, 8, "Response") == 0 && !param.empty())
        filter_response(c, e, x, y, pt);
    if (e.type == "OscClassicShape" && !param.empty())
        osc_classic(c, e, x, y, pt);
    if (e.type == "OscHypersawShape" && !param.empty())
        osc_hypersaw(c, e, x, y, pt);
    if (e.type == "ArpEdit")
        arp_edit(c, e, x, y, pt);
    if (e.type == "ADSTREnvelope" && !param.empty())
        envelope(c, e, x, y, pt, alpha);
    if (display)
        c.pop_clip();
    if (e.type == "RemoteBrowser" && remote_name && !param.empty())
        remote_browser(c, e, x, y, values(param, pt));
    if (e.type == "PatchBrowser" && bank_patch && !param.empty()) {
        patch_browser(c, e, x, y, values(param, pt), browser_pane(param)); // "Browser N/Selected Bank" (host side)
    }
    // inactive text: the DisabledFont at full opacity, else ControlFont translucent - whatever the background
    // used (a DisabledBack picture does not make the text opaque; the original's 0x180053480, E360)
    const bool dfont = inactive && !e.get("DisabledFont").empty() && c.image(e.get("DisabledFont"));
    const std::string &font = dfont ? e.get("DisabledFont") : e.get("ControlFont");
    const double text_alpha = inactive && !dfont ? disabled_alpha : 1.0;
    if (font.empty() && e.type == "SearchEdit" && atoi(e.get("FontSize").c_str()) > 0 && !param.empty() &&
        live_text) { // no bitmap font: the system font (REMOTE page "MIDI String Info", E198)
        std::string str = live_text(param);
        if (!str.empty())
            c.label(x, y, e.w, e.h, str, atoi(e.get("FontSize").c_str()), e.fore);
    }
    if (!font.empty() && e.type != "PatchBrowser" && e.get("Align") != "No Label") { // "No Label": the bitmap has it
        std::string str;
        if (e.type == "StaticText")
            str = e.get("Name");
        else if (e.type == "NameEdit" && patch_name)
            str = patch_name(pt);
        else if ((e.type == "TextSlider" || e.type == "Menu" || e.type == "HierarchicMenu" || e.type == "SearchEdit" ||
                  e.type == "URLButton") && // URLButton: the link text is its value list entry (INFO box)
                 !param.empty()) {
            str = live_text ? live_text(param) : std::string();
            if (str.empty())
                str = value_text(vparam, v);
            if (e.type == "SearchEdit" && str == value_text(vparam, v))
                str.clear();
            if (e.type != "TextSlider") // valuelists indent entries for the popup (" Mod Wheel"); the control shows them trimmed
                str.erase(0, str.find_first_not_of(' ') == std::string::npos ? str.size() : str.find_first_not_of(' '));
        }
        // text inside the bitmap's tile insets (horizontal and vertical: DSP_Menu.png top 2 / bottom 1 puts the
        // text of the 13 px menus 1 px lower, like the original)
        int tl = 0, tr = 0, tt = 0, tb = 0;
        auto t = layout_.tiles.find(e.get("NormalBack"));
        if (t != layout_.tiles.end())
            tl = t->second.left, tr = t->second.right, tt = t->second.top, tb = t->second.bottom;
        text(c, font, str, x + tl, y + tt, e.w - tl - tr, e.h - tt - tb, e.get("Align"), text_alpha);
    }
    }
    dep_rect_ = {};
    for (auto &ch : e.children)
        draw(c, ch, x, y, own_part(e) >= 0 ? own_part(e) : inherited);
}

static bool is_menu(const Element &e)
{
    return e.type == "Menu" || e.type == "HierarchicMenu" || e.type == "PatchSaveMenu";
}

bool EditorRenderer::hit(const Element &e, int ox, int oy, int inherited, int px, int py, Hit &out,
                         bool include_disabled, const std::string &cond, bool menus_only, int slack) const
{
    int pt = own_part(e) >= 0 ? own_part(e) : inherited >= 0 ? inherited : part;
    if (!cond_.eval(e.get("Condition"), values, pt))
        return false;
    const std::string &own = e.get("Condition");
    const std::string &c = own.empty() ? cond : own; // the closest condition that shows this element
    int x = ox + e.x, y = oy + e.y;
    bool inside = px >= x && px < x + e.w && py >= y && py < y + e.h;
    if (slack < 0) // graph_hit_test: only the filter / envelope displays
        inside = inside && (e.type == "ADSTREnvelope" || (e.type.size() == 11 && e.type.compare(3, 8, "Response") == 0));
    if (slack > 0) // the second pass: only the small arrow buttons, a few px taller
        inside = (e.type == "IncButton" || e.type == "DecButton") && e.h < 12 && px >= x && px < x + e.w &&
                 py >= y - slack && py < y + e.h + slack;
    bool disabled = e.get("Enabled") == "false";
    if (inside && !e.get("Parameter").empty() && (!disabled || include_disabled) && (!menus_only || is_menu(e))) {
        out.element = &e; // later (= drawn on top) elements override
        out.x = x, out.y = y, out.part = pt;
        out.disabled = disabled;
        out.why = disabled ? c : std::string();
        out.depends = disabled ? std::vector<std::string>() : inactive_inputs(e.get("Parameter"), pt);
        out.inactive = disabled || !param_active(e.get("Parameter"), pt);
    }
    for (auto &ch : e.children)
        hit(ch, x, y, own_part(e) >= 0 ? own_part(e) : inherited, px, py, out, include_disabled, c, menus_only, slack);
    return out.element != nullptr;
}

int EditorRenderer::remote_entry(const Hit &h, int x, int y)
{
    if (!h.element || h.element->type != "RemoteBrowser")
        return -1;
    int dx = x - h.x, dy = y - h.y;
    if (dx < 0 || dy < 0 || dx >= h.element->w || dy >= h.element->h)
        return -1;
    int col = dx / 114, row = dy / 11;
    return col < 4 && row < 8 ? col * 8 + row : -1;
}

int EditorRenderer::browser_entry(const Hit &h, int x, int y)
{
    if (!h.element || h.element->type != "PatchBrowser")
        return -1;
    int col = (x - h.x - 21 + 30) / 84, row = (y - h.y - 1) / 11; // entry = digits (left) + 60 px name
    if (col < 0 || col > 7 || row < 0 || row > 15)
        return -1;
    return col * 16 + row;
}

std::vector<std::array<int, 4>> EditorRenderer::param_rects(const std::string &param) const
{
    std::vector<std::array<int, 4>> out;
    std::function<void(const Element &, int, int)> walk = [&](const Element &e, int ox, int oy) {
        int x = ox + e.x, y = oy + e.y;
        if (e.get("Parameter") == param)
            out.push_back({x, y, e.w, e.h});
        for (auto &c : e.children)
            walk(c, x, y);
    };
    for (auto &e : layout_.elements)
        walk(e, 0, 0);
    return out;
}

std::vector<Hit> EditorRenderer::visible_controls(bool include_disabled) const
{
    std::vector<Hit> out;
    std::function<void(const Element &, int, int, int)> walk = [&](const Element &e, int ox, int oy, int inherited) {
        int pt = own_part(e) >= 0 ? own_part(e) : inherited >= 0 ? inherited : part;
        if (!cond_.eval(e.get("Condition"), values, pt))
            return;
        int x = ox + e.x, y = oy + e.y;
        bool disabled = e.get("Enabled") == "false";
        if (!e.get("Parameter").empty() && (!disabled || include_disabled) && e.w > 0 && e.h > 0) {
            Hit h;
            h.element = &e, h.x = x, h.y = y, h.part = pt, h.disabled = disabled;
            out.push_back(h);
        }
        for (auto &c : e.children)
            walk(c, x, y, own_part(e) >= 0 ? own_part(e) : inherited);
    };
    for (auto &e : layout_.elements)
        walk(e, 0, 0, -1);
    return out;
}

Hit EditorRenderer::hit_test(int px, int py, bool include_disabled) const
{
    Hit h;
    for (auto &e : layout_.elements)
        hit(e, 0, 0, -1, px, py, h, include_disabled);
    // nothing hit: the small arrow buttons (17 x 9 px, the browser's bank arrows) take a click up to 4 px above or
    // below them - our own addition, the user found them hard to hit (#19)
    if (!h.element)
        for (auto &e : layout_.elements)
            hit(e, 0, 0, -1, px, py, h, include_disabled, std::string(), false, 4);
    return h;
}

Hit EditorRenderer::graph_hit_test(int px, int py) const
{
    Hit h;
    for (auto &e : layout_.elements)
        hit(e, 0, 0, -1, px, py, h, false, std::string(), false, -1);
    if (!h.element)
        return h;
    // a segment that drags nothing (the fall after a filter's peak, its box can be large) leaves the click to the
    // control below (the cutoff knob); whether the original lets it through there is not known (E372)
    const int i = graph_grab(*h.element, h.x, h.y, h.part, px, py);
    if (i < 0)
        return Hit{};
    const GraphHandle g = graph_grabs(*h.element, h.x, h.y, h.part)[(size_t)i];
    return g.xparam.empty() && g.yparam.empty() ? Hit{} : h;
}

Hit EditorRenderer::help_hit_test(int px, int py) const
{
    Hit h;
    for (auto &e : layout_.elements)
        hit(e, 0, 0, -1, px, py, h, true, std::string(), false, -1);
    return h.element ? h : hit_test(px, py, true);
}

Hit EditorRenderer::menu_hit_test(int px, int py) const
{
    Hit h;
    for (auto &e : layout_.elements)
        hit(e, 0, 0, -1, px, py, h, false, std::string(), true);
    return h;
}

std::pair<int, int> EditorRenderer::menu_anchor(const Hit &h)
{
    if (h.element && h.element->type == "HierarchicMenu")
        return {h.x, h.y + h.element->h};
    return {h.x, h.y};
}

int EditorRenderer::menu_items_per_column(size_t entries)
{
    // the original's value lists (0x180040f50): a quarter per column above 32 entries; VSTGUI's Windows menu breaks
    // columns only below 160 entries (0x180019440)
    return entries > 32 && entries < 160 ? (int)entries / 4 : 0;
}

} // namespace vti
