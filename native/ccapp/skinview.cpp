// skinview.cpp - see skinview.h.
#include "skinview.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/cgradient.h"
#include "vstgui/lib/coffscreencontext.h"
#include <algorithm>
#include "vstgui/lib/events.h"
#include "vstgui/lib/platform/platformfactory.h"

using namespace VSTGUI;
using vti::cc::SkinNode;

namespace tirevive {

static CColor rgba(uint32_t c)
{
    return CColor((uint8_t)(c >> 24), (uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c);
}

SkinView::SkinView(const CRect &size, const vti::cc::SkinLayout &layout, const vti::AccessFiles &files,
                   SkinState &state)
    : CView(size), layout_(layout), files_(files), state_(state)
{
}

CBitmap *SkinView::bitmap(const std::string &name)
{
    if (name.empty())
        return nullptr;
    auto it = bitmaps_.find(name);
    if (it != bitmaps_.end())
        return it->second;
    SharedPointer<CBitmap> b;
    auto &d = files_.get(name);
    if (!d.empty())
        if (auto pb = getPlatformFactory().createBitmapFromMemory(d.data(), (uint32_t)d.size()))
            b = makeOwned<CBitmap>(pb);
    bitmaps_[name] = b;
    return b;
}

void SkinView::tick()
{
    phase_++;
    if (state_.animating()) // the original's pages: the pulsing OK button
        invalid();
}

void SkinView::draw(CDrawContext *ctx)
{
    ctx->setFillColor(CColor(0, 0, 0, 255));
    ctx->drawRect(getViewSize(), kDrawFilled);
    draw_nodes(ctx, layout_.nodes);
    setDirty(false);
}

void SkinView::draw_nodes(CDrawContext *ctx, const std::vector<SkinNode> &nodes)
{
    for (auto &n : nodes) {
        if (!n.expression.empty() && n.container && !state_.visible(n.expression))
            continue;
        if (!n.container) {
            draw_control(ctx, n);
            continue;
        }
        if ((n.back & 0xff) != 0) {
            ctx->setFillColor(rgba(n.back));
            ctx->drawRect(CRect(n.x, n.y, n.x + n.w, n.y + n.h), kDrawFilled);
        }
        if (auto *b = bitmap(n.background))
            b->draw(ctx, CRect(n.x, n.y, n.x + b->getWidth(), n.y + b->getHeight()));
        draw_nodes(ctx, n.children);
    }
}

static std::string pressed_name(const std::string &bmp) // "BTN_Backup.png" -> "BTN_Backup_PRS.png"
{
    size_t dot = bmp.rfind('.');
    return dot == std::string::npos ? bmp : bmp.substr(0, dot) + "_PRS" + bmp.substr(dot);
}

// the original's colours (sampled from BG_DeviceSelector.png, 2026-11-15)
static const CColor kGradTop(83, 89, 96), kGradBottom(112, 119, 119), kSeparator(145, 161, 179),
    kPanel(73, 83, 93), kRowDark(67, 75, 83), kRowLight(75, 84, 92), kColumn(91, 97, 104), kTitle(226, 230, 234),
    kThumb(150, 158, 168), kEdge(18, 18, 18);

// TI Revive's element types (skinview.h); false: not one of them
bool SkinView::draw_own(CDrawContext *ctx, const SkinNode &n)
{
    const CRect r(n.x, n.y, n.x + n.w, n.y + n.h);
    if (n.type == "tr_backdrop") {
        // as the original's page images: 3 dark pixels at the left and right edge (the header and footer have them
        // too), a 4-row shadow under the header, then the gradient
        ctx->setFillColor(kEdge);
        ctx->drawRect(r, kDrawFilled);
        static const CColor shadow[] = {CColor(64, 69, 75), CColor(68, 73, 79), CColor(72, 77, 85), CColor(77, 84, 90)};
        for (int y = 0; y < 4 && y < n.h; y++) {
            ctx->setFillColor(shadow[y]);
            ctx->drawRect(CRect(n.x + 3, n.y + y, n.x + n.w - 3, n.y + y + 1), kDrawFilled);
        }
        // one gradient fill, not a line per row: this page repaints often (nerd mode, 2026-11-15: it lagged)
        const CRect g(n.x + 3, n.y + 4, n.x + n.w - 3, n.y + n.h);
        auto path = owned(ctx->createGraphicsPath());
        auto grad = owned(CGradient::create(0, 1, kGradTop, kGradBottom));
        if (path && grad) {
            path->addRect(g);
            ctx->fillLinearGradient(path, *grad, CPoint(g.left, g.top), CPoint(g.left, g.bottom));
        } else {
            ctx->setFillColor(kGradTop);
            ctx->drawRect(g, kDrawFilled);
        }
        if (n.value > 0) {
            ctx->setFillColor(kSeparator);
            ctx->drawRect(CRect(n.x + 3, n.y + n.value, n.x + n.w - 3, n.y + n.value + 1), kDrawFilled);
        }
        return true;
    }
    if (n.type == "tr_panel") {
        if (auto path = owned(ctx->createRoundRectGraphicsPath(r, 4))) {
            ctx->setFillColor(kPanel);
            ctx->drawGraphicsPath(path, CDrawContext::kPathFilled);
        }
        return true;
    }
    if (n.type == "tr_rows") {
        const int rh = n.value > 0 ? n.value : 19;
        for (int y = 0, i = 0; y < n.h; y += rh, i++) {
            ctx->setFillColor(i % 2 ? kRowLight : kRowDark);
            ctx->drawRect(CRect(n.x, n.y + y, n.x + n.w, n.y + std::min(y + rh, n.h)), kDrawFilled);
        }
        return true;
    }
    if (n.type == "tr_vline") {
        ctx->setFillColor(kColumn);
        ctx->drawRect(CRect(n.x, n.y, n.x + 1, n.y + n.h), kDrawFilled);
        return true;
    }
    if (n.type == "tr_title") {
        const std::string t = state_.text(n.parameter);
        if (!t.empty()) {
            auto font = makeOwned<CFontDesc>(*kSystemFont);
            font->setSize(13);
            ctx->setFont(font);
            ctx->setFontColor(kTitle);
            ctx->drawString(t.c_str(), r, kLeftText);
        }
        return true;
    }
    if (n.type == "tr_scroll") {
        double y = 0, h = 0;
        if (state_.thumb(n, y, h) && h < n.h) { // nothing to scroll: no thumb
            ctx->setFillColor(kRowDark);
            ctx->drawRect(r, kDrawFilled);
            ctx->setFillColor(kThumb);
            ctx->drawRect(CRect(n.x + 1, n.y + y, n.x + n.w - 1, n.y + y + h), kDrawFilled);
        }
        return true;
    }
    return false;
}

void SkinView::draw_control(CDrawContext *ctx, const SkinNode &n)
{
    if (n.type == "menu" || n.type == "patchbrowser") // Burn Flash only: not offered
        return;
    if (!n.expression.empty() && n.type != "animbutton" && !state_.visible(n.expression))
        return;
    if (draw_own(ctx, n))
        return;
    bool on = state_.enabled(n);
    float alpha = on ? 1.f : 0.4f;
    CRect r(n.x, n.y, n.x + n.w, n.y + n.h);
    bool down = pressed_ == &n;
    if (n.type == "textlabel") {
        draw_text(ctx, n, state_.text(n.parameter), alpha);
        return;
    }
    if (n.type == "animation") {
        if (auto *b = bitmap(n.bitmap))
            b->draw(ctx, r, CPoint(0, state_.value(n.parameter) * n.h), alpha);
        return;
    }
    if (n.type == "animbutton") {
        auto *b = bitmap(n.bitmap);
        if (down && bitmap(n.background)) {
            bitmap(n.background)->draw(ctx, r, CPoint(0, n.h));
            return;
        }
        if (!b)
            return;
        int frames = (int)(b->getHeight() / n.h), f = 0;
        if (state_.ready(n) && frames > 1) { // pulse up and down
            int p = phase_ % (2 * frames - 2);
            f = p < frames ? p : 2 * frames - 2 - p;
        }
        b->draw(ctx, r, CPoint(0, f * n.h), state_.ready(n) ? 1.f : alpha);
        return;
    }
    // radio / button: two frames
    bool selected = n.type == "radio" ? state_.value(n.parameter) == n.value : down;
    CBitmap *b = down ? bitmap(pressed_name(n.bitmap)) : nullptr;
    if (!b)
        b = bitmap(n.bitmap);
    if (b)
        b->draw(ctx, r, CPoint(0, selected && b->getHeight() >= 2 * n.h ? n.h : 0), alpha);
}

// The LCD text of one label, drawn glyph by glyph into an image once and then reused while the text stays (nerd mode,
// 2026-11-15: its table drew ~1800 single glyphs per repaint, 18-237 ms on Linux's Cairo). The key holds everything
// that changes the image; a full cache is emptied (the visible labels are drawn again on the next repaint).
void SkinView::draw_text(CDrawContext *ctx, const SkinNode &n, const std::string &text, float alpha)
{
    auto *font = bitmap(n.bitmap);
    if (!font || text.empty() || n.w <= 0)
        return;
    const int gw = 6, gh = 9, glyphs = (int)(font->getHeight() / gh);
    const int h = std::max(n.h, gh) + 1;
    const double scale = ctx->getScaleFactor();
    std::string key = n.bitmap + '|' + std::to_string(n.w) + '|' + std::to_string(n.h) + '|' + std::to_string(n.value) +
                      '|' + std::to_string((int)(scale * 100)) + '|' + text;
    auto it = text_cache_.find(key);
    if (it == text_cache_.end()) {
        if (text_cache_.size() > 3000)
            text_cache_.clear();
        SharedPointer<CBitmap> img;
        if (auto off = COffscreenContext::create(CPoint(n.w, h), scale)) {
            off->beginDraw();
            // value 1 (only TI Revive's labels: the original's have 0): centred, for button texts that change
            const int x0 = n.value == 1 && text.find('\n') == std::string::npos
                               ? std::max(0, (n.w - gw * (int)text.size()) / 2)
                               : 0;
            int x = x0, line = 0;
            for (char ch : text) {
                if (ch == '\n') { // several lines in a taller label
                    line++, x = x0;
                    continue;
                }
                if (x + gw > n.w)
                    continue;
                const int g = (unsigned char)ch - 32;
                if (g >= 0 && g < glyphs && (line + 1) * gh <= std::max(n.h, gh))
                    font->draw(off, CRect(x, line * (gh + 1), x + gw, line * (gh + 1) + gh), CPoint(0, g * gh));
                x += gw;
            }
            off->endDraw();
            img = off->getBitmap();
        }
        it = text_cache_.emplace(std::move(key), img).first;
    }
    if (it->second)
        it->second->draw(ctx, CRect(n.x, n.y, n.x + n.w, n.y + h), CPoint(0, 0), alpha);
}

const SkinNode *SkinView::hit(const std::vector<SkinNode> &nodes, int x, int y)
{
    for (auto it = nodes.rbegin(); it != nodes.rend(); ++it) { // topmost first
        auto &n = *it;
        if (!n.expression.empty() && (n.container || n.type != "animbutton") && !state_.visible(n.expression))
            continue;
        bool inside = x >= n.x && x < n.x + n.w && y >= n.y && y < n.y + n.h;
        if (n.container) {
            if (inside)
                if (auto *h = hit(n.children, x, y))
                    return h;
            continue;
        }
        if (inside && (n.type == "radio" || n.type == "button" || n.type == "animbutton" || n.type == "tr_scroll") &&
            state_.enabled(n))
            return &n;
    }
    return nullptr;
}

void SkinView::onMouseDownEvent(MouseDownEvent &event)
{
    if (event.buttonState.isRight()) {
        if (!hit(layout_.nodes, (int)event.mousePosition.x, (int)event.mousePosition.y))
            state_.right_click(event.mousePosition);
        event.consumed = true;
        return;
    }
    pressed_ = hit(layout_.nodes, (int)event.mousePosition.x, (int)event.mousePosition.y);
    if (pressed_ && pressed_->type == "tr_scroll") { // a click on the track: at once, not on release
        state_.track_click(*pressed_, event.mousePosition.y - pressed_->y);
        pressed_ = nullptr;
    }
    invalid();
    event.consumed = true;
}

void SkinView::onMouseWheelEvent(MouseWheelEvent &event)
{
    state_.wheel(event.mousePosition, event.deltaY);
    invalid();
    event.consumed = true;
}

void SkinView::onMouseUpEvent(MouseUpEvent &event)
{
    auto *p = pressed_;
    pressed_ = nullptr;
    invalid();
    event.consumed = true;
    if (p && hit(layout_.nodes, (int)event.mousePosition.x, (int)event.mousePosition.y) == p)
        state_.clicked(*p);
}

} // namespace tirevive
