// The setup page of the plug-in (E406, E408): what a user of this DAW should check, with the real numbers of this setup.
#include "check.h"
#include "editor/setuptips.h"

#include <algorithm>
#include <string>

using namespace vti;

static bool has_key(const TipsPage &p, const char *key)
{
    return std::any_of(p.tips.begin(), p.tips.end(), [&](const Tip &t) { return t.key == key; });
}
static const Tip *tip(const TipsPage &p, const char *key)
{
    for (auto &t : p.tips)
        if (t.key == key)
            return &t;
    return nullptr;
}
static bool contains(const std::string &s, const char *what) { return s.find(what) != std::string::npos; }

static TipsFacts facts(TipsHost host, int block = 256)
{
    TipsFacts f;
    f.host = host;
    f.host_rate = 44100;
    f.virus_rate = 44100;
    f.largest_block = block;
    f.delay_ms = 90;
    return f;
}

TEST(setuptips_host_from_the_vst3_name)
{
    CHECK(tips_host("Ableton Live 12", "") == TipsHost::Live);
    CHECK(tips_host("Cubase Pro", "") == TipsHost::Cubase);
    CHECK(tips_host("Nuendo", "") == TipsHost::Cubase);
    CHECK(tips_host("REAPER", "") == TipsHost::Reaper);
    CHECK(tips_host("FL Studio", "") == TipsHost::FlStudio);
    CHECK(tips_host("Bitwig Studio", "") == TipsHost::Other);
}

TEST(setuptips_an_audio_unit_only_knows_the_wrapper_so_the_process_decides)
{
    CHECK(tips_host("VST3-AU Wrapper", "Logic Pro") == TipsHost::Logic);
    CHECK(tips_host("VST3-AU Wrapper", "Logic Pro X") == TipsHost::Logic);
    CHECK(tips_host("VST3-AU Wrapper", "GarageBand") == TipsHost::GarageBand);
    CHECK(tips_host("VST3-AU Wrapper", "Live") == TipsHost::Live);
    CHECK(tips_host("VST3-AU Wrapper", "something else") == TipsHost::Other);
}

TEST(setuptips_host_names_are_not_case_sensitive)
{
    CHECK(tips_host("ableton live", "") == TipsHost::Live);
    CHECK(tips_host("reaper", "") == TipsHost::Reaper);
}

TEST(setuptips_title_names_the_daw)
{
    CHECK(contains(setup_tips(facts(TipsHost::Logic)).title, "Logic Pro"));
    CHECK(contains(setup_tips(facts(TipsHost::Live)).title, "Ableton Live"));
    CHECK(contains(setup_tips(facts(TipsHost::Other)).title, "your DAW"));
}

TEST(setuptips_every_daw_gets_the_common_points)
{
    for (TipsHost h : {TipsHost::Logic, TipsHost::GarageBand, TipsHost::Live, TipsHost::Cubase, TipsHost::Reaper,
                       TipsHost::FlStudio, TipsHost::Other}) {
        TipsPage p = setup_tips(facts(h));
        CHECK(has_key(p, "delay"));
        CHECK(has_key(p, "compensation"));
        CHECK(has_key(p, "start"));
        CHECK(has_key(p, "buffer"));
    }
}

TEST(setuptips_logic_points)
{
    TipsPage p = setup_tips(facts(TipsHost::Logic));
    CHECK(contains(tip(p, "logic-range")->text, "Process Buffer Range"));
    CHECK(contains(tip(p, "logic-range")->text, "Small"));
    CHECK(contains(tip(p, "compensation")->text, "Plug-in Latency Compensation"));
    CHECK(has_key(p, "logic-low-latency"));
    CHECK(has_key(p, "logic-parts"));
    CHECK(!has_key(p, "live-parts"));
}

TEST(setuptips_live_points)
{
    TipsPage p = setup_tips(facts(TipsHost::Live));
    CHECK(contains(tip(p, "compensation")->text, "Delay Compensation"));
    CHECK(has_key(p, "live-parts"));
    CHECK(!has_key(p, "logic-range"));
}

TEST(setuptips_the_midi_input_is_only_named_where_it_exists)
{
    TipsFacts f = facts(TipsHost::Live);
    f.mac = false;
    CHECK(!has_key(setup_tips(f), "live-keys"));
    f.mac = true;
    CHECK(contains(tip(setup_tips(f), "live-keys")->text, "Virus TI Synth"));
}

TEST(setuptips_delay_shows_the_real_number)
{
    TipsFacts f = facts(TipsHost::Other);
    f.delay_ms = 87.4;
    CHECK(contains(tip(setup_tips(f), "delay")->text, "87 ms"));
}

TEST(setuptips_nothing_is_a_warning_on_a_good_setup)
{
    TipsPage p = setup_tips(facts(TipsHost::Logic, 512));
    for (auto &t : p.tips)
        CHECK(!t.warn);
}

TEST(setuptips_a_large_block_is_a_warning_with_its_cost)
{
    TipsFacts f = facts(TipsHost::Logic, 2048);
    f.host_rate = f.virus_rate = 48000;
    const Tip *t = tip(setup_tips(f), "large-block");
    CHECK(t != nullptr);
    CHECK(t->warn);
    CHECK(contains(t->text, "2048"));
    CHECK(contains(t->text, "37 ms")); // (2048 - 256) frames at 48 kHz, on the formula of latency_frames()
    CHECK(contains(t->text, "Process Buffer Range")); // in Logic that is where it comes from
}

TEST(setuptips_a_large_block_elsewhere_names_the_buffer)
{
    const Tip *t = tip(setup_tips(facts(TipsHost::Reaper, 1024)), "large-block");
    CHECK(t != nullptr);
    CHECK(contains(t->text, "buffer"));
    CHECK(!contains(t->text, "Process Buffer Range"));
}

TEST(setuptips_warnings_come_first)
{
    TipsPage p = setup_tips(facts(TipsHost::Logic, 2048));
    CHECK(p.tips[0].key == "large-block");
}

TEST(setuptips_a_rate_the_virus_cannot_run_is_a_warning)
{
    TipsFacts f = facts(TipsHost::Other);
    f.host_rate = 96000;
    f.virus_rate = 48000;
    const Tip *t = tip(setup_tips(f), "rate");
    CHECK(t != nullptr);
    CHECK(t->warn);
    CHECK(contains(t->text, "96000"));
    f.host_rate = 48000;
    CHECK(!has_key(setup_tips(f), "rate"));
}

TEST(setuptips_opens_by_itself_the_first_time_for_a_daw)
{
    TipsPage p = setup_tips(facts(TipsHost::Logic));
    CHECK(tips_open_at_start(p, {}));
}

TEST(setuptips_stays_closed_after_it_was_read)
{
    TipsPage p = setup_tips(facts(TipsHost::Logic));
    CHECK(!tips_open_at_start(p, tips_seen_after(p, {})));
}

TEST(setuptips_opens_again_for_a_new_warning)
{
    std::vector<std::string> seen = tips_seen_after(setup_tips(facts(TipsHost::Logic, 256)), {});
    TipsPage bad = setup_tips(facts(TipsHost::Logic, 2048));
    CHECK(tips_open_at_start(bad, seen));
    seen = tips_seen_after(bad, seen);
    CHECK(!tips_open_at_start(bad, seen)); // once: the ? menu has it from then on
}

TEST(setuptips_another_daw_is_a_first_time_of_its_own)
{
    std::vector<std::string> seen = tips_seen_after(setup_tips(facts(TipsHost::Logic)), {});
    CHECK(tips_open_at_start(setup_tips(facts(TipsHost::Live)), seen));
}

TEST(setuptips_seen_keeps_what_was_there)
{
    std::vector<std::string> seen = tips_seen_after(setup_tips(facts(TipsHost::Logic)), {});
    std::vector<std::string> more = tips_seen_after(setup_tips(facts(TipsHost::Live)), seen);
    CHECK(more.size() > seen.size());
    CHECK(!tips_open_at_start(setup_tips(facts(TipsHost::Logic)), more));
    CHECK(!tips_open_at_start(setup_tips(facts(TipsHost::Live)), more));
}

TEST(setuptips_seen_file_round_trip)
{
    const std::vector<std::string> seen = {"logic/intro", "logic/large-block"};
    CHECK(tips_seen_parse(tips_seen_text(seen)) == seen);
    CHECK(tips_seen_parse("").empty());
    CHECK(tips_seen_parse("a\r\n\r\nb\n") == (std::vector<std::string>{"a", "b"}));
}
