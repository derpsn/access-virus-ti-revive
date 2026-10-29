// editorrender.h - draws the original editor layout (editormodel.h) on any canvas and finds the control under the
// mouse. Shared by tools/vtirender (cairo, checks against screenshots of the original) and the plug-in (VSTGUI).
// Rendering rules: docs/formats.md "editor rendering rules".
#pragma once
#include "editormodel.h"
#include <array>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <map>
#include <set>
#include <string>

namespace vti {

class Canvas {
public:
    virtual ~Canvas() = default;
    virtual void *image(const std::string &name) = 0; // a PNG of the user's Access files; nullptr if missing
    virtual int image_width(void *img) = 0;
    virtual int image_height(void *img) = 0;
    // copy the w x h region at (sx, sy) of `img` to (dx, dy) with `alpha`
    virtual void blit(void *img, int sx, int sy, int dx, int dy, int w, int h, double alpha) = 0;
    virtual void fill(int x, int y, int w, int h, Rgba c) = 0;
    virtual void line(double x0, double y0, double x1, double y1, Rgba c) = 0; // 1 pixel wide
    // drawing limited to a rectangle (inside the current clip) until pop_clip; like VSTGUI views, the displays
    // (envelope, filter, oscillators) never draw outside their element
    virtual void push_clip(int x, int y, int w, int h) { (void)x, (void)y, (void)w, (void)h; }
    virtual void pop_clip() {}
    // `img` is a vertical strip of frames `frame_h` tall (knob filmstrips, fonts): canvases that resample images
    // (sharper copies on scaled displays) filter each frame alone. Called before the strip's blits.
    virtual void frames(void *img, int frame_h) { (void)img, (void)frame_h; }
    // text in the system font (`size` points, left aligned, vertically centred) for fields without a bitmap font
    virtual void label(int x, int y, int w, int h, const std::string &text, int size, Rgba c)
    {
        (void)x, (void)y, (void)w, (void)h, (void)text, (void)size, (void)c;
    }
};

struct Hit {
    const Element *element = nullptr; // the topmost visible control with a parameter under the point
    int x = 0, y = 0, part = 0;        // its absolute position and effective part
    bool disabled = false;             // Enabled="false" (only with include_disabled: the help mode)
    std::string why;                   // disabled: the closest condition that shows it (e.g. ".../Type Is Off")
    bool inactive = false;             // greyed: disabled, or its parameter's own condition is false
    std::vector<std::string> depends;  // greyed by its parameter's condition: the parameters that condition tests
};

class EditorRenderer {
public:
    EditorRenderer(const EditorLayout &layout, const Conditions &cond, const RemoteDevice &dev, const RemoteDevice &ed)
        : layout_(layout), cond_(cond), dev_(dev), ed_(ed)
    {
    }

    ValueFn values;                                 // (path, part) -> value
    std::function<std::string(int part)> patch_name; // a part's patch name
    std::function<std::string(int t)> remote_name;  // REMOTE page: remote template t's name (0..31, E193)
    std::function<std::string(int bank, int program)> bank_patch; // a stored patch's name (bank 0..3 = RAM A..D)
    std::function<bool(int bank, int program)> browser_match;     // category filter / search (false = dimmed)
    // live texts (LCD: "VirusControl/Display Parameter" / "Display Value" / "Display Part"); empty = valuelist
    std::function<std::string(const std::string &param)> live_text;
    // a proxy control's target (the soft knobs: the parameter their destination points to), "" for others; the
    // control then shows the target's value, range and value list like the original (E150)
    std::function<std::string(const std::string &param, int part)> proxy_target;
    std::function<int(int channel)> meter;           // MIDI activity 0..127 per channel (part strip VU meters)
    // the classic oscillator display's waves from the user's plug-in binary (AccessFiles::osc_waves); empty = sine
    std::vector<float> osc_waves;
    // sample i (0..63) of the classic oscillator display like the original (E180): Shape crossfades the WaveSelect
    // wave into a saw (first half) and the saw into a pulse (second half), clamped to +-1
    static double osc_classic_value(int shape, int pulsewidth, int wave, const std::vector<float> &waves, int i);
    // HyperSaw display (original KOscHypersawShape, E192): lines and the last line's height (0..1) for Density 0..127;
    // spacing for width w and DetuneSpread 0..1; x of line i (0 centre, then right/left alternately)
    static std::pair<int, double> hypersaw_lines(int density);
    static double hypersaw_spacing(int w, double spread);
    static int hypersaw_x(int i, double spacing, int w, int left);
    int part = 0;                                    // the current (panel) part
    int layer = 0;                                   // 0 = everything, 1 = static only, 2 = controls only
    std::vector<const Element *> static_key;          // filled by a layer-1 render: visible conditional statics
    // the visible conditional static elements right now (compare with static_key to know if the cache is valid)
    std::vector<const Element *> static_signature() const;
    double disabled_alpha = 95.0 / 255; // the original's inactive controls without a Disabled picture (E360)
    // the element held down by the mouse (drawn with PressedBack)
    const Element *pressed = nullptr;

    // which parameters the last full render read, for partial repaints of value changes (automation, Virus panel):
    // `cond` = read by a show/hide condition (a change can alter the structure -> full repaint), `rects` = the areas
    // of the elements that read it while drawing (knobs, displays, texts). Filled by render() when `deps` is set.
    struct Deps {
        std::unordered_set<std::string> cond;
        std::unordered_map<std::string, std::vector<std::array<int, 4>>> rects;
    };
    Deps *deps = nullptr;
    void render(Canvas &c);
    // one line in an LCD bitmap font, centred in the rectangle (the plug-in's own notes on the original's pages)
    void status_text(Canvas &c, const std::string &font, const std::string &str, int x, int y, int w, int h)
    {
        text(c, font, str, x, y, w, h, "Middle Center");
    }
    // repaint area: elements outside it are skipped (with their children); default = everything
    int clip_x0 = -100000, clip_y0 = -100000, clip_x1 = 100000, clip_y1 = 100000;
    // include_disabled: also controls marked Enabled="false" (the help mode explains them, with the reason)
    Hit hit_test(int x, int y, bool include_disabled = false) const;
    // the right button: only menus take it (VSTGUI's knobs, sliders and buttons react to the left button), so a right
    // click reaches a menu under another control - the wavetable menu under the oscillator ring (original, E189)
    Hit menu_hit_test(int x, int y) const;
    // where a menu's popup opens: a Menu over its control (popup style), a HierarchicMenu below it (original 0x1800191d0)
    static std::pair<int, int> menu_anchor(const Hit &h);
    // entries per popup column (the original's value lists: a quarter above 32 entries, E195; VSTGUI breaks columns
    // only below 160 entries), 0 = one column
    static int menu_items_per_column(size_t entries);
    // REMOTE page: the template (0..31) under a point inside a RemoteBrowser hit, or -1
    static int remote_entry(const Hit &h, int x, int y);
    // every visible control with a parameter (absolute position, effective part), in drawing order (help mode)
    std::vector<Hit> visible_controls(bool include_disabled = false) const;
    // false for inactive controls (the parameter's own condition is false; soft knobs without destination): they are
    // drawn translucent and ignore the mouse
    bool param_active(const std::string &param, int part) const;
    // absolute rectangles (x, y, w, h) of all elements bound to `param` (for partial repaints)
    std::vector<std::array<int, 4>> param_rects(const std::string &param) const;
    // the browser entry (program 0..127) under a point inside a PatchBrowser hit, or -1
    static int browser_entry(const Hit &h, int x, int y);
    // The draggable handles of a filter response or ADSTR envelope display (E371): position (editor coordinates, the
    // drawing's geometry) and the parameters a drag moves horizontally / vertically ("" = none). Empty for others.
    struct GraphHandle {
        double x = 0, y = 0;
        std::string xparam, yparam;
    };
    std::vector<GraphHandle> graph_handles(const Element &e, int x, int y, int part) const;
    // The line segments of such a display (E372): end points and what a click on the segment grabs - the parameters
    // of the handle at its end, anchored at that handle (empty parameters: a hit that drags nothing, e.g. the
    // lowpass' fall)
    struct GraphSegment {
        double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        GraphHandle grab;
    };
    std::vector<GraphSegment> graph_segments(const Element &e, int x, int y, int part) const;
    // the handles, then the segments' grabs: graph_grab's index points into this list
    std::vector<GraphHandle> graph_grabs(const Element &e, int x, int y, int part) const;
    // what a click grabs, like the original's KEnvelope: the nearest handle within 5 px, else the last segment whose
    // box (end points, at least 4 px wide and tall) holds the point; -1 for neither
    int graph_grab(const Element &e, int x, int y, int part, int px, int py) const;
    // a filter / envelope display with a handle or segment under the point, even under another control (the filter
    // display lies inside the big cutoff knob); no element if none
    Hit graph_hit_test(int px, int py) const;
    // the value of `param` that puts its handle at (hx, hy) - the inverse of the drawing, clamped to 0..127
    int graph_value(const Element &e, int x, int y, int part, const std::string &param, double hx, double hy) const;
    // the pane of a PatchBrowser's parameter "Browser N/Selected Bank": N (0 upper, 1 lower), 0 for anything else
    static int browser_pane(const std::string &param);
    // pane -> (the bank it showed, its selected programs), framed only while the pane shows that bank (E318: the
    // original keeps the selection per pane, not per bank)
    std::map<int, std::pair<int, std::set<int>>> selected;
    std::map<int, std::set<int>> highlighted; // bank -> entries of a drag: its source and the slot under the cursor
    int focused_pane = -1; // the browser pane clicked last: its selection is filled, the other one framed (E318)
    std::string value_text(const std::string &param, int v) const;
    std::vector<std::pair<int, std::string>> menu_entries(const std::string &param) const; // (value, text), menu order
    // the value a control draws for `v`: clamped to the range like the original (Init: Vocoder Bands 127 -> "32"),
    // a mapped list's own value as it is (mod matrix destinations above maxValue, E309)
    int shown_value(const std::string &param, int v) const;

private:
    const EditorLayout &layout_;
    const Conditions &cond_;
    const RemoteDevice &dev_, &ed_;
    static int own_part(const Element &e);
    bool dep_cond_ = false;           // render(): values are read by a condition
    std::array<int, 4> dep_rect_{};   // render(): the element drawing itself right now (w = 0: none)
    void dep_read(const std::string &path);
    void draw(Canvas &c, const Element &e, int ox, int oy, int inherited);
    // a frame (sy..sy+fh) of `img`, nine-part tiled to w x h when the bitmap is marked tiled
    void image(Canvas &c, const std::string &name, void *img, int sy, int fh, int x, int y, int w, int h, double alpha);
    void text(Canvas &c, const std::string &font, const std::string &str, int x, int y, int w, int h,
              const std::string &align, double alpha = 1.0);
    std::vector<std::string> inactive_inputs(const std::string &param, int part) const;
    bool hit(const Element &e, int ox, int oy, int inherited, int px, int py, Hit &out, bool include_disabled = false,
             const std::string &cond = std::string(), bool menus_only = false, int slack = 0) const;
    void patch_browser(Canvas &c, const Element &e, int x, int y, int bank, int pane);
    void remote_browser(Canvas &c, const Element &e, int x, int y, int selected);
    struct EnvGeom { // the ADSTR display's stage points (envelope())
        double x0, x1, x2, x3, x4, x5, top, bottom, h, ys, yt;
    };
    EnvGeom env_geom(const Element &e, int x, int y, int part) const;
    struct FilterGeom { // the filter display's handle and levels (filter_response())
        double hx, hy, base, top, x_off;
    };
    FilterGeom filter_geom(const Element &e, int x, int y, int part) const;
    void envelope(Canvas &c, const Element &e, int x, int y, int part, double alpha);
    void filter_response(Canvas &c, const Element &e, int x, int y, int part);
    void osc_classic(Canvas &c, const Element &e, int x, int y, int part);
    void osc_hypersaw(Canvas &c, const Element &e, int x, int y, int part);
    void arp_edit(Canvas &c, const Element &e, int x, int y, int part);

public:
    // ArpEdit geometry (shared with the editor's mouse handling): step cell left edge and the bars' bottom row
    static void arp_step_geometry(const Element &e, int x, int y, int step, int &cell_x, int &bottom);
    static int arp_step_width(int len); // right border offset from the cell's left edge for a step length
    // What a click in the ARP step editor grabs (E371, the original's KStepSequencer): the step that is on and whose
    // area (its cell plus the following steps that are off, `span` cells) holds the point - its length when the point
    // is within 8 px left of the bar's end or right of it, its velocity when it is above 8 px below the bar top
    // (both in the corner); step -1 if no step that is on is under the point
    struct ArpGrab {
        int step = -1, span = 1;
        bool length = false, velocity = false;
    };
    ArpGrab arp_grab(const Element &e, int x, int y, int part, int px, int py) const;
    static int arp_cell(const Element &e, int x, int px); // the cell (0..31) under px, for Alt+click
};

} // namespace vti
