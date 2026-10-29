// vtirender - render the original editor's layout offscreen from the user's Access files (R4 check): backgrounds,
// knob filmstrips at the current values, buttons by state, conditions evaluated. Text is not drawn yet.
//   vtirender out.png [--state FILE] [--part N] [--set PATH=VALUE]... [--outline]
// --state takes a plug-in state (formats.md, or vtihost's VTISTATE file) for the device values of the parts.
#include "access/accessfiles.h"
#include "editor/editormodel.h"
#include "editor/editorrender.h"
#include "device/memsync.h"
#include "model/remotedevice.h"
#include "model/remotetemplates.h"
#include <cairo.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>

using namespace vti;

struct PngCache {
    const AccessFiles &files;
    std::map<std::string, cairo_surface_t *> cache;
    cairo_surface_t *get(const std::string &name)
    {
        if (name.empty())
            return nullptr;
        auto it = cache.find(name);
        if (it != cache.end())
            return it->second;
        const auto &d = files.get(name);
        cairo_surface_t *s = nullptr;
        if (!d.empty()) {
            struct Reader {
                const std::vector<uint8_t> *d;
                size_t pos;
            } rd{&d, 0};
            s = cairo_image_surface_create_from_png_stream(
                [](void *c, unsigned char *out, unsigned int n) {
                    auto *r = (Reader *)c;
                    if (r->pos + n > r->d->size())
                        return CAIRO_STATUS_READ_ERROR;
                    memcpy(out, r->d->data() + r->pos, n);
                    r->pos += n;
                    return CAIRO_STATUS_SUCCESS;
                },
                &rd);
            if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) {
                cairo_surface_destroy(s);
                s = nullptr;
            }
        }
        if (!s)
            fprintf(stderr, "vtirender: missing bitmap %s\n", name.c_str());
        cache[name] = s;
        return s;
    }
};

struct CairoCanvas : Canvas {
    cairo_t *cr;
    PngCache &png;
    CairoCanvas(cairo_t *c, PngCache &p) : cr(c), png(p) {}
    void *image(const std::string &name) override { return png.get(name); }
    int image_width(void *i) override { return cairo_image_surface_get_width((cairo_surface_t *)i); }
    int image_height(void *i) override { return cairo_image_surface_get_height((cairo_surface_t *)i); }
    void blit(void *img, int sx, int sy, int dx, int dy, int w, int h, double alpha) override
    {
        cairo_save(cr);
        cairo_rectangle(cr, dx, dy, w, h);
        cairo_clip(cr);
        cairo_set_source_surface(cr, (cairo_surface_t *)img, dx - sx, dy - sy);
        cairo_paint_with_alpha(cr, alpha);
        cairo_restore(cr);
    }
    void fill(int x, int y, int w, int h, Rgba c) override
    {
        cairo_set_source_rgba(cr, c.r / 255., c.g / 255., c.b / 255., c.a / 255.);
        cairo_rectangle(cr, x, y, w, h);
        cairo_fill(cr);
    }
    void push_clip(int x, int y, int w, int h) override
    {
        cairo_save(cr);
        cairo_rectangle(cr, x, y, w, h);
        cairo_clip(cr);
    }
    void pop_clip() override { cairo_restore(cr); }
    void line(double x0, double y0, double x1, double y1, Rgba c) override
    {
        cairo_set_source_rgba(cr, c.r / 255., c.g / 255., c.b / 255., c.a / 255.);
        cairo_set_line_width(cr, 1);
        cairo_move_to(cr, x0, y0);
        cairo_line_to(cr, x1, y1);
        cairo_stroke(cr);
    }
};

static std::vector<std::vector<uint8_t>> read_state(const char *path)
{
    std::vector<uint8_t> d;
    if (FILE *f = fopen(path, "rb")) {
        for (int c; (c = fgetc(f)) != EOF;)
            d.push_back((uint8_t)c);
        fclose(f);
    }
    if (d.size() > 12 && !memcmp(d.data(), "VTISTATE", 8)) {
        uint32_t n;
        memcpy(&n, d.data() + 8, 4);
        d = std::vector<uint8_t>(d.begin() + 12, d.begin() + 12 + std::min<size_t>(n, d.size() - 12));
    }
    std::vector<std::vector<uint8_t>> recs;
    if (d.size() < 8 || memcmp(d.data(), "MIDI", 4))
        return recs;
    for (size_t pos = 8; pos + 4 <= d.size();) {
        size_t n = (size_t)d[pos] << 24 | d[pos + 1] << 16 | d[pos + 2] << 8 | d[pos + 3];
        pos += 4;
        if (pos + n > d.size())
            break;
        recs.emplace_back(d.begin() + pos, d.begin() + pos + n);
        pos += n;
    }
    return recs;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: vtirender out.png [--state FILE] [--cache DIR] [--part N] [--set PATH=VALUE]...\n");
        return 2;
    }
    std::string err;
    auto files = AccessFiles::open("", err);
    RemoteDevice dev, ed;
    EditorLayout layout;
    // VTIRENDER_SNOW: the Snow's own editor and device description (accessfiles.h device_files)
    const DeviceFiles df = files ? files->files_for(getenv("VTIRENDER_SNOW") ? DeviceType::Snow : DeviceType::TI)
                                 : device_files(DeviceType::TI);
    if (!files || !dev.load(files->get(df.remotedevice), err) || !ed.load(files->get(df.editordevice), err) ||
        !layout.load(files->get(df.editor), err)) {
        fprintf(stderr, "vtirender: %s\n", err.c_str());
        return 1;
    }
    Conditions cond(dev, ed);
    int part = 0;
    bool outline = false;
    std::map<std::string, int> set;
    std::map<int, std::map<std::string, int>> parts; // part -> decoded single dump
    std::map<int, Bank> banks;                        // RAM A..D from a cache directory (--cache)
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--part") && i + 1 < argc)
            part = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--cache") && i + 1 < argc) {
            std::string dir = argv[++i];
            for (int b = 0; b < 4; b++)
                load_bank_file(dir + "/RAM " + (char)('A' + b), banks[b]);
        } else if (!strcmp(argv[i], "--outline"))
            outline = true;
        else if (!strcmp(argv[i], "--set") && i + 1 < argc) {
            std::string a = argv[++i];
            size_t eq = a.find('=');
            if (eq != std::string::npos)
                set[a.substr(0, eq)] = atoi(a.c_str() + eq + 1);
        } else if (!strcmp(argv[i], "--state") && i + 1 < argc) {
            const MidiMessage *t = dev.message("VirusTI_Singledump");
            for (auto &r : read_state(argv[++i])) {
                if (t && r.size() == 524 && r[6] == 0x10 && r[7] == 0)
                    dev.decode_dump(*t, r, parts[r[8]], err);
                else // controllers / parameter SysEx of the state (e.g. CC7 volume per channel)
                    for (auto &c : dev.decode(r))
                        parts[c.part][c.path] = c.value;
            }
        }
    }
    ValueFn values = [&](const std::string &path, int pt) {
        auto s = set.find(path);
        if (s != set.end())
            return s->second;
        if (path == "Remote Templates/Selected Template") // nothing selected at the start, like the original
            return -1;
        if (pt >= 0) {
            auto p = parts.find(pt);
            if (p != parts.end()) {
                auto v = p->second.find(path);
                if (v != p->second.end())
                    return v->second;
            }
        }
        return cond.default_value(path);
    };
    cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, layout.width, layout.height);
    cairo_t *cr = cairo_create(surf);
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_paint(cr);
    PngCache png{*files, {}};
    CairoCanvas canvas(cr, png);
    EditorRenderer r(layout, cond, dev, ed);
    r.osc_waves = files->osc_waves();
    r.values = values;
    r.part = part;
    if (getenv("VTIRENDER_ALPHA"))
        r.disabled_alpha = atof(getenv("VTIRENDER_ALPHA"));
    RemoteTemplates remote; // REMOTE page (read-only defaults, E193)
    r.remote_name = [&](int t) { return remote.name(t); };
    r.live_text = [&](const std::string &p) {
        return remote_text(p, remote, values("Remote Templates/Selected Template", -1),
                           values("Remote Templates/MIDI Channel Behaviour", -1));
    };
    r.patch_name = [&](int pt) {
        std::string n;
        auto p = parts.find(pt);
        if (p == parts.end())
            return n;
        for (int i = 1; i <= 10; i++) {
            char k[48];
            snprintf(k, sizeof k, "Single Patch Name/Character %02d", i);
            auto c = p->second.find(k);
            n += c == p->second.end() ? ' ' : (char)c->second;
        }
        while (!n.empty() && n.back() == ' ')
            n.pop_back();
        return n;
    };
    r.bank_patch = [&](int bank, int prog) {
        auto b = banks.find(bank);
        if (b == banks.end() || b->second[prog].size() != 524)
            return std::string();
        std::string n(b->second[prog].begin() + 249, b->second[prog].begin() + 259);
        return n;
    };
    if (getenv("VTIRENDER_LAYERS")) { // static layer, then the controls on top (checks the editor's cache order)
        r.layer = 1;
        r.render(canvas);
        r.layer = 2;
        r.render(canvas);
        r.layer = 0;
    } else
        r.render(canvas);
    if (const char *bn = getenv("VTIRENDER_BENCH")) { // average render time over N repaints
        int n = atoi(bn);
        auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < n; i++)
            r.render(canvas);
        printf("render: %.2f ms\n", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / n);
        struct Null : Canvas { // the same without drawing: the model/condition overhead alone
            Canvas &real;
            explicit Null(Canvas &c) : real(c) {}
            void *image(const std::string &nm) override { return real.image(nm); }
            int image_width(void *i) override { return real.image_width(i); }
            int image_height(void *i) override { return real.image_height(i); }
            void blit(void *, int, int, int, int, int, int, double) override {}
            void fill(int, int, int, int, Rgba) override {}
            void line(double, double, double, double, Rgba) override {}
        } nc(canvas);
        t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < n; i++)
            r.render(nc);
        printf("logic only: %.2f ms\n", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / n);
    }
    if (getenv("VTIRENDER_LIST")) { // all controls of the page: type, parameter, position (4-pixel grid)
        std::map<const Element *, Hit> seen;
        for (int y = 0; y < layout.height; y += 4)
            for (int x = 0; x < layout.width; x += 4) {
                Hit h = r.hit_test(x, y);
                if (h.element && !seen.count(h.element))
                    seen[h.element] = h;
            }
        for (auto &kv : seen)
            printf("ctl %-16s %-50s part %2d at %3d,%3d %dx%d\n", kv.first->type.c_str(), kv.first->get("Parameter").c_str(),
                   kv.second.part, kv.second.x, kv.second.y, kv.first->w, kv.first->h);
    }
    if (const char *at = getenv("VTIRENDER_AT")) { // debug: the control under a point "x,y"
        Hit h = r.hit_test(atoi(at), atoi(strchr(at, ',') + 1));
        if (h.element)
            printf("hit: %s %s part %d at %d,%d\n", h.element->type.c_str(), h.element->get("Parameter").c_str(), h.part,
                   h.x, h.y);
    }
    if (outline)
        fprintf(stderr, "vtirender: --outline is not supported any more\n");
    cairo_surface_write_to_png(surf, argv[1]);
    printf("%dx%d -> %s\n", layout.width, layout.height, argv[1]);
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
    return 0;
}
