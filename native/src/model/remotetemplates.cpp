#include "model/remotetemplates.h"
#include "model/utf8.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

namespace vti {

// record layout (E196): F0 00 20 33 01 10 13 <template> then a name record: 20 <name 9..24> <flags 25> 00 F7, a knob
// record: <knob> <name 9..18> <behaviour low 19, high 20> 21..22 <MIDI string 23..62> 00 F7
static constexpr int kNameAt = 9, kNameLen = 16, kFlagsAt = 25, kKnobNameLen = 10, kBehaviourAt = 19, kMidiAt = 23,
                     kMidiLen = 40;
static constexpr size_t kNameRec = 28, kOldNameRec = 27, kKnobRec = 65;
static const uint8_t kHead[] = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x13};

static std::string trimmed(const std::string &s)
{
    size_t a = s.find_first_not_of(' '), b = s.find_last_not_of(' ');
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

static std::string text(const std::vector<uint8_t> &r, int at, int len)
{
    std::string s;
    for (int i = 0; i < len; i++)
        s += r[at + i] >= 32 && r[at + i] < 127 ? (char)r[at + i] : ' ';
    return trimmed(s);
}

// the original pads with spaces and copies at most `len` characters (strncpy)
static void put_text(std::vector<uint8_t> &r, int at, int len, const std::string &s)
{
    for (int i = 0; i < len; i++) {
        unsigned char c = i < (int)s.size() ? (unsigned char)s[i] : ' ';
        r[at + i] = c >= 32 && c < 127 ? c : ' ';
    }
}

// like the original's receiver (0x18008b8d0): the header and the length decide, not the last byte (two factory
// templates end their knob records with 00 00, E198)
static bool ours(const std::vector<uint8_t> &r)
{
    return r.size() > sizeof kHead && std::equal(kHead, kHead + sizeof kHead, r.begin());
}

static bool valid(int t, int k = 0)
{
    return t >= 0 && t < RemoteTemplates::kTemplates && k >= 0 && k < RemoteTemplates::kKnobs;
}

RemoteTemplates::RemoteTemplates()
{
    for (int t = 0; t < kTemplates; t++) {
        auto &n = names_[t] = std::vector<uint8_t>(kHead, kHead + sizeof kHead);
        n.resize(kNameRec, 0);
        n[7] = (uint8_t)t;
        n[8] = 0x20;
        put_text(n, kNameAt, kNameLen, "Init Template"); // the binary's " Init Template   " includes byte 8
        n[kNameRec - 1] = 0xF7;
        for (int k = 0; k < kKnobs; k++) {
            auto &r = knobs_[t][k] = std::vector<uint8_t>(kHead, kHead + sizeof kHead);
            r.resize(kKnobRec, 0);
            r[7] = (uint8_t)t;
            r[8] = (uint8_t)k;
            put_text(r, kNameAt, kKnobNameLen, "");
            r[kMidiAt] = 0x0E; // an empty MIDI string: just the end mark
            r[kMidiAt + 1] = 0x0F;
            r[kKnobRec - 1] = 0xF7;
        }
    }
}

std::string RemoteTemplates::name(int t) const
{
    return valid(t) ? text(names_[t], kNameAt, kNameLen) : std::string();
}

bool RemoteTemplates::channel_enabled(int t) const
{
    return valid(t) && (names_[t][kFlagsAt] & 1);
}

std::string RemoteTemplates::knob_name(int t, int k) const
{
    return valid(t, k) ? text(knobs_[t][k], kNameAt, kKnobNameLen) : std::string();
}

int RemoteTemplates::knob_behaviour(int t, int k) const
{
    if (!valid(t, k))
        return 0;
    const auto &r = knobs_[t][k];
    return (r[kBehaviourAt] & 0x0F) | (r[kBehaviourAt + 1] & 0x0F) << 4;
}

std::string RemoteTemplates::midi_string(int t, int k) const
{
    return valid(t, k) ? midi_text(knobs_[t][k].data() + kMidiAt) : std::string();
}

static int hex_digit(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

std::vector<uint8_t> RemoteTemplates::encode_midi(const std::string &text)
{
    std::string s = text.substr(0, 254);
    for (auto &c : s)
        c = (char)toupper((unsigned char)c);
    std::vector<uint8_t> out(kMidiLen, 0);
    int n = 0;
    size_t i = 0;
    auto put = [&](int lo, int hi) {
        out[n] = (uint8_t)(lo & 0x0F);
        out[n + 1] = (uint8_t)(hi & 0x0F);
        n += 2;
    };
    auto value_char = [&](size_t at) { return at < s.size() && (s[at] == '*' || s[at] == 'X' || s[at] == 'V'); };
    auto skip_separators = [&]() {
        while (i < s.size() && s[i] != '*' && s[i] != '%' &&
               (isspace((unsigned char)s[i]) || ispunct((unsigned char)s[i])))
            i++;
    };
    while (i < s.size() && n < kMidiLen) {
        if (value_char(i)) { // the knob's value; a run of them is one
            put(0x0F, 0x0F);
            while (value_char(i))
                i++;
        } else if (s[i] == '%') { // a decimal byte
            i++;
            if (i >= s.size() || !isdigit((unsigned char)s[i]))
                continue;
            int v = std::max(0, std::min(255, atoi(s.c_str() + i)));
            put(v, v >> 4);
            while (i < s.size() && isdigit((unsigned char)s[i]))
                i++;
        } else {
            int hi = hex_digit(s[i]), lo = i + 1 < s.size() ? hex_digit(s[i + 1]) : -1;
            i++;
            if (hi >= 0 && lo >= 0) {
                put(lo, hi);
                i++;
            } else if (hi >= 0) // a lone digit is the value itself
                put(hi, 0);
        }
        skip_separators();
    }
    if (n <= kMidiLen - 2)
        put(0x0E, 0x0F);
    return out;
}

std::string RemoteTemplates::midi_text(const uint8_t *b)
{
    static const char digits[] = "0123456789ABCDEF";
    std::string s;
    for (int i = 0; i < kMidiLen; i += 2) {
        if (b[i] == 0x0E && b[i + 1] == 0x0F)
            break;
        if (b[i] == 0x0F && b[i + 1] == 0x0F)
            s += "* ";
        else {
            s += digits[b[i + 1] & 0x0F];
            s += digits[b[i] & 0x0F];
            s += ' ';
        }
    }
    return trimmed(s);
}

std::string RemoteTemplates::midi_info(int t, int k) const
{
    if (!valid(t, k))
        return {};
    static const char *const types[] = {"Note Off",       "Note On",          "Poly Pressure", "Control Change",
                                        "Program Change", "Channel Pressure", "Pitch Bend"};
    const uint8_t *b = knobs_[t][k].data() + kMidiAt;
    std::string s;
    for (int i = 0; i < kMidiLen; i += 2) {
        int lo = b[i] & 0x0F, hi = b[i + 1] & 0x0F;
        if (lo == 0x0E && hi == 0x0F)
            break;
        if (i > 0)
            s += ", ";
        if (hi < 8)
            s += std::to_string(hi * 16 + lo);
        else if (hi < 0x0F)
            s += std::string(types[hi - 8]) + " Ch " + std::to_string(lo + 1);
        else
            s += lo == 0 ? "SysEx " : lo == 7 ? "End " : lo == 0x0F ? "Value " : "System Realtime ";
    }
    return s;
}

void RemoteTemplates::set_midi_string(int t, int k, const std::string &text)
{
    if (!valid(t, k))
        return;
    auto b = encode_midi(text);
    std::copy(b.begin(), b.end(), knobs_[t][k].begin() + kMidiAt);
}

void RemoteTemplates::set_name(int t, const std::string &name)
{
    if (valid(t))
        put_text(names_[t], kNameAt, kNameLen, name);
}

void RemoteTemplates::set_channel_enabled(int t, bool on)
{
    if (valid(t))
        names_[t][kFlagsAt] = (uint8_t)((names_[t][kFlagsAt] & 0x7E) | (on ? 1 : 0));
}

void RemoteTemplates::set_knob_name(int t, int k, const std::string &name)
{
    if (valid(t, k))
        put_text(knobs_[t][k], kNameAt, kKnobNameLen, name);
}

void RemoteTemplates::set_knob_behaviour(int t, int k, int value)
{
    if (!valid(t, k))
        return;
    knobs_[t][k][kBehaviourAt] = (uint8_t)(value & 0x0F);
    knobs_[t][k][kBehaviourAt + 1] = (uint8_t)((value >> 4) & 0x0F);
}

const std::vector<uint8_t> &RemoteTemplates::name_record(int t) const
{
    static const std::vector<uint8_t> none;
    return valid(t) ? names_[t] : none;
}

const std::vector<uint8_t> &RemoteTemplates::knob_record(int t, int k) const
{
    static const std::vector<uint8_t> none;
    return valid(t, k) ? knobs_[t][k] : none;
}

std::vector<std::vector<uint8_t>> RemoteTemplates::template_records(int t) const
{
    if (!valid(t))
        return {};
    std::vector<std::vector<uint8_t>> out = {names_[t]};
    out.insert(out.end(), knobs_[t].begin(), knobs_[t].end());
    return out;
}

bool RemoteTemplates::take_record(const std::vector<uint8_t> &r)
{
    if (!ours(r) || r[7] >= kTemplates)
        return false;
    if (r.size() == kKnobRec) {
        if (r[8] >= kKnobs)
            return false;
        knobs_[r[7]][r[8]] = r;
        return true;
    }
    if ((r.size() != kNameRec && r.size() != kOldNameRec) || r[8] != 0x20)
        return false;
    std::vector<uint8_t> n = r;
    if (n.size() == kOldNameRec) // no flags byte yet: a cleared one goes in after the name (0x18008b8d0)
        n.insert(n.begin() + kFlagsAt, 0);
    names_[r[7]] = n;
    return true;
}

static void put_be32(std::vector<uint8_t> &out, size_t v)
{
    for (int s : {24, 16, 8, 0})
        out.push_back((uint8_t)(v >> s));
}

std::vector<uint8_t> RemoteTemplates::cache() const
{
    std::vector<uint8_t> body;
    auto add = [&body](const std::vector<uint8_t> &r) {
        put_be32(body, r.size());
        body.insert(body.end(), r.begin(), r.end());
    };
    for (int t = 0; t < kTemplates; t++) {
        add(names_[t]);
        for (auto &k : knobs_[t])
            add(k);
    }
    std::vector<uint8_t> out = {'M', 'I', 'D', 'I'};
    put_be32(out, body.size());
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

int RemoteTemplates::load_cache(const std::vector<uint8_t> &d)
{
    static const uint8_t magic[] = {'M', 'I', 'D', 'I'};
    if (d.size() < 8 || !std::equal(magic, magic + 4, d.begin()))
        return 0;
    int taken = 0;
    for (size_t pos = 8; pos + 4 <= d.size();) {
        size_t n = (size_t)d[pos] << 24 | (size_t)d[pos + 1] << 16 | (size_t)d[pos + 2] << 8 | d[pos + 3];
        pos += 4;
        if (n > d.size() - pos) // a cut file: the complete records before the cut
            break;
        taken += take_record(std::vector<uint8_t>(d.begin() + pos, d.begin() + pos + n));
        pos += n;
    }
    return taken;
}

static uint32_t be32(const std::vector<uint8_t> &d, size_t at)
{
    return (uint32_t)d[at] << 24 | (uint32_t)d[at + 1] << 16 | (uint32_t)d[at + 2] << 8 | d[at + 3];
}

std::vector<uint8_t> RemoteTemplates::template_file(int t) const
{
    if (!valid(t))
        return {};
    std::vector<uint8_t> body;
    auto add = [&body](const std::vector<uint8_t> &r) {
        put_be32(body, r.size());
        body.insert(body.end(), r.begin(), r.end());
    };
    add(names_[t]);
    for (auto &k : knobs_[t])
        add(k);
    std::vector<uint8_t> midi = {'M', 'I', 'D', 'I'};
    put_be32(midi, body.size());
    midi.insert(midi.end(), body.begin(), body.end());
    std::vector<uint8_t> out = {'R', 'M', 'O', 'T'};
    put_be32(out, 12); // the header's size
    put_be32(out, 1);  // version
    out.insert(out.end(), {'R', 'S', 'E', 'T'});
    put_be32(out, midi.size());
    out.insert(out.end(), midi.begin(), midi.end());
    return out;
}

int RemoteTemplates::load_template_file(const std::vector<uint8_t> &d, int t)
{
    if (!valid(t) || d.size() < 12 || !std::equal(d.begin(), d.begin() + 4, "RMOT") || be32(d, 8) != 1)
        return 0;
    int taken = 0;
    for (size_t pos = std::max<size_t>(12, be32(d, 4)); pos + 8 <= d.size();) {
        size_t n = be32(d, pos + 4);
        bool rset = std::equal(d.begin() + pos, d.begin() + pos + 4, "RSET");
        pos += 8;
        if (n > d.size() - pos)
            break;
        if (rset) { // the records go to template t (0x180086bf0 rewrites their byte 7)
            std::vector<uint8_t> c(d.begin() + pos, d.begin() + pos + n);
            for (size_t r = 8; r + 4 <= c.size();) {
                size_t len = be32(c, r);
                r += 4;
                if (len > c.size() - r)
                    break;
                std::vector<uint8_t> rec(c.begin() + r, c.begin() + r + len);
                if (rec.size() > 7 && (len == kNameRec || len == kOldNameRec || len == kKnobRec))
                    rec[7] = (uint8_t)t;
                taken += take_record(rec);
                r += len;
            }
        }
        pos += n;
    }
    return taken;
}

std::vector<std::string> remote_template_files(const std::string &dir)
{
    namespace fs = std::filesystem;
    std::vector<std::string> out;
    std::error_code ec;
    const fs::path root = u8path(dir); // UTF-8 names (#35: path::string() threw on Windows outside the code page)
    for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code fe;
        if (!it->is_regular_file(fe))
            continue;
        std::string ext = u8str(it->path().extension());
        for (auto &c : ext)
            if (c >= 'A' && c <= 'Z')
                c = (char)(c - 'A' + 'a');
        if (ext == ".vrt")
            out.push_back(u8generic(fs::relative(it->path(), root, fe)));
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::string remote_template_name(const std::string &file)
{
    return file.size() > 4 ? file.substr(0, file.size() - 4) : file;
}

bool RemoteTemplates::load_file(const std::string &path)
{
    std::vector<uint8_t> d;
    if (FILE *f = u8fopen(path, "rb")) {
        uint8_t buf[4096];
        for (size_t n; (n = fread(buf, 1, sizeof buf, f)) > 0;)
            d.insert(d.end(), buf, buf + n);
        fclose(f);
    }
    return load_cache(d) > 0;
}

bool RemoteTemplates::save_file(const std::string &path) const
{
    const std::string tmp = path + ".tmp";
    FILE *f = u8fopen(tmp, "wb");
    if (!f)
        return false;
    auto d = cache();
    bool ok = fwrite(d.data(), 1, d.size(), f) == d.size();
    ok = fclose(f) == 0 && ok;
    std::error_code ec;
    if (ok)
        std::filesystem::rename(u8path(tmp), u8path(path), ec); // another instance reading meanwhile sees the old or the new file
    if (!ok || ec)
        std::filesystem::remove(u8path(tmp), ec);
    return ok && !ec;
}

std::string remote_text(const std::string &param, const RemoteTemplates &t, int selected, int knob)
{
    const int sel = selected < 0 ? 0 : selected; // nothing selected yet: the original shows template 0
    if (param == "Remote Templates/Template Name")
        return t.name(sel);
    if (param == "Remote Templates/MIDI Channel Behaviour")
        return t.channel_enabled(sel) ? "Enable MIDI Channel" : "Disable MIDI channel";
    if (param == "Remote Templates/Knob Name")
        return t.knob_name(sel, knob);
    if (param == "Remote Templates/MIDI String")
        return t.midi_string(sel, knob);
    if (param == "Remote Templates/MIDI String Info")
        return t.midi_info(sel, knob);
    return {};
}

std::vector<uint8_t> remote_edit_record(const std::string &param, const RemoteTemplates &t, int selected, int knob)
{
    const int sel = selected < 0 ? 0 : selected;
    if (param == "Remote Templates/Template Name" || param == "Remote Templates/MIDI Channel Behaviour")
        return t.name_record(sel);
    if (param == "Remote Templates/Knob Name" || param == "Remote Templates/MIDI String")
        return t.knob_record(sel, knob);
    return {};
}

bool remote_set_text(const std::string &param, RemoteTemplates &t, int selected, int knob, const std::string &text)
{
    const int sel = selected < 0 ? 0 : selected;
    if (param == "Remote Templates/Template Name")
        t.set_name(sel, text);
    else if (param == "Remote Templates/Knob Name")
        t.set_knob_name(sel, knob, text);
    else if (param == "Remote Templates/MIDI String")
        t.set_midi_string(sel, knob, text);
    else
        return false;
    return true;
}

} // namespace vti
