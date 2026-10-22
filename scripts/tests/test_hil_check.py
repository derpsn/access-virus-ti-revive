"""Tests for scripts/hil_check.py (pass/fail rules of the automated hardware run scripts/hil.sh)."""
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import hil_check  # noqa: E402

GRID = [8.0493 + k for k in range(12)]  # a clean real-time run on the TI Desktop (2026-10-19)


class Grid(unittest.TestCase):
    def test_clean_run_passes(self):
        r = hil_check.grid(GRID, 1.0, 20.0)
        self.assertEqual(r["missing"], 0)
        self.assertEqual(r["offgrid"], 0)
        self.assertLess(r["maxdev_ms"], 0.1)
        self.assertTrue(all(ok for _, ok, _ in hil_check.grid_verdicts(r)))

    def test_leftover_sound_before_the_first_note_is_ignored(self):
        # cal2: a release tail of the run before at 0.06 s, then the notes from 5 s on
        r = hil_check.grid([0.0555] + [5.0493 + k for k in range(35)], 1.0, 40.0)
        self.assertEqual(r["missing"], 0)
        self.assertEqual(r["first_s"], 5.0493)

    def test_a_lost_note_fails(self):
        on = GRID[:5] + GRID[6:]
        r = hil_check.grid(on, 1.0, 20.0)
        self.assertEqual(r["missing"], 1)
        self.assertIn(("notes: none lost", False), [(n, ok) for n, ok, _ in hil_check.grid_verdicts(r)])

    def test_a_late_note_fails(self):
        on = list(GRID)
        on[6] += 0.012  # 12 ms late
        r = hil_check.grid(on, 1.0, 20.0)
        self.assertGreater(r["maxdev_ms"], 10)
        self.assertIn(("timing: every note within 5 ms of the grid", False),
                      [(n, ok) for n, ok, _ in hil_check.grid_verdicts(r)])

    def test_an_extra_note_after_the_start_fails(self):
        on = sorted(GRID + [12.5])
        r = hil_check.grid(on, 1.0, 20.0)
        self.assertEqual(r["offgrid"], 1)

    def test_first_note_too_late_fails(self):
        r = hil_check.grid([15.0493 + k for k in range(5)], 1.0, 20.0)
        self.assertIn(("start: first note within 12 s", False),
                      [(n, ok) for n, ok, _ in hil_check.grid_verdicts(r)])

    def test_from_judges_only_the_notes_after_a_wake(self):
        # a suspend at 25 s: a burst of catch-up sound, then the grid again
        on = [5.05 + k for k in range(20)] + [25.3, 25.31, 25.9] + [70.05 + k for k in range(50)]
        r = hil_check.grid(on, 1.0, 120.0, since=70.0)
        self.assertEqual((r["missing"], r["offgrid"]), (0, 0))
        self.assertTrue(all(ok for n, ok, _ in hil_check.grid_verdicts(r) if not n.startswith("start")))

    def test_a_planned_pause_is_not_lost_notes(self):
        # H3: export until 36 s, stopped until 40 s (no notes), then playback again
        on = [8.05 + k for k in range(28)] + [40.05 + k for k in range(14)]
        r = hil_check.grid(on, 1.0, 54.0, gaps=[(36.0, 40.0)])
        self.assertEqual(r["missing"], 0)
        r = hil_check.grid(on, 1.0, 54.0)
        self.assertEqual(r["missing"], 4)

    def test_a_note_lost_outside_the_pause_still_counts(self):
        on = [8.05 + k for k in range(28)] + [40.05 + k for k in range(14) if k != 3]
        r = hil_check.grid(on, 1.0, 54.0, gaps=[(36.0, 40.0)])
        self.assertEqual(r["missing"], 1)

    def test_start_counts_from_since(self):
        # H6: judged from 34 s on, the first judged note at 34.05 s is on time
        r = hil_check.grid([34.05 + k for k in range(26)], 1.0, 60.0, since=34.0)
        self.assertIn(("start: first note within 12 s", True), [(n, ok) for n, ok, _ in hil_check.grid_verdicts(r)])

    def test_silence_fails(self):
        r = hil_check.grid([], 1.0, 20.0)
        self.assertFalse(all(ok for _, ok, _ in hil_check.grid_verdicts(r)))


LOG_OK = """Virus TI native: Virus ready after 1.1 s
Virus TI native: sync marker at frame 316404 (ping at 315577, 827 frames later)
"""


class Log(unittest.TestCase):
    def verdicts(self, text):
        return {n: ok for n, ok, _ in hil_check.log_verdicts(text)}

    def test_clean_start(self):
        self.assertTrue(all(self.verdicts(LOG_OK).values()))

    def test_no_ready_line_fails(self):
        self.assertFalse(self.verdicts(LOG_OK.splitlines()[1])["log: Virus ready"])

    def test_audio_stood_still_fails(self):
        v = self.verdicts(LOG_OK + "Virus TI native: the Virus' audio stood still (sleep?) - set-up\n")
        self.assertFalse(v["log: audio never stood still"])

    def test_stood_still_allowed_after_a_sleep(self):
        v = {n: ok for n, ok, _ in hil_check.log_verdicts(
            LOG_OK + "Virus TI native: the Virus' audio stood still (sleep?)\n", sleep=True)}
        self.assertTrue(all(v.values()))

    def test_missing_marker_fails(self):
        self.assertFalse(self.verdicts(LOG_OK.splitlines()[0] + "\n")["log: sync marker"])


class Offline(unittest.TestCase):
    def test_paced_render_passes(self):
        out = "offline part: 28.00 s of audio in 27.99 s wall time (1.00x)\n"
        self.assertTrue(all(ok for _, ok, _ in hil_check.offline_verdicts(out)))

    def test_render_running_free_fails(self):
        out = "offline part: 28.00 s of audio in 2.10 s wall time (13.33x)\n"
        self.assertFalse(all(ok for _, ok, _ in hil_check.offline_verdicts(out)))

    def test_no_offline_line_fails(self):
        self.assertFalse(all(ok for _, ok, _ in hil_check.offline_verdicts("wrote x.wav\n")))


def write_wav(path, onsets, seconds, rate=44100):
    """16-bit stereo: a 100 ms 440 Hz burst at each onset (what nativehost records of C4 on part 1)"""
    import math
    import struct
    n = int(seconds * rate)
    x = [0] * n
    for t in onsets:
        s = int(t * rate)
        for i in range(s, min(n, s + rate // 10)):
            x[i] = int(16000 * math.sin(2 * math.pi * 440 * (i - s) / rate))
    data = b"".join(struct.pack("<hh", v, v) for v in x)
    with open(path, "wb") as f:
        f.write(b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVEfmt " +
                struct.pack("<IHHIIHH", 16, 1, 2, rate, rate * 4, 4, 16) + b"data" + struct.pack("<I", len(data)) + data)


class Folder(unittest.TestCase):
    """a results folder of the Windows / Mac run (hil-win.ps1, hil-mac.command), graded on any system"""

    def setUp(self):
        import tempfile
        self.dir = tempfile.mkdtemp()

    def item(self, name, seconds, onsets, out=""):
        p = os.path.join(self.dir, name)
        write_wav(p + ".wav", onsets, seconds)
        with open(p + ".err", "w") as f:
            f.write(LOG_OK)
        with open(p + ".out", "w") as f:
            f.write(out)

    def clean_folder(self):
        for name in ("H1", "H2-1", "H2-2"):
            self.item(name, 20, [6.05 + k for k in range(14)])
        self.item("H3", 54, [6.05 + k for k in range(30)] + [40.05 + k for k in range(14)],
                  "offline part: 24.00 s of audio in 24.10 s wall time (1.00x)\n")
        self.item("H6", 60, [6.05 + k for k in range(14)] + [34.05 + k for k in range(26)])

    def test_clean_folder_passes(self):
        self.clean_folder()
        ok, lines = hil_check.grade_folder(self.dir)
        self.assertTrue(ok, "\n".join(lines))
        self.assertEqual(sum(1 for line in lines if line.endswith(": pass")), len(hil_check.ITEMS))

    def test_an_item_not_run_fails(self):
        self.clean_folder()
        os.remove(os.path.join(self.dir, "H6.err"))
        ok, lines = hil_check.grade_folder(self.dir)
        self.assertFalse(ok)
        self.assertIn("H6: FAIL (not run)", lines)

    def test_a_free_running_export_fails(self):
        self.clean_folder()
        self.item("H3", 54, [6.05 + k for k in range(30)] + [40.05 + k for k in range(14)],
                  "offline part: 24.00 s of audio in 2.00 s wall time (12.00x)\n")
        ok, lines = hil_check.grade_folder(self.dir)
        self.assertFalse(ok)
        self.assertIn("H3: FAIL", lines)

    def test_runners_play_the_items_the_grader_expects(self):
        # the Windows and Mac runners name each item with its length; a changed length there must change ITEMS too
        here = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
        for runner in ("hil-win.ps1", "hil-mac.command"):
            with open(os.path.join(here, runner), encoding="utf-8") as f:
                text = f.read()
            for name, item in hil_check.ITEMS.items():
                self.assertRegex(text, r"(?i)(run|start)_?item\s+['\"]?%s['\"]?\s+%d\b" % (name, item["seconds"]),
                                 "%s: %s %d s" % (runner, name, item["seconds"]))


if __name__ == "__main__":
    unittest.main()
