// project state container, editor-only records, meta parameter mapping
#include "check.h"
#include "model/state.h"
#include <cstring>

using namespace vti;
using Bytes = std::vector<uint8_t>;

TEST(container_round_trip)
{
    std::vector<Bytes> recs = {{}, {0xB0, 7, 100}, Bytes(524, 0x55), {0xF0, 0x7D, 0xF7}};
    auto d = build_container(recs);
    CHECK(d.size() == 8 + 4 * recs.size() + 3 + 524 + 3);
    CHECK(d[0] == 'M' && d[3] == 'I');
    CHECK(parse_container(d) == recs);
}

TEST(container_damaged)
{
    auto d = build_container({{1, 2, 3}, {4, 5, 6, 7}});
    CHECK(parse_container(Bytes(d.begin(), d.end() - 2)).size() == 1); // a truncated last record is dropped
    Bytes bad = d;
    bad[0] = 'X';
    CHECK(parse_container(bad).empty());
    CHECK(parse_container({}).empty());
    Bytes huge = d;
    huge[8] = 0x7F; // first record claims ~2 GB
    CHECK(parse_container(huge).empty());
}

TEST(part_dump_detection)
{
    Bytes m(524, 0);
    m[0] = 0xF0, m[6] = 0x10, m[7] = 0x00, m[8] = 5;
    CHECK(is_part_dump(m, 5));
    CHECK(!is_part_dump(m, 4));
    m.resize(267);
    CHECK(!is_part_dump(m, 5));
}

TEST(editor_record_round_trip)
{
    auto r = editor_record("VirusControl Links/LFO 1 Link", 3, 1);
    CHECK(r.front() == 0xF0 && r.back() == 0xF7 && r[1] == 0x7D);
    std::string path;
    int part = -1, value = -1;
    CHECK(parse_editor_record(r, path, part, value));
    CHECK(path == "VirusControl Links/LFO 1 Link" && part == 3 && value == 1);
    r = editor_record("A/B", 15, 200); // 7-bit on the wire
    CHECK(parse_editor_record(r, path, part, value) && value == (200 & 0x7F));
    CHECK(!parse_editor_record({0xF0, 0x00, 0x20, 0x33, 1, 2, 3, 4, 5, 6, 0xF7}, path, part, value)); // Access SysEx
    CHECK(!parse_editor_record({0xF0, 0x7D, 'V', 'T', 'N', 1, 0, 'A', 'B'}, path, part, value));       // cut off
}

TEST(meta_mapping)
{
    MetaClient lin; // client = 2 * meta + 1
    lin.a = 2, lin.b = 1;
    CHECK_EQ(meta_to_client(lin, 10), 21);
    int mv = -1;
    CHECK(client_to_meta(lin, 21, mv) && mv == 10);
    MetaClient mapped;
    mapped.maps = {{0, 5}, {64, 6}, {127, 7}};
    CHECK_EQ(meta_to_client(mapped, 6), 64);
    CHECK(client_to_meta(mapped, 127, mv) && mv == 7);
    CHECK(!client_to_meta(mapped, 3, mv)); // a client value without map entry does not move the meta
    MetaClient flat;
    flat.a = 0;
    CHECK(!client_to_meta(flat, 5, mv)); // not invertible
}

TEST(state_system_clock_globals_never_kept)
{
    // E445: a report of System Clock/Frequency saved with a project went back to the Virus at every load, its USB chip
    // switched clock behind Core Audio's back. The original sends none of the three (E327)
    const auto global = [](uint8_t part, uint8_t id, uint8_t v) {
        return Bytes{0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x73, part, id, v, 0xF7};
    };
    CHECK(is_system_clock_global(global(0x00, 0x0F, 1))); // SA Freq
    CHECK(is_system_clock_global(global(0x00, 0x10, 0))); // Frequency, what the project of E445 held
    CHECK(is_system_clock_global(global(0x00, 0x11, 1))); // Mode
    CHECK(is_system_clock_global(global(0x40, 0x10, 1))); // the same, as the Virus reports it with another part byte
    // the neighbours and look-alikes stay
    CHECK(!is_system_clock_global(global(0x00, 0x0E, 0)));
    CHECK(!is_system_clock_global(global(0x00, 0x12, 0)));
    CHECK(!is_system_clock_global(global(0x00, 0x40, 0))); // Local On
    CHECK(!is_system_clock_global(global(0x00, 0x09, 1))); // USB IO configuration
    Bytes multi = global(0x00, 0x10, 0);
    multi[6] = 0x72; // page 0x72: a multi parameter with the same number
    CHECK(!is_system_clock_global(multi));
    const Bytes longer = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x73, 0x00, 0x10, 0x00, 0x00, 0xF7};
    CHECK(!is_system_clock_global(longer));
    Bytes other_maker = global(0x00, 0x10, 0);
    other_maker[1] = 0x7D;
    CHECK(!is_system_clock_global(other_maker));
    CHECK(!is_system_clock_global({}));
    CHECK(!is_system_clock_global({0xB0, 0x10, 0}));

    // an old project: those records go, everything else stays in its order
    const Bytes keep1 = global(0x00, 0x40, 0), keep2 = {0xB0, 7, 100}, keep3 = Bytes(524, 0x55);
    std::vector<Bytes> recs = {keep1, global(0x00, 0x10, 0), keep2, global(0x00, 0x0F, 1), {}, keep3};
    CHECK(drop_system_clock_globals(recs) == 2);
    CHECK((recs == std::vector<Bytes>{keep1, keep2, {}, keep3}));
    CHECK(drop_system_clock_globals(recs) == 0);
    // through the container, as a saved project comes back
    auto saved = parse_container(build_container({keep1, global(0x00, 0x10, 0)}));
    CHECK(drop_system_clock_globals(saved) == 1);
    CHECK((saved == std::vector<Bytes>{keep1}));
}

TEST(state_part_dumps_upgraded)
{
    // E174: the Virus converts a version-9 part dump itself and resets the newer fields (Input Mode, unison,
    // envelopes 3/4 ...) - so the state keeps part dumps in the current format; the editor's values then reach it
    std::vector<uint8_t> d(524, 0);
    const uint8_t head[] = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x10, 0x00, 0x03, 9};
    memcpy(d.data(), head, sizeof head);
    d[523] = 0xF7;
    std::vector<uint8_t> stored = d;
    stored[7] = 0x01; // a stored bank patch (not an edit buffer): the firmware converts those itself
    std::vector<uint8_t> current = d;
    current[9] = 12;
    std::vector<std::vector<uint8_t>> recs = {d, stored, current, {0xB0, 7, 100}};
    CHECK(upgrade_state_dumps(recs) == 1);
    CHECK(recs[0][9] == 12);                       // the edit-buffer dump of part 4: upgraded
    CHECK(recs[1] == stored);                       // stored patch untouched
    CHECK(recs[2] == current);                      // already current
    CHECK(recs[3] == (std::vector<uint8_t>{0xB0, 7, 100}));
    CHECK(upgrade_state_dumps(recs) == 0);         // idempotent
}

TEST(state_missing_part_dumps_come_from_the_default)
{
    // audit #22: a truncated project (parts 3..16 missing) dropped every edit and patch load of those parts
    auto part = [](int p, uint8_t mark) {
        Bytes d(524, mark);
        const uint8_t head[9] = {0xF0, 0x00, 0x20, 0x33, 0x01, 0x10, 0x10, 0x00, (uint8_t)p};
        std::memcpy(d.data(), head, 9);
        d[523] = 0xF7;
        return d;
    };
    std::vector<Bytes> defaults = {{0xF0, 0x11, 0xF7}};
    for (int p = 0; p < 16; p++)
        defaults.push_back(part(p, 0x01));
    std::vector<Bytes> recs = {part(0, 0x22), part(1, 0x22), {0xF0, 0x42, 0xF7}};
    CHECK_EQ(add_missing_part_dumps(recs, defaults), 14);
    CHECK_EQ(recs.size(), 17u);
    CHECK(recs[0][100] == 0x22 && recs[1][100] == 0x22); // the project's own parts stay
    for (int p = 0; p < 16; p++) {
        int n = 0;
        for (auto &r : recs)
            n += is_part_dump(r, p);
        CHECK_EQ(n, 1);
    }
    CHECK_EQ(add_missing_part_dumps(recs, defaults), 0); // complete: nothing added
}

TEST(state_values_the_state_lacks_are_kept)
{
    // audit #24: a patch load rebuilt every value from the state; the System Clock the Virus reported (never in the
    // state, E445) went back to its default on screen
    std::map<int, std::map<std::string, int>> prev = {{-1, {{"Global/Clock", 2}, {"Global/Tempo", 90}}},
                                                       {0, {{"Osc/Shape", 10}}}};
    std::map<int, std::map<std::string, int>> next = {{-1, {{"Global/Tempo", 120}}}, {0, {{"Osc/Shape", 64}}}};
    keep_unset_values(next, prev);
    CHECK_EQ(next[-1]["Global/Clock"], 2);  // not in the state: kept
    CHECK_EQ(next[-1]["Global/Tempo"], 120); // in the state: the state wins
    CHECK_EQ(next[0]["Osc/Shape"], 64);
}
