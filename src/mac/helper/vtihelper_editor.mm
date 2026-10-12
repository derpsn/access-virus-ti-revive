// vtihelper_editor.mm - see vtihelper_editor.h. The original's editor (VSTGUI, CoreGraphics drawing, E104) lives in a
// borderless window with alpha 0 that ignores the mouse, placed exactly behind the bridge's view: invisible and
// click-through, but at the right screen position for VSTGUI's popup menus and text fields. Whenever a view asks for
// a redraw, the window's content is rendered into a double-buffered pixel shm, which the bridge draws; the bridge's
// mouse and key events are replayed into this window.
#import <Cocoa/Cocoa.h>
#include <fcntl.h>
#include <objc/runtime.h>
#include <string>
#include <sys/mman.h>
#include <unistd.h>

#include "pluginterfaces/gui/iplugview.h"
#include "vtihelper_editor.h"

using namespace Steinberg;

namespace Steinberg {
DEF_CLASS_IID(IPlugView)
DEF_CLASS_IID(IPlugFrame)
}

// --- redraw detection: every -setNeedsDisplay(InRect): in this process marks the editors dirty
static volatile bool g_dirty = true;
static IMP g_orig_sndir, g_orig_snd;
static void swz_sndir(id self, SEL sel, NSRect r)
{
    g_dirty = true;
    ((void (*)(id, SEL, NSRect))g_orig_sndir)(self, sel, r);
}
static void swz_snd(id self, SEL sel, BOOL f)
{
    g_dirty = true;
    ((void (*)(id, SEL, BOOL))g_orig_snd)(self, sel, f);
}
static void install_dirty_hooks()
{
    static bool done;
    if (done)
        return;
    done = true;
    Method m1 = class_getInstanceMethod([NSView class], @selector(setNeedsDisplayInRect:));
    g_orig_sndir = method_setImplementation(m1, (IMP)swz_sndir);
    Method m2 = class_getInstanceMethod([NSView class], @selector(setNeedsDisplay:));
    g_orig_snd = method_setImplementation(m2, (IMP)swz_snd);
}

struct HelperEditor;
@interface TIRRenderTimer : NSObject
@property(assign) HelperEditor *editor;
- (void)tick:(NSTimer *)t;
@end

struct HelperEditor : IPlugFrame {
    std::atomic<uint32> refs{1};
    IPlugView *view = nullptr;
    NSWindow *win = nil;
    NSView *container = nil;
    NSTimer *timer = nil;
    TIRRenderTimer *target = nil;
    std::string prefix;
    int gen = 0; // pixel shm generation (a resize creates a new shm)
    tirb_pixels *px = nullptr;
    size_t px_size = 0;
    char px_name[32] = "";
    double scale = 1, top_x = 0, top_y = 0;
    int w = 0, h = 0;
    editor_resize_cb on_resize = nullptr;
    void *ctx = nullptr;

    tresult PLUGIN_API queryInterface(const TUID iid, void **obj) override
    {
        if (FUnknownPrivate::iidEqual(iid, IPlugFrame::iid) || FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {
            *obj = this;
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return ++refs; }
    uint32 PLUGIN_API release() override { return --refs; }
    tresult PLUGIN_API resizeView(IPlugView *v, ViewRect *r) override // the original changes its size
    {
        if (!r)
            return kInvalidArgument;
        set_size(r->getWidth(), r->getHeight());
        v->onSize(r);
        tirb_editor_reply rep{w, h, {}};
        strcpy(rep.pixels_shm, px_name);
        if (on_resize)
            on_resize(ctx, rep);
        return kResultOk;
    }

    bool map_pixels()
    {
        if (px) {
            munmap(px, px_size);
            shm_unlink(px_name);
            px = nullptr;
        }
        uint32_t pw = (uint32_t)(w * scale), ph = (uint32_t)(h * scale);
        px_size = sizeof(tirb_pixels) + 2ull * pw * ph * 4;
        snprintf(px_name, sizeof px_name, "%s-v%d", prefix.c_str(), ++gen);
        shm_unlink(px_name);
        int fd = shm_open(px_name, O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd < 0 || ftruncate(fd, (off_t)px_size)) {
            if (fd >= 0)
                close(fd);
            return false;
        }
        void *m = mmap(nullptr, px_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        close(fd);
        if (m == MAP_FAILED)
            return false;
        px = (tirb_pixels *)m;
        memset(px, 0, sizeof(tirb_pixels));
        px->pw = pw, px->ph = ph, px->scale = (uint32_t)scale;
        g_dirty = true;
        return true;
    }
    void set_size(int nw, int nh)
    {
        w = nw, h = nh;
        [container setFrame:NSMakeRect(0, 0, w, h)];
        place();
        map_pixels();
    }
    void place() // window top-left at (top_x, top_y), Cocoa screen coordinates
    {
        [win setFrame:NSMakeRect(top_x, top_y - h, w, h) display:NO];
    }
    void render()
    {
        if (!px || !g_dirty)
            return;
        g_dirty = false;
        uint32_t back = px->front ^ 1;
        uint8_t *mem = px->data + (size_t)back * px->pw * px->ph * 4;
        CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        CGContextRef cg = CGBitmapContextCreate(mem, px->pw, px->ph, 8, px->pw * 4, cs,
                                                kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
        CGColorSpaceRelease(cs);
        if (!cg)
            return;
        CGContextClearRect(cg, CGRectMake(0, 0, px->pw, px->ph));
        CGContextScaleCTM(cg, scale, scale);
        NSGraphicsContext *gc = [NSGraphicsContext graphicsContextWithCGContext:cg flipped:NO];
        [NSGraphicsContext saveGraphicsState];
        [NSGraphicsContext setCurrentContext:gc];
        [container displayRectIgnoringOpacity:[container bounds] inContext:gc];
        [NSGraphicsContext restoreGraphicsState];
        CGContextRelease(cg);
        __atomic_store_n(&px->front, back, __ATOMIC_RELEASE);
        __atomic_add_fetch(&px->frame, 1, __ATOMIC_RELEASE);
    }
};

@implementation TIRRenderTimer
- (void)tick:(NSTimer *)t
{
    (void)t;
    if (self.editor)
        self.editor->render();
}
@end

HelperEditor *editor_open(Vst::IEditController *ctl, const char *shm_prefix, double x, double y, double scale,
                          tirb_editor_reply &reply, editor_resize_cb on_resize, void *ctx)
{
    static bool app_ready;
    if (!app_ready) { // the helper runs a plain CFRunLoop, not -[NSApp run]: set AppKit up once by hand
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory]; // no Dock icon, no menu bar
        [NSApp finishLaunching];
        app_ready = true;
    }
    install_dirty_hooks();
    IPlugView *v = ctl->createView(Vst::ViewType::kEditor);
    if (!v)
        return nullptr;
    if (v->isPlatformTypeSupported(kPlatformTypeNSView) != kResultTrue) {
        v->release();
        return nullptr;
    }
    HelperEditor *e = new HelperEditor;
    e->view = v;
    e->prefix = shm_prefix;
    e->scale = scale >= 2 ? 2 : 1;
    e->top_x = x, e->top_y = y;
    e->on_resize = on_resize, e->ctx = ctx;
    ViewRect r{};
    v->getSize(&r);
    e->w = r.getWidth() > 0 ? r.getWidth() : 800;
    e->h = r.getHeight() > 0 ? r.getHeight() : 600;
    e->win = [[NSWindow alloc] initWithContentRect:NSMakeRect(x, y - e->h, e->w, e->h)
                                         styleMask:NSWindowStyleMaskBorderless
                                           backing:NSBackingStoreBuffered
                                             defer:NO];
    [e->win setReleasedWhenClosed:NO];
    [e->win setAlphaValue:0.0];
    [e->win setIgnoresMouseEvents:YES];
    [e->win setHasShadow:NO];
    [e->win setAcceptsMouseMovedEvents:YES];
    e->container = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, e->w, e->h)];
    [e->win setContentView:e->container];
    [e->win orderFront:nil];
    v->setFrame(e);
    if (v->attached((__bridge void *)e->container, kPlatformTypeNSView) != kResultOk) {
        [e->win close];
        v->release();
        delete e;
        return nullptr;
    }
    if (!e->map_pixels()) {
        editor_close(e);
        return nullptr;
    }
    e->target = [TIRRenderTimer new];
    e->target.editor = e;
    e->timer = [NSTimer timerWithTimeInterval:1.0 / 30
                                       target:e->target
                                     selector:@selector(tick:)
                                     userInfo:nil
                                      repeats:YES];
    [[NSRunLoop mainRunLoop] addTimer:e->timer forMode:NSRunLoopCommonModes]; // also while a popup menu tracks
    e->render();
    reply.width = e->w, reply.height = e->h;
    strcpy(reply.pixels_shm, e->px_name);
    return e;
}

void editor_close(HelperEditor *e)
{
    if (!e)
        return;
    [e->timer invalidate];
    e->target.editor = nullptr;
    e->view->removed();
    e->view->setFrame(nullptr);
    e->view->release();
    [e->win orderOut:nil];
    [e->win close];
    if (e->px) {
        munmap(e->px, e->px_size);
        shm_unlink(e->px_name);
    }
    delete e;
}

void editor_move(HelperEditor *e, double x, double y)
{
    if (!e)
        return;
    e->top_x = x, e->top_y = y;
    e->place();
}

static NSEventModifierFlags mods(int32_t m)
{
    NSEventModifierFlags f = 0;
    if (m & 1)
        f |= NSEventModifierFlagShift;
    if (m & 2)
        f |= NSEventModifierFlagControl;
    if (m & 4)
        f |= NSEventModifierFlagOption;
    if (m & 8)
        f |= NSEventModifierFlagCommand;
    return f;
}

void editor_input(HelperEditor *e, const tirb_input &in)
{
    if (!e)
        return;
    NSPoint p = NSMakePoint(in.x, e->h - in.y); // view (top-left origin) -> window coordinates
    NSTimeInterval ts = [[NSProcessInfo processInfo] systemUptime];
    NSInteger wn = [e->win windowNumber];
    NSEventModifierFlags mf = mods(in.modifiers);
    NSEvent *ev = nil;
    bool right = in.button == 1;
    switch (in.kind) {
    case 1: // down
    case 2: // up
    case 3: // drag
    case 4: // move
    case 8: // enter
    case 9: { // exit
        NSEventType t = in.kind == 1   ? (right ? NSEventTypeRightMouseDown : NSEventTypeLeftMouseDown)
                        : in.kind == 2 ? (right ? NSEventTypeRightMouseUp : NSEventTypeLeftMouseUp)
                        : in.kind == 3 ? (right ? NSEventTypeRightMouseDragged : NSEventTypeLeftMouseDragged)
                                       : NSEventTypeMouseMoved;
        ev = [NSEvent mouseEventWithType:t
                                location:p
                           modifierFlags:mf
                               timestamp:ts
                            windowNumber:wn
                                 context:nil
                             eventNumber:0
                              clickCount:in.click_count > 0 ? in.click_count : 1
                                pressure:in.kind == 1 || in.kind == 3 ? 1.0f : 0.0f];
        break;
    }
    case 5: { // wheel: a CGEvent placed on our window, then dispatched like a real one
        CGEventRef cg = CGEventCreateScrollWheelEvent2(nullptr, kCGScrollEventUnitPixel, 2, (int32_t)in.dy,
                                                       (int32_t)in.dx, 0);
        NSRect wr = [e->win frame];
        NSPoint sp = NSMakePoint(wr.origin.x + p.x, wr.origin.y + p.y);
        CGFloat main_h = NSMaxY([[[NSScreen screens] firstObject] frame]);
        CGEventSetLocation(cg, CGPointMake(sp.x, main_h - sp.y));
        CGEventSetIntegerValueField(cg, kCGMouseEventWindowUnderMousePointer, wn);
        CGEventSetIntegerValueField(cg, kCGMouseEventWindowUnderMousePointerThatCanHandleThisEvent, wn);
        CGEventSetFlags(cg, (CGEventFlags)mf);
        ev = [NSEvent eventWithCGEvent:cg];
        CFRelease(cg);
        NSView *target = [e->container hitTest:p];
        if (target && ev) {
            [target scrollWheel:ev];
            return;
        }
        break;
    }
    case 6:
    case 7: {
        NSString *chars = [NSString stringWithUTF8String:in.chars] ?: @"";
        ev = [NSEvent keyEventWithType:in.kind == 6 ? NSEventTypeKeyDown : NSEventTypeKeyUp
                              location:p
                         modifierFlags:mf
                             timestamp:ts
                          windowNumber:wn
                               context:nil
                            characters:chars
           charactersIgnoringModifiers:chars
                             isARepeat:NO
                               keyCode:(unsigned short)in.key_code];
        break;
    }
    }
    if (ev)
        [e->win sendEvent:ev];
}
