// tests on the user's own Access files (skipped without them, e.g. in CI): every parameter's MIDI encoding, the
// dump fast path on the real single dump, init patch/upgrader, automation map, editor layout and renderer
#include "access/accessfiles.h"
#include "access/firmware.h"
#include "access/library.h"
#include "check.h"
#include "editor/editorrender.h"
#include "model/remotedevice.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <set>

using namespace vti;
using Bytes = std::vector<uint8_t>;

static AccessFiles *files()
{
    static std::unique_ptr<AccessFiles> f;
    static bool tried = false;
    if (!tried) {
        tried = true;
        std::string err;
        f = AccessFiles::open("", err);
    }
    if (!f)
        SKIP("no Access installation");
    return f.get();
}

static const RemoteDevice &dev()
{
    static RemoteDevice d;
    static bool loaded = false;
    AccessFiles *a = files();
    if (!loaded) {
        std::string err;
        loaded = d.load(a->get("remotedevice.xml"), err);
        CHECK(loaded);
    }
    return d;
}

TEST(access_encode_decode_all_parameters)
{
    const RemoteDevice &d = dev();
    int checked = 0, missing = 0;
    for (auto &p : d.parameters) {
        int part = p.part == "none" ? 16 : 3;
        for (int v : {p.min, (p.min + p.max) / 2, p.max}) {
            Bytes m = d.encode(p.path, v, part);
            if (m.empty())
                continue; // no realtime message (dump-only parameters)
            uint8_t buf[64];
            size_t n = d.encode_into(p.path, v, part, buf, sizeof buf);
            CHECK(n == m.size() && std::equal(m.begin(), m.end(), buf));
            bool found = false;
            for (auto &c : d.decode(m))
                found |= c.path == p.path && c.value == v;
            if (!found && ++missing <= 5)
                std::fprintf(stderr, "  %s = %d does not decode back\n", p.path.c_str(), v);
            checked++;
        }
    }
    CHECK(checked > 500);
    CHECK_EQ(missing, 0);
}

TEST(access_single_dump_fast_path)
{
    const RemoteDevice &d = dev();
    const MidiMessage *t = d.message("VirusTI_Singledump");
    CHECK(t != nullptr);
    if (!t)
        return;
    Bytes init = files()->init_single();
    RemoteDevice::upgrade_single(init);
    CHECK_EQ(init.size(), 524u);
    std::map<std::string, int> vals;
    std::string err;
    CHECK(d.decode_dump(*t, init, vals, err));
    CHECK(t->field_bytes.size() > 400);
    int bad = 0;
    for (auto &kv : t->field_bytes)
        for (int v : {0, 1, 63, 127}) {
            Bytes a = init, b = init;
            d.update_dump(*t, a, {{kv.first, v}});
            d.update_dump(*t, b, {{kv.first, v}, {"zz/none", 0}});
            bad += a != b;
            std::map<std::string, int> back;
            CHECK(d.decode_dump(*t, a, back, err)); // checksums stay valid
        }
    CHECK_EQ(bad, 0);
    int fp = RemoteDevice::single_fingerprint(init);
    CHECK(fp >= 0 && fp < 16384);
}

TEST(access_init_messages)
{
    auto msgs = dev().init_messages();
    CHECK(!msgs.empty());
    for (auto &m : msgs)
        CHECK(!m.empty() && (m[0] & 0x80));
}

TEST(access_init_messages_show_configuring_like_the_original)
{
    // E321: the original's start-up shows "Configuring Virus TI" on the Virus (Actions/Rise Message 7, message timer
    // 100) right after the global settings, and sets the timer to 1 once the parts are set up
    auto msgs = dev().init_messages();
    const Bytes rise = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x74, 0x00, 0x7E, 0x07, 0xF7};
    const Bytes timer_on = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x74, 0x00, 0x29, 0x64, 0xF7};
    const Bytes timer_off = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x74, 0x00, 0x29, 0x01, 0xF7};
    size_t at = 0;
    while (at < msgs.size() && msgs[at] != rise)
        at++;
    CHECK(at > 0 && at + 1 < msgs.size());
    if (at == 0 || at + 1 >= msgs.size())
        return;
    CHECK(msgs[at + 1] == timer_on);
    for (size_t i = 0; i < at; i++)
        CHECK(msgs[i].size() == 11 && msgs[i][6] == 0x73); // all global settings first
    CHECK(msgs[at + 2][6] == 0x72);                         // then the parts (ParamInit)
    CHECK(msgs.back() == timer_off);
    // with the plug-in's fixed multi: sent unchanged between the message and the parts, like the original (the
    // state keeps it with bytes 25..40 and the checksum zeroed, but the Virus gets the table as it is)
    auto multi = files()->init_multi();
    CHECK_EQ(multi.size(), 267u);
    auto with = dev().init_messages(0x10, multi);
    CHECK_EQ(with.size(), msgs.size() + 1);
    if (with.size() == msgs.size() + 1) {
        CHECK(with[at + 1] == timer_on);
        CHECK(with[at + 2] == multi);
        CHECK(with[at + 3][6] == 0x72);
    }
}

TEST(access_automation_map)
{
    std::vector<AutomationEntry> a;
    std::string err;
    CHECK(load_automation(files()->get("automation.xml"), a, err));
    CHECK(a.size() >= 200);
    std::set<int> ids;
    for (auto &e : a) {
        CHECK(ids.insert(e.id).second); // unique VST parameter ids
        if (!dev().find(e.path) && e.path.rfind("Softknobs/", 0) != 0)
            std::fprintf(stderr, "  automation path not in the device: %s\n", e.path.c_str());
    }
}

// a canvas that draws nothing and records what is drawn where
struct RecordCanvas : Canvas {
    int depth = 0, max_depth = 0, draws = 0, outside = 0;
    std::vector<std::array<int, 4>> clips;
    void *image(const std::string &) override { return nullptr; }
    int image_width(void *) override { return 0; }
    int image_height(void *) override { return 0; }
    void blit(void *, int, int, int, int, int, int, double) override { draws++; }
    void fill(int x, int y, int w, int h, Rgba) override { note(x, y, x + w, y + h); }
    void line(double x0, double y0, double x1, double y1, Rgba) override
    {
        note((int)std::min(x0, x1), (int)std::min(y0, y1), (int)std::max(x0, x1), (int)std::max(y0, y1));
    }
    void push_clip(int x, int y, int w, int h) override
    {
        clips.push_back({x, y, x + w, y + h});
        max_depth = std::max(max_depth, ++depth);
    }
    void pop_clip() override
    {
        clips.pop_back();
        depth--;
    }
    void note(int x0, int y0, int x1, int y1)
    {
        draws++;
        if (!clips.empty()) { // inside a display: nothing outside its element (1 px for anti-aliased lines)
            auto &c = clips.back();
            if (x0 < c[0] - 1 || y0 < c[1] - 1 || x1 > c[2] + 1 || y1 > c[3] + 1)
                outside++;
        }
    }
};

TEST(access_editor_render_deps_and_clip)
{
    AccessFiles *a = files();
    static RemoteDevice ed;
    std::string err;
    CHECK(ed.load(a->get("editordevice.xml"), err));
    EditorLayout layout;
    CHECK(layout.load(a->get("editor.xml"), err));
    Conditions cond(dev(), ed);
    EditorRenderer r(layout, cond, dev(), ed);
    // extreme values: the longest envelopes, highest cutoff/resonance
    r.values = [&](const std::string &p, int) {
        if (p == "VirusControl/SelectedPage")
            return 3; // FILTER page
        if (p == "VirusControl/InitialSyncProgress")
            return 127; // start-up sync done
        const Parameter *q = cond.find(p);
        bool extreme = p.rfind("Filter", 0) == 0 || p.rfind("Envelope", 0) == 0;
        return q ? (extreme ? q->max : q->def) : 0;
    };
    EditorRenderer::Deps deps;
    r.deps = &deps;
    RecordCanvas c;
    r.render(c);
    CHECK_EQ(c.depth, 0);   // every push_clip popped
    CHECK(c.max_depth >= 1); // the page has displays
    CHECK(c.draws > 0);
    CHECK(!deps.cond.empty());
    if (getenv("VTITESTS_VERBOSE"))
        for (auto &p : deps.cond)
            std::fprintf(stderr, "  cond: %s\n", p.c_str());
    CHECK(deps.cond.count("VirusControl/SelectedPage")); // page switch = structure change
    CHECK(!deps.rects.empty());
    if (c.outside)
        std::fprintf(stderr, "  %d display strokes outside their element (the canvas clips them)\n", c.outside);
    for (auto &kv : deps.rects)
        for (auto &rc : kv.second)
            CHECK(rc[2] > 0 && rc[3] > 0);
}

// the Virus' Sync Information Event (E072: F0 00 20 33 01 10 73 00 54 01 F7 on EP 0x86 cable 2) is the global the
// sync error page counts (E205), and the editor's Info Panel shows another set of elements for page 1
TEST(access_sync_information_event)
{
    bool found = false;
    for (auto &c : dev().decode({0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x73, 0x00, 0x54, 0x01, 0xF7}))
        found |= c.path == "Global USB/Sync Information Event" && c.value == 1;
    CHECK(found);
    AccessFiles *a = files();
    static RemoteDevice ed;
    std::string err;
    CHECK(ed.load(a->get("editordevice.xml"), err));
    EditorLayout layout;
    CHECK(layout.load(a->get("editor.xml"), err));
    Conditions cond(dev(), ed);
    EditorRenderer r(layout, cond, dev(), ed);
    int info_page = 0;
    r.values = [&](const std::string &p, int) {
        if (p == "Info Panel/Page")
            return info_page;
        if (p == "VirusControl/InitialSyncProgress")
            return 127;
        const Parameter *q = cond.find(p);
        return q ? q->def : 0;
    };
    EditorRenderer::Deps deps;
    r.deps = &deps;
    RecordCanvas c0;
    r.render(c0);
    CHECK(deps.cond.count("Info Panel/Page")); // a page change is a structure change (full repaint)
    const auto normal = r.static_signature();
    info_page = 1;
    RecordCanvas c1;
    r.render(c1);
    CHECK(r.static_signature() != normal); // the alert's elements instead of the standard info panel
}

// every page of the original's editor (pages, sections, part strips), with images of a fixed size so the drawing
// code that depends on image sizes (knob frames, sliders, fonts) runs too; hit tests over the whole frame
struct ImageCanvas : RecordCanvas {
    int img = 0;
    void *image(const std::string &) override { return &img; }
    int image_width(void *) override { return 64; }
    int image_height(void *) override { return 64 * 32; }
};

TEST(access_editor_render_every_page)
{
    AccessFiles *a = files();
    static RemoteDevice ed;
    std::string err;
    CHECK(ed.load(a->get("editordevice.xml"), err));
    EditorLayout layout;
    CHECK(layout.load(a->get("editor.xml"), err));
    Conditions cond(dev(), ed);
    EditorRenderer r(layout, cond, dev(), ed);
    int page = 0, section = 0;
    r.values = [&](const std::string &p, int part) {
        if (p == "VirusControl/SelectedPage")
            return page;
        if (p == "VirusControl/Selected Section")
            return section;
        if (p == "VirusControl/InitialSyncProgress")
            return 127;
        const Parameter *q = cond.find(p);
        return q ? (part % 2 ? q->max : q->def) : 0; // odd parts at the maximum: other branches of the displays
    };
    r.patch_name = [](int part) { return "Part " + std::to_string(part + 1); };
    r.bank_patch = [](int bank, int prog) { return "B" + std::to_string(bank) + " P" + std::to_string(prog); };
    r.browser_match = [](int, int prog) { return prog % 3 != 0; };
    r.live_text = [](const std::string &p) { return p.find("Display") != std::string::npos ? std::string("LCD") : std::string(); };
    r.meter = [](int ch) { return ch * 8; };
    r.selected[0] = {0, {5}};
    const Parameter *pages = cond.find("VirusControl/SelectedPage");
    const Parameter *sections = cond.find("VirusControl/Selected Section");
    int last_page = pages ? pages->max : 10, last_section = sections ? sections->max : 0;
    int hits = 0, renders = 0;
    for (section = 0; section <= last_section; section++)
        for (page = 0; page <= last_page; page++) {
            EditorRenderer::Deps deps;
            r.deps = &deps;
            for (r.layer = 0; r.layer <= 2; r.layer++) {
                ImageCanvas c;
                r.render(c);
                CHECK_EQ(c.depth, 0);
                renders++;
            }
            r.layer = 0;
            r.deps = nullptr;
            CHECK(!deps.cond.empty());
            for (int y = 0; y < layout.height; y += 9)
                for (int x = 0; x < layout.width; x += 9) {
                    Hit h = r.hit_test(x, y);
                    if (!h.element)
                        continue;
                    hits++;
                    const std::string &param = h.element->get("Parameter");
                    r.param_active(param, h.part);
                    CHECK(!r.param_rects(param).empty());
                    r.menu_entries(param);
                    r.value_text(param, 1);
                    if (h.element->type == "PatchBrowser")
                        EditorRenderer::browser_entry(h, x, y);
                }
        }
    CHECK(renders > 10);
    CHECK(hits > 100);
    // a partial repaint (clip) and the bitmap font helper
    r.clip_x0 = 100, r.clip_y0 = 100, r.clip_x1 = 300, r.clip_y1 = 300;
    ImageCanvas c;
    r.render(c);
    r.status_text(c, "", "Synchronizing", 0, 0, 200, 20);
    CHECK(r.static_signature().size() < 100000);
}

TEST(access_rom_banks_every_model)
{
    // read-only: firmware.bin of the user's installation is parsed, nothing goes to the Virus
    AccessFiles *a = files();
    const std::string fw = firmware_path(a->source());
    std::string err;
    for (VirusModel m : {VirusModel::TI, VirusModel::TI2, VirusModel::Snow}) {
        auto banks = load_rom_banks(fw, m, err);
        if (banks.empty())
            SKIP("no firmware.bin next to Virus Control (" + err + ")");
        bool ok = true, names = true;
        for (size_t b = 0; b < banks.size(); b++)
            for (auto &d : banks[b]) {
                ok = ok && d.size() == 524 && d[0] == 0xF0 && d[6] == 0x10 && d[523] == 0xF7;
                names = names && d.size() == 524 && d[249] >= 0x20; // a printable first character of the name
            }
        CHECK(banks.size() >= 2);
        CHECK(banks[0].size() == 128);
        CHECK(ok);
        CHECK(names);
    }
}

TEST(rom_banks_bad_files)
{
    // no Access files needed: a missing file and files that are no firmware package
    namespace fs = std::filesystem;
    std::string err;
    CHECK(load_rom_banks("/nonexistent/firmware.bin", VirusModel::TI2, err).empty() && !err.empty());
    fs::path dir = fs::temp_directory_path() / ("vtitests-fw-" + std::to_string(::time(nullptr)));
    fs::create_directories(dir);
    auto write = [&](const char *name, const Bytes &b) {
        std::string path = (dir / name).string();
        FILE *f = fopen(path.c_str(), "wb");
        fwrite(b.data(), 1, b.size(), f);
        fclose(f);
        return path;
    };
    err.clear();
    CHECK(load_rom_banks(write("junk.bin", Bytes(100, 0x55)), VirusModel::TI, err).empty() && !err.empty());
    err.clear();
    Bytes form = {'F', 'O', 'R', 'M', 0, 0, 0, 4, 'X', 'X', 'X', 'X'}; // a FORM without TABL
    CHECK(load_rom_banks(write("form.bin", form), VirusModel::Snow, err).empty() && !err.empty());
    CHECK(firmware_path("").size() < 4096);
    fs::remove_all(dir);
}

TEST(library_save_load_round_trip)
{
    // no Access files needed: synthetic single dumps through the SMF writer and reader
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / ("vtitests-lib-" + std::to_string(::time(nullptr)));
    fs::create_directories(dir);
    PatchLibrary lib;
    lib.name = "Test";
    lib.path = (dir / "Test.mid").string();
    for (int i = 0; i < 3; i++) {
        Bytes m(524, (uint8_t)(i + 1));
        m[0] = 0xF0, m[1] = 0x00, m[2] = 0x20, m[3] = 0x33, m[4] = 0x01, m[5] = 0x10, m[6] = 0x10, m[7] = 0x01;
        m[8] = (uint8_t)i, m[523] = 0xF7;
        lib.dumps.push_back(m);
    }
    std::string err;
    CHECK(save_library(lib, err));
    auto libs = load_libraries(dir.string());
    CHECK_EQ(libs.size(), 1u);
    if (libs.size() == 1) {
        CHECK(libs[0].name == "Test");
        CHECK_EQ(libs[0].dumps.size(), 3u);
        for (size_t i = 0; i < libs[0].dumps.size() && i < 3; i++)
            CHECK(libs[0].dumps[i].size() == 524 && libs[0].dumps[i][9] == lib.dumps[i][9]);
    }
    fs::remove_all(dir);
}
