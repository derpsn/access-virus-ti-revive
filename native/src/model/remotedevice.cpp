// remotedevice.cpp - see remotedevice.h. Format notes: docs/formats.md.
#include "remotedevice.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <pugixml.hpp>

namespace vti {

static int num(const pugi::xml_node &n, const char *a, int def)
{
    pugi::xml_attribute at = n.attribute(a);
    return at ? std::atoi(at.value()) : def;
}

const Valuelist::Entry *Valuelist::find(int v) const
{
    for (auto &e : entries)
        if (value_of(e) == v)
            return &e;
    return nullptr;
}

std::string Valuelist::text(int v) const
{
    const Entry *e = find(v);
    return e ? e->text : std::to_string(v);
}

bool RemoteDevice::load(const std::vector<uint8_t> &xml, std::string &error)
{
    pugi::xml_document doc;
    // remotedevice.xml declares ISO-8859-1
    pugi::xml_parse_result r = doc.load_buffer(xml.data(), xml.size(), pugi::parse_default, pugi::encoding_latin1);
    if (!r) {
        error = std::string("remotedevice.xml: ") + r.description();
        return false;
    }
    pugi::xml_node root = doc.child("Remotedevice");
    if (!root) {
        error = "remotedevice.xml: no <Remotedevice>";
        return false;
    }
    device = root.attribute("device").value();
    version = root.attribute("version").value();

    for (auto set : root.child("ParameterSets").children("ParameterSet")) {
        std::string group = set.attribute("name").value();
        for (auto p : set.children("Parameter")) {
            Parameter q;
            q.group = group;
            q.name = p.attribute("name").value();
            q.path = group + "/" + q.name;
            q.min = num(p, "minValue", 0);
            q.max = num(p, "maxValue", 127);
            q.def = num(p, "defaultValue", 0);
            q.valuelist = p.attribute("valuelist").value();
            q.part = p.attribute("part").value();
            q.condition = p.attribute("condition").value();
            q.message = p.attribute("defaultmessage").value();
            q.controller = num(p, "controllerindex", -1);
            q.shortname = p.attribute("shortname").value();
            q.displayname = p.attribute("displayname").value();
            q.automatable = std::string(p.attribute("automatable").value()) == "true";
            q.performance = (bool)p.attribute("performanceCtrl");
            q.meta = std::string(p.attribute("meta").value()) == "true";
            q.retrigger = std::string(p.attribute("retrigger").value()) == "true";
            q.catch_always = std::string(p.attribute("catchAlways").value()) != "false";
            for (auto c : p.children("Client")) {
                MetaClient mc;
                mc.path = c.attribute("name").value();
                mc.a = c.attribute("a").as_double(1);
                mc.b = c.attribute("b").as_double(0);
                mc.notrigger = std::string(c.attribute("notrigger").value()) == "true";
                mc.nosource = std::string(c.attribute("nosource").value()) == "true";
                for (auto m : c.children("Map"))
                    mc.maps.push_back({num(m, "clientval", 0), num(m, "metaval", 0)});
                q.clients.push_back(mc);
            }
            index_[q.path] = parameters.size();
            parameters.push_back(q);
        }
    }
    for (auto v : root.child("Valuelists").children("Valuelist")) {
        Valuelist l;
        l.name = v.attribute("name").value();
        l.mapped = std::string(v.attribute("type").value()) == "mapped";
        for (auto e : v.children("Entry"))
            l.entries.push_back({num(e, "index", 0), num(e, "value", -1), e.text().get()});
        valuelists[l.name] = l;
    }
    for (auto c : root.child("Conditionsets").children("Condition")) {
        Condition k;
        k.name = c.attribute("name").value();
        k.any = std::string(c.attribute("operand").value()) != "and";
        k.invert = std::string(c.attribute("result").value()) == "invert";
        for (auto t : c.children("Test")) {
            std::string type = t.attribute("type").value();
            auto kind = type == "inside" ? Condition::Test::Inside
                        : type == "outside" ? Condition::Test::Outside : Condition::Test::Equals;
            k.tests.push_back({kind, num(t, "minvalue", 0), num(t, "maxvalue", 0), t.attribute("param").value()});
        }
        conditions.emplace(k.name, k);
    }
    for (auto m : root.child("MIDIMessages").children("MIDIMessage")) {
        MidiMessage mm;
        mm.id = m.attribute("id").value();
        mm.name = m.attribute("name").value();
        for (auto b : m.children("byte")) {
            MessageByte mb;
            mb.index = num(b, "index", 0);
            mb.content = b.attribute("content").value();
            mb.tag = num(b, "tag", -1);
            mb.mask = num(b, "mask", -1);
            mb.shift = num(b, "shift", 0);
            mb.part = num(b, "part", -1);
            mb.method = b.attribute("method").value();
            mb.start = num(b, "startindex", -1);
            mb.stop = num(b, "stopindex", -1);
            for (auto p : b.children("parameter"))
                mb.controllers.push_back({num(p, "mapindex", 0), p.attribute("content").value()});
            mm.bytes.push_back(mb);
        }
        for (size_t i = 0; i < mm.bytes.size(); i++) {
            const auto &b = mm.bytes[i];
            if (b.content.find('/') != std::string::npos)
                mm.field_bytes[b.part < 0 ? b.content : b.content + "@" + std::to_string(b.part)].push_back(i);
            if (!b.method.empty())
                mm.checksum_bytes.push_back(i);
        }
        messages.push_back(mm);
    }
    for (auto m : root.child("SoftknobMapping").children("Mapping")) {
        size_t i = (size_t)num(m, "index", 0);
        if (softknobs.size() <= i)
            softknobs.resize(i + 1);
        softknobs[i] = m.attribute("parameter").value();
    }
    for (auto g : root.child("Initialisation").children("GlobalInit"))
        global_init.push_back({g.attribute("param").value(), g.attribute("value").value()});
    for (auto g : root.child("Initialisation").children("ParamInit"))
        param_init.push_back({g.attribute("param").value(), g.attribute("value").value(), num(g, "part", 0)});
    return true;
}

const Parameter *RemoteDevice::find(const std::string &path) const
{
    auto it = index_.find(path);
    return it == index_.end() ? nullptr : &parameters[it->second];
}

const MidiMessage *RemoteDevice::message(const std::string &id) const
{
    for (auto &m : messages)
        if (m.id == id)
            return &m;
    return nullptr;
}

size_t RemoteDevice::encode_into(const std::string &path, int value, int part, uint8_t *out, size_t cap, int device_id) const
{
    const Parameter *p = find(path);
    const MidiMessage *m = p ? message(p->message) : nullptr;
    if (!m || m->bytes.size() > cap)
        return 0;
    int ctrl = -1;
    for (auto &b : m->bytes)
        for (auto &kv : b.controllers)
            if (kv.second == path)
                ctrl = kv.first;
    if (ctrl < 0)
        return 0;
    size_t n = 0;
    int channel = part & 15;
    for (auto &b : m->bytes) {
        const std::string &c = b.content;
        uint8_t v;
        if (c == "controller")
            v = (uint8_t)(0xB0 | channel);
        else if (c == "polypressure")
            v = (uint8_t)(0xA0 | channel);
        else if (c == "sysexstart")
            v = 0xF0;
        else if (c == "sysexend")
            v = 0xF7;
        else if (c == "byte")
            v = (uint8_t)(b.tag < 0 ? 0 : b.tag);
        else if (c == "deviceid")
            v = (uint8_t)device_id;
        else if (c == "part")
            v = (uint8_t)part;
        else if (c == "controllerid")
            v = (uint8_t)ctrl;
        else if (c == "value")
            v = (uint8_t)(value & 0x7F);
        else
            return 0;
        out[n++] = v;
    }
    return n;
}

std::vector<uint8_t> RemoteDevice::encode(const std::string &path, int value, int part, int device_id) const
{
    const Parameter *p = find(path);
    const MidiMessage *m = p ? message(p->message) : nullptr;
    if (!m)
        return {};
    int ctrl = -1; // the parameter's id inside this message (map index of the controllerid byte)
    for (auto &b : m->bytes)
        for (auto &kv : b.controllers)
            if (kv.second == path)
                ctrl = kv.first;
    if (ctrl < 0)
        return {};
    std::vector<uint8_t> out;
    int channel = part & 15;
    for (auto &b : m->bytes) {
        const std::string &c = b.content;
        if (c == "controller")
            out.push_back((uint8_t)(0xB0 | channel));
        else if (c == "polypressure")
            out.push_back((uint8_t)(0xA0 | channel));
        else if (c == "sysexstart")
            out.push_back(0xF0);
        else if (c == "sysexend")
            out.push_back(0xF7);
        else if (c == "byte")
            out.push_back((uint8_t)(b.tag < 0 ? 0 : b.tag));
        else if (c == "deviceid")
            out.push_back((uint8_t)device_id);
        else if (c == "part")
            out.push_back((uint8_t)part);
        else if (c == "controllerid")
            out.push_back((uint8_t)ctrl);
        else if (c == "value")
            out.push_back((uint8_t)(value & 0x7F));
        else
            return {}; // dumps etc. are not single-parameter transports
    }
    return out;
}

static bool is_field(const MessageByte &b)
{
    return b.content.find('/') != std::string::npos;
}

static std::string field_key(const MessageByte &b)
{
    return b.part < 0 ? b.content : b.content + "@" + std::to_string(b.part);
}

static int checksum(const MessageByte &b, const std::vector<uint8_t> &msg)
{
    unsigned sum = 0; // add7: 7-bit sum of the bytes start..stop (the only method in remotedevice.xml)
    for (int i = b.start; i <= b.stop && i < (int)msg.size(); i++)
        sum += msg[i];
    return sum & 0x7F;
}

size_t RemoteDevice::dump_size(const MidiMessage &m)
{
    size_t n = 0;
    for (auto &b : m.bytes)
        n = std::max(n, (size_t)b.index + 1);
    return n;
}

int RemoteDevice::single_fingerprint(const std::vector<uint8_t> &msg)
{
    if (msg.size() != 524)
        return -1;
    unsigned sum = 0;
    for (size_t i = 13; i <= 521; i++)
        if (i != 265)
            sum += msg[i];
    return (int)(sum & 0x3FFF);
}

// Single dump format versions: docs/formats.md, "Single dump format versions" (the table there is the specification
// of this code). Offsets are counted from byte 9, the format version.
namespace {
namespace off {
enum : int {
    VERSION = 0x00, EXPRESSION = 0x0B, OSC1_KEYFOLLOW = 0x15, OLD_RINGMOD = 0x26, NOISE_COLOUR = 0x27,
    RINGMOD_VOLUME = 0x32, LFO1_SHAPE = 0x44, LFO1_CONTOUR = 0x47, LFO2_SHAPE = 0x50, LFO2_CONTOUR = 0x53,
    OLD_UNISON = 0x61, CHORUS_TYPE = 0x67, CHORUS_MIX = 0x68, CHORUS_FEEDBACK = 0x6D, CHORUS_XOVER = 0x6F,
    DELAY_MODE = 0x70, DELAY_SEND = 0x71, DELAY_TIME = 0x72, DELAY_FEEDBACK = 0x73, DELAY_LFO_RATE = 0x74,
    DELAY_LFO_DEPTH = 0x75, DELAY_LFO_SHAPE = 0x76, DELAY_COLOUR = 0x77, P80 = 0x80, OLD_ARP_MODE = 0x81,
    ARP_PATTERN = 0x82, ARP_NOTE_LENGTH = 0x85, ARP_SWING = 0x86, LFO3_RATE = 0x87, LFO3_ASSIGN_AMOUNT = 0x8C,
    LFO3_FADE_IN = 0x8D, ARP_MODE = 0x8F, TEMPO = 0x90, ARP_CLOCK = 0x91, LFO1_CLOCK = 0x92, LFO2_CLOCK = 0x93,
    DELAY_CLOCK = 0x94, LFO3_CLOCK = 0x95, SMOOTH_MODE = 0x99, FILTER_KEYFOLLOW_BASE = 0xA1, OSC2_FM_MODE = 0xA2,
    INPUT_FOLLOWER = 0xA6, OSC3_MODE = 0xA9, OSC3_VOLUME = 0xAA, OSC3_SEMITONE = 0xAB, OSC3_DETUNE = 0xAC,
    EQ_LOW_FREQ = 0xAD, EQ_HIGH_FREQ = 0xAE, SOFTKNOB1_NAME = 0xB3, SOFTKNOB2_NAME = 0xB4, SURROUND_BALANCE = 0xBA,
    SOFTKNOB1_DEST = 0xBE, SOFTKNOB2_DEST = 0xBF, SLOT1_SOURCE = 0xC0, SLOT1_DEST = 0xC1, SLOT2_SOURCE = 0xC3,
    SLOT2_DEST1 = 0xC4, SLOT2_DEST2 = 0xC6, SLOT3_SOURCE = 0xC8, SLOT3_DEST1 = 0xC9, SLOT3_DEST2 = 0xCB,
    SLOT3_DEST3 = 0xCD, LFO1_TARGET = 0xCF, LFO1_TARGET_AMOUNT = 0xD0, LFO2_TARGET = 0xD1, LFO2_TARGET_AMOUNT = 0xD2,
    PHASER = 0xD4, EQ_MID_GAIN = 0xDC, EQ_MID_FREQ = 0xDD, EQ_MID_Q = 0xDE, EQ_LOW_GAIN = 0xDF, EQ_HIGH_GAIN = 0xE0,
    BOOST_INTENSITY = 0xE1, BOOST_TUNE = 0xE2, INPUT_RINGMOD = 0xE3, DISTORTION_TYPE = 0xE4, SLOT4 = 0xE7,
    SLOT5 = 0xEA, SLOT6 = 0xED, CATEGORY1 = 0xFB, CATEGORY2 = 0xFC, PFE = 0xFE, OSC_SELECT = 0xFF,
    PAGE_C = 0x101, REVERB_MODE = 0x102, REVERB_SEND = 0x103, REVERB_TYPE = 0x104, REVERB_TIME = 0x105,
    REVERB_DAMPING = 0x106, REVERB_COLOUR = 0x107, REVERB_PREDELAY = 0x108, REVERB_CLOCK = 0x109,
    REVERB_FEEDBACK = 0x10A, DELAY_TYPE = 0x10B, TAPE_RATIO = 0x10D, TAPE_LEFT_CLOCK = 0x10E,
    TAPE_RIGHT_CLOCK = 0x10F, TAPE_BANDWIDTH = 0x112, FILTERBANK_TYPE = 0x114, FILTERBANK_MIX = 0x115,
    FILTERBANK_FREQ = 0x116, FILTERBANK_PHASE = 0x117, FILTERBANK_SHAPE_L = 0x118, FILTERBANK_SHAPE_R = 0x119,
    FILTERBANK_RESONANCE = 0x11A, BOOST_TYPE = 0x11B, OSC1_MODE = 0x11F, OSC1_FORMANT = 0x12B, OSC2_MODE = 0x124,
    OSC2_FORMANT = 0x13F, DIST_TREBLE = 0x147, DIST_HIGH_CUT = 0x148, DIST_MIX = 0x149, DIST_QUALITY = 0x14A,
    DIST_TONE = 0x14B, ENVELOPES_3_4 = 0x151, UNISON = 0x1F9,
};
}

using Fields = std::initializer_list<std::pair<int, uint8_t>>;

void put(uint8_t *p, Fields f)
{
    for (auto &kv : f)
        p[kv.first] = kv.second;
}

void to_v1(uint8_t *p)
{
    using namespace off;
    put(p, {{TEMPO, 0x39}, {FILTER_KEYFOLLOW_BASE, 0x24}, {OSC1_KEYFOLLOW, 0x60}, {LFO1_CLOCK, 0}, {LFO2_CLOCK, 0},
            {DELAY_CLOCK, 0}, {LFO3_FADE_IN, 0}, {SOFTKNOB1_DEST, 0}});
}

void to_v2(uint8_t *p)
{
    p[off::CHORUS_FEEDBACK] = (uint8_t)(p[off::CHORUS_FEEDBACK] / 2 + 64); // unipolar -> bipolar
}

void to_v3(uint8_t *p)
{
    using namespace off;
    put(p, {{OLD_RINGMOD, 0}, {LFO3_CLOCK, 0}, {LFO1_TARGET, 0}, {LFO1_TARGET_AMOUNT, 0x40}, {LFO2_TARGET, 0},
            {LFO2_TARGET_AMOUNT, 0x40}});
}

void to_v4(uint8_t *p)
{
    put(p, {{off::SMOOTH_MODE, 1}, {off::BOOST_INTENSITY, 0}, {off::BOOST_TUNE, 0x20}});
}

void to_v5(uint8_t *p)
{
    using namespace off;
    put(p, {{OSC2_FM_MODE, 0}, {INPUT_FOLLOWER, 0}, {INPUT_RINGMOD, 0}, {CATEGORY1, 0}, {CATEGORY2, 0},
            {DELAY_COLOUR, 0x40}, {NOISE_COLOUR, 0x40}, {DELAY_MODE, 1}, {OSC3_MODE, 0}, {OSC3_VOLUME, 0x40},
            {OSC3_SEMITONE, 0x40}, {OSC3_DETUNE, 0x28}});
    if (p[LFO1_SHAPE] != 1)
        p[LFO1_CONTOUR] = 0x40;
    if (p[LFO2_SHAPE] != 1)
        p[LFO2_CONTOUR] = 0x40;
    static const uint8_t phaser[7] = {3, 0, 0x24, 0x70, 0x30, 0x40, 0x7F}; // stages mix rate depth freq feedback spread
    for (int k = 0; k < 7; k++)
        p[PHASER + k] = phaser[k];
    for (int at : std::initializer_list<int>{SOFTKNOB1_DEST, SOFTKNOB2_DEST}) // destination list renumbered
        if (int d = (int8_t)p[at])
            p[at] = (uint8_t)(d + 13 > 0x52 ? d - 0x45 : d + 13);
}

void to_v6(uint8_t *p)
{
    using namespace off;
    put(p, {{EXPRESSION, 0x7F}, {SURROUND_BALANCE, 0}, {ARP_PATTERN, 0}, {ARP_NOTE_LENGTH, 0x40}, {ARP_SWING, 0}});
    if (p[OLD_ARP_MODE] == 0)
        p[ARP_CLOCK] = 4;
    else if (p[ARP_CLOCK] == 3)
        p[ARP_CLOCK] = 4, p[ARP_PATTERN] = 1;
}

void to_v7(uint8_t *p)
{
    using namespace off;
    for (int slot : std::initializer_list<int>{SLOT4, SLOT5, SLOT6}) // source, destination, amount
        p[slot] = 3, p[slot + 1] = 0, p[slot + 2] = 0x40;
    put(p, {{EQ_MID_GAIN, 0x40}, {EQ_MID_FREQ, 0x47}, {EQ_MID_Q, 0x33}, {EQ_LOW_GAIN, 0x40}, {EQ_HIGH_GAIN, 0x40},
            {EQ_LOW_FREQ, 0x2B}, {EQ_HIGH_FREQ, 0x55}, {SOFTKNOB1_NAME, 0}, {SOFTKNOB2_NAME, 0}, {PFE, 0}});
}

void to_v8(uint8_t *p, const std::vector<uint8_t> &page_cd)
{
    using namespace off;
    memcpy(p + PAGE_C, page_cd.data(), 256);
    bool lfo3_free = p[LFO3_ASSIGN_AMOUNT] == 0;
    for (int at : std::initializer_list<int>{SLOT1_SOURCE, SLOT2_SOURCE, SLOT3_SOURCE, SLOT4, SLOT5, SLOT6})
        lfo3_free = lfo3_free && p[at] != 0x17;
    for (int at : std::initializer_list<int>{SLOT1_DEST, SLOT2_DEST1, SLOT2_DEST2, SLOT3_DEST1, SLOT3_DEST2, SLOT3_DEST3, SLOT4 + 1, SLOT5 + 1,
                   SLOT6 + 1, LFO1_TARGET, LFO2_TARGET})
        lfo3_free = lfo3_free && p[at] != 0x3A;
    if (lfo3_free)
        p[LFO3_RATE] = 0x30;
    const int mode = (int8_t)p[DELAY_MODE];
    const bool was_reverb = mode >= 2 && mode <= 4;
    if (was_reverb) // the reverb takes over the fields the old reverb modes used
        put(p, {{REVERB_MODE, (uint8_t)(mode - 1)}, {REVERB_SEND, p[DELAY_SEND]}, {REVERB_TYPE, p[DELAY_LFO_DEPTH]},
                {REVERB_TIME, p[DELAY_LFO_RATE]}, {REVERB_DAMPING, p[DELAY_LFO_SHAPE]}, {REVERB_COLOUR, p[DELAY_COLOUR]},
                {REVERB_PREDELAY, p[DELAY_TIME]}, {REVERB_CLOCK, p[DELAY_CLOCK]},
                {REVERB_FEEDBACK, p[DELAY_FEEDBACK]}});
    else {
        if (mode > 4)
            p[DELAY_MODE] = (uint8_t)(mode - 3);
        put(p, {{REVERB_MODE, 1}, {REVERB_SEND, 0}, {REVERB_TYPE, 2}, {REVERB_TIME, 0x50}, {REVERB_DAMPING, 0x14},
                {REVERB_COLOUR, 0x31}, {REVERB_PREDELAY, 0x1C}, {REVERB_CLOCK, 0}, {REVERB_FEEDBACK, 0}});
    }
    if (mode == 0 || was_reverb)
        put(p, {{DELAY_MODE, 1}, {DELAY_SEND, 0}, {DELAY_TIME, 0x40}, {DELAY_FEEDBACK, 0}, {DELAY_LFO_RATE, 0x28},
                {DELAY_LFO_DEPTH, 6}, {DELAY_LFO_SHAPE, 1}, {DELAY_COLOUR, 0x40}, {DELAY_CLOCK, 0}});
}

void to_v9(uint8_t *p)
{
    using namespace off;
    if (p[OSC1_MODE] < 4)
        p[OSC1_FORMANT] = 0x40;
    if (p[OSC2_MODE] < 4)
        p[OSC2_FORMANT] = 0x40;
    if ((int8_t)p[PFE] > 1)
        p[PFE] = 0;
    put(p, {{TAPE_RATIO, 3}, {TAPE_LEFT_CLOCK, 4}, {TAPE_RIGHT_CLOCK, 2}, {TAPE_BANDWIDTH, 0}, {BOOST_TYPE, 0},
            {DELAY_TYPE, 0}, {DIST_TREBLE, 0}, {DIST_HIGH_CUT, 0}, {DIST_MIX, 0x7F}, {FILTERBANK_TYPE, 0},
            {FILTERBANK_MIX, 0x7F}, {FILTERBANK_FREQ, 0x40}, {FILTERBANK_PHASE, 0x40}, {FILTERBANK_SHAPE_L, 0x7F},
            {FILTERBANK_SHAPE_R, 0x7F}});
}

void to_v10(uint8_t *p)
{
    using namespace off;
    if ((int8_t)p[OSC_SELECT] > 1)
        p[OSC_SELECT] = 0;
    if ((int8_t)p[DISTORTION_TYPE] < 0x12)
        p[DIST_QUALITY] = 0x7F;
    if ((int8_t)p[DISTORTION_TYPE] < 0x14)
        p[DIST_TONE] = 0x40;
    if ((int8_t)p[FILTERBANK_TYPE] < 3)
        p[FILTERBANK_RESONANCE] = 0x40;
}

void to_v11(uint8_t *p)
{
    using namespace off;
    put(p, {{CHORUS_TYPE, 1}, {CHORUS_MIX, 0x7F}, {CHORUS_XOVER, 0}, {P80, 0x40}, {RINGMOD_VOLUME, p[OLD_RINGMOD]},
            {ARP_MODE, p[OLD_ARP_MODE]}});
    memcpy(p + UNISON, p + OLD_UNISON, 6);
}

void to_v12(uint8_t *p)
{
    static const uint8_t env[5] = {0x14, 0x46, 0, 0, 0x46}; // attack decay sustain slope release
    for (int e = 0; e < 2; e++)
        memcpy(p + off::ENVELOPES_3_4 + 5 * e, env, 5);
}

void single_checksums(std::vector<uint8_t> &m)
{
    unsigned sum = 0;
    for (int i = 5; i < 265; i++)
        sum += m[i];
    m[265] = sum & 0x7F;
    for (int i = 265; i < 522; i++)
        sum += m[i];
    m[522] = sum & 0x7F;
}
} // namespace

bool RemoteDevice::upgrade_single(std::vector<uint8_t> &m, const std::vector<uint8_t> &page_b)
{
    if (m.size() != 524 || m[0] != 0xF0 || m[6] != 0x10)
        return false;
    uint8_t *p = m.data() + 9;
    const int from = (int8_t)p[off::VERSION];
    if (from < 8 && page_b.size() != 256)
        return false; // pages C/D cannot be created
    const bool edit_buffer = m[7] == 0;
    int v = from;
    auto step = [&](int to, auto &&fn) {
        if (v < to) {
            fn();
            v = to;
        }
    };
    step(1, [&] { to_v1(p); });
    step(2, [&] { to_v2(p); });
    step(3, [&] { to_v3(p); });
    step(4, [&] { to_v4(p); });
    step(5, [&] { to_v5(p); });
    step(6, [&] { to_v6(p); });
    step(7, [&] { to_v7(p); });
    step(8, [&] { to_v8(p, page_b); });
    step(9, [&] { to_v9(p); });
    if (edit_buffer) { // stored patches stay at 9: the firmware converts them
        step(10, [&] { to_v10(p); });
        step(11, [&] { to_v11(p); });
        step(12, [&] { to_v12(p); });
    }
    if (v != from)
        p[off::VERSION] = (uint8_t)v;
    single_checksums(m);
    return v != from;
}

std::vector<RemoteDevice::Change> RemoteDevice::decode(const std::vector<uint8_t> &m) const
{
    std::vector<Change> out;
    if (by_ctl_.empty()) { // built lazily: (message id, controller index) -> parameters
        auto *self = const_cast<RemoteDevice *>(this);
        for (auto &p : parameters)
            if (p.controller >= 0 && !p.message.empty())
                self->by_ctl_[{p.message, p.controller}].push_back(p.path);
    }
    std::string id;
    int ctl = -1, part = 0, value = 0;
    if (m.size() == 3 && (m[0] & 0xF0) == 0xB0)
        id = "cc", ctl = m[1], part = m[0] & 15, value = m[2];
    else if (m.size() == 3 && (m[0] & 0xF0) == 0xA0)
        id = "pp", ctl = m[1], part = m[0] & 15, value = m[2];
    else if (m.size() == 11 && m[0] == 0xF0 && m[1] == 0x00 && m[2] == 0x20 && m[3] == 0x33 && m[10] == 0xF7) {
        for (auto &msg : messages) // the parameter SysEx template whose byte 6 is this page
            if (msg.bytes.size() == 11 && msg.bytes[6].tag == m[6])
                id = msg.id;
        ctl = m[8], part = m[7], value = m[9];
    }
    auto it = by_ctl_.find({id, ctl});
    if (it != by_ctl_.end())
        for (auto &path : it->second)
            out.push_back({path, part, value});
    return out;
}

int RemoteDevice::shown(const std::string &path, int v) const
{
    const Parameter *p = find(path);
    if (!p || p->max <= p->min)
        return v;
    auto vl = valuelists.find(p->valuelist);
    if (vl != valuelists.end() && vl->second.mapped && vl->second.find(v))
        return v;
    return std::max(p->min, std::min(p->max, v));
}

int RemoteDevice::value_of(const std::string &path, const std::string &text) const
{
    const Parameter *p = find(path);
    if (!p)
        return -1;
    auto vl = valuelists.find(p->valuelist);
    if (vl != valuelists.end())
        for (auto &e : vl->second.entries)
            if (e.text == text)
                return e.index;
    char *end = nullptr;
    long v = strtol(text.c_str(), &end, 10);
    if (end && *end == 0 && !text.empty()) {
        // numbered lists show 1-based numbers ("01" = MIDI channel 1 = value 0) when the list's first text is "1"/"01"
        if (vl != valuelists.end() && !vl->second.entries.empty()) {
            const auto &first = vl->second.entries.front();
            if (atoi(first.text.c_str()) == 1 && first.index == 0)
                return (int)v - 1;
        }
        return (int)v;
    }
    return -1;
}

std::vector<std::vector<uint8_t>> RemoteDevice::init_messages(int device_id, const std::vector<uint8_t> &multi) const
{
    std::vector<std::vector<uint8_t>> out;
    for (auto &g : global_init) {
        int v = value_of(g.first, g.second);
        auto m = v >= 0 ? encode(g.first, v, 0, device_id) : std::vector<uint8_t>{};
        if (!m.empty())
            out.push_back(m);
        else
            fprintf(stderr, "remotedevice: GlobalInit %s = %s not encodable\n", g.first.c_str(), g.second.c_str());
    }
    // like the original (E321): "Configuring Virus TI" on the Virus' display (Rise Message 7) with the message timer
    // at 100 while the parts are set up, the timer back at 1 after them
    auto push = [&](const char *path, int v) {
        auto m = encode(path, v, 0, device_id);
        if (!m.empty())
            out.push_back(m);
    };
    push("Actions/Rise Message", 7);
    push("Intern Display/Messagetimer", 100);
    if (!multi.empty())
        out.push_back(multi);
    for (auto &pi : param_init) {
        int v = value_of(pi.param, pi.value);
        auto m = v >= 0 ? encode(pi.param, v, pi.part - 1, device_id) : std::vector<uint8_t>{};
        if (!m.empty())
            out.push_back(m);
        else
            fprintf(stderr, "remotedevice: ParamInit %s = %s not encodable\n", pi.param.c_str(), pi.value.c_str());
    }
    push("Intern Display/Messagetimer", 1);
    return out;
}

bool RemoteDevice::decode_dump(const MidiMessage &m, const std::vector<uint8_t> &msg, std::map<std::string, int> &values,
                               std::string &error) const
{
    if (msg.size() != dump_size(m)) {
        error = m.id + ": " + std::to_string(msg.size()) + " bytes, expected " + std::to_string(dump_size(m));
        return false;
    }
    for (auto &b : m.bytes) {
        if (!b.method.empty() && checksum(b, msg) != msg[b.index]) {
            error = m.id + ": checksum mismatch at byte " + std::to_string(b.index);
            return false;
        }
        if (b.content == "byte" && b.tag >= 0 && b.index < 7 && msg[b.index] != b.tag) {
            error = m.id + ": header byte " + std::to_string(b.index) + " differs";
            return false;
        }
    }
    for (auto &b : m.bytes)
        if (is_field(b)) {
            int v = msg[b.index];
            if (b.mask >= 0)
                v = (v & b.mask) >> b.shift;
            values[field_key(b)] = v;
        }
    return true;
}

void RemoteDevice::update_dump(const MidiMessage &m, std::vector<uint8_t> &msg, const std::map<std::string, int> &values) const
{
    if (msg.size() < dump_size(m))
        msg.resize(dump_size(m));
    if (values.size() == 1 && !m.field_bytes.empty()) { // one value (automation, knobs): only its bytes
        auto f = m.field_bytes.find(values.begin()->first);
        if (f != m.field_bytes.end()) {
            int v = values.begin()->second;
            for (size_t i : f->second) {
                const auto &b = m.bytes[i];
                if (b.mask >= 0)
                    msg[b.index] = (uint8_t)((msg[b.index] & ~b.mask) | ((v << b.shift) & b.mask));
                else
                    msg[b.index] = (uint8_t)(v & 0x7F);
            }
        }
        for (size_t i : m.checksum_bytes)
            msg[m.bytes[i].index] = (uint8_t)checksum(m.bytes[i], msg);
        return;
    }
    for (auto &b : m.bytes) {
        if (!is_field(b))
            continue;
        auto it = values.find(field_key(b));
        if (it == values.end())
            continue;
        if (b.mask >= 0)
            msg[b.index] = (uint8_t)((msg[b.index] & ~b.mask) | ((it->second << b.shift) & b.mask));
        else
            msg[b.index] = (uint8_t)(it->second & 0x7F);
    }
    for (auto &b : m.bytes)
        if (!b.method.empty())
            msg[b.index] = (uint8_t)checksum(b, msg);
}

bool load_automation(const std::vector<uint8_t> &xml, std::vector<AutomationEntry> &out, std::string &error)
{
    pugi::xml_document doc;
    auto r = doc.load_buffer(xml.data(), xml.size());
    if (!r) {
        error = std::string("automation.xml: ") + r.description();
        return false;
    }
    for (auto a : doc.child("AutomationList").children("Automation")) {
        AutomationEntry e;
        e.path = a.attribute("Parameter").as_string();
        e.part = a.attribute("Part").as_int(1);
        e.id = a.attribute("Index").as_int(-1);
        if (e.id < 0 || e.path.empty())
            continue;
        // keyed by (parameter, part) like the original: a later entry replaces an earlier one (automation.xml lists
        // Filter Bank/Filter Frequency twice for part 4 - id 1574 should be part 3; the original leaves 1574 unmapped)
        auto same = std::find_if(out.begin(), out.end(),
                                 [&](const AutomationEntry &o) { return o.path == e.path && o.part == e.part; });
        if (same != out.end())
            *same = e;
        else
            out.push_back(e);
    }
    return !out.empty();
}

std::string automation_title(const RemoteDevice &dev, const AutomationEntry &e)
{
    const Parameter *p = dev.find(e.path);
    if (p && !p->shortname.empty())
        return p->shortname;
    size_t slash = e.path.rfind('/');
    return slash == std::string::npos ? e.path : e.path.substr(slash + 1);
}

} // namespace vti
