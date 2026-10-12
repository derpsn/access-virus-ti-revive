// tirbridge_view.mm - see tirbridge_view.h.
#import <Cocoa/Cocoa.h>
#include <atomic>
#include <fcntl.h>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "tirbridge_view.h"

using namespace Steinberg;

namespace Steinberg {
DEF_CLASS_IID(IPlugView)
DEF_CLASS_IID(IPlugFrame)
}

struct BridgeView;

@interface TIRMirrorView : NSView
@property(assign) BridgeView *owner;
@end

struct BridgeView : IPlugView {
    std::atomic<uint32> refs{1};
    EditorLink *link;
    IPlugFrame *frame = nullptr;
    TIRMirrorView *ns = nil;
    NSTimer *timer = nil;
    bool opened = false;
    int w = 0, h = 0;
    tirb_pixels *px = nullptr;
    size_t px_size = 0;
    uint32_t last_frame = ~0u;
    CGImageRef image = nullptr;

    explicit BridgeView(EditorLink *l) : link(l) {}
    virtual ~BridgeView()
    {
        unmap();
        if (image)
            CGImageRelease(image);
    }

    tresult PLUGIN_API queryInterface(const TUID iid, void **obj) override
    {
        if (FUnknownPrivate::iidEqual(iid, IPlugView::iid) || FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {
            addRef();
            *obj = this;
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return ++refs; }
    uint32 PLUGIN_API release() override
    {
        uint32 r = --refs;
        if (!r) {
            if (opened)
                link->editor_close();
            link->editor_view_gone();
            delete this;
        }
        return r;
    }

    bool open()
    {
        if (opened)
            return true;
        NSPoint tl = top_left();
        tirb_editor_reply r{};
        double scale = [[NSScreen mainScreen] backingScaleFactor];
        if (!link->editor_open(tl.x, tl.y, scale, r))
            return false;
        opened = true;
        w = r.width, h = r.height;
        return map(r.pixels_shm);
    }
    bool map(const char *name)
    {
        unmap();
        int fd = shm_open(name, O_RDONLY, 0);
        if (fd < 0)
            return false;
        struct stat st;
        fstat(fd, &st);
        void *m = mmap(nullptr, (size_t)st.st_size, PROT_READ, MAP_SHARED, fd, 0);
        close(fd);
        if (m == MAP_FAILED)
            return false;
        px = (tirb_pixels *)m;
        px_size = (size_t)st.st_size;
        last_frame = ~0u;
        return true;
    }
    void unmap()
    {
        if (px)
            munmap(px, px_size);
        px = nullptr;
    }
    NSPoint top_left() // Cocoa screen coordinates of the view's top-left corner
    {
        if (!ns || ![ns window])
            return NSMakePoint(0, 0);
        NSRect r = [[ns window] convertRectToScreen:[ns convertRect:[ns bounds] toView:nil]];
        return NSMakePoint(r.origin.x, r.origin.y + r.size.height);
    }
    void moved()
    {
        if (!opened)
            return;
        NSPoint p = top_left();
        link->editor_move(p.x, p.y);
    }
    void poll() // timer: pick up a new frame
    {
        if (!px)
            return;
        uint32_t f = __atomic_load_n(&px->frame, __ATOMIC_ACQUIRE);
        if (f == last_frame || !px->pw || !px->ph)
            return;
        last_frame = f;
        uint32_t front = __atomic_load_n(&px->front, __ATOMIC_ACQUIRE) & 1;
        size_t bytes = (size_t)px->pw * px->ph * 4;
        CFDataRef data = CFDataCreate(nullptr, px->data + front * bytes, (CFIndex)bytes); // copy: no tearing
        CGDataProviderRef dp = CGDataProviderCreateWithCFData(data);
        CFRelease(data);
        CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        CGImageRef img = CGImageCreate(px->pw, px->ph, 8, 32, px->pw * 4, cs,
                                       kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little, dp, nullptr,
                                       false, kCGRenderingIntentDefault);
        CGColorSpaceRelease(cs);
        CGDataProviderRelease(dp);
        if (image)
            CGImageRelease(image);
        image = img;
        [ns setNeedsDisplay:YES];
    }

    // IPlugView
    tresult PLUGIN_API isPlatformTypeSupported(FIDString type) override
    {
        return strcmp(type, kPlatformTypeNSView) == 0 ? kResultTrue : kResultFalse;
    }
    tresult PLUGIN_API attached(void *parent, FIDString type) override
    {
        if (strcmp(type, kPlatformTypeNSView) || !open())
            return kResultFalse;
        ns = [[TIRMirrorView alloc] initWithFrame:NSMakeRect(0, 0, w, h)];
        ns.owner = this;
        [(__bridge NSView *)parent addSubview:ns];
        TIRMirrorView *v = ns;
        timer = [NSTimer timerWithTimeInterval:1.0 / 60
                                       repeats:YES
                                         block:^(NSTimer *) {
                                           if (v.owner)
                                               v.owner->poll();
                                         }];
        [[NSRunLoop mainRunLoop] addTimer:timer forMode:NSRunLoopCommonModes];
        moved();
        return kResultOk;
    }
    tresult PLUGIN_API removed() override
    {
        [timer invalidate];
        timer = nil;
        if (ns) {
            ns.owner = nullptr;
            [[NSNotificationCenter defaultCenter] removeObserver:ns];
            [ns removeFromSuperview];
            ns = nil;
        }
        if (opened)
            link->editor_close();
        opened = false;
        unmap();
        return kResultOk;
    }
    tresult PLUGIN_API onWheel(float) override { return kResultFalse; }
    tresult PLUGIN_API onKeyDown(char16, int16, int16) override { return kResultFalse; }
    tresult PLUGIN_API onKeyUp(char16, int16, int16) override { return kResultFalse; }
    tresult PLUGIN_API getSize(ViewRect *size) override
    {
        if (!open())
            return kResultFalse;
        *size = ViewRect(0, 0, w, h);
        return kResultOk;
    }
    tresult PLUGIN_API onSize(ViewRect *) override { return kResultOk; }
    tresult PLUGIN_API onFocus(TBool) override { return kResultOk; }
    tresult PLUGIN_API setFrame(IPlugFrame *f) override
    {
        frame = f;
        return kResultOk;
    }
    tresult PLUGIN_API canResize() override { return kResultFalse; }
    tresult PLUGIN_API checkSizeConstraint(ViewRect *) override { return kResultOk; }

    void resized(const tirb_editor_reply &r) // main thread
    {
        w = r.width, h = r.height;
        map(r.pixels_shm);
        [ns setFrame:NSMakeRect(0, 0, w, h)];
        ViewRect vr(0, 0, w, h);
        if (frame)
            frame->resizeView(this, &vr);
        moved();
    }
};

static int32_t mods(NSEventModifierFlags f)
{
    return (f & NSEventModifierFlagShift ? 1 : 0) | (f & NSEventModifierFlagControl ? 2 : 0) |
           (f & NSEventModifierFlagOption ? 4 : 0) | (f & NSEventModifierFlagCommand ? 8 : 0);
}

@implementation TIRMirrorView {
    NSTrackingArea *_tracking;
}
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent *)e { return YES; }
- (BOOL)isOpaque { return YES; }
- (void)drawRect:(NSRect)r
{
    (void)r;
    CGContextRef cg = [[NSGraphicsContext currentContext] CGContext];
    if (self.owner && self.owner->image)
        CGContextDrawImage(cg, NSRectToCGRect([self bounds]), self.owner->image);
    else {
        CGContextSetRGBFillColor(cg, 0.1, 0.1, 0.1, 1);
        CGContextFillRect(cg, NSRectToCGRect([self bounds]));
    }
}
- (void)viewDidMoveToWindow
{
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    [self setPostsFrameChangedNotifications:YES];
    if ([self window]) {
        for (NSNotificationName n in @[ NSWindowDidMoveNotification, NSWindowDidResizeNotification,
                                        NSWindowDidChangeScreenNotification ])
            [[NSNotificationCenter defaultCenter] addObserver:self
                                                     selector:@selector(geometryChanged:)
                                                         name:n
                                                       object:[self window]];
        [[NSNotificationCenter defaultCenter] addObserver:self
                                                 selector:@selector(geometryChanged:)
                                                     name:NSViewFrameDidChangeNotification
                                                   object:self];
        if (self.owner)
            self.owner->moved();
    }
}
- (void)geometryChanged:(NSNotification *)n
{
    (void)n;
    if (self.owner)
        self.owner->moved();
}
- (void)updateTrackingAreas
{
    if (_tracking)
        [self removeTrackingArea:_tracking];
    _tracking = [[NSTrackingArea alloc]
        initWithRect:[self bounds]
             options:NSTrackingMouseEnteredAndExited | NSTrackingMouseMoved | NSTrackingActiveAlways
               owner:self
            userInfo:nil];
    [self addTrackingArea:_tracking];
    [super updateTrackingAreas];
}
- (void)send:(NSEvent *)e kind:(int32_t)kind button:(int32_t)button
{
    if (!self.owner)
        return;
    NSPoint p = [self convertPoint:[e locationInWindow] fromView:nil];
    tirb_input in{};
    in.kind = kind;
    in.button = button;
    in.modifiers = mods([e modifierFlags]);
    in.x = (float)p.x;
    in.y = (float)([self bounds].size.height - p.y);
    if (kind >= 1 && kind <= 3)
        in.click_count = (int32_t)[e clickCount];
    if (kind == 5) {
        in.dx = (float)[e scrollingDeltaX];
        in.dy = (float)[e scrollingDeltaY];
        if (![e hasPreciseScrollingDeltas])
            in.dx *= 10, in.dy *= 10;
    }
    if (kind == 6 || kind == 7) {
        in.key_code = [e keyCode];
        const char *c = [[e characters] UTF8String];
        if (c)
            strncpy(in.chars, c, sizeof in.chars - 1);
    }
    self.owner->link->editor_input(in);
}
- (void)mouseDown:(NSEvent *)e
{
    [[self window] makeFirstResponder:self];
    [self send:e kind:1 button:0];
}
- (void)mouseUp:(NSEvent *)e { [self send:e kind:2 button:0]; }
- (void)mouseDragged:(NSEvent *)e { [self send:e kind:3 button:0]; }
- (void)rightMouseDown:(NSEvent *)e { [self send:e kind:1 button:1]; }
- (void)rightMouseUp:(NSEvent *)e { [self send:e kind:2 button:1]; }
- (void)rightMouseDragged:(NSEvent *)e { [self send:e kind:3 button:1]; }
- (void)mouseMoved:(NSEvent *)e { [self send:e kind:4 button:0]; }
- (void)mouseEntered:(NSEvent *)e { [self send:e kind:8 button:0]; }
- (void)mouseExited:(NSEvent *)e { [self send:e kind:9 button:0]; }
- (void)scrollWheel:(NSEvent *)e { [self send:e kind:5 button:0]; }
- (void)keyDown:(NSEvent *)e { [self send:e kind:6 button:0]; }
- (void)keyUp:(NSEvent *)e { [self send:e kind:7 button:0]; }
@end

IPlugView *bridge_create_view(EditorLink *link) { return new BridgeView(link); }

void bridge_view_resized(IPlugView *view, const tirb_editor_reply &r)
{
    BridgeView *v = static_cast<BridgeView *>(view);
    v->addRef();
    tirb_editor_reply copy = r;
    dispatch_async(dispatch_get_main_queue(), ^{
      v->resized(copy);
      v->release();
    });
}
