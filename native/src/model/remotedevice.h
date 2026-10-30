// remotedevice.h - the Virus device model, read from the user's Access "remotedevice.xml" (see docs/formats.md).
//
//   parameters (group/name, range, default, value list, transport message, visibility condition),
//   value lists (texts per value, plain or mapped), conditions (tests on other parameters),
//   MIDI message templates (CC, poly pressure, parameter SysEx per bank, single/multi dumps with byte layouts),
//   soft-knob mapping and initialisation lists.
#pragma once
#include <cstdint>
#include <map>
#include <unordered_map>
#include <optional>
#include <string>
#include <vector>

namespace vti {

// a meta parameter's client (editordevice.xml <Client>): client = a * meta + b, or through value maps
struct MetaClient {
    std::string path;
    double a = 1, b = 0;
    std::vector<std::pair<int, int>> maps; // (client value, meta value)
    bool notrigger = false; // the meta value is not passed to this client
    bool nosource = false;  // changes of this client do not set the meta value
};

struct Parameter {
    std::string group, name, path; // path = "group/name", the key used everywhere in the XML
    int min = 0, max = 127, def = 0;
    std::string valuelist, part, condition, message, shortname, displayname;
    int controller = -1; // controllerindex
    bool automatable = false, performance = false;
    // meta parameters (editordevice.xml meta="true"): link other parameters (filter link, FX switches, LFO clocks)
    bool meta = false, retrigger = false, catch_always = true;
    std::vector<MetaClient> clients;
    // editordevice.xml type="section" (section locks, E378): the parameters a lock keeps (<Child>, in file order);
    // type="reflector": the value is its condition's result
    std::string type;
    std::vector<std::string> children;
};

struct Valuelist {
    std::string name;
    bool mapped = false;
    struct Entry {
        int index = 0;  // plain lists: the parameter value; mapped lists: the menu position
        int value = -1; // mapped lists: the parameter value (see value_of)
        std::string text;
    };
    std::vector<Entry> entries;
    // the parameter value an entry stands for: mapped lists carry it in `value` (`index` is then the menu position,
    // e.g. arp clock 1/16 = value 4 at position 8; categories Lead = value 1 at position 12), else `index`
    int value_of(const Entry &e) const { return mapped && e.value >= 0 ? e.value : e.index; }
    const Entry *find(int v) const; // the entry of parameter value v (nullptr if none)
    std::string text(int v) const;
};

struct Condition {
    std::string name;
    bool any = true;     // operand "or" (true) / "and"
    bool invert = false; // result "invert"
    struct Test {
        enum Kind { Equals, Inside, Outside } kind = Equals; // "equals"/"equal": == min, "inside"/"outside" min..max
        int min = 0, max = 0;
        std::string param;
        bool holds(int v) const
        {
            return kind == Equals ? v == min : kind == Inside ? (v >= min && v <= max) : (v < min || v > max);
        }
    };
    std::vector<Test> tests;
};

struct MessageByte {
    int index = 0;
    std::string content; // sysexstart, byte, deviceid, part, controllerid, value, sysexend, checksum or a parameter path
    int tag = -1;        // fixed byte value (decimal in the XML)
    int mask = -1, shift = 0, part = -1;
    std::string method;  // checksum method
    int start = -1, stop = -1;
    // controllerid byte: (map index, parameter path); several parameters can share an index (e.g. CC 40 = Filter 1 Cutoff
    // or Vocoder Carrier Center Frequency, depending on the mode)
    std::vector<std::pair<int, std::string>> controllers;
};

struct MidiMessage {
    std::string id, name;
    std::vector<MessageByte> bytes;
    // built at load: field key -> its bytes, and the checksum bytes (update_dump of one value without walking all
    // ~520 bytes and building a key string for each - ~8 us per automation change before)
    std::unordered_map<std::string, std::vector<size_t>> field_bytes;
    std::vector<size_t> checksum_bytes;
};

// automation.xml: the original plug-in's VST3 parameter ids. id = ParamID, title as the original shows it
// ("[<part>] <shortname>"), unit "Part NN/".
struct AutomationEntry {
    int id = 0, part = 1;
    std::string path;
};
bool load_automation(const std::vector<uint8_t> &xml, std::vector<AutomationEntry> &out, std::string &error);
// Short title as the original shows it: the parameter's shortname; "Softknobs/Softknob N" (virtual) -> "Softknob N".
std::string automation_title(const class RemoteDevice &dev, const AutomationEntry &e);

class RemoteDevice {
public:
    bool load(const std::vector<uint8_t> &xml, std::string &error);

    std::string device, version;
    std::vector<Parameter> parameters;
    std::map<std::string, Valuelist> valuelists;
    std::multimap<std::string, Condition> conditions; // several entries per name = alternatives
    std::vector<MidiMessage> messages;
    std::vector<std::string> softknobs;                  // index -> parameter path
    // the parameter a soft knob with destination `dest` controls (SoftknobMapping); "" for 0 ("Do Nothing"), out of
    // range or a path that is no parameter of this device
    std::string softknob_parameter(int dest) const
    {
        if (dest <= 0 || dest >= (int)softknobs.size() || !find(softknobs[(size_t)dest]))
            return {};
        return softknobs[(size_t)dest];
    }
    std::vector<std::pair<std::string, std::string>> global_init;
    struct ParamInit {
        std::string param, value;
        int part = 0;
    };
    std::vector<ParamInit> param_init;

    const Parameter *find(const std::string &path) const;
    const MidiMessage *message(const std::string &id) const;
    // The message that changes `path` to `value` for `part` (0-based; 16 = global) and device id - CC, poly pressure
    // or a parameter SysEx, built from the parameter's transport template. Empty if the parameter has none.
    std::vector<uint8_t> encode(const std::string &path, int value, int part, int device_id = 0x10) const;
    // Same without allocating (realtime use): writes up to `cap` bytes, returns the length (0 = none).
    size_t encode_into(const std::string &path, int value, int part, uint8_t *out, size_t cap, int device_id = 0x10) const;
    // Inverse of encode: the parameter changes in one message from the Virus (CC = page A, poly pressure = page B,
    // parameter SysEx pages 6E..74). Several parameters can share a controller (mode-dependent meaning); all are
    // returned. part = MIDI channel / SysEx part byte (0-based; 16 = global page / part "none").
    struct Change {
        std::string path;
        int part = 0, value = 0;
    };
    std::vector<Change> decode(const std::vector<uint8_t> &msg) const;
    // The value a control shows for a stored or reported `v`: clamped to the parameter's range like the original
    // (Vocoder/Bands 127 of 0..31 in old patches), except a value of a mapped list, which is its own entry
    // (mod matrix destinations up to 127 with maxValue 121, E309). Unknown paths: `v` unchanged.
    int shown(const std::string &path, int v) const;
    // Value of `path` for a valuelist text ("MIDI REMOTE", "Off", "01"); -1 if neither an entry nor a number.
    int value_of(const std::string &path, const std::string &text) const;
    // The messages the original sends at start-up before the project state: GlobalInit, "Configuring Virus TI" on the
    // display, `multi` (the plug-in's fixed multi dump, sent unchanged; none if empty), then ParamInit (E321).
    std::vector<std::vector<uint8_t>> init_messages(int device_id = 0x10, const std::vector<uint8_t> &multi = {}) const;

    // Dumps (VirusTI_Singledump, VirusTI_MultiDump, ...): the template's byte index is the offset in the SysEx message.
    // Parameter values of a received dump; key = path, or "path@part" for bytes with a part attribute. Several
    // parameters can share a byte (mode-dependent meaning) - all of them get the value. Fails on a wrong length or
    // checksum.
    bool decode_dump(const MidiMessage &m, const std::vector<uint8_t> &msg, std::map<std::string, int> &values,
                     std::string &error) const;
    // Writes `values` (same keys) into an existing dump and recomputes the checksums. Bytes the template does not
    // describe keep their content, so start from a dump the Virus sent.
    void update_dump(const MidiMessage &m, std::vector<uint8_t> &msg, const std::map<std::string, int> &values) const;
    static size_t dump_size(const MidiMessage &m);
    // Brings a 524-byte single dump of an older format version (byte 9) up to the plug-in's current version, as the
    // original does before sending a stored/init patch (decompile 0x1800a0c00, docs/protocol.md). Versions below 8
    // need the original's default page B (`page_b`, 256 bytes from the user's plug-in binary); without it they are
    // left unchanged. Recomputes both checksums. Returns true if the dump was changed.
    static bool upgrade_single(std::vector<uint8_t> &dump, const std::vector<uint8_t> &page_b = {});
    // The 14-bit value the Virus reports per program in a bank info reply (39 -> 14): sum of a 524-byte single dump's
    // bytes 13..521 without 265, mod 16384. Equal value = the cached patch is current. -1 for a wrong size.
    static int single_fingerprint(const std::vector<uint8_t> &msg);

private:
    std::map<std::string, size_t> index_;
    std::map<std::pair<std::string, int>, std::vector<std::string>> by_ctl_; // (message id, controller) -> paths
};

// The patch's origin in single-dump bytes 10..12 (Patchmanagement/Flags, Original Bank, Original Patch, E156), which
// the Virus shows as the part's location: set like the original before a patch load (0x180083ae0, E369) - from a bank
// of the Virus (our bank 0.. = RAM A.., ROM A from 4) 0, bank + 1, index ("RAM-B 4"); from anywhere else the flag 0x20
// ("eLIB"). The checksums are the caller's (load_dump computes them after).
inline void set_patch_origin(std::vector<uint8_t> &dump, int bank, int program, bool from_virus)
{
    if (dump.size() != 524)
        return;
    if (!from_virus) {
        dump[10] = 0x20;
        return;
    }
    dump[10] = 0;
    dump[11] = (uint8_t)((bank + 1) & 0x7F);
    dump[12] = (uint8_t)(program & 0x7F);
}

} // namespace vti
