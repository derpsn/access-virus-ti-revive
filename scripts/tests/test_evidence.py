"""Tests for scripts/evidence.py (format check, corrections, index) and a check of the real findings/evidence.md.
Run: python3 -I -m unittest discover -s scripts/tests"""
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import evidence  # noqa: E402


def entry(n, claim="A claim.", **fields):
    f = {"Date": "2026-11-13", "Pointer": "cap.pcapng frame 1", "Method": "usbmon", "Confidence": "HIGH"}
    f.update(fields)
    lines = ["### E%03d" % n, "", claim, ""]
    lines += ["- **%s:** %s" % (k, v) for k, v in f.items() if v is not None]
    return "\n".join(lines) + "\n\n"


def run_of(first, last, **fields):
    return "".join(entry(n, **fields) for n in range(first, last + 1) if n not in evidence.KNOWN_GAPS)


def problems(text):
    return evidence.check(evidence.parse(text))


class Parse(unittest.TestCase):
    def test_fields_and_multi_paragraph_claim(self):
        e = evidence.parse("# Evidence log\n\nintro\n\n" + entry(1, "First.\n\nSecond.", Hardware="no"))[0]
        self.assertEqual(e["id"], 1)
        self.assertEqual(e["claim"], "First.\nSecond.")
        self.assertEqual(e["fields"]["Hardware"], "no")
        self.assertEqual(e["fields"]["Pointer"], "cap.pcapng frame 1")


class Check(unittest.TestCase):
    def test_good_entries_pass(self):
        self.assertEqual(problems(entry(1) + entry(2)), [])

    def test_duplicate_and_order(self):
        self.assertTrue(any("duplicate" in p for p in problems(entry(1) + entry(1))))
        self.assertTrue(any("not ascending" in p for p in problems(entry(2) + entry(1))))

    def test_skipped_numbers_except_known_gaps(self):
        self.assertTrue(any("E002" in p for p in problems(entry(1) + entry(3))))
        self.assertEqual(problems(run_of(1, 92)), [])  # E089, E090 are known gaps

    def test_required_fields_and_date(self):
        self.assertTrue(any("no Pointer" in p for p in problems(entry(1, Pointer=None))))
        self.assertTrue(any("YYYY-MM-DD" in p for p in problems(entry(1, Date="12.11.2026"))))

    def test_confidence_needs_a_level(self):
        self.assertTrue(any("names no level" in p for p in problems(entry(1, Confidence="sure"))))
        self.assertEqual(problems(entry(1, Confidence="HIGH (log) / open (cause)")), [])

    def test_hardware_required_from_e505(self):
        pre = run_of(1, 504)
        self.assertTrue(any("Hardware" in p for p in problems(pre + entry(505))))
        for hw in ("yes - TI2 Desktop, Linux, build 1a2b3c4", "no", "n/a (static analysis)"):
            self.assertEqual(problems(pre + entry(505, Hardware=hw)), [], hw)
        self.assertTrue(any("Hardware" in p for p in problems(pre + entry(505, Hardware="maybe"))))

    def test_correction_needs_status_on_the_old_entry(self):
        later = entry(2, "This corrects E001: the delay is the unit's.")
        self.assertTrue(any("corrected by E002" in p for p in problems(entry(1) + later)))
        self.assertEqual(problems(entry(1, Status="corrected by E002") + later), [])

    def test_text_after_fields_is_reported(self):
        self.assertTrue(any("after the fields" in p for p in problems(entry(1) + "loose line\n")))


class Levels(unittest.TestCase):
    def test_lowest_level_and_open(self):
        self.assertEqual(evidence.levels("HIGH (log) / MEDIUM (role) / open"), (["MED", "HIGH"], True))
        self.assertEqual(evidence.levels("HIGH"), (["HIGH"], False))


class Index(unittest.TestCase):
    def test_marks_and_topics(self):
        es = evidence.parse(entry(1, "A stall on EP 0x85 in REAPER. More.", Confidence="HIGH / open (cause)",
                                  Hardware="no", Status="corrected by E002")
                            + entry(2, "This corrects E001."))
        out = evidence.render_index(es)
        self.assertIn("## USB transport", out)
        self.assertIn("- [E001](evidence.md#e001) A stall on EP 0x85 in REAPER "
                      "(open; not on hardware; corrected by E002)", out)


class RealFile(unittest.TestCase):
    """the repository's evidence log itself: no format problems and a current index"""

    def test_evidence_md_passes_the_check(self):
        self.assertEqual(evidence.main(["check"]), 0)


if __name__ == "__main__":
    unittest.main()
