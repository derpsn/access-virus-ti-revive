// viruseditor.cpp - see viruseditor.h.
#include "viruseditor.h"
#include "model/sysinfo.h"
#if SMTG_OS_WINDOWS
#include <windows.h>
#include <shellapi.h>
#else
#include <spawn.h>
#include <sys/wait.h>
#include <thread>
extern char **environ;
#endif
#include "virusplugin.h"
#include "vstgui/lib/cbitmap.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cfileselector.h"
#include "vstgui/lib/coffscreencontext.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/controls/ctextedit.h"
#include "vstgui/lib/controls/itexteditlistener.h"
#include "vstgui/lib/cvstguitimer.h"
#include "vstgui/lib/events.h"
#include "vstgui/lib/platform/iplatformfont.h"
#include "vstgui/lib/platform/platformfactory.h"
#include "access/library.h"
#include "cc/diag.h"
#include "editor/helptext.h"
#include "editor/dragsend.h"
#include "editor/textfield.h"
#include "editor/popupmenu.h"
#include "popupwindow.h"
#include "editor/upscale.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cgraphicspath.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <functional>
#include <list>
#include <set>
#include <cmath>
#if SMTG_OS_LINUX
// server-side (X pixmap) copies of the bitmaps: VSTGUI keeps CPU image surfaces and uploads their pixels on every
// draw into the X back buffer (~67 ms per full repaint on a real X server / XWayland vs ~8 ms on Xvfb). Cairo::Bitmap
// has no way to wrap another surface, so its members are opened up for this file only.
#define private public
#include "vstgui/lib/platform/linux/cairobitmap.h"
#undef private
#include "vstgui/lib/platform/linux/x11platform.h"
#include <cairo/cairo-xcb.h>
#include <xcb/xcb.h>
#include "public.sdk/source/vst/vstgui_linux_runloop_support.h"
#include "vstgui/lib/platform/platform_x11.h"
#include "vstgui/lib/platform/linux/linuxfactory.h"
#include "vstgui/lib/platform/iplatformframe.h"
#include "vstgui/lib/cfont.h"
#endif

using namespace VSTGUI;

// opens a link in the default browser, without a shell (INFO box)
static void open_url(const char *url)
{
#if SMTG_OS_WINDOWS
    ShellExecuteA(nullptr, "open", url, nullptr, nullptr, SW_SHOWNORMAL);
#else
    const char *tool = SMTG_OS_MACOS ? "open" : "xdg-open";
    char *argv[] = {const_cast<char *>(tool), const_cast<char *>(url), nullptr};
    pid_t pid;
    if (posix_spawnp(&pid, tool, nullptr, nullptr, argv, environ) == 0)
        std::thread([pid] { waitpid(pid, nullptr, 0); }).detach(); // reap it (open/xdg-open return quickly)
#endif
}

namespace vti {

static bool env_debug()
{
    static const bool on = getenv("VTI_NATIVE_DEBUG") != nullptr; // read once (called on hot paths)
    return on;
}

static bool env_no_partial() // fallback / test reference: value changes repaint everything (as before 2026-10-14)
{
    static const bool on = getenv("VTI_NATIVE_NO_PARTIAL") != nullptr;
    return on;
}

// the 1x images only, also on scaled displays (reference / fallback for the sharper copies, skin option A)
static bool env_no_hidpi()
{
    static const bool on = getenv("VTI_NATIVE_NO_HIDPI") != nullptr;
    return on;
}

static bool env_drawtime()
{
    static const bool on = getenv("VTI_NATIVE_DRAWTIME") != nullptr; // read once (called on hot paths)
    return on;
}


// Canvas on a VSTGUI draw context; bitmaps are created once from the user's PNGs.
#if SMTG_OS_LINUX
static cairo_surface_t *ref = nullptr; // a 1x1 surface on a frame's window: create_similar makes pixmaps
static int open_frames = 0;            // editors with an X11 frame in this process

// VSTGUI's X11 code reaches the host's run loop through the platform factory's current adapter on every call and keeps
// no reference of its own; the adapter owns the handler wrappers it registered with the host. The SDK installs a new
// adapter at every factory setHostContext, and open() did at every editor: with a second instance or editor the
// first one's adapter was freed with its wrappers, and hosts that keep plain pointers (REAPER) called a freed handler
// (crash at a duplicated track, E183). The adapter of the first open editor stays installed while editors are open.
static SharedPointer<IRunLoop> pinned_runloop;

static void keep_runloop() // UI thread: undo a replacement since the last call
{
    if (!pinned_runloop)
        return;
    if (auto lf = getPlatformFactory().asLinuxFactory(); lf && lf->getRunLoop() != pinned_runloop)
        lf->setRunLoop(pinned_runloop);
}

// VSTGUI closes its X connection when the last frame goes (X11::RunLoop::exit) without cairo_device_finish: cairo
// keeps that connection's data under its address, and the next editor's new connection often gets the same address
// - an assertion in cairo-xcb-screen.c (_get_screen_index) aborted the host on reopening the editor (REAPER,
// 2026-10-16; since 0.8.0). `ref` also belonged to the old connection. Called before the last frame is destroyed.
static void x11_last_frame_closing()
{
    if (ref) {
        cairo_surface_destroy(ref);
        ref = nullptr;
    }
    auto *conn = X11::RunLoop::instance().getXcbConnection();
    if (!conn)
        return;
    // the device belongs to the connection; a 1x1 surface on the root window gives it without a frame window
    xcb_screen_t *sc = xcb_setup_roots_iterator(xcb_get_setup(conn)).data;
    xcb_visualtype_t *vt = nullptr;
    for (auto d = xcb_screen_allowed_depths_iterator(sc); d.rem && !vt; xcb_depth_next(&d))
        for (auto v = xcb_depth_visuals_iterator(d.data); v.rem; xcb_visualtype_next(&v))
            if (v.data->visual_id == sc->root_visual) {
                vt = v.data;
                break;
            }
    if (!vt)
        return;
    cairo_surface_t *s = cairo_xcb_surface_create(conn, sc->root, vt, 1, 1);
    cairo_device_t *dev = cairo_device_reference(cairo_surface_get_device(s));
    cairo_surface_destroy(s);
    if (dev) {
        cairo_device_finish(dev); // flushes and forgets the connection; its surfaces left over are freed later
        cairo_device_destroy(dev);
    }
}

// an X pixmap copy of `bmp` for drawing into the frame's window (server-side composite, GPU with glamor)
static SharedPointer<CBitmap> server_bitmap(CFrame *frame, CBitmap *bmp)
{
    if (!frame || !bmp || getenv("VTI_NATIVE_NO_PIXMAPS"))
        return nullptr;
    auto *cb = dynamic_cast<Cairo::Bitmap *>(bmp->getPlatformBitmap().get());
    if (!cb || !cb->surface || cairo_surface_get_type(cb->surface) != CAIRO_SURFACE_TYPE_IMAGE)
        return nullptr;
    if (!ref) {
        auto *conn = X11::RunLoop::instance().getXcbConnection();
        auto win = (xcb_window_t)(uintptr_t)frame->getPlatformFrame()->getPlatformRepresentation();
        if (!conn || !win)
            return nullptr;
        auto *attr = xcb_get_window_attributes_reply(conn, xcb_get_window_attributes(conn, win), nullptr);
        if (!attr)
            return nullptr;
        xcb_visualtype_t *vt = nullptr;
        for (auto sc = xcb_setup_roots_iterator(xcb_get_setup(conn)); sc.rem && !vt; xcb_screen_next(&sc))
            for (auto d = xcb_screen_allowed_depths_iterator(sc.data); d.rem && !vt; xcb_depth_next(&d))
                for (auto v = xcb_depth_visuals_iterator(d.data); v.rem; xcb_visualtype_next(&v))
                    if (v.data->visual_id == attr->visual) {
                        vt = v.data;
                        break;
                    }
        free(attr);
        if (!vt)
            return nullptr;
        ref = cairo_xcb_surface_create(conn, win, vt, 1, 1);
    }
    int w = cairo_image_surface_get_width(cb->surface), h = cairo_image_surface_get_height(cb->surface);
    cairo_surface_t *srv = cairo_surface_create_similar(ref, CAIRO_CONTENT_COLOR_ALPHA, w, h);
    if (cairo_surface_status(srv) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(srv);
        return nullptr;
    }
    cairo_t *cr = cairo_create(srv);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_surface(cr, cb->surface, 0, 0);
    cairo_paint(cr);
    cairo_destroy(cr);
    cairo_surface_flush(srv);
    auto nb = makeOwned<Cairo::Bitmap>();
    nb->surface = Cairo::SurfaceHandle(srv);
    nb->size = CPoint(w, h);
    nb->scaleFactor = cb->scaleFactor;
    return makeOwned<CBitmap>(nb);
}
#endif

// Images taller than this are split into tiles: Direct2D cannot create a bitmap over 16384 px on a side, and the
// big knob filmstrips are 16512 px tall (they stayed invisible on Windows). VTI_NATIVE_TILE overrides it (tests).
static int tile_limit()
{
    static int v = [] {
        const char *e = getenv("VTI_NATIVE_TILE");
        return e && atoi(e) > 0 ? atoi(e) : 16384;
    }();
    return v;
}

// rows [y0, y0 + h) of `bmp` as a new bitmap (same platform pixel format)
static SharedPointer<CBitmap> crop_rows(CBitmap *bmp, int y0, int h)
{
    auto src = bmp->getPlatformBitmap();
    int w = (int)bmp->getWidth();
    auto pb = getPlatformFactory().createBitmap(CPoint(w, h));
    if (!src || !pb)
        return nullptr;
    auto in = src->lockPixels(false);
    auto out = pb->lockPixels(false);
    if (!in || !out)
        return nullptr;
    for (int y = 0; y < h; y++)
        memcpy(out->getAddress() + (size_t)y * out->getBytesPerRow(),
               in->getAddress() + (size_t)(y0 + y) * in->getBytesPerRow(), (size_t)w * 4);
    return makeOwned<CBitmap>(pb);
}

// the pixels of `pb` (premultiplied) in the platform's channel order `fmt`
static vti::Image read_pixels(IPlatformBitmap *pb, IPlatformBitmapPixelAccess::PixelFormat &fmt)
{
    vti::Image im;
    auto in = pb ? pb->lockPixels(true) : nullptr;
    if (!in)
        return im;
    fmt = in->getPixelFormat();
    im.w = (int)pb->getSize().x, im.h = (int)pb->getSize().y;
    im.px.resize((size_t)im.w * im.h * 4);
    for (int y = 0; y < im.h; y++)
        memcpy(&im.px[(size_t)y * im.w * 4], in->getAddress() + (size_t)y * in->getBytesPerRow(), (size_t)im.w * 4);
    return im;
}

// rows [y0, y1) of `im` as a platform bitmap drawn at `scale` (its logical size = pixels / scale); nullptr if the
// platform stores another channel order than `fmt`
static PlatformBitmapPtr make_bitmap(const vti::Image &im, int y0, int y1, double scale,
                                     IPlatformBitmapPixelAccess::PixelFormat fmt)
{
    auto pb = getPlatformFactory().createBitmap(CPoint(im.w, y1 - y0));
    if (!pb)
        return nullptr;
    {
        auto out = pb->lockPixels(true);
        if (!out || out->getPixelFormat() != fmt)
            return nullptr;
        for (int y = y0; y < y1; y++)
            memcpy(out->getAddress() + (size_t)(y - y0) * out->getBytesPerRow(), &im.px[(size_t)y * im.w * 4],
                   (size_t)im.w * 4);
    } // unlocked: the pixels are written back (Direct2D)
    pb->setScaleFactor(scale);
    return pb;
}

class VstguiCanvas : public Canvas {
public:
    VstguiCanvas(const AccessFiles &files, std::map<std::string, SharedPointer<CBitmap>> &cache)
        : files_(files), cache_(cache)
    {
    }
    CDrawContext *ctx = nullptr;
    CFrame *frame = nullptr; // set: drawing into the window - use the server-side copies (Linux)
    std::map<std::string, SharedPointer<CBitmap>> server_; // X pixmap copies

    void *image(const std::string &name) override
    {
        if (name.empty())
            return nullptr;
        auto it = cache_.find(name);
        if (it == cache_.end()) {
            SharedPointer<CBitmap> bmp;
            const auto &d = files_.get(name);
            if (!d.empty())
                if (auto pb = getPlatformFactory().createBitmapFromMemory(d.data(), (uint32_t)d.size()))
                    bmp = makeOwned<CBitmap>(pb);
            const int lim = tile_limit();
            if (bmp && bmp->getHeight() > lim) {
                std::vector<SharedPointer<CBitmap>> t;
                for (int y = 0; y < (int)bmp->getHeight(); y += lim)
                    t.push_back(crop_rows(bmp, y, std::min(lim, (int)bmp->getHeight() - y)));
                bool ok = true;
                for (auto &b : t)
                    ok = ok && b;
                if (env_debug())
                    fprintf(stderr, "editor: %s %dx%d split into %zu tiles%s\n", name.c_str(), (int)bmp->getWidth(),
                            (int)bmp->getHeight(), t.size(), ok ? "" : " - FAILED, drawn whole");
                if (ok)
                    tiles_[bmp.get()] = {lim, std::move(t)};
            }
            it = cache_.emplace(name, bmp).first;
        }
#if SMTG_OS_LINUX
        if (frame && it->second && !tiles_.count(it->second.get())) {
            auto sv = server_.find(name);
            if (sv == server_.end()) {
                sv = server_.emplace(name, server_bitmap(frame, it->second.get())).first;
                if (sv->second)
                    base_[sv->second.get()] = it->second.get();
            }
            if (sv->second)
                return sv->second.get();
        }
#endif
        return it->second.get();
    }
    int image_width(void *i) override { return (int)((CBitmap *)i)->getWidth(); }
    int image_height(void *i) override { return (int)((CBitmap *)i)->getHeight(); }
    void blit(void *img, int sx, int sy, int dx, int dy, int w, int h, double alpha) override
    {
        if (scale > 1.01 && !sharp_.count((CBitmap *)img))
            sharpen((CBitmap *)img);
        auto t = tiles_.find((CBitmap *)img);
        if (t == tiles_.end()) {
            ((CBitmap *)img)->draw(ctx, CRect(dx, dy, dx + w, dy + h), CPoint(sx, sy), (float)alpha);
            return;
        }
        const int lim = t->second.h; // a source rectangle can straddle two tiles
        while (h > 0) {
            int i = sy / lim, off = sy - i * lim, n = std::min(h, lim - off);
            if (i >= (int)t->second.t.size())
                break;
            t->second.t[i]->draw(ctx, CRect(dx, dy, dx + w, dy + n), CPoint(sx, off), (float)alpha);
            sy += n, dy += n, h -= n;
        }
    }
    void frames(void *img, int frame_h) override
    {
        if (img && frame_h > 0)
            frame_h_.emplace((CBitmap *)img, frame_h);
    }

    // device pixels per editor pixel (display scaling x editor size). Above 1 the images are drawn from copies
    // enlarged to it (vti::upscale), made when first drawn; a new scale drops every bitmap made so far.
    double scale = 1;
    void set_scale(double s)
    {
        if (s == scale)
            return;
        scale = s;
        cache_.clear(), tiles_.clear(), server_.clear(), base_.clear(), sharp_.clear(), frame_h_.clear();
    }
    void fill(int x, int y, int w, int h, Rgba c) override
    {
        ctx->setFillColor(CColor(c.r, c.g, c.b, c.a));
        ctx->drawRect(CRect(x, y, x + w, y + h), kDrawFilled);
    }
    void label(int x, int y, int w, int h, const std::string &text, int size, Rgba c) override
    {
        auto font = makeOwned<CFontDesc>(kSystemFont->getName(), size);
        ctx->setFont(font);
        ctx->setFontColor(CColor(c.r, c.g, c.b, c.a));
        ctx->drawString(text.c_str(), CRect(x, y, x + w, y + h), kLeftText, true);
    }
    void line(double x0, double y0, double x1, double y1, Rgba c) override
    {
        ctx->setFrameColor(CColor(c.r, c.g, c.b, c.a));
        ctx->setLineWidth(1);
        ctx->drawLine(CPoint(x0, y0), CPoint(x1, y1));
    }
    void push_clip(int x, int y, int w, int h) override
    {
        CRect old;
        ctx->getClipRect(old);
        clips_.push_back(old);
        CRect r(x, y, x + w, y + h);
        r.bound(old);
        ctx->setClipRect(r);
    }
    void pop_clip() override
    {
        if (!clips_.empty()) {
            ctx->setClipRect(clips_.back());
            clips_.pop_back();
        }
    }

private:
    const AccessFiles &files_;
    std::map<std::string, SharedPointer<CBitmap>> &cache_;
    struct Tiles {
        int h; // logical rows per tile
        std::vector<SharedPointer<CBitmap>> t;
    };
    std::map<CBitmap *, Tiles> tiles_;     // of the images in cache_ over tile_limit()
    std::map<CBitmap *, CBitmap *> base_;  // server copy -> the image in cache_ it was made from
    std::set<CBitmap *> sharp_;            // drawn bitmaps that have their copy at `scale` (or cannot have one)
    std::map<CBitmap *, int> frame_h_;     // frames() hints
    std::vector<CRect> clips_;

    // adds the copy at `scale` to `drawn`: to the image itself (offscreen drawing), to its server copy (Linux
    // window), or as new tiles (images over the Direct2D limit, which an enlarged copy exceeds even sooner)
    void sharpen(CBitmap *drawn)
    {
        sharp_.insert(drawn);
        if (env_no_hidpi())
            return;
        auto b = base_.find(drawn);
        CBitmap *src = b != base_.end() ? b->second : drawn;
        auto fh = frame_h_.find(drawn);
        const int f = fh != frame_h_.end() ? fh->second : 0;
        auto t0 = std::chrono::steady_clock::now();
        auto t = tiles_.find(src);
        PlatformBitmapPtr have = src->getBestPlatformBitmapForScaleFactor(scale);
        if (!have || have->getScaleFactor() != scale)
            have = nullptr; // the image's own copy, made when the offscreen drew it first
        vti::Image im, hi;
        auto fmt = IPlatformBitmapPixelAccess::kBGRA;
        if (t != tiles_.end() || !have) {
            im = read_pixels(src->getPlatformBitmap().get(), fmt);
            if (im.w <= 0)
                return;
            const int alpha = fmt == IPlatformBitmapPixelAccess::kARGB || fmt == IPlatformBitmapPixelAccess::kABGR ? 0 : 3;
            hi = vti::upscale(im, scale, f, alpha);
        }
        if (t != tiles_.end()) {
            // tiles of at most tile_limit() device pixels, cut at frame boundaries where the frames allow it
            int th = std::max(1, (int)(tile_limit() / scale));
            if (f > 0 && th >= f)
                th = th / f * f;
            std::vector<SharedPointer<CBitmap>> nt;
            for (int y = 0; y < im.h; y += th) {
                const int n = std::min(th, im.h - y);
                auto tile = crop_rows(src, y, n);
                int y0 = std::min(hi.h, (int)std::lround(y * scale)), y1 = std::min(hi.h, (int)std::lround((y + n) * scale));
                auto pb = y1 > y0 ? make_bitmap(hi, y0, y1, scale, fmt) : nullptr;
                if (!tile || !pb)
                    return; // keep the 1x tiles
                tile->addBitmap(pb);
                nt.push_back(tile);
            }
            t->second = {th, std::move(nt)};
        } else {
            if (!have) {
                have = make_bitmap(hi, 0, hi.h, scale, fmt);
                if (!have)
                    return;
                src->addBitmap(have);
            }
#if SMTG_OS_LINUX
            if (drawn != src && frame) { // the server copy gets its own enlarged pixmap
                auto tmp = makeOwned<CBitmap>(have);
                if (auto sv = server_bitmap(frame, tmp))
                    drawn->addBitmap(sv->getPlatformBitmap());
            }
#endif
        }
        if (env_drawtime() && im.w > 0)
            fprintf(stderr, "editor: %dx%d image enlarged x%.2f in %.1f ms\n", im.w, im.h, scale,
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    }
};

// runs `fn` once the frame has finished the current event (a view must not remove itself while handling it), or now
static void after_event(CFrame *frame, std::function<void()> fn)
{
    if (!frame->doAfterEventProcessing(fn))
        fn();
}

// draws one menu of a cascade with its top-left corner at (x0, y0) like the original's Windows 10 menus: with columns
// in the older look (E194), a single column in the themed look (E219); of its rows only those inside `update` (a hover
// repaints two rows, not 100 texts)
static void draw_popup_menu(CDrawContext *ctx, CFontDesc *font, const CRect &update, const vti::PopupMenu &menu,
                            double x0, double y0)
{
    using vti::PopupMenu;
    const bool themed = menu.themed();
    const CColor back = themed ? CColor(242, 242, 242, 255) : CColor(240, 240, 240, 255),
                 border = themed ? CColor(204, 204, 204, 255) : CColor(160, 160, 160, 255),
                 gutter(240, 240, 240, 255), line(215, 215, 215, 255), light(255, 255, 255, 255),
                 hot(145, 201, 247, 255), checkbox(144, 200, 246, 255), text(0, 0, 0, 255), grey(109, 109, 109, 255),
                 mark(51, 51, 51, 255), chevron(62, 62, 62, 255);
    if (!CRect(x0, y0, x0 + menu.width(), y0 + menu.height()).rectOverlap(update))
        return;
    ctx->setDrawMode(kAliasing);
    ctx->setLineWidth(1);
    ctx->setFillColor(border);
    ctx->drawRect(CRect(x0, y0, x0 + menu.width(), y0 + menu.height()), kDrawFilled);
    ctx->setFillColor(back);
    ctx->drawRect(CRect(x0 + 1, y0 + 1, x0 + menu.width() - 1, y0 + menu.height() - 1), kDrawFilled);
    if (themed) { // the check mark band left of the texts, as high as the rows, and a 3 px strip of its colour at the
                  // bottom of the last row (E220: in the original's menus and in Windows' own)
        const double f = PopupMenu::kThemedFrame;
        ctx->setFillColor(gutter);
        ctx->drawRect(CRect(x0 + f, y0 + f, x0 + f + PopupMenu::kThemedGutter, y0 + menu.height() - f), kDrawFilled);
        ctx->drawRect(CRect(x0 + f, y0 + menu.height() - f - 3, x0 + menu.width() - f, y0 + menu.height() - f),
                      kDrawFilled);
    }
    // between the columns a dark and a light line, the full height inside the border
    for (int c = 1; c < menu.columns(); c++) {
        const double sx = x0 + menu.column_x()[c] - PopupMenu::kSeparator;
        ctx->setFillColor(border);
        ctx->drawRect(CRect(sx, y0 + 1, sx + 1, y0 + menu.height() - 1), kDrawFilled);
        ctx->setFillColor(light);
        ctx->drawRect(CRect(sx + 1, y0 + 1, sx + 2, y0 + menu.height() - 1), kDrawFilled);
    }
    ctx->setFont(font);
    const auto &items = menu.items();
    for (int i = 0; i < (int)items.size(); i++) {
        auto r = menu.item_rect(i);
        const CRect row(x0 + r.x, y0 + r.y, x0 + r.x + r.w, y0 + r.y + r.h);
        if (!row.rectOverlap(update))
            continue;
        if (items[i].separator) { // themed: one line after the band; columns: a dark and a light line
            if (themed) {
                ctx->setFillColor(line);
                ctx->drawRect(CRect(row.left + PopupMenu::kThemedGutter, row.top + 3, row.right, row.top + 4),
                              kDrawFilled);
            } else {
                ctx->setFillColor(border);
                ctx->drawRect(CRect(row.left + 1, row.top + 3, row.right - 1, row.top + 4), kDrawFilled);
                ctx->setFillColor(light);
                ctx->drawRect(CRect(row.left + 1, row.top + 4, row.right - 1, row.top + 5), kDrawFilled);
            }
            continue;
        }
        if (i == menu.hot) {
            ctx->setFillColor(hot);
            ctx->drawRect(row, kDrawFilled);
        }
        const CColor ink = items[i].enabled ? text : grey;
        if (i == menu.checked()) { // a tick before the text; themed: on a blue square filling the band's row
            ctx->setDrawMode(kAntiAliasing);
            ctx->setLineWidth(1.6);
            if (themed) {
                ctx->setFillColor(checkbox);
                ctx->drawRect(CRect(row.left, row.top, row.left + r.h, row.bottom), kDrawFilled);
                ctx->setFrameColor(mark);
                ctx->drawLine(CPoint(row.left + 6.5, row.top + 10), CPoint(row.left + 9.5, row.top + 13));
                ctx->drawLine(CPoint(row.left + 9.5, row.top + 13), CPoint(row.left + 16, row.top + 6.5));
            } else {
                ctx->setFrameColor(text);
                ctx->drawLine(CPoint(row.left + 6, row.top + 9.5), CPoint(row.left + 8.5, row.top + 12));
                ctx->drawLine(CPoint(row.left + 8.5, row.top + 12), CPoint(row.left + 13, row.top + 6));
            }
            ctx->setDrawMode(kAliasing);
            ctx->setLineWidth(1);
        }
        if (items[i].has_sub()) { // the submenu sign after the text: themed a chevron, columns a filled arrow
            ctx->setDrawMode(kAntiAliasing);
            if (themed) {
                const double ax = row.right - 13.5, ay = row.top + 7.5;
                ctx->setLineWidth(1);
                ctx->setFrameColor(items[i].enabled ? chevron : grey);
                ctx->drawLine(CPoint(ax, ay), CPoint(ax + 3.5, ay + 3.5));
                ctx->drawLine(CPoint(ax + 3.5, ay + 3.5), CPoint(ax, ay + 7));
            } else if (auto arrow = owned(ctx->createGraphicsPath())) {
                const double ax = row.right - 13, ay = row.top + 6;
                arrow->beginSubpath(CPoint(ax, ay));
                arrow->addLine(CPoint(ax + 4, ay + 3.5));
                arrow->addLine(CPoint(ax, ay + 7));
                arrow->closeSubpath();
                ctx->setFillColor(ink);
                ctx->drawGraphicsPath(arrow, CDrawContext::kPathFilled);
            }
            ctx->setDrawMode(kAliasing);
        }
        ctx->setFontColor(ink);
        CRect tr(row);
        tr.left += themed ? PopupMenu::kThemedTextLeft : PopupMenu::kTextLeft;
        ctx->drawString(items[i].text.c_str(), tr, kLeftText, true);
    }
}

static long long now_ms()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

class PopupView;

// one open menu (the root or a submenu) in its own window (popupwindow.h): draws it and passes the mouse to the
// PopupView it currently serves (none while its window is hidden in the pool); coordinates are the menu's own
class PopupLevelView : public CView {
public:
    PopupLevelView() : CView(CRect(0, 0, 1, 1)) {}
    void attach(PopupView *owner, int level)
    {
        owner_ = owner;
        level_ = level;
    }
    void draw(CDrawContext *ctx) override { drawRect(ctx, getViewSize()); }
    void drawRect(CDrawContext *ctx, const CRect &update) override;
    void onMouseMoveEvent(MouseMoveEvent &event) override;
    // not consumed: VSTGUI on X11 moves the keyboard focus to a frame whose view consumed the press (Windows: see
    // popupwindow_win.cpp)
    void onMouseDownEvent(MouseDownEvent &event) override;
    void onMouseUpEvent(MouseUpEvent &event) override { event.consumed = true; }
    void onMouseWheelEvent(MouseWheelEvent &event) override { event.consumed = true; }
    void onKeyboardEvent(KeyboardEvent &event) override;

private:
    PopupView *owner_ = nullptr;
    int level_ = 0;
};

// the menu windows of one editor and the timer of its menus, kept from the first menu until the editor closes
// (VirusEditor::close). Nothing of it is destroyed or stopped while the editor is open: REAPER on Linux keeps plain
// pointers to the timers it was given and called a freed one when a menu closed and destroyed its windows or timers
// from a timer callback (round 0.8.22, crash on choosing a preset; same host behaviour as E183). Windows of closed
// menus are hidden and reused.
struct PopupHost {
    struct Slot {
        std::unique_ptr<vti::PopupWindow> win;
        PopupLevelView *view = nullptr; // owned by the window's frame
        vti::PopupMenu::Rect at;        // the menu it shows now (menu pixels in the work area)
        int from = -2;                  // that menu's entry in its parent; -2: free (hidden)
    };
    std::vector<Slot> slots;
    SharedPointer<CVSTGUITimer> timer;
    PopupView *active = nullptr;
    ~PopupHost()
    {
        if (timer)
            timer->stop();
        slots.clear();
    }
    static std::map<CFrame *, std::shared_ptr<PopupHost>> &all()
    {
        static std::map<CFrame *, std::shared_ptr<PopupHost>> hosts;
        return hosts;
    }
    static std::shared_ptr<PopupHost> of(CFrame *frame)
    {
        auto &h = all()[frame];
        if (!h)
            h = std::make_shared<PopupHost>();
        return h;
    }
    static void close(CFrame *frame) { all().erase(frame); } // the editor closes: windows and timer go
};

// our popup menu (editor/popupmenu.h), drawn like the original's Windows menus with columns (E194) on every
// platform, submenus included (E195). Each open menu is a window of its own (popupwindow.h) placed on the screen like
// the original's (round 0.8.22 L4); this view is a modal, invisible view over the whole editor that takes the keys
// and a click into the editor (closes the menu). Without such windows (VTI_NATIVE_INLINE_POPUP, or the platform
// cannot make one) the menus are drawn inside the editor by this view, as before. A click on an entry or Return calls
// `chosen` with the entry's path (its index in each menu from the root); a click elsewhere or Esc on the root closes
// the menu without one.
class PopupView : public CView {
public:
    static void open(CFrame *frame, const CRect &area, double content_scale, std::vector<vti::PopupItem> items,
                     int checked, int per_column, CPoint at, std::function<void(const std::vector<int> &)> chosen,
                     bool inline_only = false)
    {
        if (!frame || items.empty())
            return;
        auto font = popup_font();
        auto width = [font](const std::string &s) {
            auto str = getPlatformFactory().createString(s.c_str());
            auto *painter = font->getPlatformFont() ? font->getPlatformFont()->getPainter() : nullptr;
            return painter ? (int)std::ceil(painter->getStringWidth(nullptr, str, true)) : 7 * (int)s.size();
        };
        static const bool env_inline = getenv("VTI_NATIVE_INLINE_POPUP") != nullptr;
        vti::PopupScreen scr, top_left, bottom_right;
        const bool windowed = !inline_only && !env_inline && popup_screen(frame, at, content_scale, scr) &&
                              popup_screen(frame, area.getTopLeft(), content_scale, top_left) &&
                              popup_screen(frame, area.getBottomRight(), content_scale, bottom_right);
        // the menus' area in menu pixels: the monitor's work area, or the editor
        int aw = (int)area.getWidth(), ah = (int)area.getHeight(), ax = (int)at.x, ay = (int)at.y;
        if (windowed) {
            aw = (int)std::floor(scr.work.w / scr.scale);
            ah = (int)std::floor(scr.work.h / scr.scale);
            ax = (int)std::lround((scr.anchor_x - scr.work.x) / scr.scale);
            ay = (int)std::lround((scr.anchor_y - scr.work.y) / scr.scale);
        }
        auto saved = windowed ? items : std::vector<vti::PopupItem>{}; // for the fallback below
        vti::PopupCascade menus(std::move(items), checked, width, per_column, aw, ah);
        if (windowed)
            menus.set_submenu_delay(vti::popup_submenu_delay());
        menus.open_at(ax, ay);
        auto *v = new PopupView(area, std::move(menus), chosen, font);
        if (windowed) {
            v->host_ = PopupHost::of(frame);
            v->screen_ = scr;
            v->editor_rect_ = {top_left.anchor_x, top_left.anchor_y, bottom_right.anchor_x - top_left.anchor_x,
                               bottom_right.anchor_y - top_left.anchor_y};
            v->focus_ = vti::popup_focus_token(frame);
        }
        if (auto id = frame->beginModalViewSession(v)) { // the frame owns the view until the session ends
            v->session_ = *id;
            v->in_session_ = true;
        } else {
            v->forget();
            return;
        }
        if (windowed && !v->start_windows()) { // no window for the root menu: inside the editor after all
            v->done_ = true;
            frame->endModalViewSession(v->session_);
            open(frame, area, content_scale, std::move(saved), checked, per_column, at, std::move(chosen), true);
        }
    }
    // the editor closes: its menu windows and timer go (an open menu's view goes with the frame)
    static void close_windows(CFrame *frame) { PopupHost::close(frame); }

    ~PopupView() override { release_windows(); }

    void draw(CDrawContext *ctx) override { drawRect(ctx, getViewSize()); }
    // inside the editor: only the open menus (in windows the level views draw them)
    void drawRect(CDrawContext *ctx, const CRect &update) override
    {
        if (!windowed())
            for (int l = 0; l < menus_.levels(); l++)
                draw_popup_menu(ctx, font_, update, menus_.level(l), getViewSize().left + menus_.level(l).x,
                                getViewSize().top + menus_.level(l).y);
        setDirty(false);
    }

    void onMouseMoveEvent(MouseMoveEvent &event) override
    {
        if (!windowed())
            apply_dirty(menus_.hover(local_x(event.mousePosition), local_y(event.mousePosition), now_ms()));
        event.consumed = true;
    }
    void onMouseDownEvent(MouseDownEvent &event) override
    {
        if (windowed()) // a click into the editor: outside every menu window
            finish({}, true);
        else
            apply(menus_.click(local_x(event.mousePosition), local_y(event.mousePosition)), true);
        event.consumed = true;
    }
    void onMouseUpEvent(MouseUpEvent &event) override { event.consumed = true; }
    void onMouseWheelEvent(MouseWheelEvent &event) override { event.consumed = true; }
    void onKeyboardEvent(KeyboardEvent &event) override { key_event(event, true); }

    // from the level views (menu `level`'s own coordinates)
    void draw_level(CDrawContext *ctx, const CRect &update, int level)
    {
        if (level < menus_.levels())
            draw_popup_menu(ctx, font_, update, menus_.level(level), 0, 0);
    }
    void level_move(int level, const CPoint &p)
    {
        if (done_ || level >= menus_.levels())
            return;
        const auto &m = menus_.level(level);
        apply_dirty(menus_.hover(m.x + (int)std::floor(p.x), m.y + (int)std::floor(p.y), now_ms()));
    }
    void level_down(int level, const CPoint &p)
    {
        if (done_ || level >= menus_.levels())
            return;
        const auto &m = menus_.level(level);
        apply(menus_.click(m.x + (int)std::floor(p.x), m.y + (int)std::floor(p.y)), false);
    }
    void key_event(KeyboardEvent &event, bool own_event)
    {
        if (event.type != EventType::KeyDown || done_)
            return;
        using K = vti::PopupCascade::Key;
        switch (event.virt) {
        case VirtualKey::Escape: apply(menus_.key(K::Escape), own_event); break;
        case VirtualKey::Return:
        case VirtualKey::Enter: apply(menus_.key(K::Return), own_event); break;
        case VirtualKey::Up: apply(menus_.key(K::Up), own_event); break;
        case VirtualKey::Down: apply(menus_.key(K::Down), own_event); break;
        case VirtualKey::Left: apply(menus_.key(K::Left), own_event); break;
        case VirtualKey::Right: apply(menus_.key(K::Right), own_event); break;
        case VirtualKey::Home: apply(menus_.key(K::Home), own_event); break;
        case VirtualKey::End: apply(menus_.key(K::End), own_event); break;
        default: break;
        }
        event.consumed = true; // nothing reaches the editor while the menu is open
    }
    // the host's timer (every 15 ms while this menu is the active one)
    void on_timer()
    {
        if (done_)
            return;
        apply_dirty(menus_.tick(now_ms()));
        // what a native menu notices without an event of its own: a press outside the menus and the editor (another
        // window, the desktop) and a switch to another window
        int x = 0, y = 0;
        bool down = false;
        if (vti::popup_pointer(getFrame(), x, y, down)) {
            if (!down)
                armed_ = true; // the press that opened the menu is over
            else if (armed_ && !editor_rect_.contains(x, y)) {
                bool over = false;
                if (auto h = host_.lock())
                    for (auto &s : h->slots)
                        over |= s.from != -2 && native(s.at).contains(x, y);
                if (!over) {
                    finish({}, false);
                    return;
                }
            }
        }
        if (focus_ && vti::popup_focus_token(getFrame()) != focus_)
            finish({}, false);
    }

private:
    PopupView(const CRect &area, vti::PopupCascade menus, std::function<void(const std::vector<int> &)> chosen,
              SharedPointer<CFontDesc> font)
        : CView(area), menus_(std::move(menus)), chosen_(std::move(chosen)), font_(std::move(font))
    {
    }
    // the menu font: Windows' own menu font, a sans serif of the same size elsewhere
    static SharedPointer<CFontDesc> popup_font()
    {
#if SMTG_OS_WINDOWS
        return makeOwned<CFontDesc>("Segoe UI", 12);
#elif SMTG_OS_MACOS
        return makeOwned<CFontDesc>("Helvetica Neue", 12);
#else
        return makeOwned<CFontDesc>(kSystemFont->getName(), 12);
#endif
    }

    bool windowed() const { return !host_.expired(); }
    vti::NativeRect native(const vti::PopupMenu::Rect &r) const
    {
        const double s = screen_.scale;
        return {screen_.work.x + (int)std::lround(r.x * s), screen_.work.y + (int)std::lround(r.y * s),
                (int)std::lround(r.w * s), (int)std::lround(r.h * s)};
    }
    // becomes the host's active menu, starts its timer once (it runs until the editor closes) and shows the root
    bool start_windows()
    {
        auto h = host_.lock();
        if (!h)
            return false;
        if (h->active && h->active != this)
            h->active->release_windows();
        h->active = this;
        if (!sync_windows())
            return false;
        if (!h->timer) {
            CFrame *frame = getFrame();
            h->timer = makeOwned<CVSTGUITimer>(
                [frame](CVSTGUITimer *) {
                    auto &all = PopupHost::all();
                    auto it = all.find(frame);
                    if (it != all.end() && it->second->active)
                        it->second->active->on_timer();
                },
                15);
        }
        return true;
    }
    // a window for each open menu, from the host's pool: kept while its menu stays (moved when it moved), given the
    // new menu when another submenu took its place (and then all below it); the rest hidden. false when the root
    // menu has no window.
    bool sync_windows()
    {
        auto h = host_.lock();
        if (!h)
            return false;
#if SMTG_OS_LINUX
        keep_runloop();
        X11::FrameConfig cfg;
        if (auto lf = getPlatformFactory().asLinuxFactory())
            cfg.runLoop = lf->getRunLoop();
        IPlatformFrameConfig *config = &cfg;
#else
        IPlatformFrameConfig *config = nullptr;
#endif
        bool anew = false;
        for (int l = 0; l < menus_.levels(); l++) {
            const auto b = menus_.level(l).bounds();
            const int from = menus_.from(l);
            if (l >= (int)h->slots.size()) {
                PopupHost::Slot s;
                s.view = new PopupLevelView();
                s.win = vti::PopupWindow::create(getFrame(), native(b), b.w, b.h, screen_.scale, s.view, config);
                if (!s.win)
                    return l > 0; // deeper menus without a window stay invisible; the root falls back
                h->slots.push_back(std::move(s));
            }
            auto &s = h->slots[l];
            if (!anew && s.from == from && s.at.w == b.w && s.at.h == b.h) {
                if (s.at.x != b.x || s.at.y != b.y) {
                    s.win->move(native(b));
                    s.at = b;
                }
                continue;
            }
            anew = true;
            s.view->attach(this, l);
            fit(s, b);
            s.from = from;
            s.win->show();
        }
        for (int l = menus_.levels(); l < (int)h->slots.size(); l++)
            hide(h->slots[l]);
        return true;
    }
    // the slot's window and frame to the menu's place and size, all of it repainted
    void fit(PopupHost::Slot &s, const vti::PopupMenu::Rect &b)
    {
        CFrame *f = s.win->frame();
        s.win->move(native(b));
        if (f->getZoom() != screen_.scale)
            f->setZoom(screen_.scale);
        f->setSize(std::lround(b.w * screen_.scale), std::lround(b.h * screen_.scale));
        s.view->setViewSize(CRect(0, 0, b.w, b.h));
        s.view->setMouseableArea(CRect(0, 0, b.w, b.h));
        s.view->invalid();
        s.at = b;
    }
    static void hide(PopupHost::Slot &s)
    {
        if (s.from == -2)
            return;
        s.win->hide();
        s.view->attach(nullptr, 0);
        s.from = -2;
    }
    // hides this menu's windows and gives them back to the pool (nothing is destroyed)
    void release_windows()
    {
        auto h = host_.lock();
        if (!h || h->active != this)
            return;
        for (auto &s : h->slots)
            hide(s);
        h->active = nullptr;
    }

    // the view lies over the whole editor, but only the menus need repainting: adding and removing the view call
    // invalid(), and a full-size area would repaint the whole editor below (slow on the Mac, E194)
    void invalid() override
    {
        setDirty(false);
        if (!windowed())
            for (int l = 0; l < menus_.levels(); l++)
                repaint(menus_.level(l).bounds());
    }
    void repaint(const vti::PopupMenu::Rect &r)
    {
        if (!windowed()) {
            const CRect &v = getViewSize();
            invalidRect(CRect(v.left + r.x, v.top + r.y, v.left + r.x + r.w, v.top + r.y + r.h));
            return;
        }
        auto h = host_.lock();
        if (!h)
            return;
        for (auto &s : h->slots) { // the part inside each menu window, in its coordinates
            if (s.from == -2)
                continue;
            const int x0 = std::max(r.x, s.at.x), y0 = std::max(r.y, s.at.y);
            const int x1 = std::min(r.x + r.w, s.at.x + s.at.w), y1 = std::min(r.y + r.h, s.at.y + s.at.h);
            if (x0 < x1 && y0 < y1)
                s.view->invalidRect(CRect(x0 - s.at.x, y0 - s.at.y, x1 - s.at.x, y1 - s.at.y));
        }
    }
    void apply_dirty(const std::vector<vti::PopupMenu::Rect> &dirty)
    {
        if (dirty.empty())
            return;
        if (windowed())
            sync_windows();
        for (auto &r : dirty)
            repaint(r);
    }
    int local_x(const CPoint &p) const { return (int)std::floor(p.x - getViewSize().left); }
    int local_y(const CPoint &p) const { return (int)std::floor(p.y - getViewSize().top); }
    // `own_event`: called while the editor frame handles an event addressed to this view
    void apply(const vti::PopupCascade::Result &res, bool own_event)
    {
        apply_dirty(res.dirty);
        if (res.kind != vti::PopupCascade::Result::None)
            finish(res.kind == vti::PopupCascade::Result::Chosen ? res.path : std::vector<int>{}, own_event);
    }
    // closes the menu: its windows go back to the pool at once, the frame removes this view when the session ends
    // (after the current event when the view is handling one of its own); empty = no choice
    void finish(const std::vector<int> &path, bool own_event)
    {
        if (done_)
            return;
        done_ = true;
        release_windows();
        auto chosen = chosen_; // this view may be gone below
        CFrame *frame = getFrame();
        if (frame && in_session_) {
            ModalViewSessionID id = session_;
            if (own_event) {
                SharedPointer<CFrame> f(frame);
                after_event(frame, [f, id]() { f->endModalViewSession(id); });
            } else
                frame->endModalViewSession(id); // from a menu window or the timer: not inside this view
        }
        if (!path.empty() && chosen)
            chosen(path);
    }

    vti::PopupCascade menus_;
    std::function<void(const std::vector<int> &)> chosen_;
    SharedPointer<CFontDesc> font_;
    ModalViewSessionID session_ = 0;
    bool in_session_ = false, done_ = false, armed_ = false;
    std::weak_ptr<PopupHost> host_; // set: the menus are windows of their own
    vti::PopupScreen screen_;
    vti::NativeRect editor_rect_;
    uintptr_t focus_ = 0;
};

void PopupLevelView::drawRect(CDrawContext *ctx, const CRect &update)
{
    if (owner_)
        owner_->draw_level(ctx, update, level_);
    setDirty(false);
}
void PopupLevelView::onMouseMoveEvent(MouseMoveEvent &event)
{
    if (owner_)
        owner_->level_move(level_, event.mousePosition);
    event.consumed = true;
}
void PopupLevelView::onMouseDownEvent(MouseDownEvent &event)
{
    if (owner_)
        owner_->level_down(level_, event.mousePosition);
}
void PopupLevelView::onKeyboardEvent(KeyboardEvent &event)
{
    if (owner_)
        owner_->key_event(event, false);
}

// files dragged in from the desktop (Explorer, Finder, a file manager): VSTGUI hands them to the view under the
// cursor; the canvas decides what they mean (the browser's own drags never get here, they are mouse tracking)
class FileDropTarget : public IDropTarget, public NonAtomicReferenceCounted {
public:
    std::function<DragOperation(DragEventData)> move;
    std::function<void()> leave;
    std::function<bool(DragEventData)> drop;
    DragOperation onDragEnter(DragEventData d) override { return move(d); }
    DragOperation onDragMove(DragEventData d) override { return move(d); }
    void onDragLeave(DragEventData) override { leave(); }
    bool onDrop(DragEventData d) override { return drop(d); }
};

class EditorCanvasView : public CView, public IControlListener, public TextEditListenerAdapter, public IKeyboardHook,
                        public IMouseObserver {
public:
    EditorCanvasView(VirusPlugin *plugin, const CRect &r, VirusEditor *editor)
        : CView(r), plugin_(plugin), editor_(editor), renderer_(plugin->layout(), plugin->conditions(), plugin->device(), plugin->editor_device()),
          canvas_(plugin->files(), bitmaps_)
    {
        renderer_.values = [this](const std::string &path, int part) { return plugin_->ui_value(path, part); };
        renderer_.patch_name = [this](int part) { return plugin_->patch_name(part); };
        renderer_.bank_patch = [this](int bank, int prog) { return plugin_->bank_patch(bank, prog); };
        renderer_.live_text = [this](const std::string &p) { return plugin_->live_text(p); };
        renderer_.remote_name = [this](int t) { return plugin_->remote_name(t); };
        renderer_.proxy_target = [this](const std::string &p, int part) { return plugin_->softknob_target(p, part); };
        renderer_.meter = [this](int ch) { return plugin_->meter(ch); };
        renderer_.osc_waves = plugin_->osc_waves();
        // device changes (knobs on the Virus, automation) repaint the view
        timer_ = makeOwned<CVSTGUITimer>([this](CVSTGUITimer *) {
            if (!remove_later_.empty()) // text edits that ended outside event handling (remove_edit)
                if (auto *f = getFrame()) {
                    for (auto *te : remove_later_)
                        if (f->isChild(te)) // compares the pointer, never touches a removed view
                            f->removeView(te);
                    remove_later_.clear();
                }
#if SMTG_OS_LINUX
            keep_runloop(); // a new instance's setHostContext may have replaced it (E183)
#endif
            plugin_->diag_report();
            plugin_->tick_gestures(); // knob moves on the Virus -> host edits (E304)
            drop_switched_selections();
            if (plugin_->ui_changed() && !partial_) { // during a drag the drag repaints its own area
                plugin_->diag.dirty_ticks++;
                plugin_->lcd_changed();
                plugin_->values_changed();
                plugin_->take_changed();
                invalid();
            } else if (!partial_ && plugin_->values_changed()) {
                // automation / Virus panel: repaint the elements that read the changed values in the last full
                // repaint (deps_), and everything when a value decides what is shown (conditions, the panel part)
                bool overflow = false, full = deps_.rects.empty() || env_no_partial();
                auto changed = plugin_->take_changed(&overflow);
                for (auto &p : changed)
                    if (deps_.cond.count(p) || p == plugin_->layout().frame_parameter)
                        full = true;
                if (full || overflow) {
                    plugin_->diag.dirty_ticks++;
                    invalid();
                } else {
                    for (auto &p : changed) {
                        auto it = deps_.rects.find(p);
                        if (it != deps_.rects.end())
                            for (auto &rc : it->second)
                                invalidRect(CRect(rc[0] - 2, rc[1] - 2, rc[0] + rc[2] + 2, rc[1] + rc[3] + 2));
                    }
                    if (plugin_->lcd_changed())
                        invalidRect(CRect(200, 470, 560, 512));
                }
            } else if (plugin_->lcd_changed() && !partial_)
                invalidRect(CRect(200, 470, 560, 512)); // the LCD only
            else if (!plugin_->meters_pending())
                preload_step();
            // the MIDI activity meters every tick, also while automation changes values each tick (they were the
            // last branch above and froze while a host automated a parameter - user, W block)
            if (int changed = plugin_->tick_meters(2)) { // the original: -1 per ~25 ms idle tick; 2 per 50 ms
                // repaint only the changed meters, merged into one area (a repaint per meter made hosts sluggish)
                if (meter_rects_.empty()) { // the VU sliders (one per part), top to bottom = part 1..16
                    for (auto &rc : renderer_.param_rects("Internal MIDI Standard Parameters/07 Volume"))
                        meter_rects_.push_back(CRect(rc[0], rc[1], rc[0] + rc[2], rc[1] + rc[3]));
                    std::sort(meter_rects_.begin(), meter_rects_.end(),
                              [](const CRect &a, const CRect &b) { return a.top < b.top; });
                }
                CRect area;
                for (int ch = 0; ch < 16 && ch < (int)meter_rects_.size(); ch++)
                    if (changed & (1 << ch)) {
                        if (area.isEmpty())
                            area = meter_rects_[ch];
                        else
                            area.unite(meter_rects_[ch]);
                    }
                if (!area.isEmpty())
                    invalidRect(area);
            }
        }, 50);
    }

    // idle ticks: decode the skin's bitmaps ahead of use (Linux: and upload them as pixmaps), so the first visit of a
    // page does not load its bitmaps while drawing
    void preload_step()
    {
        if (preload_pos_ == 0 && preload_.empty()) {
            std::set<std::string> names;
            std::function<void(const Element &)> walk = [&](const Element &e) {
                for (auto &kv : e.attr)
                    if (kv.second.size() > 4) {
                        std::string ext = kv.second.substr(kv.second.size() - 4);
                        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) { return (char)tolower(ch); });
                        if (ext == ".png")
                            names.insert(kv.second);
                    }
                for (auto &c : e.children)
                    walk(c);
            };
            for (auto &e : plugin_->layout().elements)
                walk(e);
            preload_.assign(names.begin(), names.end());
            if (preload_.empty())
                preload_pos_ = 1; // nothing to do
        }
        if (!getFrame() || preload_pos_ >= preload_.size())
            return;
        canvas_.frame = getFrame();
        for (int i = 0; i < 6 && preload_pos_ < preload_.size(); i++)
            canvas_.image(preload_[preload_pos_++]);
    }

    // the areas that show `param`: its own controls, plus every element that read it in the last full repaint (the
    // envelope display is bound to Attack but also draws Decay, Sustain, Slope and Release)
    std::vector<std::array<int, 4>> shown_rects(const std::string &param)
    {
        auto v = renderer_.param_rects(param);
        auto it = deps_.rects.find(param);
        if (it != deps_.rects.end())
            v.insert(v.end(), it->second.begin(), it->second.end());
        return v;
    }

    void draw(CDrawContext *ctx) override { drawRect(ctx, getViewSize()); }

    // Esc leaves the help mode (a frame hook: the view has no keyboard focus)
    bool attached(CView *parent) override
    {
        bool ok = CView::attached(parent);
        if (auto *f = getFrame()) {
            f->registerKeyboardHook(this);
            f->registerMouseObserver(this); // hover (LCD, help mode): views get moves only while a button is down
        }
        return ok;
    }
    bool removed(CView *parent) override
    {
        if (auto *f = getFrame()) {
            f->unregisterKeyboardHook(this);
            f->unregisterMouseObserver(this);
        }
        return CView::removed(parent);
    }
    // IMouseObserver: every mouse event of the frame (frame coordinates, before its zoom transform)
    void onMouseEntered(CView *, CFrame *) override {}
    void onMouseExited(CView *, CFrame *) override {}
    void onMouseEvent(MouseEvent &event, CFrame *frame) override
    {
        if (event.type != EventType::MouseMove || event.buttonState.isLeft())
            return;
        (void)frame;
        CPoint p = static_cast<MousePositionEvent &>(event).mousePosition; // already through the frame's zoom
        p.offset(-getViewSize().left, -getViewSize().top);
        lcd_hover(p);
        if (!help_)
            return;
        if (env_debug())
            fprintf(stderr, "help hover %.0f,%.0f\n", p.x, p.y);
        help_hover(p);
    }
    void onKeyboardEvent(KeyboardEvent &event, CFrame *) override
    {
        if (help_ && event.type == EventType::KeyDown && event.virt == VirtualKey::Escape) {
            set_help(false);
            event.consumed = true;
        }
    }

    void drawRect(CDrawContext *ctx, const CRect &update) override
    {
        renderer_.part = plugin_->ui_value(plugin_->layout().frame_parameter, -1);
        canvas_.ctx = ctx;
        CRect r(update);
        r.offset(-getViewSize().left, -getViewSize().top);
        renderer_.clip_x0 = (int)std::floor(r.left), renderer_.clip_y0 = (int)std::floor(r.top);
        renderer_.clip_x1 = (int)std::ceil(r.right), renderer_.clip_y1 = (int)std::ceil(r.bottom);
        auto t0 = std::chrono::steady_clock::now();
        // device pixels per editor pixel: VSTGUI picks the bitmap copy for it (CDrawContext::drawBitmap)
        const CGraphicsTransform &tm = ctx->getCurrentTransform();
        double scale = ctx->getScaleFactor() * (tm.m11 == tm.m22 && tm.m12 == 0 && tm.m21 == 0 ? tm.m11 : 1.0);
        if (env_no_hidpi() || scale < 1.01)
            scale = 1;
        if (scale != canvas_.scale) {
            canvas_.set_scale(scale);
            statics_.clear();
        }
        // the static layer (backgrounds, panels, labels) lives in an offscreen bitmap, rebuilt only when the
        // visible structure changes (page, oscillator mode, ...); every repaint copies it and draws the controls
        // the structure can only change with full repaints (page switch, menus, state); partial ones (knob drags,
        // meters, LCD) reuse the cache without walking the conditions
        bool full_update = r.getWidth() >= getViewSize().getWidth() - 1 && r.getHeight() >= getViewSize().getHeight() - 1;
        std::vector<const Element *> sig;
        if (statics_.empty() || full_update)
            sig = renderer_.static_signature();
        bool rebuild = false;
        if (statics_.empty() || (full_update && sig != statics_.front().key)) {
            // the last pages' static layers stay cached: switching back is a copy instead of a rebuild
            auto hit = std::find_if(statics_.begin(), statics_.end(), [&](const StaticLayer &l) { return l.key == sig; });
            if (hit != statics_.end())
                statics_.splice(statics_.begin(), statics_, hit);
            else
                rebuild = true;
        }
        if (rebuild) {
            CRect full = getViewSize();
            // at device resolution: the offscreen has scale x the pixels and draws through a scale transform (the
            // Cairo bitmap context ignores a bitmap's scale factor, Core Graphics applies it - so set it afterwards)
            const CPoint px(std::ceil(full.getWidth() * scale), std::ceil(full.getHeight() * scale));
            if (auto off = COffscreenContext::create(px)) {
                off->beginDraw();
                canvas_.ctx = off;
                canvas_.frame = nullptr; // CPU images into the CPU offscreen
                int c0 = renderer_.clip_x0, c1 = renderer_.clip_y0, c2 = renderer_.clip_x1, c3 = renderer_.clip_y1;
                renderer_.clip_x0 = renderer_.clip_y0 = -100000;
                renderer_.clip_x1 = renderer_.clip_y1 = 100000;
                renderer_.layer = 1;
                {
                    CDrawContext::Transform tr(*off, CGraphicsTransform().scale(scale, scale));
                    renderer_.render(canvas_);
                }
                renderer_.clip_x0 = c0, renderer_.clip_y0 = c1, renderer_.clip_x1 = c2, renderer_.clip_y1 = c3;
                off->endDraw();
                SharedPointer<CBitmap> bg = off->getBitmap();
                if (scale != 1)
                    bg->getPlatformBitmap()->setScaleFactor(scale);
#if SMTG_OS_LINUX
                if (auto sb = server_bitmap(getFrame(), bg))
                    bg = sb; // the cache as an X pixmap too
#endif
                statics_.push_front({sig, bg});
                while (statics_.size() > (scale > 1.5 ? 3u : 8u)) // ~1.9 MB each at 1x, scale^2 x that
                    statics_.pop_back();
                canvas_.ctx = ctx;
                if (env_drawtime())
                    fprintf(stderr, "static layer rebuilt\n");
            }
        }
        canvas_.frame = getFrame(); // into the window: server-side bitmaps (Linux)
        if (!statics_.empty() && full_update) { // small areas: drawing their few elements directly is cheaper on X11
            CRect dst(update);
            statics_.front().bg->draw(ctx, dst, CPoint(dst.left - getViewSize().left, dst.top - getViewSize().top));
            renderer_.layer = 2;
        } else
            renderer_.layer = 0;
        if (full_update) { // record what the controls read, for partial repaints of value changes
            deps_.cond.clear();
            deps_.rects.clear();
            renderer_.deps = &deps_;
        }
        renderer_.render(canvas_);
        renderer_.deps = nullptr;
        // the sync page's notes, only while that page shows (since round 0.8.27 only when banks are really fetched;
        // a bank check paused for playing drew them over the EASY page - user, W block)
        const bool sync_page = plugin_->ui_value("VirusControl/InitialSyncProgress", -1) < 127;
        if (sync_page && plugin_->sync_paused()) // under the sync page's progress bar (the original hides the LCD meanwhile)
            renderer_.status_text(canvas_, "FNT_LCD_5x7_Rot.png", "SYNC PAUSED WHILE YOU PLAY", 480, 290, 381, 12);
        else if (int eta = plugin_->sync_eta(); sync_page && eta >= 60) { // a first sync: tell how long, not "a couple of seconds"
            char t[64];
            snprintf(t, sizeof t, "READING THE VIRUS' BANKS: ABOUT %d MIN LEFT", (eta + 59) / 60);
            renderer_.status_text(canvas_, "FNT_LCD_5x7_Rot.png", t, 480, 290, 381, 12);
        }
        // "No Virus TI can be found": name another installed Virus driver under the page's text (our own, E366)
        if (plugin_->ui_value("SystemStatus/Error", -1) == 3) {
            const auto &lines = plugin_->other_driver_lines();
            for (size_t i = 0; i < lines.size(); i++) // the page's container is at 520/62, its last line at 250
                renderer_.status_text(canvas_, "FNT_LCD_5x7_Rot.png", lines[i], 529, 332 + 10 * (int)i, 351, 12);
        }
        if (help_)
            draw_help(ctx);
        {
            long us = (long)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count();
            plugin_->diag.draw_us += us;
            (r.getWidth() >= getViewSize().getWidth() - 1 ? plugin_->diag.full_draws : plugin_->diag.part_draws)++;
        }
        if (env_drawtime())
            fprintf(stderr, "editor draw %dx%d: %.2f ms\n", (int)r.getWidth(), (int)r.getHeight(),
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
        if (env_debug()) {
            int ok = 0, missing = 0;
            for (auto &b : bitmaps_)
                (b.second ? ok : missing)++;
            fprintf(stderr, "editor draw: %d bitmaps, %d missing\n", ok, missing);
        }
        setDirty(false);
    }

    void onMouseDownEvent(MouseDownEvent &event) override
    {
        Hit h = renderer_.hit_test((int)event.mousePosition.x, (int)event.mousePosition.y);
        // a handle of a filter / envelope display wins over the control drawn above it (the cutoff knob, #19)
        if (Hit g = renderer_.graph_hit_test((int)event.mousePosition.x, (int)event.mousePosition.y); g.element && !help_)
            h = g;
        if (env_debug())
            fprintf(stderr, "mouse down %.0f,%.0f -> %s %s\n", event.mousePosition.x, event.mousePosition.y,
                    h.element ? h.element->type.c_str() : "-", h.element ? h.element->get("Parameter").c_str() : "");
        // the browser list clicked last has the focus: its selection filled, the other's framed; any other click takes
        // the focus away, like the original's focus view (E318)
        const int focus = h.element && h.element->type == "PatchBrowser"
                              ? EditorRenderer::browser_pane(h.element->get("Parameter")) : -1;
        if (focus != renderer_.focused_pane) {
            renderer_.focused_pane = focus;
            invalid();
        }
        // right button: a menu under the point takes it, even under another control (the wavetable menu inside the
        // oscillator ring); knobs and buttons ignore it like in the original (VSTGUI: left button only)
        if (event.buttonState.isRight() && !help_) {
            Hit m = renderer_.menu_hit_test((int)event.mousePosition.x, (int)event.mousePosition.y);
            if (m.element && renderer_.param_active(m.element->get("Parameter"), m.part)) {
                open_menu(m);
                event.consumed = true;
                return;
            }
        }
        // right-click on the user pattern: the original's pattern menu (0x1800684d0, E371) - shift, randomize, reset,
        // fill up for the steps up to the pattern length
        if (event.buttonState.isRight() && !help_ && h.element &&
            h.element->get("Parameter").rfind("Arpeggiator User Pattern/", 0) == 0) {
            std::vector<vti::PopupItem> items;
            for (int a = 0; a < VirusPlugin::ARP_ACTION_COUNT; a++)
                items.push_back({VirusPlugin::arp_action_name(a), true});
            const int part = h.part;
            show_list(std::move(items), -1, 0, event.mousePosition, [this, part](int a) {
                plugin_->arp_action(part, a);
                invalid();
            });
            event.consumed = true;
            return;
        }
        // right-click on a browser entry: the original's browser menu (E294 (4))
        if (event.buttonState.isRight() && !help_ && h.element && h.element->type == "PatchBrowser") {
            browser_menu(h, event.mousePosition);
            event.consumed = true;
            return;
        }
        // right-click where there is no control: the editor's size (the original has no such setting)
        if (event.buttonState.isRight() && (!h.element || h.element->get("Parameter").empty())) {
            size_menu(event.mousePosition);
            event.consumed = true;
            return;
        }
        // "?" (the original's help menu of manuals): our help mode; in help mode a click pins a control's note and
        // changes nothing - only the page/part/envelope selectors still work, so every page can be read
        if (h.element && h.element->get("Parameter") == "VirusControl/Help Menu") {
            set_help(!help_);
            event.consumed = true;
            return;
        }
        if (help_ && !(h.element && help_navigates(h.element->get("Parameter")))) {
            h = renderer_.hit_test((int)event.mousePosition.x, (int)event.mousePosition.y, true); // disabled too
            HelpText t;
            help_pinned_ = h.element && help_for(h.element->get("Parameter"), t);
            help_hit_ = help_pinned_ ? h : Hit{};
            invalid();
            event.consumed = true;
            return;
        }
        if (!h.element)
            return;
        const Element &e = *h.element;
        const std::string &param = e.get("Parameter");
        if (!param.empty() && !renderer_.param_active(param, h.part)) { // greyed out: locked like in the original
            event.consumed = true;
            return;
        }
        // knobs and sliders: Ctrl+click or double click = back to the parameter's default value
        if ((e.type.find("Knob") != std::string::npos || e.type.find("Slider") != std::string::npos) &&
            (event.modifiers.has(ModifierKey::Control) || event.clickCount >= 2)) {
            std::string t = plugin_->softknob_target(param, h.part); // a soft knob: its target's default
            plugin_->ui_set(param, h.part, plugin_->conditions().default_value(t.empty() ? param : t));
            invalid();
            event.consumed = true;
            return;
        }
        drag_ = h;
        drag_sent_.reset();
        plugin_->take_changed(); // start collecting the changes of this drag
        drag_y_ = event.mousePosition.y;
        drag_x_ = event.mousePosition.x;
        dnd_ = false, dnd_prog_ = -1;
        drag_value_ = plugin_->ui_value(param, h.part);
        if (e.type == "NameEdit" && event.clickCount >= 2) { // rename the part's patch
            edit_name(h);
            event.consumed = true;
            return;
        }
        // filter / envelope displays: drag a handle like the original's KEnvelope (E371) - relative, the handle stays
        // under the mouse, Shift = 4x finer; Ctrl+click puts its parameters back to the middle (resonance to 0)
        if (e.type == "ADSTREnvelope" || (e.type.size() == 11 && e.type.compare(3, 8, "Response") == 0)) {
            graph_handle_ = -1;
            auto hs = renderer_.graph_handles(e, h.x, h.y, h.part);
            double best = 1e9;
            for (size_t i = 0; i < hs.size(); i++) {
                const double dx = std::abs(event.mousePosition.x - hs[i].x), dy = std::abs(event.mousePosition.y - hs[i].y);
                if (dx <= 5 && dy <= 5 && dx + dy < best)
                    best = dx + dy, graph_handle_ = (int)i;
            }
            if (graph_handle_ >= 0) {
                const auto &gh = hs[(size_t)graph_handle_];
                if (event.modifiers.has(ModifierKey::Control)) {
                    for (const std::string *pp : {&gh.xparam, &gh.yparam})
                        if (!pp->empty())
                            plugin_->ui_set(*pp, h.part, pp->find("/Resonance") != std::string::npos ? 0 : 64);
                    graph_handle_ = -1;
                    invalid();
                } else
                    graph_anchor(gh, event.mousePosition, event.modifiers.has(ModifierKey::Shift));
            }
            event.consumed = true;
            return;
        }
        if (e.type == "ArpEdit") { // user pattern, like the original's KStepSequencer (E371)
            arp_grab_ = {};
            const int px = (int)event.mousePosition.x, py = (int)event.mousePosition.y;
            if (event.modifiers.has(ModifierKey::Alt)) { // Alt+click switches the step under the mouse on or off
                const std::string bf = "Arpeggiator User Pattern/Step " +
                                       std::to_string(EditorRenderer::arp_cell(e, h.x, px) + 1) + " Bitfield";
                plugin_->ui_set(bf, h.part, plugin_->ui_value(bf, h.part) ? 0 : 1);
                invalid();
            } else if (auto g = renderer_.arp_grab(e, h.x, h.y, h.part, px, py); g.step >= 0 && (g.length || g.velocity)) {
                // length near / right of the bar end, velocity near its top; relative, on this step only
                arp_grab_ = g;
                const std::string st = "Arpeggiator User Pattern/Step " + std::to_string(g.step + 1) + " ";
                arp_len0_ = plugin_->ui_value(st + "Length", h.part);
                arp_vel0_ = plugin_->ui_value(st + "Velocity", h.part);
            }
            event.consumed = true;
            return;
        }
        // click selects (Shift: a range, Ctrl: one more), double click loads into the current part; the selection
        // may be dragged onto a part strip or a RAM slot of the other pane (E294)
        if (e.type == "PatchBrowser") {
            int prog = EditorRenderer::browser_entry(h, (int)event.mousePosition.x, (int)event.mousePosition.y);
            int bank = plugin_->ui_value(e.get("Parameter"), -1);
            if (prog >= 0 && event.modifiers.has(ModifierKey::Alt)) { // Alt+Click: rename (RAM or a library)
                if ((bank >= 0 && bank < 4) || is_library(bank))
                    edit_browser_name(h, bank, prog);
                event.consumed = true;
                return;
            }
            if (prog >= 0) {
                const bool shift = event.modifiers.has(ModifierKey::Shift), ctrl = event.modifiers.has(ModifierKey::Control);
                const int pane = EditorRenderer::browser_pane(e.get("Parameter"));
                pane_param_[pane] = e.get("Parameter");
                dnd_bank_ = bank, dnd_prog_ = prog, dnd_pane_ = pane;
                browser_sel_.at(pane, bank).press(prog, shift, ctrl);
                show_selection(pane, bank);
                if (event.clickCount >= 2 && !shift && !ctrl)
                    plugin_->load_patch_remembered(bank, prog, plugin_->ui_value(plugin_->layout().frame_parameter, -1));
                invalid();
            }
            event.consumed = true;
            return;
        }
        if (e.type == "IncButton" || e.type == "DecButton") { // step the value (browser banks, categories ...)
            int lo = 0, hi = plugin_->bank_count() - 1;
            if (const Parameter *p = plugin_->conditions().find(param))
                lo = p->min, hi = p->max;
            else if (param == "Repository Actions/Category Filter")
                hi = 22;
            if (drag_value_ == VirusPlugin::SEARCH_BANK) { // the arrows page through the search results
                plugin_->results_step(e.type == "IncButton" ? 1 : -1);
                renderer_.pressed = &e;
                invalid();
                event.consumed = true;
                return;
            }
            int v = drag_value_ + (e.type == "IncButton" ? 1 : -1);
            if (param.rfind("Browser ", 0) == 0 && param.find("/Selected Bank") != std::string::npos)
                v = vti::browser_bank_step(bank_menu_order(), drag_value_, e.type == "IncButton" ? 1 : -1);
            else
                v = std::max(lo, std::min(hi, v));
            plugin_->ui_set(param, h.part, v);
            renderer_.pressed = &e;
            invalid();
            event.consumed = true;
            return;
        }
        if (e.type == "SearchEdit" && param.rfind("Remote Templates/", 0) == 0) { // REMOTE page fields (E196)
            if (param != "Remote Templates/MIDI String Info") // the info is a display
                edit_name(h, param);
            event.consumed = true;
            return;
        }
        if (e.type == "SearchEdit" || (e.type == "RadioButton" && param == "Repository Actions/Search")) {
            edit_search(h);
            event.consumed = true;
            return;
        }
        if (e.type == "URLButton") { // INFO box link: the TI Revive website (the original's points to access-music.de)
            open_url(website_url());
            event.consumed = true;
            return;
        }
        if (e.type == "RemoteBrowser") { // REMOTE page: a click selects, a double-click chooses and sends (E199)
            int t = EditorRenderer::remote_entry(h, (int)event.mousePosition.x, (int)event.mousePosition.y);
            if (t >= 0 && event.clickCount >= 2)
                plugin_->choose_remote_template(t);
            else if (t >= 0)
                plugin_->ui_set(param, -1, t);
            invalid();
            event.consumed = true;
            return;
        }
        if (e.type == "Menu" || e.type == "HierarchicMenu" || e.type == "PatchSaveMenu") {
            open_menu(h);
            event.consumed = true;
            return;
        }
        if (e.type == "RadioButton")
            plugin_->ui_set(param, h.part, atoi(e.get("OnValue").c_str()));
        else if (e.type == "Button" || e.type == "KickButton")
            plugin_->ui_set(param, h.part, drag_value_ ? 0 : 1);
        renderer_.pressed = &e;
        invalid();
        event.consumed = true;
    }

    void onMouseMoveEvent(MouseMoveEvent &event) override
    {
        if (help_ && !event.buttonState.isLeft()) {
            help_hover(event.mousePosition);
            return;
        }
        if (!drag_.element || !event.buttonState.isLeft())
            return;
        const Element &e = *drag_.element;
        if (graph_handle_ >= 0) { // a filter / envelope handle (mouse down)
            auto hs = renderer_.graph_handles(e, drag_.x, drag_.y, drag_.part);
            if ((size_t)graph_handle_ >= hs.size())
                return;
            const auto &gh = hs[(size_t)graph_handle_];
            const bool fine = event.modifiers.has(ModifierKey::Shift);
            if (fine != graph_fine_) // the modifier changed: go on from here, no jump (like the original)
                graph_anchor(gh, event.mousePosition, fine);
            const double f = graph_fine_ ? 4.0 : 1.0;
            const double tx = graph_hx_ + (event.mousePosition.x - graph_mx_) / f;
            const double ty = graph_hy_ + (event.mousePosition.y - graph_my_) / f;
            for (const std::string *pp : {&gh.xparam, &gh.yparam})
                if (!pp->empty())
                    drag_set(*pp, drag_.part, renderer_.graph_value(e, drag_.x, drag_.y, drag_.part, *pp, tx, ty));
            invalidRect(CRect(drag_.x - 4, drag_.y - 4, drag_.x + e.w + 4, drag_.y + e.h + 4));
            for (auto &other : plugin_->take_changed()) // the knobs of these parameters move along
                for (auto &rc : shown_rects(other))
                    invalidRect(CRect(rc[0] - 2, rc[1] - 2, rc[0] + rc[2] + 2, rc[1] + rc[3] + 2));
            invalidRect(CRect(200, 470, 560, 512));
            partial_ = true;
            event.consumed = true;
            return;
        }
        if (e.type == "ArpEdit") {
            if (arp_grab_.step < 0)
                return;
            const std::string st = "Arpeggiator User Pattern/Step " + std::to_string(arp_grab_.step + 1) + " ";
            auto clamp = [](double v) { return (int)std::lround(std::max(0.0, std::min(127.0, v))); };
            if (arp_grab_.length) // the step's whole area (its span) is the full length, like the original
                drag_set(st + "Length", drag_.part,
                         clamp(arp_len0_ + (event.mousePosition.x - drag_x_) * 127.0 /
                                               std::max(1, arp_grab_.span * (e.w / 32))));
            if (arp_grab_.velocity) // the bar's height is the velocity in px
                drag_set(st + "Velocity", drag_.part, clamp(arp_vel0_ - (event.mousePosition.y - drag_y_)));
            invalid();
            event.consumed = true;
            return;
        }
        // drag and drop like the original: a part's patch name (or a browser entry) onto another part strip
        if (e.type == "NameEdit" || (e.type == "PatchBrowser" && dnd_prog_ >= 0)) {
            if (!dnd_ && std::hypot(event.mousePosition.x - drag_x_, event.mousePosition.y - drag_y_) > 4) {
                dnd_ = true;
                if (auto *f = getFrame())
                    f->setCursor(kCursorCopy);
            }
            if (dnd_)
                drag_highlight((int)event.mousePosition.x, (int)event.mousePosition.y);
            event.consumed = true;
            return;
        }
        if (e.type.find("Knob") == std::string::npos && e.type.find("Slider") == std::string::npos)
            return;
        const std::string &param = e.get("Parameter");
        const std::string target = plugin_->softknob_target(param, drag_.part); // a soft knob: its target's range
        const Parameter *p = plugin_->conditions().find(target.empty() ? param : target);
        int lo = p ? p->min : 0, hi = p ? p->max : 127;
        // knobs and text sliders: full range over 200 pixels upwards, like a VSTGUI knob; sliders follow the mouse
        // 1:1 along their track (the handle stays under the cursor)
        double delta = (drag_y_ - event.mousePosition.y) * (hi - lo) / 200.0;
        if (e.type == "HorizontalSlider" || e.type == "VUSlider") {
            int hw = 0;
            if (void *hd = canvas_.image(e.get("NormalHandle")))
                hw = canvas_.image_width(hd);
            delta = (event.mousePosition.x - drag_x_) * (hi - lo) / std::max(1.0, (double)(e.w - hw));
        } else if (e.type == "VerticalSlider") {
            int hh = 0;
            if (void *hd = canvas_.image(e.get("NormalHandle")))
                hh = canvas_.image_height(hd);
            delta = (drag_y_ - event.mousePosition.y) * (hi - lo) / std::max(1.0, (double)(e.h - hh));
        }
        int v = (int)std::lround(drag_value_ + delta);
        drag_set(param, drag_.part, std::max(lo, std::min(hi, v)));
        // repaint only the control and the LCD (bottom info panel); value-dependent layouts change on release
        if (drag_rects_.empty())
            drag_rects_ = shown_rects(param);
        for (auto &rc : drag_rects_) // every control of this parameter (knob + value display ...)
            invalidRect(CRect(rc[0] - 2, rc[1] - 2, rc[0] + rc[2] + 2, rc[1] + rc[3] + 2));
        for (auto &other : plugin_->take_changed()) // linked parameters (LFO/filter links) move along
            if (other != param)
                for (auto &rc : shown_rects(other))
                    invalidRect(CRect(rc[0] - 2, rc[1] - 2, rc[0] + rc[2] + 2, rc[1] + rc[3] + 2));
        invalidRect(CRect(200, 470, 560, 512));
        partial_ = true;
        event.consumed = true;
    }

    void onMouseUpEvent(MouseUpEvent &event) override
    {
        const bool dragged = dnd_;
        if (dnd_ && drag_.element) { // dropped: onto a part strip (the part panel, x < 192) or a browser slot?
            // the cursor goes back after this event: X11 VSTGUI still holds the pointer grab here and releases it
            // after the mouse-up, a cursor set inside the grab stayed on screen (the copy hand, user 2026-10-21)
            if (auto *f = getFrame(); f && !f->doAfterEventProcessing([f]() { f->setCursor(kCursorDefault); }))
                f->setCursor(kCursorDefault);
            const int x = (int)event.mousePosition.x, y = (int)event.mousePosition.y;
            Hit t = renderer_.hit_test(x, y);
            bool strip = t.element && event.mousePosition.x < 192 && t.part >= 0 && t.part < 16;
            if (strip && drag_.element->type == "NameEdit")
                plugin_->copy_part(drag_.part, t.part);
            else if (strip && dnd_prog_ >= 0)
                plugin_->load_patch_remembered(dnd_bank_, dnd_prog_, t.part);
            else if (dnd_prog_ >= 0 && t.element && t.element->type == "PatchBrowser")
                drop_on_browser(t, x, y);
            else if (drag_.element->type == "NameEdit" && t.element && t.element->type == "PatchBrowser") {
                // a part's patch dropped on a RAM slot: stored there, like SAVE (the original's Tutorial)
                const int bank = plugin_->ui_value(t.element->get("Parameter"), -1), slot = EditorRenderer::browser_entry(t, x, y);
                if (bank >= 0 && bank < 4 && slot >= 0)
                    plugin_->store_patch(drag_.part, bank, slot);
            }
            dnd_ = false;
        }
        if (drag_.element && drag_.element->type == "PatchBrowser" && dnd_prog_ >= 0) {
            browser_sel_.at(dnd_pane_, dnd_bank_).release(dnd_prog_, dragged);
            show_selection(dnd_pane_, dnd_bank_);
        }
        renderer_.highlighted.clear();
        graph_handle_ = -1;
        arp_grab_ = {};
        drag_ = {};
        drag_rects_.clear();
        renderer_.pressed = nullptr;
        partial_ = false;
        invalid();
        event.consumed = true;
    }

    // IControlListener: the name edit was committed
    void valueChanged(CControl *c) override
    {
        if (auto *te = dynamic_cast<CTextEdit *>(c)) {
            std::string t = te->getText().getString();
            if (editing_search_) { // live search: every change filters; the field goes away with the focus
                plugin_->set_search(t);
                invalid();
                return;
            }
            if (is_library(edit_bank_))
                plugin_->rename_in_library(edit_bank_ - VirusPlugin::LIB_BASE, edit_prog_, t);
            else if (edit_bank_ >= 0)
                plugin_->rename_in_ram(edit_bank_, edit_prog_, t);
            else if (edit_param_.empty())
                plugin_->rename_part(edit_part_, t);
            else // a REMOTE page field: saved with the templates and sent like the original's
                plugin_->set_remote_text(edit_param_, t);
            edit_param_.clear();
            edit_bank_ = -1;
            invalid(); // the field goes away with the focus (below)
        }
    }

    // ITextEditListener: a text field lost the focus (return, escape, click elsewhere). VSTGUI reports a commit
    // (valueChanged above) only for a changed text, so the field is removed here: before, a name confirmed unchanged
    // stayed on the part bar as a white field and hid every later name (issue #18).
    void onTextEditPlatformControlLostFocus(CTextEdit *te) override
    {
        te->unregisterTextEditListener(this);
        editing_search_ = false;
        edit_param_.clear();
        edit_bank_ = -1;
        remove_edit(te);
        invalid();
    }

    // A text edit removes itself after the event that ended it. Outside the frame's event handling (a native Win32 or
    // Cocoa edit may commit or lose the focus there) doAfterEventProcessing refuses, and removing it at once inside
    // its own callback is unsafe: the 50 ms timer removes it then (review 2026-10-19; before, it stayed).
    void remove_edit(CTextEdit *te)
    {
        auto *f = getFrame();
        if (f && !f->doAfterEventProcessing([f, te]() { f->removeView(te); }))
            remove_later_.push_back(te);
    }
    std::vector<CTextEdit *> remove_later_;

    // a library file dropped on the browser becomes a personal library (TI Revive's own addition, E357)
    SharedPointer<IDropTarget> getDropTarget() override
    {
        auto t = makeOwned<FileDropTarget>();
        t->move = [this](DragEventData d) { return file_drag(d); };
        t->leave = [this]() { file_drag_end(); };
        t->drop = [this](DragEventData d) { return file_drop(d); };
        return t;
    }

private:
    // the .mid/.midi/.syx paths in a drag from outside (anything else is ignored)
    static std::vector<std::string> dropped_libraries(IDataPackage *p)
    {
        std::vector<std::string> out;
        for (uint32_t i = 0; p && i < p->getCount(); i++) {
            const void *buf = nullptr;
            IDataPackage::Type type = IDataPackage::kError;
            const uint32_t n = p->getData(i, buf, type);
            if (type != IDataPackage::kFilePath || !buf || n == 0)
                continue;
            std::string path((const char *)buf, n);
            while (!path.empty() && path.back() == '\0')
                path.pop_back();
#ifdef _WIN32
            // VSTGUI gives UTF-8; the plug-in's paths are narrow (ANSI code page) like library_dir(): converted, a
            // name the code page cannot hold is kept and fails with "Cannot read" instead of opening something else
            try {
                path = std::filesystem::u8path(path).string();
            } catch (...) {
            }
#endif
            if (importable_library(path))
                out.push_back(path);
        }
        return out;
    }
    const Element *browser_at(const CPoint &p)
    {
        Hit t = renderer_.hit_test((int)p.x, (int)p.y);
        return t.element && t.element->type == "PatchBrowser" ? t.element : nullptr;
    }
    // over a browser with a library file: the copy cursor and a hint in the LCD (a drop takes the whole file, so no
    // slot is highlighted); anywhere else, or other files: no drop
    DragOperation file_drag(const DragEventData &d)
    {
        const bool ok = browser_at(d.pos) && !dropped_libraries(d.drag).empty();
        plugin_->lcd_hint(ok, "Drop to add a library", "(Personal Libraries)"); // off: the LCD as it was
        return ok ? DragOperation::Copy : DragOperation::None;
    }
    void file_drag_end() { plugin_->lcd_hint(false); }
    bool file_drop(const DragEventData &d)
    {
        plugin_->lcd_hint(false);
        const Element *browser = browser_at(d.pos);
        const auto files = dropped_libraries(d.drag);
        if (!browser || files.empty())
            return false;
        int shown = -1, added = 0, there = 0, failed = 0;
        LibraryImport one;
        for (auto &f : files) {
            int lib = -1;
            one = plugin_->import_library_file(f, lib);
            if (lib < 0)
                failed++;
            else {
                shown = lib;
                (one.existed ? there : added)++;
            }
        }
        if (shown >= 0) // the pane it was dropped on shows the (last) library
            plugin_->ui_set(browser->get("Parameter"), -1, VirusPlugin::LIB_BASE + shown);
        // the LCD's second line holds 20 characters, the first a few more: the library's name goes below
        if (files.size() == 1 && failed)
            plugin_->lcd_message("Library not added", one.error);
        else if (files.size() == 1) {
            std::string what = one.existed ? "Library already there" : "Library added";
            if (!one.existed && one.skipped) // damaged patches left out (E356)
                what = "Added (" + std::to_string(one.skipped) + " damaged)";
            plugin_->lcd_message(what, one.name);
        } else {
            std::string more;
            if (there)
                more = std::to_string(there) + (failed ? " there" : " already there");
            if (failed)
                more += (more.empty() ? "" : ", ") + std::to_string(failed) + " not added";
            plugin_->lcd_message(std::to_string(added) + " libraries added", more);
        }
        invalid();
        return shown >= 0;
    }
    // help mode ("?"): frames around the explained controls, a note for the one under the mouse (or clicked)
    bool help_ = false, help_pinned_ = false;
    Hit help_hit_;
    Hit lcd_hit_; // the control lcd_hover last told the plug-in about
    void set_help(bool on)
    {
        help_ = on;
        help_pinned_ = false;
        help_hit_ = {};
        invalid();
    }
    // the note follows the mouse; a pinned (clicked) one stays while the mouse is over no explained control
    // the LCD shows the control under the mouse like the original (E353); the plug-in only hears of a new one
    void lcd_hover(const CPoint &p)
    {
        const Hit h = renderer_.hit_test((int)p.x, (int)p.y);
        if (h.element == lcd_hit_.element && h.part == lcd_hit_.part)
            return;
        lcd_hit_ = h;
        plugin_->hover(h.element ? h.element->get("Parameter") : std::string(), h.part); // the timer repaints the LCD
    }
    void help_hover(const CPoint &p)
    {
        Hit h = renderer_.hit_test((int)p.x, (int)p.y, true); // disabled controls too (with the reason)
        HelpText t;
        if (!(h.element && help_for(h.element->get("Parameter"), t)))
            h = help_pinned_ ? help_hit_ : Hit{};
        else
            help_pinned_ = false;
        if (h.element != help_hit_.element || h.part != help_hit_.part) {
            help_hit_ = h;
            invalid();
        }
    }
    static bool help_navigates(const std::string &param)
    {
        return param == "VirusControl/SelectedPage" || param == "VirusControl/SelectedUtilityPage" ||
               param == "VirusControl/Selected Section" || param == "Multi Parameters/Panel Part" ||
               param == "Envelope/Select";
    }
    static std::vector<std::string> wrap(CDrawContext *ctx, const std::string &text, double width)
    {
        std::vector<std::string> lines;
        std::string line, word;
        auto flush_word = [&]() {
            if (word.empty())
                return;
            std::string tryline = line.empty() ? word : line + " " + word;
            if (!line.empty() && ctx->getStringWidth(tryline.c_str()) > width) {
                lines.push_back(line);
                line = word;
            } else
                line = tryline;
            word.clear();
        };
        for (char ch : text) {
            if (ch == ' ')
                flush_word();
            else
                word += ch;
        }
        flush_word();
        if (!line.empty())
            lines.push_back(line);
        return lines;
    }
    void draw_help(CDrawContext *ctx)
    {
        const CColor frame_col(120, 190, 255, 150), hot_col(120, 190, 255, 255);
        ctx->setLineWidth(1);
        ctx->setDrawMode(kAntiAliasing);
        HelpText t;
        for (auto &c : renderer_.visible_controls(true))
            if (help_for(c.element->get("Parameter"), t)) {
                ctx->setFrameColor(frame_col);
                ctx->drawRect(CRect(c.x + 0.5, c.y + 0.5, c.x + c.element->w - 0.5, c.y + c.element->h - 0.5),
                              kDrawStroked);
            }
        const CRect view(0, 0, getViewSize().getWidth(), getViewSize().getHeight());
        // the hint, above the "?" button
        auto q = renderer_.param_rects("VirusControl/Help Menu");
        SharedPointer<CFontDesc> font = makeOwned<CFontDesc>(*kNormalFontSmall);
        SharedPointer<CFontDesc> bold = makeOwned<CFontDesc>(*kNormalFontSmall);
        bold->setStyle(kBoldFace);
        ctx->setFont(font);
        const char *hint = "Help: point at a control, click to keep the note. Esc or ? to leave.";
        double hw = ctx->getStringWidth(hint) + 12;
        double hx = q.empty() ? view.right - hw - 4 : std::min<double>(view.right - hw - 4, q[0][0] + q[0][2] - hw);
        double hy = q.empty() ? view.bottom - 40 : q[0][1] - 20;
        CRect hint_r(hx, hy, hx + hw, hy + 16);
        ctx->setFillColor(CColor(20, 22, 28, 230));
        ctx->drawRect(hint_r, kDrawFilled);
        ctx->setFontColor(CColor(220, 230, 240, 255));
        ctx->drawString(hint, hint_r, kCenterText);
        if (!q.empty()) {
            ctx->setFrameColor(hot_col);
            ctx->setLineWidth(2);
            ctx->drawRect(CRect(q[0][0], q[0][1], q[0][0] + q[0][2], q[0][1] + q[0][3]), kDrawStroked);
        }
        if (!help_hit_.element || !help_for(help_hit_.element->get("Parameter"), t))
            return;
        const Element &e = *help_hit_.element;
        CRect cr(help_hit_.x, help_hit_.y, help_hit_.x + e.w, help_hit_.y + e.h);
        ctx->setFrameColor(hot_col);
        ctx->setLineWidth(2);
        ctx->drawRect(cr, kDrawStroked);
        // the note: title + wrapped text, below the control if it fits, else above; kept inside the editor
        const double w = 260, pad = 8, lh = 14;
        ctx->setFont(font);
        auto lines = wrap(ctx, t.text, w - 2 * pad);
        if (help_hit_.inactive) { // why it is greyed out now: the condition of the page that shows it, or the
                                  // parameters its own condition tests
            std::string why = help_hit_.disabled ? inactive_reason(help_hit_.why) : depends_reason(help_hit_.depends);
            for (auto &l : wrap(ctx, why.empty() ? "Inactive with the current settings." : "Inactive now: " + why + ".",
                                w - 2 * pad))
                lines.push_back(l);
        }
        double h = pad * 2 + lh * (1 + lines.size()) + 2;
        double x = std::max(4.0, std::min(view.right - w - 4, cr.left));
        double y = cr.bottom + 6 + h <= view.bottom - 4 ? cr.bottom + 6 : std::max(4.0, cr.top - 6 - h);
        CRect box(x, y, x + w, y + h);
        if (auto path = owned(ctx->createRoundRectGraphicsPath(box, 6))) {
            ctx->setFillColor(CColor(24, 26, 32, 240));
            ctx->drawGraphicsPath(path, CDrawContext::kPathFilled);
            ctx->setFrameColor(hot_col);
            ctx->setLineWidth(1);
            ctx->drawGraphicsPath(path, CDrawContext::kPathStroked);
        }
        ctx->setFont(bold);
        ctx->setFontColor(CColor(255, 255, 255, 255));
        ctx->drawString(t.title.c_str(), CRect(x + pad, y + pad, x + w - pad, y + pad + lh), kLeftText);
        ctx->setFont(font);
        ctx->setFontColor(CColor(215, 222, 232, 255));
        for (size_t i = 0; i < lines.size(); i++) {
            double ly = y + pad + lh * (i + 1) + 2;
            ctx->drawString(lines[i].c_str(), CRect(x + pad, ly, x + w - pad, ly + lh), kLeftText);
        }
    }
    EditorRenderer::ArpGrab arp_grab_; // ArpEdit: the grabbed step and what is dragged (E371)
    int arp_len0_ = 0, arp_vel0_ = 0;  // its length and velocity when grabbed
    // filter / envelope display drag (E371): the grabbed handle, where it and the mouse were at the (re)anchor
    int graph_handle_ = -1;
    double graph_hx_ = 0, graph_hy_ = 0, graph_mx_ = 0, graph_my_ = 0;
    bool graph_fine_ = false;
    void graph_anchor(const EditorRenderer::GraphHandle &gh, const CPoint &m, bool fine)
    {
        graph_hx_ = gh.x, graph_hy_ = gh.y, graph_mx_ = m.x, graph_my_ = m.y, graph_fine_ = fine;
    }
    DragSend drag_sent_;       // a drag sends a value once, not on every mouse move (dragsend.h)
    void drag_set(const std::string &path, int part, int value)
    {
        if (drag_sent_.changed(path, part, value))
            plugin_->ui_set(path, part, value);
    }
    int edit_part_ = 0;
    std::string edit_param_; // edit_name: the REMOTE page field being edited, empty for a patch name
    int edit_bank_ = -1, edit_prog_ = -1; // edit_browser_name: the RAM slot being renamed (-1: none)
    std::vector<CRect> meter_rects_; // the part strips' meters
    EditorRenderer::Deps deps_;      // what the controls read (filled by full repaints)
    struct StaticLayer {
        std::vector<const Element *> key; // the visible conditional static elements (page, modes, ...)
        SharedPointer<CBitmap> bg;        // the static layer drawn with them
    };
    std::list<StaticLayer> statics_;   // front = current; the last 8 pages
    std::vector<std::string> preload_; // the skin's bitmaps, loaded a few per idle tick (first page visits)
    size_t preload_pos_ = 0;
    bool editing_search_ = false;
    CRect search_rect_;

    // the last entry under "Personal Libraries" in the patch and bank menus, as in the original (E217)
    static constexpr const char *kCreateLibrary = "Create Empty Library";

    // the original's save dialog for it: title, filter, the library folder and "New Library.mid"; the file is written
    // when the dialog ends with a name
    void create_library_dialog()
    {
        auto *fs = CNewFileSelector::create(getFrame(), CNewFileSelector::kSelectSaveFile);
        if (!fs)
            return;
        fs->setTitle("Save empty library as...");
        fs->addFileExtension(CFileExtension("Virus TI Patch Libraries", "mid"));
        const std::string dir = vti::library_dir(), name = "New Library.mid";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
#if defined(_WIN32) || defined(__APPLE__)
        fs->setInitialDirectory(dir.c_str());
        fs->setDefaultSaveName(name.c_str());
#else
        fs->setInitialDirectory((std::filesystem::path(dir) / name).string().c_str()); // zenity/kdialog: a full path
#endif
        auto *plugin = plugin_;
        fs->run([plugin](CNewFileSelector *sel) {
            if (sel->getNumSelectedFiles() == 1)
                plugin->create_library(sel->getSelectedFile(0));
        });
        fs->forget();
    }

    // LOAD / SAVE like the original: "<device> > RAM A.. (SAVE: RAM only) > 000 Name" and "Personal Libraries"
    void patch_menu_popup(const Hit &h, const CPoint &where, bool save)
    {
        std::vector<vti::PopupItem> dev;
        for (int b = 0; b < (save ? 4 : plugin_->bank_count()); b++) {
            std::vector<vti::PopupItem> patches;
            for (int p = 0; p < 128; p++) {
                char t[40];
                snprintf(t, sizeof t, "%03d %s", p, plugin_->bank_patch(b, p).c_str());
                patches.push_back({t});
            }
            dev.push_back({VirusPlugin::bank_name(b), true, std::move(patches)});
        }
        // the library menus (E307): "Personal Libraries", and "User" / "Public Libraries" when they hold one; SAVE
        // can also append. libs: the path below a library menu (menu's position first) -> library
        const auto menus = plugin_->library_menus();
        std::vector<std::vector<vti::PopupItem>> lib(menus.size());
        auto libs = std::make_shared<std::map<std::vector<int>, int>>();
        std::vector<std::map<std::string, std::vector<int>>> folders(menus.size());
        for (int i = 0; i < plugin_->library_count(); i++) {
            const int m = (int)(std::find(menus.begin(), menus.end(), plugin_->library_root(i)) - menus.begin());
            if (m >= (int)menus.size())
                continue;
            std::vector<vti::PopupItem> patches;
            for (int p = 0; p < plugin_->library_size(i); p++) {
                char t[40];
                snprintf(t, sizeof t, "%03d %s", p, plugin_->bank_patch(VirusPlugin::LIB_BASE + i, p).c_str());
                patches.push_back({t});
            }
            if (save && plugin_->library_size(i) < 128) {
                char t[40];
                snprintf(t, sizeof t, "%03d (new)", plugin_->library_size(i));
                patches.push_back({t});
            }
            if (patches.empty()) // an empty library: a greyed placeholder keeps it a submenu
                patches.push_back({"(empty)", false});
            vti::PopupItem entry("", true, std::move(patches));
            auto at = vti::popup_add_path(lib[m], plugin_->library_name(i), std::move(entry), folders[m]);
            at.insert(at.begin(), m);
            (*libs)[at] = i;
        }
        int create = -1; // only SAVE has it, last in "Personal Libraries" after a separator (0x180041a10, E297 (C))
        if (save) {
            lib[0].push_back(vti::PopupItem::line());
            create = (int)lib[0].size();
            lib[0].push_back({kCreateLibrary});
        }
        std::vector<vti::PopupItem> items;
        int part = h.part;
        // LOAD only: "Init Patch" (our own), then like the original (E220) "Revert to "<name>"" after a load,
        // "Recently Used" > "<n> <name>" (n = the entry's position in this menu), then a separator
        int revert = -1, recent = -1, init = -1;
        if (!save) {
            // TI Revive's own addition (#14): start a sound from scratch
            if (plugin_->has_init_patch()) {
                init = (int)items.size();
                items.push_back({"Init Patch"});
            }
            const std::string rn = plugin_->revert_name(part);
            if (!rn.empty()) {
                revert = (int)items.size();
                items.push_back({"Revert to \"" + rn + "\""});
            }
            std::vector<vti::PopupItem> used;
            auto names = plugin_->recent_names(part);
            for (size_t i = 0; i < names.size(); i++)
                used.push_back({std::to_string(i + (revert >= 0 ? 1 : 0)) + " " + names[i]});
            if (!used.empty()) {
                recent = (int)items.size();
                items.push_back({"Recently Used", true, std::move(used)});
            }
            if (!items.empty())
                items.push_back(vti::PopupItem::line());
        }
        const int devi = (int)items.size(), libi = devi + 1;
        items.push_back({plugin_->device_name(), true, std::move(dev)});
        for (size_t m = 0; m < menus.size(); m++)
            items.push_back({menus[m], true, std::move(lib[m])});
        show_popup(std::move(items), -1, 0, where,
                   [this, part, save, libs, create, revert, recent, init, devi, libi](const std::vector<int> &path) {
            if (path.empty())
                return;
            if (path.size() == 1 && path[0] == init) {
                plugin_->init_patch(part);
                invalid();
                return;
            }
            if (path == std::vector<int>{libi, create}) {
                create_library_dialog();
                return;
            }
            if (path.size() == 1 && path[0] == revert) {
                plugin_->revert_patch(part);
                invalid();
                return;
            }
            if (path.size() == 2 && path[0] == recent) {
                plugin_->load_recent(part, path[1]);
                invalid();
                return;
            }
            if (path.size() < 3)
                return;
            int bank = -1, slot = path.back();
            if (path[0] == devi && path.size() == 3)
                bank = path[1];
            else if (path[0] >= libi) {
                std::vector<int> at(path.begin() + 1, path.end() - 1);
                at.insert(at.begin(), path[0] - libi);
                auto it = libs->find(at);
                if (it != libs->end())
                    bank = VirusPlugin::LIB_BASE + it->second;
            }
            if (bank < 0)
                return;
            if (save && bank >= VirusPlugin::LIB_BASE)
                plugin_->store_to_library(part, bank - VirusPlugin::LIB_BASE, slot);
            else if (save)
                plugin_->store_patch(part, bank, slot);
            else
                plugin_->load_patch_remembered(bank, slot, part);
            invalid();
        });
    }

    // the browser's bank selector: "<device> > RAM A .. ROM Z" and the library menus (E307)
    // the bank menu's libraries: their submenus per library menu (Personal Libraries, ...) and menu's position +
    // index path -> library. Entries are only appended, so the map's order is the order the menu lists them in
    std::map<std::vector<int>, int> library_tree(const std::vector<std::string> &menus,
                                                 std::vector<std::vector<vti::PopupItem>> &lib)
    {
        std::map<std::vector<int>, int> libs;
        lib.assign(menus.size(), {});
        std::vector<std::map<std::string, std::vector<int>>> folders(menus.size());
        for (int i = 0; i < plugin_->library_count(); i++) {
            const int m = (int)(std::find(menus.begin(), menus.end(), plugin_->library_root(i)) - menus.begin());
            if (m >= (int)menus.size())
                continue;
            auto at = vti::popup_add_path(lib[m], plugin_->library_name(i), {""}, folders[m]);
            at.insert(at.begin(), m);
            libs[at] = i;
        }
        return libs;
    }

    // the banks in the bank menu's order: the Virus' banks, then the libraries (the arrows step through it, #19)
    std::vector<int> bank_menu_order()
    {
        std::vector<int> order;
        for (int b = 0; b < plugin_->bank_count(); b++)
            order.push_back(b);
        std::vector<std::vector<vti::PopupItem>> lib;
        for (auto &kv : library_tree(plugin_->library_menus(), lib))
            order.push_back(VirusPlugin::LIB_BASE + kv.second);
        return order;
    }

    void bank_menu_popup(const Hit &h, const CPoint &where)
    {
        std::vector<vti::PopupItem> dev;
        for (int b = 0; b < plugin_->bank_count(); b++)
            dev.push_back({VirusPlugin::bank_name(b)});
        const auto menus = plugin_->library_menus();
        std::vector<std::vector<vti::PopupItem>> lib;
        auto libs = std::make_shared<std::map<std::vector<int>, int>>(library_tree(menus, lib));
        // the browser's bank menu has no "Create Empty Library" (only SAVE has, E297 (C))
        std::vector<vti::PopupItem> items;
        items.push_back({plugin_->device_name(), true, std::move(dev)});
        for (size_t m = 0; m < menus.size(); m++)
            items.push_back({menus[m], true, std::move(lib[m])});
        std::string param = h.element->get("Parameter");
        show_popup(std::move(items), -1, 0, where, [this, param, libs](const std::vector<int> &path) {
            if (path.size() == 2 && path[0] == 0)
                plugin_->ui_set(param, -1, path[1]);
            else if (path.size() >= 2 && path[0] >= 1) {
                std::vector<int> at(path.begin() + 1, path.end());
                at.insert(at.begin(), path[0] - 1);
                auto it = libs->find(at);
                if (it != libs->end())
                    plugin_->ui_set(param, -1, VirusPlugin::LIB_BASE + it->second);
            }
            invalid();
        });
    }

    void patch_menu(const Hit &h, const CPoint &where, bool save) { patch_menu_popup(h, where, save); }
    void bank_menu(const Hit &h, const CPoint &where) { bank_menu_popup(h, where); }

    void category_menu(const CPoint &where)
    {
        // the original's list: "All" + the categories in alphabetical order (editordevice "Val Name Category Filter")
        std::vector<vti::PopupItem> items;
        auto vl = plugin_->editor_device().valuelists.find("Val Name Category Filter");
        if (vl != plugin_->editor_device().valuelists.end())
            for (auto &en : vl->second.entries)
                items.push_back({en.text, true});
        else
            items.push_back({"All", true});
        int cur = plugin_->ui_value("Repository Actions/Category Filter", -1);
        const int per_column = EditorRenderer::menu_items_per_column(items.size());
        show_list(std::move(items), cur, per_column, where, [this](int i) {
            plugin_->ui_set("Repository Actions/Category Filter", -1, i);
            invalid();
        });
    }

    // our popup (PopupView) at the editor point `where`; `chosen` gets the entry's path (one index
    // for a list without submenus)
    void show_popup(std::vector<vti::PopupItem> items, int checked, int per_column, const CPoint &where,
                    std::function<void(const std::vector<int> &)> chosen)
    {
        CRect area = getViewSize();
        PopupView::open(getFrame(), area, editor_->content_scale(), std::move(items), checked, per_column, where,
                        std::move(chosen));
    }
    void show_list(std::vector<vti::PopupItem> items, int checked, int per_column, const CPoint &where,
                   std::function<void(int)> chosen)
    {
        show_popup(std::move(items), checked, per_column, where,
                   [chosen](const std::vector<int> &path) { chosen(path.front()); });
    }

    void edit_search(const Hit &h)
    {
        auto *f = getFrame();
        if (!f)
            return;
        if (editing_search_)
            return;
        editing_search_ = true;
        // the field of the layout (the magnifier button left of it opens the same field)
        const Element *field = h.element;
        if (field->type != "SearchEdit")
            for (auto &rc : renderer_.param_rects("Repository Actions/Search"))
                if (rc[2] > rc[3] * 2) { // the wide one is the text field
                    search_rect_ = CRect(rc[0], rc[1], rc[0] + rc[2], rc[1] + rc[3]);
                    field = nullptr;
                }
        CRect r = field ? CRect(h.x, h.y, h.x + field->w, h.y + field->h) : search_rect_;
        auto *te = new CTextEdit(r, this, 0, plugin_->search().c_str());
        te->setBackColor(CColor(224, 232, 240, 255)); // close to the LCD field, system font like the name edit
        te->setFrameColor(CColor(224, 232, 240, 255));
        te->setFontColor(CColor(20, 40, 120, 255));
        te->setFont(kNormalFontSmall);
        te->setHoriAlign(kLeftText);
        te->setImmediateTextChange(true);
        te->registerTextEditListener(this);
        f->addView(te);
        te->takeFocus();
    }
    // the part's patch name, or with `param` a REMOTE page text field (Template Name, Knob Name, MIDI String)
    void edit_name(const Hit &h, const std::string &param = {})
    {
        auto *f = getFrame();
        if (!f)
            return;
        edit_part_ = h.part;
        edit_param_ = param;
        edit_bank_ = -1;
        editing_search_ = false;
        CRect r(h.x, h.y, h.x + h.element->w, h.y + h.element->h);
        std::string text = param.empty() ? plugin_->patch_name(h.part) : plugin_->live_text(param);
        auto *te = new CTextEdit(r, this, 0, text.c_str());
        te->setBackColor(CColor(255, 255, 255, 255)); // the original: white field, system font
        te->setFontColor(CColor(0, 0, 0, 255));
        te->setFrameColor(CColor(255, 255, 255, 255));
        // a font that fits the field: the patch name's is 10 px high, 12 pt overflowed it on macOS (textfield.h)
        te->setFont(makeOwned<CFontDesc>(kSystemFont->getName(), text_field_font(r.getHeight())));
        if (!param.empty()) { // the REMOTE page's fields (10-13 px): the text from the left
            te->setHoriAlign(kLeftText);
            te->setTextInset(CPoint(1, 0));
        }
        te->registerTextEditListener(this);
        f->addView(te);
        te->takeFocus();
    }

    // Alt+Click on a RAM entry in the browser: a field over the entry (the browser table's geometry,
    // EditorRenderer::patch_browser), committed like the part name's
    void edit_browser_name(const Hit &h, int bank, int prog)
    {
        auto *f = getFrame();
        if (!f)
            return;
        edit_bank_ = bank, edit_prog_ = prog;
        edit_param_.clear();
        editing_search_ = false;
        const int nx = h.x + 21 + (prog / 16) * 84, ny = h.y + 1 + (prog % 16) * 11;
        CRect r(nx - 2, ny - 1, nx + 61, ny + 11);
        std::string text = plugin_->bank_patch(bank, prog);
        auto *te = new CTextEdit(r, this, 0, text.c_str());
        te->setBackColor(CColor(255, 255, 255, 255));
        te->setFontColor(CColor(0, 0, 0, 255));
        te->setFrameColor(CColor(255, 255, 255, 255));
        te->setFont(makeOwned<CFontDesc>(kSystemFont->getName(), text_field_font(r.getHeight())));
        te->setHoriAlign(kLeftText);
        te->setTextInset(CPoint(1, 0));
        te->registerTextEditListener(this);
        f->addView(te);
        te->takeFocus();
    }

    // a menu control's popup, anchored at the control like the original's (not at the mouse)
    void open_menu(const Hit &h)
    {
        const std::string &param = h.element->get("Parameter");
        auto [ax, ay] = EditorRenderer::menu_anchor(h);
        CPoint at(ax, ay);
        if (param == "Repository Actions/Current Patch" || param == "Repository Actions/Current Patch Save Action")
            patch_menu(h, at, param == "Repository Actions/Current Patch Save Action");
        else if (param.rfind("Browser ", 0) == 0)
            bank_menu(h, at);
        else if (param == "Repository Actions/Category Filter")
            category_menu(at);
        else if (param == "Remote Templates/Knob Template")
            knob_template_menu(at);
        else if (param == "Repository Actions/Load Template File" || param == "Repository Actions/Save Template File")
            template_file_menu(at, param == "Repository Actions/Save Template File");
        else if (param == "Remote Templates/MIDI Channel Behaviour") // code-defined in the original (E196)
            show_list({{"Disable MIDI channel"}, {"Enable MIDI Channel"}}, plugin_->ui_value(param, -1), 0, at,
                      [this, param](int i) {
                          plugin_->ui_set(param, -1, i);
                          invalid();
                      });
        else
            popup_menu(h, at);
    }

    // REMOTE page: the original's knob templates ("/" = submenu, E197); a choice fills the selected knob's MIDI string
    // like the original's (0x18008b020) and sends that knob's record
    void knob_template_menu(const CPoint &where)
    {
        auto list = std::make_shared<std::vector<KnobTemplate>>(plugin_->knob_templates());
        if (list->empty())
            return;
        std::vector<vti::PopupItem> items;
        std::map<std::string, std::vector<int>> folders;
        auto index = std::make_shared<std::map<std::vector<int>, int>>(); // menu path -> entry
        for (int i = 0; i < (int)list->size(); i++)
            (*index)[vti::popup_add_path(items, (*list)[i].name, {""}, folders)] = i;
        show_popup(std::move(items), -1, 0, where, [this, list, index](const std::vector<int> &path) {
            auto it = index->find(path);
            if (it == index->end())
                return;
            plugin_->set_remote_text("Remote Templates/MIDI String", (*list)[it->second].midi);
            invalid();
        });
    }

    // REMOTE page LOAD / SAVE like the original's (E198): "Personal Libraries" and "Personal Remote Templates" with
    // their template files (sub folders as submenus, only folders with templates); SAVE also offers a new file under
    // Personal Remote Templates (" Save as new Template.." in the original). LOAD sends the template (E200).
    void template_file_menu(const CPoint &where, bool save)
    {
        auto files = std::make_shared<std::vector<std::string>>(); // full paths; "" = a new file
        std::vector<vti::PopupItem> items;
        std::map<std::string, std::vector<int>> folders;
        auto index = std::make_shared<std::map<std::vector<int>, int>>(); // menu path -> entry of `files`
        for (auto &[name, dir] : plugin_->remote_template_folders()) {
            const bool own = name == "Personal Remote Templates";
            auto list = vti::remote_template_files(dir);
            if (list.empty() && !(save && own))
                continue;
            if (save && own) {
                files->push_back("");
                (*index)[vti::popup_add_path(items, name + "/Save as new Template..", {""}, folders)] =
                    (int)files->size() - 1;
            }
            for (auto &f : list) {
                files->push_back((std::filesystem::path(dir) / f).string());
                (*index)[vti::popup_add_path(items, name + "/" + vti::remote_template_name(f), {""}, folders)] =
                    (int)files->size() - 1;
            }
        }
        if (items.empty())
            items.push_back({"(none)", false});
        show_popup(std::move(items), -1, 0, where, [this, files, index, save](const std::vector<int> &path) {
            auto it = index->find(path);
            if (it == index->end())
                return;
            if (save)
                plugin_->save_remote_template((*files)[it->second]);
            else
                plugin_->load_remote_template((*files)[it->second]);
            invalid();
        });
    }

    // a parameter's valuelist as a popup at the control; the choice sets the parameter
    void popup_menu(const Hit &h, const CPoint &where)
    {
        const std::string param = h.element->get("Parameter");
        const Parameter *p = plugin_->conditions().find(param);
        if (!p)
            return;
        std::vector<vti::PopupItem> items;
        std::vector<int> values;
        int current = plugin_->ui_value(param, h.part), index = -1;
        for (auto &[v, t] : renderer_.menu_entries(param)) { // the list's order (mapped lists: index, not value)
            if (v == current)
                index = (int)values.size();
            // "-G": the original's marker for an entry shown greyed (e.g. "-GUSB3 L+R" while the third USB output is
            // off, E158) - disabled here, without the marker
            const bool grey = t.rfind("-G", 0) == 0;
            items.push_back({grey ? t.substr(2) : t, !grey});
            values.push_back(v);
        }
        int part = h.part;
        const int per_column = EditorRenderer::menu_items_per_column(values.size()); // 4 x 25 wavetables
        show_list(std::move(items), index, per_column, where, [this, param, part, values](int r) {
            if (r >= 0 && r < (int)values.size()) {
                plugin_->ui_set(param, part, values[r]);
                invalid();
            }
        });
    }

    VirusPlugin *plugin_;
    VirusEditor *editor_;

    // a pane's selection as the renderer frames it (only while the pane shows `bank`)
    void show_selection(int pane, int bank) { renderer_.selected[pane] = {bank, browser_sel_.framed(pane, bank)}; }

    // a pane that switched banks (its dropdown, up/down) loses its selection: the original keeps none per bank (E318)
    void drop_switched_selections()
    {
        for (auto &[pane, param] : pane_param_) {
            const int bank = plugin_->ui_value(param, -1);
            auto it = renderer_.selected.find(pane);
            if (it != renderer_.selected.end() && it->second.first != bank) {
                browser_sel_.at(pane, bank);
                show_selection(pane, bank);
            }
        }
    }

    static bool is_library(int bank) { return bank >= VirusPlugin::LIB_BASE && bank != VirusPlugin::SEARCH_BANK; }

    // while dragging: the dragged browser entries and the slot under the cursor (if it takes the drop) drawn
    // highlighted like the original's (E298 (L7)); a part's patch name only highlights its RAM target slot
    void drag_highlight(int x, int y)
    {
        std::map<int, std::set<int>> hl;
        const bool from_browser = drag_.element && drag_.element->type == "PatchBrowser" && dnd_prog_ >= 0;
        if (from_browser) {
            auto items = browser_sel_.at(dnd_pane_, dnd_bank_).items();
            hl[dnd_bank_] = items.empty() ? std::set<int>{dnd_prog_} : std::set<int>(items.begin(), items.end());
        }
        Hit t = renderer_.hit_test(x, y);
        if (t.element && t.element->type == "PatchBrowser") {
            const int bank = plugin_->ui_value(t.element->get("Parameter"), -1), slot = EditorRenderer::browser_entry(t, x, y);
            const bool ram = bank >= 0 && bank < 4;
            if (slot >= 0 && (ram || (from_browser && is_library(bank))))
                hl[bank].insert(slot);
        }
        if (hl != renderer_.highlighted) {
            renderer_.highlighted = std::move(hl);
            invalid();
        }
    }

    // the dragged selection dropped on a browser slot: onto a RAM bank or a personal library it is written into the
    // positions from there on (E294, E298), within the same bank/library it swaps places with them (E297, E298);
    // ROM and the search results take no drop
    void drop_on_browser(const Hit &t, int x, int y)
    {
        const int bank = plugin_->ui_value(t.element->get("Parameter"), -1), slot = EditorRenderer::browser_entry(t, x, y);
        const bool ram = bank >= 0 && bank < 4;
        if ((!ram && !is_library(bank)) || slot < 0)
            return;
        auto programs = browser_sel_.at(dnd_pane_, dnd_bank_).items();
        if (programs.empty())
            programs = {dnd_prog_};
        if (ram && bank == dnd_bank_)
            plugin_->swap_in_ram(bank, programs, slot);
        else if (ram)
            plugin_->drop_to_ram(dnd_bank_, programs, bank, slot);
        else if (bank == dnd_bank_)
            plugin_->swap_in_library(bank - VirusPlugin::LIB_BASE, programs, slot);
        else
            plugin_->drop_to_library(dnd_bank_, programs, bank - VirusPlugin::LIB_BASE, slot);
    }

    // the original's browser menu (E294 (4), E297, E302): "Undo last operation" when there is something to undo,
    // then for RAM "Download entire bank from Virus TI" and "Sort alphabetically", for a personal library "Delete
    // selection" (with a selection) and "Sort alphabetically". Entries that do not apply are left out, not greyed
    void browser_menu(const Hit &h, const CPoint &where)
    {
        const int bank = plugin_->ui_value(h.element->get("Parameter"), -1);
        const int pane = EditorRenderer::browser_pane(h.element->get("Parameter"));
        const bool lib = is_library(bank), ram = bank >= 0 && bank < 4;
        auto selection = browser_sel_.at(pane, bank).items();
        enum Act { Undo, Download, Delete, Sort };
        std::vector<vti::PopupItem> items;
        std::vector<Act> acts;
        auto add = [&](const char *t, Act a) { items.push_back({t}), acts.push_back(a); };
        if (plugin_->can_undo_drop())
            add("Undo last operation", Undo);
        if (ram && !plugin_->downloading())
            add("Download entire bank from Virus TI", Download);
        if (lib && !selection.empty())
            add("Delete selection", Delete);
        if ((ram && !plugin_->downloading()) || lib)
            add("Sort alphabetically", Sort);
        if (items.empty())
            return;
        show_popup(std::move(items), -1, 0, where, [this, bank, pane, lib, selection, acts](const std::vector<int> &path) {
            if (path.empty() || path.front() < 0 || path.front() >= (int)acts.size())
                return;
            bool done = false;
            switch (acts[path.front()]) {
            case Undo:
                done = plugin_->undo_drop();
                break;
            case Download:
                done = plugin_->download_bank(bank);
                break;
            case Delete:
                done = plugin_->delete_from_library(bank - VirusPlugin::LIB_BASE, selection);
                break;
            case Sort:
                done = lib ? plugin_->sort_library(bank - VirusPlugin::LIB_BASE) : plugin_->sort_ram_bank(bank);
                break;
            }
            if (acts[path.front()] == Delete || acts[path.front()] == Sort) {
                browser_sel_.at(pane, bank).clear();
                show_selection(pane, bank);
            }
            if (done)
                invalid();
        });
    }

    void size_menu(const CPoint &where)
    {
        static const double sizes[] = {1.0, 1.25, 1.5, 1.75, 2.0};
        std::vector<vti::PopupItem> items;
        int checked = -1;
        for (double z : sizes) {
            char t[32];
            snprintf(t, sizeof t, "Size %d %%", (int)std::lround(z * 100));
            if (std::fabs(editor_->user_zoom() - z) < 0.01)
                checked = (int)items.size();
            items.push_back({t, true});
        }
        // the zoom changes the frame: after the menu's session has ended
        show_list(std::move(items), checked, 0, where, [this](int idx) {
            if (idx >= 0 && idx < (int)(sizeof sizes / sizeof sizes[0]))
                if (auto *f = getFrame())
                    after_event(f, [this, idx]() { editor_->set_user_zoom(sizes[idx]); });
        });
    }
    EditorRenderer renderer_;
    std::map<std::string, SharedPointer<CBitmap>> bitmaps_;
    VstguiCanvas canvas_;
    SharedPointer<CVSTGUITimer> timer_;
    Hit drag_;
    bool partial_ = false;
    std::vector<std::array<int, 4>> drag_rects_;
    double drag_y_ = 0, drag_x_ = 0;
    bool dnd_ = false;             // a patch is being dragged
    int dnd_bank_ = 0, dnd_prog_ = -1, dnd_pane_ = 0;
    vti::PaneSelections browser_sel_;      // one selection per browser pane (E318)
    std::map<int, std::string> pane_param_; // pane -> its "Browser N/Selected Bank" parameter
    int drag_value_ = 0;
};

// the user's size choice, kept for every editor (not per project): <plug-in folder>/ui-zoom ("1.5");
// VTI_NATIVE_ZOOM overrides it (tests)
static std::string zoom_file() { return vti::cc::plugin_dir() + "/ui-zoom"; }

static double load_zoom()
{
    double z = 1.0;
    if (const char *e = getenv("VTI_NATIVE_ZOOM"))
        z = atof(e);
    else if (FILE *f = fopen(zoom_file().c_str(), "r")) {
        if (fscanf(f, "%lf", &z) != 1)
            z = 1.0;
        fclose(f);
    }
    return z >= 0.5 && z <= 4.0 ? z : 1.0;
}

VirusEditor::VirusEditor(VirusPlugin *plugin) : VSTGUIEditor(plugin), plugin_(plugin), user_zoom_(load_zoom())
{
    apply_zoom(); // no frame yet: only the size the host asks for before open()
}

Steinberg::tresult PLUGIN_API VirusEditor::queryInterface(const Steinberg::TUID iid, void **obj)
{
    QUERY_INTERFACE(iid, obj, Steinberg::IPlugViewContentScaleSupport::iid, Steinberg::IPlugViewContentScaleSupport)
    return VSTGUIEditor::queryInterface(iid, obj);
}

void VirusEditor::apply_zoom()
{
    if (!frame) {
        rect = Steinberg::ViewRect(0, 0, (int)std::lround(plugin_->layout().width * zoom()),
                                   (int)std::lround(plugin_->layout().height * zoom()));
        return;
    }
    // scales drawing and mouse events; the new size goes to the host through beforeSizeChange (resizeView); if the
    // host refuses, VSTGUI keeps the old zoom
    frame->setZoom(zoom());
}

Steinberg::tresult PLUGIN_API VirusEditor::setContentScaleFactor(ScaleFactor factor)
{
    content_scale_ = factor > 0 ? factor : 1.0;
    apply_zoom();
    return Steinberg::kResultTrue;
}

void VirusEditor::set_user_zoom(double z)
{
    if (z == user_zoom_)
        return;
    user_zoom_ = z;
    apply_zoom();
    if (FILE *f = fopen(zoom_file().c_str(), "w")) {
        fprintf(f, "%g\n", z);
        fclose(f);
    }
}

bool VirusEditor::beforeSizeChange(const CRect &newSize, const CRect &)
{
    if (resizing_ || !plugFrame)
        return true;
    resizing_ = true; // the host answers with onSize, which sets the frame size again
    Steinberg::ViewRect vr(0, 0, (int)std::lround(newSize.getWidth()), (int)std::lround(newSize.getHeight()));
    bool ok = plugFrame->resizeView(this, &vr) == Steinberg::kResultTrue;
    resizing_ = false;
    return ok;
}

Steinberg::tresult PLUGIN_API VirusEditor::onSize(Steinberg::ViewRect *newSize)
{
    bool was = resizing_;
    resizing_ = true;
    auto r = VSTGUIEditor::onSize(newSize);
    resizing_ = was;
    return r;
}

bool PLUGIN_API VirusEditor::open(void *parent, const PlatformType &platformType)
{
    if (frame)
        return false;
    CRect size(0, 0, plugin_->layout().width, plugin_->layout().height);
    frame = new CFrame(size, this);
    // VSTGUI's own text field (Linux) marks selected text in the frame's focus colour, red by default: a light blue
    // like the system selection on Windows and macOS (the black name stays readable); focus frames are not drawn
    frame->setFocusColor(CColor(179, 215, 255, 255));
#if SMTG_OS_LINUX
    // VSTGUI on X11 needs the host's run loop (IRunLoop from the plug frame or the host context)
    if (open_frames == 0) {
        if (plugFrame)
            Steinberg::Linux::setupVSTGUIRunloop(plugFrame); // hosts offer IRunLoop on the frame or the host context
        if (auto lf = getPlatformFactory().asLinuxFactory())
            pinned_runloop = lf->getRunLoop();
    }
    keep_runloop();
    X11::FrameConfig cfg;
    if (auto lf = getPlatformFactory().asLinuxFactory())
        cfg.runLoop = lf->getRunLoop();
    if (!cfg.runLoop || !frame->open(parent, platformType, &cfg)) { // no run loop or no window: no editor
        frame->forget();                                              // (server_bitmap needs the platform frame)
        frame = nullptr;
        return false;
    }
    open_frames++;
#else
    if (!frame->open(parent, platformType)) {
        frame->forget();
        frame = nullptr;
        return false;
    }
#endif
    frame->addView(new EditorCanvasView(plugin_, size, this));
    if (zoom() != 1.0) { // the host already got the scaled size from getSize()
        resizing_ = true;
        frame->setZoom(zoom());
        resizing_ = false;
    }
    plugin_->editor_event("opened");
    plugin_->editor_opened(); // only now: the early returns above leave no editor
    return true;
}

VirusEditor::~VirusEditor()
{
    close(); // a host that releases the view without removed(): the frame, its 50 ms timer and open_frames go too
}

Steinberg::tresult PLUGIN_API VirusEditor::removed()
{
#if SMTG_OS_LINUX
    keep_runloop(); // the SDK stops its timer before close(): through the adapter that registered it (E183)
#endif
    return VSTGUIEditor::removed();
}

void PLUGIN_API VirusEditor::close()
{
    if (frame) {
        plugin_->editor_closed(); // open host edits end now (no more timer ticks)
#if SMTG_OS_LINUX
        keep_runloop(); // first: the menu frames and the frame unregister their handlers through the adapter that
                        // registered them (E183; review 2026-10-19: the menu frames went before this)
#endif
        PopupView::close_windows(frame); // an open menu's own frames go first
#if SMTG_OS_LINUX
        if (--open_frames == 0)
            x11_last_frame_closing();
        else if (ref) { // `ref` may live on this editor's window: the next pixmap is made on a remaining one
            cairo_surface_destroy(ref);
            ref = nullptr;
        }
#endif
        frame->forget();
        frame = nullptr;
#if SMTG_OS_LINUX
        if (open_frames == 0)
            pinned_runloop = nullptr; // nothing registered any more: the next editor sets up a fresh one
#endif
        plugin_->editor_event("closed");
    }
}

} // namespace vti
