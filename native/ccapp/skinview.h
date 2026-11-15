// skinview.h - draws the layout of Access' Virus Control Center (src/cc/ccskin.h) with its own images, loaded at
// runtime from the user's installer, and reports clicks. What each element shows and does is up to a SkinState
// (the app's logic); this view only knows the drawing conventions of the original's images:
//   - two-frame images (height = 2 x element): top = normal / not selected, bottom = pressed / selected;
//     an "<name>_PRS.png" next to it is the pressed look while the mouse is down
//   - animbutton: an animation strip (frames of the element's height), pulsing while it is ready; its
//     `background` image is the pressed look
//   - animation: one frame of a strip, chosen by the state
//   - textlabel: the LCD bitmap font (6 x 9 pixel glyphs, ASCII 32..127, in one column)
// TI Revive's own pages in the same frame (nerd mode, 2026-11-15) add element types drawn in the original's colours
// (sampled from BG_DeviceSelector.png): tr_backdrop (the page gradient, a separator line `value` pixels down),
// tr_panel (a dark rounded panel), tr_rows (alternating row stripes, `value` pixels high), tr_vline (a column line),
// tr_title (a page title in a plain light font, text from the state) and tr_scroll (a scrollbar, thumb from the state).
#pragma once
#include "access/accessfiles.h"
#include "cc/ccskin.h"
#include "vstgui/lib/cbitmap.h"
#include "vstgui/lib/cview.h"
#include "vstgui/lib/cpoint.h"
#include <map>
#include <string>

namespace tirevive {

class SkinState {
public:
    virtual ~SkinState() = default;
    // is a container / control with this `expression` shown (empty expression: always)
    virtual bool visible(const std::string &expression) = 0;
    // radio: selected value of the parameter; animation: frame
    virtual int value(const std::string &parameter) = 0;
    virtual std::string text(const std::string &parameter) = 0;
    // can it be clicked (disabled elements are drawn dimmed)
    virtual bool enabled(const vti::cc::SkinNode &n) = 0;
    // animbutton: ready = pulsing
    virtual bool ready(const vti::cc::SkinNode &n) = 0;
    virtual void clicked(const vti::cc::SkinNode &n) = 0;
    virtual void right_click(VSTGUI::CPoint where) { (void)where; } // not on a control
    // TI Revive's pages: the mouse wheel anywhere (dy > 0: up), a scrollbar's thumb and a click on its track
    virtual void wheel(VSTGUI::CPoint where, double dy) { (void)where, (void)dy; }
    virtual bool thumb(const vti::cc::SkinNode &n, double &y, double &h) { (void)n, (void)y, (void)h; return false; }
    virtual void track_click(const vti::cc::SkinNode &n, double at) { (void)n, (void)at; }
    // repaint at every tick (the original's pulsing OK buttons); TI Revive's pages repaint only when they change
    virtual bool animating() { return true; }
};

class SkinView : public VSTGUI::CView {
public:
    SkinView(const VSTGUI::CRect &size, const vti::cc::SkinLayout &layout, const vti::AccessFiles &files,
             SkinState &state);
    void draw(VSTGUI::CDrawContext *ctx) override;
    void onMouseDownEvent(VSTGUI::MouseDownEvent &event) override;
    void onMouseUpEvent(VSTGUI::MouseUpEvent &event) override;
    void onMouseWheelEvent(VSTGUI::MouseWheelEvent &event) override;
    void tick(); // call ~20 times a second: animations

private:
    const vti::cc::SkinLayout &layout_;
    const vti::AccessFiles &files_;
    SkinState &state_;
    std::map<std::string, VSTGUI::SharedPointer<VSTGUI::CBitmap>> bitmaps_;
    std::map<std::string, VSTGUI::SharedPointer<VSTGUI::CBitmap>> text_cache_; // drawn LCD texts (draw_text)
    const vti::cc::SkinNode *pressed_ = nullptr;
    int phase_ = 0;

    VSTGUI::CBitmap *bitmap(const std::string &name);
    void draw_nodes(VSTGUI::CDrawContext *ctx, const std::vector<vti::cc::SkinNode> &nodes);
    bool draw_own(VSTGUI::CDrawContext *ctx, const vti::cc::SkinNode &n);
    void draw_control(VSTGUI::CDrawContext *ctx, const vti::cc::SkinNode &n);
    void draw_text(VSTGUI::CDrawContext *ctx, const vti::cc::SkinNode &n, const std::string &text, float alpha);
    const vti::cc::SkinNode *hit(const std::vector<vti::cc::SkinNode> &nodes, int x, int y);
};

} // namespace tirevive
