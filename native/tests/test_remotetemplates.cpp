// REMOTE page, read-only (E193): the original keeps 32 remote templates in the plug-in (default " Init Template   ",
// constructor 0x1800899b0) and reads none from the Virus; its code defines the page's parameters ("Remote Templates/
// ...", value list "Val MIDI Channel Behaviour": Disable MIDI channel / Enable MIDI Channel).
#include "check.h"
#include "model/remotetemplates.h"
#include <ctime>
#include <filesystem>
#include <fstream>
using namespace vti;

TEST(remote_templates_defaults)
{
    RemoteTemplates t;
    CHECK(t.name(0) == "Init Template" && t.name(31) == "Init Template");
    CHECK(t.name(-1).empty() && t.name(32).empty());
    CHECK(remote_text("Remote Templates/Template Name", t, 5, 0) == "Init Template");
    CHECK(remote_text("Remote Templates/Template Name", t, -1, 0) == "Init Template"); // nothing selected: template 0
    CHECK(remote_text("Remote Templates/MIDI Channel Behaviour", t, 0, 0) == "Disable MIDI channel");
    t.set_channel_enabled(0, true); // the selected template's flag
    CHECK(remote_text("Remote Templates/MIDI Channel Behaviour", t, 0, 0) == "Enable MIDI Channel");
    CHECK(remote_text("Remote Templates/MIDI Channel Behaviour", t, -1, 0) == "Enable MIDI Channel");
    CHECK(remote_text("Remote Templates/MIDI Channel Behaviour", t, 1, 0) == "Disable MIDI channel");
    CHECK(remote_text("Osc/Shape", t, 0, 0).empty());
}

// a template's name from its SysEx name record (command 0x13, byte 8 = 0x20): trimmed like the original shows it
TEST(remote_templates_name_record)
{
    RemoteTemplates t;
    const uint8_t rec[] = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x13, 0x03, 0x20, ' ', 'M', 'y', ' ', 'S', 'y', 'n', 't',
                           'h', ' ', ' ', ' ', ' ', ' ', ' ', ' ', 0x00, 0x00, 0xF7};
    CHECK(t.take_record(std::vector<uint8_t>(rec, rec + sizeof rec)));
    CHECK(t.name(3) == "My Synth");
    std::vector<uint8_t> other(rec, rec + sizeof rec);
    other[6] = 0x10; // a single dump: not ours
    CHECK(!t.take_record(other));
    other = std::vector<uint8_t>(rec, rec + sizeof rec);
    other[7] = 40; // template out of range
    CHECK(!t.take_record(other));
}

namespace {
// the cache's container: "MIDI", a big-endian total length, then each record behind its big-endian length (E196)
std::vector<uint8_t> container(const std::vector<std::vector<uint8_t>> &recs)
{
    std::vector<uint8_t> body;
    for (auto &r : recs) {
        for (int s : {24, 16, 8, 0})
            body.push_back((uint8_t)(r.size() >> s));
        body.insert(body.end(), r.begin(), r.end());
    }
    std::vector<uint8_t> out = {'M', 'I', 'D', 'I'};
    for (int s : {24, 16, 8, 0})
        out.push_back((uint8_t)(body.size() >> s));
    out.insert(out.end(), body.begin(), body.end());
    return out;
}
} // namespace

// the default records: what the original's constructor builds (0x1800899b0) and its cache holds untouched (E196)
TEST(remote_templates_default_records)
{
    RemoteTemplates t;
    const auto &n = t.name_record(7);
    CHECK(n.size() == 28 && n[0] == 0xF0 && n[5] == 0x10 && n[6] == 0x13 && n[7] == 7 && n[8] == 0x20);
    CHECK(std::string(n.begin() + 9, n.begin() + 25) == "Init Template   " && n[25] == 0 && n[26] == 0 && n[27] == 0xF7);
    const auto &k = t.knob_record(7, 31);
    CHECK(k.size() == 65 && k[6] == 0x13 && k[7] == 7 && k[8] == 31 && k[64] == 0xF7);
    CHECK(std::string(k.begin() + 9, k.begin() + 19) == "          " && k[19] == 0 && k[20] == 0);
    CHECK(k[23] == 0x0E && k[24] == 0x0F && k[25] == 0); // the default MIDI string
    CHECK(t.knob_name(7, 31).empty() && t.knob_behaviour(7, 31) == 0 && !t.channel_enabled(7));
    CHECK(t.knob_record(32, 0).empty() && t.knob_record(0, 32).empty() && t.name_record(-1).empty());
}

// edits change the records the way the original's value handler does (0x18008b020): names padded with spaces and cut
// to 16 / 10 characters, MIDI Channel Behaviour = bit 0 of the flags byte, Knob Behaviour in two nibbles
TEST(remote_templates_edit)
{
    RemoteTemplates t;
    t.set_name(2, "Ableton Live Device Ctrl");
    CHECK(t.name(2) == "Ableton Live Dev");
    CHECK(std::string(t.name_record(2).begin() + 9, t.name_record(2).begin() + 25) == "Ableton Live Dev");
    t.set_name(2, "Live");
    CHECK(std::string(t.name_record(2).begin() + 9, t.name_record(2).begin() + 25) == "Live            ");
    t.set_channel_enabled(2, true);
    CHECK(t.channel_enabled(2) && t.name_record(2)[25] == 0x01);
    t.set_channel_enabled(2, false);
    CHECK(!t.channel_enabled(2) && t.name_record(2)[25] == 0x00);

    t.set_knob_name(2, 5, "Cutoff Frequency");
    CHECK(t.knob_name(2, 5) == "Cutoff Fre");
    CHECK(std::string(t.knob_record(2, 5).begin() + 9, t.knob_record(2, 5).begin() + 19) == "Cutoff Fre");
    t.set_knob_behaviour(2, 5, 0x5A);
    CHECK(t.knob_behaviour(2, 5) == 0x5A && t.knob_record(2, 5)[19] == 0x0A && t.knob_record(2, 5)[20] == 0x05);
    CHECK(t.knob_name(2, 4).empty() && t.name(3) == "Init Template"); // nothing else changed
    t.set_name(40, "x");                                             // out of range: ignored
    t.set_knob_name(0, 40, "x");
    CHECK(t.name(0) == "Init Template");
}

// records from a cache or a template file: knob records (65 bytes), name records (28), and the older 27-byte name
// record without the flags byte, which the original converts (0x18008b8d0)
TEST(remote_templates_take_records)
{
    RemoteTemplates src;
    src.set_knob_name(4, 9, "Reso");
    src.set_knob_behaviour(4, 9, 3);
    RemoteTemplates t;
    CHECK(t.take_record(src.knob_record(4, 9)));
    CHECK(t.knob_name(4, 9) == "Reso" && t.knob_behaviour(4, 9) == 3);
    std::vector<uint8_t> bad = src.knob_record(4, 9);
    bad[8] = 40; // knob out of range
    CHECK(!t.take_record(bad));
    bad = src.knob_record(4, 9);
    bad[5] = 0x00; // another device id: the original checks 01 10
    CHECK(!t.take_record(bad));

    const uint8_t old[] = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x13, 0x06, 0x20, 'O', 'l', 'd', ' ', ' ', ' ', ' ', ' ',
                           ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', 0x00, 0xF7};
    CHECK(t.take_record(std::vector<uint8_t>(old, old + sizeof old)));
    const auto &n = t.name_record(6);
    CHECK(n.size() == 28 && t.name(6) == "Old" && n[25] == 0 && n[26] == 0x00 && n[27] == 0xF7);
}

// the cache file round trip ({common_appdata}/Access Music/Virus TI/Cache/RemoteSets in the original): 32 x (name
// record + 32 knob records), the same layout as the original's (checked on a real one: 71688 bytes)
TEST(remote_templates_cache)
{
    RemoteTemplates a;
    a.set_name(31, "Last");
    a.set_channel_enabled(31, true);
    a.set_knob_name(0, 0, "First");
    auto bytes = a.cache();
    CHECK(bytes.size() == 71688);
    CHECK(bytes[0] == 'M' && bytes[3] == 'I' && bytes[4] == 0x00 && bytes[5] == 0x01 && bytes[6] == 0x18 && bytes[7] == 0);
    CHECK(bytes[8] == 0 && bytes[11] == 28 && bytes[12] == 0xF0); // template 0's name record first
    CHECK(bytes[8 + 4 + 28 + 3] == 65);                        // then its knob records

    RemoteTemplates b;
    CHECK(b.load_cache(bytes) == 32 * 33);
    CHECK(b.name(31) == "Last" && b.channel_enabled(31) && b.knob_name(0, 0) == "First");
    CHECK(b.cache() == bytes);

    RemoteTemplates c;
    CHECK(c.load_cache(container({a.name_record(31)})) == 1 && c.name(31) == "Last"); // a part is fine
    CHECK(c.load_cache({'R', 'I', 'F', 'F', 0, 0, 0, 0}) == 0);                     // not a MIDI container
    std::vector<uint8_t> cut = bytes;
    cut.resize(100); // a truncated file: the complete records before the cut
    RemoteTemplates d;
    CHECK(d.load_cache(cut) == 1);
}

// the page's text fields: Template Name, Knob Name and MIDI String of the selected template / knob (nothing selected:
// template 0, like the original shows it)
TEST(remote_templates_text_fields)
{
    RemoteTemplates t;
    CHECK(remote_set_text("Remote Templates/Template Name", t, 4, 0, "Bitwig"));
    CHECK(t.name(4) == "Bitwig" && remote_text("Remote Templates/Template Name", t, 4, 0) == "Bitwig");
    CHECK(remote_set_text("Remote Templates/Template Name", t, -1, 0, "Zero") && t.name(0) == "Zero");
    CHECK(remote_set_text("Remote Templates/Knob Name", t, 4, 17, "Drive"));
    CHECK(t.knob_name(4, 17) == "Drive" && remote_text("Remote Templates/Knob Name", t, 4, 17) == "Drive");
    CHECK(remote_text("Remote Templates/Knob Name", t, 4, 16).empty());
    CHECK(remote_set_text("Remote Templates/MIDI String", t, 4, 17, "b0 5 x"));
    CHECK(remote_text("Remote Templates/MIDI String", t, 4, 17) == "B0 05 *");
    CHECK(!remote_set_text("Repository Actions/Search", t, 4, 17, "x"));
}

// the cache file on disk: written whole (a temporary file renamed over the old one), read back the same
TEST(remote_templates_file)
{
    auto dir = std::filesystem::temp_directory_path() / ("vtitests-remote-" + std::to_string(::time(nullptr)));
    std::filesystem::create_directories(dir);
    const std::string path = (dir / "RemoteSets").string();
    RemoteTemplates a;
    a.set_name(9, "Saved");
    CHECK(a.save_file(path));
    RemoteTemplates b;
    CHECK(b.load_file(path) && b.name(9) == "Saved");
    CHECK(!std::filesystem::exists(path + ".tmp"));
    RemoteTemplates c;
    CHECK(!c.load_file((dir / "missing").string()) && c.name(9) == "Init Template");
    CHECK(!a.save_file((dir / "no" / "such" / "dir" / "RemoteSets").string()));
    std::filesystem::remove_all(dir);
}

// the MIDI string's 40 bytes like the original's parser (0x1800893f0, E196): each MIDI byte as two bytes (low nibble,
// high nibble), "*", "X" or "V" = the knob's value (0F 0F), "%n" = a decimal byte, spaces and punctuation skipped, a
// lone hex digit = that value, the end marked by 0E 0F
TEST(remote_templates_midi_string)
{
    auto enc = RemoteTemplates::encode_midi("B0 05 *");
    CHECK(enc.size() == 40);
    const uint8_t want[] = {0x00, 0x0B, 0x05, 0x00, 0x0F, 0x0F, 0x0E, 0x0F, 0x00};
    CHECK(std::equal(want, want + sizeof want, enc.begin()));
    CHECK(RemoteTemplates::encode_midi("b0,5;XXX") == enc);       // lower case, punctuation, a run of X = one value
    CHECK(RemoteTemplates::encode_midi("%176 %5 v") == enc);      // decimal bytes
    CHECK(RemoteTemplates::encode_midi("%300")[0] == 0x0F && RemoteTemplates::encode_midi("%300")[1] == 0x0F); // 255
    CHECK(RemoteTemplates::encode_midi("ff")[0] == 0x0F && RemoteTemplates::encode_midi("ff")[1] == 0x0F); // = value
    auto empty = RemoteTemplates::encode_midi("");
    CHECK(empty[0] == 0x0E && empty[1] == 0x0F && empty[2] == 0); // the default record's string
    CHECK(RemoteTemplates::encode_midi("G!") == empty);          // nothing usable
    // 20 bytes fill all 40: no end marker
    std::string twenty;
    for (int i = 0; i < 20; i++)
        twenty += "12 ";
    auto full = RemoteTemplates::encode_midi(twenty + "34");
    CHECK(full[38] == 0x02 && full[39] == 0x01);
    auto nineteen = RemoteTemplates::encode_midi(twenty.substr(0, 19 * 3));
    CHECK(nineteen[38] == 0x0E && nineteen[39] == 0x0F);

    CHECK(RemoteTemplates::midi_text(enc.data()) == "B0 05 *");
    CHECK(RemoteTemplates::midi_text(empty.data()).empty());
    CHECK(RemoteTemplates::midi_text(full.data()).size() == 20 * 3 - 1);

    RemoteTemplates t;
    CHECK(t.midi_string(1, 2).empty()); // the default 0E 0F
    t.set_midi_string(1, 2, "C0 *");
    CHECK(t.midi_string(1, 2) == "C0 *");
    CHECK(t.knob_record(1, 2)[23] == 0x00 && t.knob_record(1, 2)[24] == 0x0C && t.knob_record(1, 2)[25] == 0x0F);
    CHECK(t.knob_record(1, 2)[63] == 0x00 && t.knob_record(1, 2)[64] == 0xF7);
}

// "MIDI String Info" like the original's (0x18008bee0): per byte a data value in decimal, a channel message's type and
// channel, "SysEx" / "End" / "Value" / "System Realtime" for F0 / F7 / the value / other Fx, joined by ", "
TEST(remote_templates_midi_info)
{
    RemoteTemplates t;
    CHECK(t.midi_info(0, 0).empty());
    t.set_midi_string(0, 0, "B0 05 *");
    CHECK(t.midi_info(0, 0) == "Control Change Ch 1, 5, Value ");
    t.set_midi_string(0, 0, "9F 3C 7F 80 E5");
    CHECK(t.midi_info(0, 0) == "Note On Ch 16, 60, 127, Note Off Ch 1, Pitch Bend Ch 6");
    t.set_midi_string(0, 0, "F0 7F F7 F8 A0 C1 D2");
    CHECK(t.midi_info(0, 0) == "SysEx , 127, End , System Realtime , Poly Pressure Ch 1, Program Change Ch 2, "
                                "Channel Pressure Ch 3");
    CHECK(remote_text("Remote Templates/MIDI String Info", t, 0, 0) == t.midi_info(0, 0));
}

// template files (*.vrt, E198): "RMOT", header size 12, version 1 (big-endian), then chunks of (id, big-endian
// length); "RSET" holds the template's name record and 32 knob records in a "MIDI" container. Loading puts the
// records into the selected template whatever template they were saved from, like the original (0x180086bf0).
TEST(remote_templates_template_file)
{
    RemoteTemplates a;
    a.set_name(5, "Synth Ctl");
    a.set_knob_name(5, 3, "Filter");
    a.set_midi_string(5, 3, "B0 4A *");
    auto f = a.template_file(5);
    const uint8_t head[] = {'R', 'M', 'O', 'T', 0, 0, 0, 12, 0, 0, 0, 1, 'R', 'S', 'E', 'T'};
    CHECK(f.size() > 20 && std::equal(head, head + sizeof head, f.begin()));
    const size_t body = (size_t)f[16] << 24 | f[17] << 16 | f[18] << 8 | f[19];
    CHECK(body == f.size() - 20 && f[20] == 'M' && f[23] == 'I');
    CHECK(body == 8 + 4 + 28 + 32 * (4 + 65)); // one template

    RemoteTemplates b;
    CHECK(b.load_template_file(f, 12) == 33);
    CHECK(b.name(12) == "Synth Ctl" && b.knob_name(12, 3) == "Filter" && b.midi_string(12, 3) == "B0 4A *");
    CHECK(b.name_record(12)[7] == 12 && b.knob_record(12, 3)[7] == 12);
    CHECK(b.name(5) == "Init Template"); // the template it came from is untouched

    // unknown chunks before RSET are skipped; a bigger header size is honoured
    std::vector<uint8_t> g = {'R', 'M', 'O', 'T', 0, 0, 0, 16, 0, 0, 0, 1, 9, 9, 9, 9, 'X', 'T', 'R', 'A', 0, 0, 0, 2, 7, 7};
    g.insert(g.end(), f.begin() + 12, f.end());
    RemoteTemplates c;
    CHECK(c.load_template_file(g, 0) == 33 && c.name(0) == "Synth Ctl");
    CHECK(c.load_template_file({'R', 'I', 'F', 'F'}, 0) == 0);
    std::vector<uint8_t> v2 = f;
    v2[11] = 2; // another version: not read, like the original
    CHECK(c.load_template_file(v2, 1) == 0);
    CHECK(c.load_template_file(f, 40) == 0); // no such template
}

// the template files of a folder like the original's list: *.vrt (any case), sub folders as "folder/name", sorted
TEST(remote_templates_file_list)
{
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / ("vtitests-vrt-" + std::to_string(::time(nullptr)));
    fs::create_directories(dir / "Live");
    for (auto p : {"b.vrt", "A.VRT", "Live/x.vrt", "notes.txt"})
        std::ofstream(dir / p) << "x";
    auto l = remote_template_files(dir.string());
    CHECK((l == std::vector<std::string>{"A.VRT", "Live/x.vrt", "b.vrt"}));
    CHECK(remote_template_files((dir / "missing").string()).empty());
    CHECK(remote_template_name("Live/x.vrt") == "Live/x" && remote_template_name("A.VRT") == "A");
    fs::remove_all(dir);
}

// the original's receiver checks only the header (F0 00 20 33 01 10 13), the template / knob and the length, not the
// last byte: two factory templates (Steinberg "Attack") end their knob records with 00 00 instead of F7 (E198)
TEST(remote_templates_record_without_f7)
{
    RemoteTemplates src;
    src.set_knob_name(2, 4, "Attack");
    std::vector<uint8_t> r = src.knob_record(2, 4);
    r[64] = 0x00;
    RemoteTemplates t;
    CHECK(t.take_record(r) && t.knob_name(2, 4) == "Attack");
}

// what goes to the Virus when a template is chosen or loaded (E199, E200): its name record, then knobs 0..31, in the
// original's order; the record an edit sends: the name record for the template name and the channel flag, the
// knob's record for its name, MIDI string and knob template
TEST(remote_templates_records_to_send)
{
    RemoteTemplates t;
    t.set_knob_name(4, 31, "Last");
    auto all = t.template_records(4);
    CHECK(all.size() == 33 && all[0] == t.name_record(4) && all[1] == t.knob_record(4, 0) && all[32] == t.knob_record(4, 31));
    CHECK(t.template_records(-1).empty() && t.template_records(32).empty());
    CHECK(remote_edit_record("Remote Templates/Template Name", t, 4, 7) == t.name_record(4));
    CHECK(remote_edit_record("Remote Templates/MIDI Channel Behaviour", t, -1, 7) == t.name_record(0));
    CHECK(remote_edit_record("Remote Templates/Knob Name", t, 4, 7) == t.knob_record(4, 7));
    CHECK(remote_edit_record("Remote Templates/MIDI String", t, 4, 31) == t.knob_record(4, 31));
    CHECK(remote_edit_record("Osc/Shape", t, 4, 7).empty());
}

// audit 2026-11-17 #3: a record from a file with a byte above 0x7F inside is not taken (it would go to the Virus
// unchanged, and a REMOTE template is a write that stays); the last byte stays free (factory templates end in 00 00)
TEST(remote_templates_record_with_a_status_byte_inside)
{
    RemoteTemplates src;
    src.set_knob_name(2, 4, "Attack");
    RemoteTemplates t;
    std::vector<uint8_t> r = src.knob_record(2, 4);
    r[20] = 0x90;
    CHECK(!t.take_record(r));
    r = src.knob_record(2, 4);
    r[r.size() - 1] = 0xF4; // the last byte: F7 or a data byte, nothing else
    CHECK(!t.take_record(r));
    std::vector<uint8_t> n = src.name_record(2);
    n[10] = 0xC3; // a UTF-8 byte in a name from another tool
    CHECK(!t.take_record(n));
    r = src.knob_record(2, 4);
    r[60] = 0xF7; // an early end, then 00 padding: a real factory record (Steinberg "Attack 01-12.vrt", knob 16)
    for (size_t k = 61; k < r.size(); k++)
        r[k] = 0;
    CHECK(t.take_record(r));
    r[62] = 0x05; // but data after the end is not padding
    CHECK(!t.take_record(r));
    CHECK(t.take_record(src.knob_record(2, 4)) && t.take_record(src.name_record(2)));
}
