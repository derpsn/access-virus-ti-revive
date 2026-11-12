"""Tests for scripts/risk.py (risk class per changed file -> the round's core checklist items) and the core-item
filter of scripts/checklist.py. Run: python3 -I -m unittest discover -s scripts/tests"""
import json
import os
import subprocess
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import checklist  # noqa: E402
import risk  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
CHECKLISTS = os.path.join(HERE, "..", "checklists")

MAP = {
    "platforms": {"linux": "linux", "windows": "vm", "mac": "mac"},
    "core_keys": ["cold", "reopen", "export", "power", "sleep"],
    "automated": {"linux": ["cold", "reopen", "export"]},
    "rules": [
        {"paths": ["*.md", "site/*"], "class": "A"},
        {"paths": ["native/src/device/*"], "class": "B", "platforms": ["linux", "windows", "mac"],
         "core": ["cold", "export"]},
        {"paths": ["native/au/*"], "class": "C", "platforms": ["mac"], "core": ["cold", "reopen", "export"]},
    ],
}


class Classify(unittest.TestCase):
    def test_first_matching_rule_wins(self):
        self.assertEqual(risk.classify("README.md", MAP)["class"], "A")
        self.assertEqual(risk.classify("native/src/device/dumppacer.h", MAP)["class"], "B")

    def test_glob_star_crosses_directories(self):
        # fnmatch's * matches "/" too: "site/*" covers files in subfolders
        self.assertEqual(risk.classify("site/src/index.html", MAP)["class"], "A")

    def test_unknown_file_is_class_c_everywhere(self):
        r = risk.classify("somewhere/new.c", MAP)
        self.assertEqual(r["class"], "C")
        self.assertEqual(sorted(r["platforms"]), ["linux", "mac", "windows"])
        self.assertEqual(r["core"], MAP["core_keys"])


class Select(unittest.TestCase):
    def test_docs_only_needs_no_manual_items(self):
        sel = risk.select(["README.md", "findings/HANDOFF.md"], MAP)
        self.assertEqual(sel["class"], "A")
        self.assertEqual(sel["platforms"], {})

    def test_class_b_on_all_platforms_with_linux_automated(self):
        sel = risk.select(["native/src/device/dumppacer.h"], MAP)
        self.assertEqual(sel["class"], "B")
        self.assertEqual(sel["platforms"]["mac"]["manual"], ["cold", "export"])
        self.assertEqual(sel["platforms"]["windows"]["manual"], ["cold", "export"])
        self.assertEqual(sel["platforms"]["linux"]["manual"], [])
        self.assertEqual(sel["platforms"]["linux"]["automated"], ["cold", "export"])

    def test_union_in_core_order_and_highest_class(self):
        sel = risk.select(["native/src/device/x.h", "native/au/tirevive_au.mm"], MAP)
        self.assertEqual(sel["class"], "C")
        self.assertEqual(sel["platforms"]["mac"]["manual"], ["cold", "reopen", "export"])
        self.assertEqual(sel["platforms"]["mac"]["class"], "C")
        self.assertEqual(sel["platforms"]["linux"]["class"], "B")

    def test_files_listed_per_platform(self):
        sel = risk.select(["native/src/device/x.h", "native/au/tirevive_au.mm"], MAP)
        self.assertEqual(sel["platforms"]["mac"]["files"], ["native/au/tirevive_au.mm", "native/src/device/x.h"])


class Rotation(unittest.TestCase):
    """core items that never found anything (findings/testing.md) run by hand only every n-th round for class B"""
    MAP_R = dict(MAP, rotation={"keys": ["reopen"], "every": 3})
    PLUGIN = dict(paths=["native/plugin/*"], **{"class": "B", "platforms": ["mac"], "core": ["cold", "reopen"]})

    def rmap(self):
        return dict(self.MAP_R, rules=self.MAP_R["rules"] + [self.PLUGIN])

    def test_class_b_drops_a_rotated_key_when_not_due(self):
        sel = risk.select(["native/plugin/x.cpp"], self.rmap(), round_no=28)
        self.assertEqual(sel["platforms"]["mac"]["manual"], ["cold"])
        self.assertEqual(sel["platforms"]["mac"]["rotated"], ["reopen"])

    def test_class_b_keeps_it_when_due(self):
        sel = risk.select(["native/plugin/x.cpp"], self.rmap(), round_no=27)
        self.assertEqual(sel["platforms"]["mac"]["manual"], ["cold", "reopen"])

    def test_class_c_always_keeps_it(self):
        sel = risk.select(["native/au/x.mm"], self.rmap(), round_no=28)
        self.assertEqual(sel["platforms"]["mac"]["manual"], ["cold", "reopen", "export"])

    def test_without_a_round_number_nothing_rotates(self):
        sel = risk.select(["native/plugin/x.cpp"], self.rmap())
        self.assertEqual(sel["platforms"]["mac"]["manual"], ["cold", "reopen"])

    def test_round_number_from_version(self):
        self.assertEqual(risk.round_no("0.8.28"), 28)
        self.assertEqual(risk.round_no("0.8.27-2"), 27)
        self.assertIsNone(risk.round_no("X"))


class Spec(unittest.TestCase):
    def test_spec_names_core_item_ids_of_the_manual_keys(self):
        core = {"sections": [
            {"id": "linux", "items": [{"id": "LC1", "key": "cold"}, {"id": "LC3", "key": "export"}]},
            {"id": "vm", "items": [{"id": "WC1", "key": "cold"}, {"id": "WC3", "key": "export"}]},
            {"id": "mac", "items": [{"id": "MC1", "key": "cold"}, {"id": "MC3", "key": "export"}]}]}
        sel = risk.select(["native/src/device/x.h"], MAP)
        spec = risk.spec(sel, MAP, core, "0.8.28")
        self.assertEqual(spec["round"], "0.8.28")
        self.assertEqual(spec["core"], "core.json")
        self.assertEqual(spec["core_items"], ["WC1", "WC3", "MC1", "MC3"])
        self.assertEqual([s["id"] for s in spec["sections"]], ["vm", "mac"])

    def test_spec_titles_and_automated_note(self):
        core = {"sections": [{"id": "vm", "items": [{"id": "WC1", "key": "cold"}]}]}
        spec = risk.spec(risk.select(["native/src/device/x.h"], MAP), MAP, core, "0.8.28")
        self.assertEqual(spec["sections"][0]["title"], "Windows 11 VM")
        self.assertIn("scripts/hil.sh", spec["lead"])
        self.assertIn("cold, export", spec["lead"])

    def test_spec_for_docs_only_has_no_sections(self):
        spec = risk.spec(risk.select(["README.md"], MAP), MAP, {"sections": []}, "0.8.28")
        self.assertEqual(spec["sections"], [])
        self.assertEqual(spec["core_items"], [])


class Mutation(unittest.TestCase):
    def test_targets_are_changed_code_vtitests_builds(self):
        files = ["native/src/device/sendguard.h", "native/src/model/state.cpp", "src/broker/vtireaderr.h",
                 "src/broker/vticore.c", "native/tests/test_state.cpp", "README.md", "native/plugin/source/x.cpp"]
        self.assertEqual(risk.mutation_targets(files),
                         ["native/src/device/sendguard.h", "native/src/model/state.cpp", "src/broker/vtireaderr.h"])

    def test_report_names_the_mutation_command(self):
        files = ["native/src/device/sendguard.h"]
        out = risk.report(risk.select(files, MAP), files, MAP)
        self.assertIn("scripts/mutation.py --tree COPY --build COPY/build native/src/device/sendguard.h", out)

    def test_docs_only_report_has_no_mutation_line(self):
        out = risk.report(risk.select(["README.md"], MAP), ["README.md"], MAP)
        self.assertNotIn("mutation.py", out)


class CoreFilter(unittest.TestCase):
    def test_core_items_filter_keeps_only_listed_ids(self):
        spec = {"round": "t", "core": "core.json", "core_items": ["LC1", "LC3"],
                "sections": [{"id": "linux", "title": "Linux", "items": []}]}
        out = checklist.with_core(spec, os.path.join(CHECKLISTS, "x.json"))
        self.assertEqual([i["id"] for i in out["sections"][0]["items"]], ["LC1", "LC3"])

    def test_without_filter_all_core_items_stay(self):
        spec = {"round": "t", "core": "core.json", "sections": [{"id": "linux", "title": "Linux", "items": []}]}
        out = checklist.with_core(spec, os.path.join(CHECKLISTS, "x.json"))
        self.assertEqual([i["id"] for i in out["sections"][0]["items"]], ["LC1", "LC2", "LC3", "LC4", "LC5"])


class RealMap(unittest.TestCase):
    """the repository's own map and core list fit together"""

    def setUp(self):
        with open(os.path.join(CHECKLISTS, "risk.json"), encoding="utf-8") as f:
            self.map = json.load(f)
        with open(os.path.join(CHECKLISTS, "core.json"), encoding="utf-8") as f:
            self.core = json.load(f)

    def test_every_core_item_has_a_known_key(self):
        for s in self.core["sections"]:
            for it in s["items"]:
                self.assertIn(it.get("key"), self.map["core_keys"], it["id"])

    def test_rules_use_known_platforms_and_keys(self):
        for r in self.map["rules"]:
            self.assertIn(r["class"], "ABC")
            for p in r.get("platforms", []):
                self.assertIn(p, self.map["platforms"])
            for k in r.get("core", []):
                self.assertIn(k, self.map["core_keys"])

    def test_every_tracked_source_file_is_classified_on_purpose(self):
        # files without a rule fall back to class C everywhere; the map should name every folder we have
        root = os.path.join(HERE, "..", "..")
        try:
            out = subprocess.run(["git", "-C", root, "ls-files"], capture_output=True, text=True, check=True).stdout
        except (OSError, subprocess.CalledProcessError):
            self.skipTest("not a git checkout")
        missing = [p for p in out.splitlines() if risk.classify(p, self.map).get("fallback")]
        self.assertEqual(missing, [])

    def test_this_rounds_pacing_change_is_class_b(self):
        sel = risk.select(["native/src/device/dumppacer.h", "native/tests/test_dumppacer.cpp"], self.map)
        self.assertEqual(sel["class"], "B")


if __name__ == "__main__":
    unittest.main()
