// Section locking (E378, issue #21) on a small hand-written editor/device description (the file format of
// editordevice.xml / remotedevice.xml, no Access content): sections and their children, the menu presets from the
// "Locks/Page X Lockings" conditions, and the merge of the locked values into an incoming single dump
#include "check.h"
#include "model/remotedevice.h"
#include "model/sectionlock.h"
#include <algorithm>
#include <cstring>

using namespace vti;
using Bytes = std::vector<uint8_t>;

static const char *DEV = R"(<?xml version="1.0"?>
<Remotedevice device="Test" version="1">
 <ParameterSets>
  <ParameterSet name="Osc">
   <Parameter name="Shape" minValue="0" maxValue="127" defaultValue="64" part="all"/>
   <Parameter name="Mode" minValue="0" maxValue="3" defaultValue="0" part="all"/>
   <Parameter name="Density" minValue="0" maxValue="31" defaultValue="0" part="all"/>
  </ParameterSet>
  <ParameterSet name="Filt">
   <Parameter name="Cutoff" minValue="0" maxValue="127" defaultValue="127" part="all"/>
   <Parameter name="Reso" minValue="0" maxValue="127" defaultValue="0" part="all"/>
  </ParameterSet>
 </ParameterSets>
 <MIDIMessages>
  <MIDIMessage id="Dump" name="Dump">
   <byte index="0" content="byte" tag="240"/>
   <byte index="1" content="Osc/Shape"/>
   <byte index="2" content="Osc/Mode" mask="3" shift="0"/>
   <byte index="2" content="Osc/Density" mask="124" shift="2"/>
   <byte index="3" content="Filt/Cutoff"/>
   <byte index="4" content="Filt/Reso"/>
   <byte index="5" content="byte" tag="0"/>
   <byte index="6" content="byte" tag="0"/>
   <byte index="7" content="byte" tag="0"/>
   <byte index="8" content="byte" tag="0"/>
   <byte index="9" content="byte" tag="0"/>
   <byte index="10" content="byte" tag="0"/>
   <byte index="11" content="checksum" method="add7" startindex="1" stopindex="10"/>
   <byte index="12" content="byte" tag="247"/>
  </MIDIMessage>
 </MIDIMessages>
</Remotedevice>)";

static const char *ED = R"(<?xml version="1.0"?>
<Remotedevice device="TestEditor" version="1">
 <ParameterSets>
  <ParameterSet name="Locks">
   <Parameter name="Oscillator" minValue="0" maxValue="1" defaultValue="0" part="all" type="section">
    <Child>Osc/Shape</Child>
    <Child>Osc/Mode</Child>
   </Parameter>
   <Parameter name="Filter" minValue="0" maxValue="1" defaultValue="0" part="all" type="section">
    <Child>Filt/Cutoff</Child>
    <Child>Filt/Reso</Child>
    <Child>Filt/Missing</Child>
   </Parameter>
   <Parameter name="FX A" minValue="0" maxValue="1" defaultValue="0" part="all" type="section">
    <Child>Osc/Shape</Child>
   </Parameter>
   <Parameter name="FX B" minValue="0" maxValue="1" defaultValue="0" part="all" type="section"/>
  </ParameterSet>
  <ParameterSet name="LockView">
   <Parameter name="Part Lock Indicator" minValue="0" maxValue="1" defaultValue="0" part="all" type="reflector"
              condition="Locks/Part Lockings"/>
  </ParameterSet>
 </ParameterSets>
 <Conditionsets>
  <Condition name="Locks/Page OSC Lockings" operand="or" result="normal">
   <Test type="equals" minvalue="1" maxvalue="1" param="Locks/Oscillator"/>
  </Condition>
  <Condition name="Locks/Page OSC No Lockings" operand="or" result="invert">
   <Test type="equals" minvalue="1" maxvalue="1" param="Locks/Oscillator"/>
  </Condition>
  <Condition name="Locks/Page Filter Lockings" operand="or" result="normal">
   <Test type="equals" minvalue="1" maxvalue="1" param="Locks/Filter"/>
  </Condition>
  <Condition name="Locks/Page FX Lockings" operand="or" result="normal">
   <Test type="equals" minvalue="1" maxvalue="1" param="Locks/FX A"/>
  </Condition>
  <Condition name="Locks/Page FX 2 Lockings" operand="or" result="normal">
   <Test type="equals" minvalue="1" maxvalue="1" param="Locks/FX B"/>
  </Condition>
 </Conditionsets>
</Remotedevice>)";

static const RemoteDevice &load(RemoteDevice &d, bool &loaded, const char *xml)
{
    if (!loaded) {
        std::string err;
        Bytes x(xml, xml + strlen(xml));
        loaded = d.load(x, err);
        if (!loaded)
            std::fprintf(stderr, "  load: %s\n", err.c_str());
    }
    return d;
}
static const RemoteDevice &dev()
{
    static RemoteDevice d;
    static bool l = false;
    return load(d, l, DEV);
}
static const RemoteDevice &ed()
{
    static RemoteDevice d;
    static bool l = false;
    return load(d, l, ED);
}

// a dump of the test template with these values and valid checksums
static Bytes dump(int shape, int mode, int density, int cutoff, int reso)
{
    const MidiMessage *m = dev().message("Dump");
    Bytes d(13, 0);
    d[0] = 0xF0, d[12] = 0xF7;
    dev().update_dump(*m, d, {{"Osc/Shape", shape}, {"Osc/Mode", mode}, {"Osc/Density", density},
                              {"Filt/Cutoff", cutoff}, {"Filt/Reso", reso}});
    return d;
}

TEST(sectionlock_parse_sections)
{
    const Parameter *p = ed().find("Locks/Filter");
    CHECK(p && p->type == "section");
    CHECK(p && p->children == std::vector<std::string>({"Filt/Cutoff", "Filt/Reso", "Filt/Missing"}));
    CHECK(ed().find("LockView/Part Lock Indicator")->type == "reflector");
    CHECK(ed().find("LockView/Part Lock Indicator")->children.empty());
    CHECK(SectionLock::sections(ed()) ==
          std::vector<std::string>({"Locks/Oscillator", "Locks/Filter", "Locks/FX A", "Locks/FX B"}));
    CHECK(SectionLock::sections(dev()).empty()); // no section parameters
}

TEST(sectionlock_presets_from_page_conditions)
{
    // a preset = the sections the page's "Lockings" condition tests (the FX preset: both FX pages); never the
    // inverted "No Lockings" twin
    CHECK(SectionLock::preset_sections(ed(), SectionLock::LOCK_OSC) == std::vector<std::string>({"Locks/Oscillator"}));
    CHECK(SectionLock::preset_sections(ed(), SectionLock::LOCK_FILTER) == std::vector<std::string>({"Locks/Filter"}));
    CHECK(SectionLock::preset_sections(ed(), SectionLock::LOCK_FX) ==
          std::vector<std::string>({"Locks/FX A", "Locks/FX B"}));
    CHECK(SectionLock::preset_sections(ed(), SectionLock::LOCK_LFO).empty()); // no such condition
    CHECK(SectionLock::preset_sections(ed(), -1).empty());
    CHECK(SectionLock::preset_sections(ed(), SectionLock::PRESET_COUNT).empty());
    CHECK(std::string(SectionLock::preset_name(SectionLock::LOCK_OSC)) == "Lock OSC");
    CHECK(std::string(SectionLock::preset_name(SectionLock::LOCK_ARP)) == "Lock Arpeggiator");
    CHECK(std::string(SectionLock::preset_name(SectionLock::LOCK_FX)) == "Lock FX");
    CHECK(std::string(SectionLock::preset_name(99)).empty());
}

TEST(sectionlock_locked_children)
{
    auto locked = [](std::initializer_list<const char *> on) {
        std::vector<std::string> v(on.begin(), on.end());
        return [v](const std::string &s) { return std::find(v.begin(), v.end(), s) != v.end(); };
    };
    CHECK(SectionLock::locked_children(ed(), locked({})).empty());
    CHECK(SectionLock::locked_children(ed(), locked({"Locks/Filter"})) ==
          std::vector<std::string>({"Filt/Cutoff", "Filt/Reso", "Filt/Missing"}));
    // a parameter in two locked sections comes once
    CHECK(SectionLock::locked_children(ed(), locked({"Locks/Oscillator", "Locks/FX A"})) ==
          std::vector<std::string>({"Osc/Shape", "Osc/Mode"}));
    CHECK(SectionLock::locked_children(ed(), locked({"Locks/FX B"})).empty()); // a section without children
}

TEST(sectionlock_merge_keeps_locked_values)
{
    // the original (E378): the incoming patch with the locked parameters' current values, flags byte 10 = 0x20, one
    // dump; TI Revive also recomputes the checksums (the original leaves them stale)
    const MidiMessage *m = dev().message("Dump");
    const Bytes current = dump(10, 2, 5, 30, 40);
    Bytes in = dump(100, 1, 20, 15, 90);
    CHECK(SectionLock::merge(dev(), *m, current, in, {"Osc/Mode", "Filt/Cutoff", "Filt/Missing"}));
    std::map<std::string, int> v;
    std::string err;
    CHECK(dev().decode_dump(*m, in, v, err)); // checksum valid
    CHECK(v["Osc/Mode"] == 2);      // locked: the current value
    CHECK(v["Osc/Density"] == 20);  // shares Mode's byte, not locked: the incoming patch's
    CHECK(v["Osc/Shape"] == 100);
    CHECK(v["Filt/Cutoff"] == 30);  // locked
    CHECK(v["Filt/Reso"] == 90);
    CHECK(in[10] == 0x20);
}

TEST(sectionlock_merge_nothing_locked)
{
    const MidiMessage *m = dev().message("Dump");
    const Bytes current = dump(10, 2, 5, 30, 40), orig = dump(100, 1, 20, 15, 90);
    Bytes in = orig;
    CHECK(!SectionLock::merge(dev(), *m, current, in, {}));
    CHECK(in == orig); // unchanged, flags included
    CHECK(!SectionLock::merge(dev(), *m, current, in, {"Filt/Missing"})); // no field of the dump: nothing to keep
    CHECK(in == orig);
    Bytes shorter(current.begin(), current.end() - 1);
    CHECK(!SectionLock::merge(dev(), *m, shorter, in, {"Filt/Cutoff"})); // a current dump of the wrong size
    CHECK(in == orig);
}
