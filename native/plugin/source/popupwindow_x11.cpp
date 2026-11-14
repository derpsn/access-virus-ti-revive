// popupwindow_x11.cpp - popup menu windows on Linux (X11, also under XWayland): an override-redirect window on the
// root, like the menus of X11 toolkits, so the window manager neither decorates, moves nor focuses it. See
// popupwindow.h.
#include "popupwindow.h"
#include "vstgui/lib/platform/linux/x11platform.h"
#include "vstgui/lib/platform/platform_x11.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <xcb/randr.h>
#include <xcb/xcb.h>

using namespace VSTGUI;

namespace vti {

namespace {

xcb_connection_t *conn() { return X11::RunLoop::instance().getXcbConnection(); }

xcb_screen_t *screen_of(xcb_connection_t *c)
{
    return xcb_setup_roots_iterator(xcb_get_setup(c)).data;
}

xcb_atom_t atom(xcb_connection_t *c, const char *name)
{
    auto *r = xcb_intern_atom_reply(c, xcb_intern_atom(c, 0, (uint16_t)strlen(name), name), nullptr);
    xcb_atom_t a = r ? r->atom : (xcb_atom_t)XCB_ATOM_NONE;
    free(r);
    return a;
}

// the X window of a VSTGUI frame
// up to `max` CARDINAL values of a root window property (empty when the window manager does not set it)
std::vector<uint32_t> cardinals(xcb_connection_t *c, xcb_window_t root, const char *name, uint32_t max)
{
    std::vector<uint32_t> out;
    const xcb_atom_t a = atom(c, name);
    if (a == XCB_ATOM_NONE)
        return out;
    auto *p = xcb_get_property_reply(c, xcb_get_property(c, 0, root, a, XCB_ATOM_CARDINAL, 0, max), nullptr);
    if (p && p->type == XCB_ATOM_CARDINAL && p->format == 32) {
        const auto *v = static_cast<const uint32_t *>(xcb_get_property_value(p));
        out.assign(v, v + xcb_get_property_value_length(p) / 4);
    }
    free(p);
    return out;
}

// the monitors RandR 1.5 reports (active outputs); empty without the extension
std::vector<NativeRect> randr_monitors(xcb_connection_t *c, xcb_window_t root)
{
    std::vector<NativeRect> out;
    const auto *ext = xcb_get_extension_data(c, &xcb_randr_id);
    if (!ext || !ext->present)
        return out;
    auto *r = xcb_randr_get_monitors_reply(c, xcb_randr_get_monitors(c, root, 1), nullptr);
    if (!r)
        return out;
    for (auto it = xcb_randr_get_monitors_monitors_iterator(r); it.rem; xcb_randr_monitor_info_next(&it))
        out.push_back({it.data->x, it.data->y, it.data->width, it.data->height});
    free(r);
    return out;
}

xcb_window_t window_of(CFrame *frame)
{
    auto *pf = frame ? frame->getPlatformFrame() : nullptr;
    auto *x = dynamic_cast<X11::IX11Frame *>(pf);
    return x ? x->getX11WindowID() : 0;
}

class X11PopupWindow : public PopupWindow {
public:
    ~X11PopupWindow() override
    {
        if (frame_)
            frame_->forget(); // destroys VSTGUI's child window
        if (auto *c = conn(); c && win_) {
            xcb_destroy_window(c, win_);
            xcb_flush(c);
        }
    }
    void move(const NativeRect &r) override
    {
        const uint32_t v[] = {(uint32_t)r.x, (uint32_t)r.y, (uint32_t)std::max(1, r.w), (uint32_t)std::max(1, r.h)};
        xcb_configure_window(conn(), win_,
                             XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH |
                                 XCB_CONFIG_WINDOW_HEIGHT,
                             v);
        xcb_flush(conn());
    }
    CFrame *frame() const override { return frame_; }
    void show() override
    {
        const uint32_t above = XCB_STACK_MODE_ABOVE;
        xcb_map_window(conn(), win_);
        xcb_configure_window(conn(), win_, XCB_CONFIG_WINDOW_STACK_MODE, &above);
        xcb_flush(conn());
    }
    void hide() override
    {
        xcb_unmap_window(conn(), win_);
        xcb_flush(conn());
    }

    xcb_window_t win_ = 0;
    CFrame *frame_ = nullptr;
};

} // namespace

std::unique_ptr<PopupWindow> PopupWindow::create(CFrame *editor, const NativeRect &r, int w, int h, double scale,
                                                 CView *content, IPlatformFrameConfig *config)
{
    auto *c = conn();
    if (!c || !window_of(editor)) {
        content->forget();
        return nullptr;
    }
    auto *scr = screen_of(c);
    auto win = std::make_unique<X11PopupWindow>();
    win->win_ = xcb_generate_id(c);
    const uint32_t values[] = {scr->black_pixel, 1u}; // background, override-redirect
    xcb_create_window(c, XCB_COPY_FROM_PARENT, win->win_, scr->root, (int16_t)r.x, (int16_t)r.y,
                      (uint16_t)std::max(1, r.w), (uint16_t)std::max(1, r.h), 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
                      scr->root_visual, XCB_CW_BACK_PIXEL | XCB_CW_OVERRIDE_REDIRECT, values);
    // tells compositors it is a menu (shadow, no animation of a normal window)
    const xcb_atom_t type = atom(c, "_NET_WM_WINDOW_TYPE"), menu = atom(c, "_NET_WM_WINDOW_TYPE_POPUP_MENU");
    if (type != XCB_ATOM_NONE && menu != XCB_ATOM_NONE)
        xcb_change_property(c, XCB_PROP_MODE_REPLACE, win->win_, type, XCB_ATOM_ATOM, 32, 1, &menu);

    auto *frame = new CFrame(CRect(0, 0, w, h), nullptr);
    if (!frame->open(reinterpret_cast<void *>((uintptr_t)win->win_), PlatformType::kX11EmbedWindowID, config)) {
        frame->forget();
        content->forget();
        return nullptr; // ~X11PopupWindow destroys win_ (destroying it here too gave a BadWindow error)
    }
    win->frame_ = frame;
    if (scale != 1.0)
        frame->setZoom(scale);
    frame->addView(content);
    // VSTGUI maps its child window only for an XEmbed host; ours is the parent
    if (auto child = window_of(frame))
        xcb_map_window(c, child);
    xcb_flush(c);
    return win;
}

bool popup_screen(CFrame *editor, CPoint at, double content_scale, PopupScreen &out)
{
    auto *c = conn();
    const xcb_window_t w = window_of(editor);
    if (!c || !w)
        return false;
    auto *scr = screen_of(c);
    auto *tr = xcb_translate_coordinates_reply(c, xcb_translate_coordinates(c, w, scr->root, 0, 0), nullptr);
    if (!tr)
        return false;
    const double zoom = editor->getZoom();
    out.anchor_x = tr->dst_x + (int)std::lround(at.x * zoom);
    out.anchor_y = tr->dst_y + (int)std::lround(at.y * zoom);
    free(tr);
    out.scale = content_scale > 0 ? content_scale : 1.0;
    // the work area of the monitor at the anchor (workarea.h): _NET_WORKAREA alone is one box around all monitors
    const NativeRect screen{0, 0, scr->width_in_pixels, scr->height_in_pixels};
    const auto desk = cardinals(c, scr->root, "_NET_CURRENT_DESKTOP", 1);
    // a desktop number from the root window as is: 0xFFFFFFFF wrapped 4 * (d + 1) to 0 and the read below went far
    // out of bounds (review 2026-10-19); no window manager has more than a few dozen
    const uint32_t d = desk.empty() || desk[0] > 1023 ? 0 : desk[0];
    const auto net = cardinals(c, scr->root, "_NET_WORKAREA", 4 * (d + 1)); // x, y, w, h per desktop
    const auto box = net.size() >= 4 * (d + 1) ? parse_work_areas(net.data() + 4 * d, 4) : std::vector<NativeRect>{};
    const NativeRect desktop = box.empty() ? screen : box[0];
    // mutter (GNOME) publishes each monitor's work area; other window managers: the RandR monitors cut to the box
    const auto gtk = cardinals(c, scr->root, ("_GTK_WORKAREAS_D" + std::to_string(d)).c_str(), 64);
    auto areas = parse_work_areas(gtk.data(), gtk.size());
    if (areas.empty())
        areas = clip_monitors(randr_monitors(c, scr->root), box.empty() ? NativeRect{} : desktop);
    out.work = work_area_at(areas, out.anchor_x, out.anchor_y, desktop);
    return true;
}

bool popup_pointer(CFrame *, int &x, int &y, bool &down)
{
    auto *c = conn();
    if (!c)
        return false;
    auto *p = xcb_query_pointer_reply(c, xcb_query_pointer(c, screen_of(c)->root), nullptr);
    if (!p)
        return false;
    x = p->root_x;
    y = p->root_y;
    down = (p->mask & (XCB_BUTTON_MASK_1 | XCB_BUTTON_MASK_2 | XCB_BUTTON_MASK_3)) != 0;
    free(p);
    return true;
}

// the window manager's active window (_NET_ACTIVE_WINDOW on the root; XWayland keeps it too and sets it to None while
// a Wayland application has the focus): Alt-Tab to another window changes it (round 0.8.22 test L7)
uintptr_t popup_focus_token(CFrame *)
{
    auto *c = conn();
    if (!c)
        return 0;
    static const xcb_atom_t active = atom(c, "_NET_ACTIVE_WINDOW");
    if (active == XCB_ATOM_NONE)
        return 0;
    auto *p = xcb_get_property_reply(c, xcb_get_property(c, 0, screen_of(c)->root, active, XCB_ATOM_WINDOW, 0, 1),
                                     nullptr);
    if (!p)
        return 0;
    uintptr_t token = 0; // no such property: the window manager cannot tell
    if (p->type == XCB_ATOM_WINDOW && xcb_get_property_value_length(p) >= 4)
        token = 1 + *static_cast<const uint32_t *>(xcb_get_property_value(p)); // None (0) counts as a window too
    free(p);
    return token;
}

int popup_submenu_delay() { return 400; }

} // namespace vti
