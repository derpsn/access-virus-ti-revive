// The patch randomizer (model/randomizer.h, TI Revive's own addition, asked for in #19): the unlocked sections'
// parameters of a part move by up to `amount` of their range, list parameters pick another entry with that
// chance; locked sections, parameters outside the single dump and the ones that would silence the patch or start
// it playing stay. On a small hand-written description (the file format of editordevice.xml, no Access content).
// Hardware-free.
#include "check.h"
#include "model/randomizer.h"
#include <cstring>
#include <random>
#include <set>

using namespace vti;

static const char *DEV = R"(<?xml version="1.0"?>
<Remotedevice device="Test" version="1">
 <Valuelists>
  <Valuelist name="Val 0-3"><Entry index="0">0</Entry><Entry index="1">+1</Entry><Entry index="2">-2</Entry><Entry index="3">3 %</Entry></Valuelist>
  <Valuelist name="Shapes"><Entry index="0">Saw</Entry><Entry index="1">Pulse</Entry><Entry index="2">Sine</Entry></Valuelist>
 </Valuelists>
 <ParameterSets>
  <ParameterSet name="Osc">
   <Parameter name="Shape" minValue="0" maxValue="2" defaultValue="0" part="all" valuelist="Shapes"/>
   <Parameter name="Detune" minValue="0" maxValue="127" defaultValue="64" part="all"/>
  </ParameterSet>
  <ParameterSet name="Filt">
   <Parameter name="Cutoff" minValue="0" maxValue="127" defaultValue="127" part="all"/>
   <Parameter name="Slope" minValue="0" maxValue="3" defaultValue="0" part="all" valuelist="Val 0-3"/>
  </ParameterSet>
  <ParameterSet name="Patch Common">
   <Parameter name="Volume" minValue="0" maxValue="127" defaultValue="100" part="all"/>
  </ParameterSet>
  <ParameterSet name="Input">
   <Parameter name="Mode" minValue="0" maxValue="3" defaultValue="0" part="all"/>
  </ParameterSet>
 </ParameterSets>
</Remotedevice>)";

static const char *ED = R"(<?xml version="1.0"?>
<Remotedevice device="Editor" version="1">
 <ParameterSets>
  <ParameterSet name="Locks">
   <Parameter name="Oscillator 1" type="section" part="all"><Child>Osc/Shape</Child><Child>Osc/Detune</Child></Parameter>
   <Parameter name="Filter 1" type="section" part="all"><Child>Filt/Cutoff</Child><Child>Filt/Slope</Child><Child>Osc/Detune</Child><Child>Patch Common/Volume</Child><Child>LFO/Clock</Child></Parameter>
   <Parameter name="FX Input" type="section" part="all"><Child>Input/Mode</Child></Parameter>
  </ParameterSet>
 </ParameterSets>
</Remotedevice>)";

struct Fixture {
    RemoteDevice dev, ed;
    Fixture()
    {
        std::string err;
        CHECK(dev.load(std::vector<uint8_t>(DEV, DEV + strlen(DEV)), err));
        CHECK(ed.load(std::vector<uint8_t>(ED, ED + strlen(ED)), err));
    }
};

static bool in_dump(const std::string &p) { return p != "LFO/Clock"; } // an editor meta, not in the dump

static std::vector<std::string> paths(const std::vector<Randomizer::Target> &t)
{
    std::vector<std::string> out;
    for (auto &x : t)
        out.push_back(x.path);
    return out;
}

TEST(randomizer_targets_unlocked_sections_once)
{
    Fixture f;
    auto t = Randomizer::targets(f.ed, f.dev, [](const std::string &) { return false; }, in_dump);
    // Detune once (two sections list it); no Patch Common/Volume, no LFO/Clock (not in the dump), no FX Input
    CHECK((paths(t) == std::vector<std::string>{"Osc/Shape", "Osc/Detune", "Filt/Cutoff", "Filt/Slope"}));
    CHECK(t[0].choices == (std::vector<int>{0, 1, 2}) && t[1].choices.empty());
    // a list of numbers ("Val 0-127" of Cutoff, Resonance, LFO rates) is a number range, not a menu: with the
    // menu rule half of them kept their value at 50 % (seen on the TI2: Cutoff unchanged in 3 of 6 runs)
    CHECK(t[3].choices.empty());
    CHECK(t[1].min == 0 && t[1].max == 127);
}

TEST(randomizer_locked_section_stays)
{
    Fixture f;
    auto t = Randomizer::targets(f.ed, f.dev, [](const std::string &s) { return s == "Locks/Oscillator 1"; }, in_dump);
    // Detune is in the locked Oscillator 1 too: a lock keeps it, like the merge (E378)
    CHECK((paths(t) == std::vector<std::string>{"Filt/Cutoff", "Filt/Slope"}));
}

TEST(randomizer_amount_bounds_numeric_moves)
{
    Fixture f;
    auto t = Randomizer::targets(f.ed, f.dev, [](const std::string &) { return false; }, in_dump);
    std::mt19937 rng(7);
    std::uniform_real_distribution<double> uni(0.0, 1.0);
    auto rnd = [&] { return uni(rng); };
    for (int i = 0; i < 200; i++) {
        auto v = Randomizer::values(t, {{"Osc/Shape", 1}, {"Osc/Detune", 64}, {"Filt/Cutoff", 120}}, 0.1, rnd);
        CHECK(v.count("Osc/Detune") && v["Osc/Detune"] >= 64 - 13 && v["Osc/Detune"] <= 64 + 13);
        CHECK(v["Filt/Cutoff"] >= 120 - 13 && v["Filt/Cutoff"] <= 127); // clamped to the range
        CHECK(v["Osc/Shape"] >= 0 && v["Osc/Shape"] <= 2);
    }
}

TEST(randomizer_full_amount_reaches_the_range)
{
    Fixture f;
    auto t = Randomizer::targets(f.ed, f.dev, [](const std::string &) { return false; }, in_dump);
    std::mt19937 rng(3);
    std::uniform_real_distribution<double> uni(0.0, 1.0);
    auto rnd = [&] { return uni(rng); };
    int lo = 127, hi = 0;
    std::set<int> shapes;
    for (int i = 0; i < 500; i++) {
        auto v = Randomizer::values(t, {{"Osc/Shape", 0}, {"Osc/Detune", 64}, {"Filt/Cutoff", 64}}, 1.0, rnd);
        lo = std::min(lo, v["Filt/Cutoff"]), hi = std::max(hi, v["Filt/Cutoff"]);
        shapes.insert(v["Osc/Shape"]);
    }
    CHECK(lo < 10 && hi > 117);
    CHECK(shapes.size() == 3); // every list entry comes up
}

TEST(randomizer_zero_amount_and_unknown_values)
{
    Fixture f;
    auto t = Randomizer::targets(f.ed, f.dev, [](const std::string &) { return false; }, in_dump);
    auto rnd = [] { return 0.99; };
    auto v = Randomizer::values(t, {{"Osc/Shape", 1}, {"Osc/Detune", 64}}, 0.0, rnd);
    CHECK(v["Osc/Shape"] == 1 && v["Osc/Detune"] == 64);
    CHECK(!v.count("Filt/Cutoff")); // no current value known: left out
}

TEST(randomizer_amount_names)
{
    CHECK(Randomizer::kAmounts[0].percent == 10 && std::string(Randomizer::kAmounts[0].name) == "Subtle");
    CHECK(sizeof Randomizer::kAmounts / sizeof Randomizer::kAmounts[0] == 3);
}

TEST(randomizer_number_texts)
{
    CHECK(Randomizer::number_text("12") && Randomizer::number_text(" -64") && Randomizer::number_text("+63"));
    CHECK(Randomizer::number_text("50 %") && Randomizer::number_text("1.5"));
    CHECK(!Randomizer::number_text("") && !Randomizer::number_text("+") && !Randomizer::number_text("Saw"));
    CHECK(!Randomizer::number_text("1/16") && !Randomizer::number_text("12 dB"));
}

// a section child the device does not describe (or one without a range) is left out, not guessed
TEST(randomizer_unknown_children_left_out)
{
    Fixture f;
    auto t = Randomizer::targets(f.ed, f.dev, [](const std::string &) { return false; },
                                 [](const std::string &) { return true; }); // LFO/Clock "in the dump", but unknown
    for (auto &x : t)
        CHECK(x.path != "LFO/Clock");
}
