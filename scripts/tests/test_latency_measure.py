"""Tests for scripts/latency_measure.py (note-in to audio-out, stamps on the wire; E400, #30)."""
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import latency_measure as lm  # noqa: E402


def recording(n, first, period, delay_of, level=0.5, floor=0.0):
    """n notes on a grid; delay_of(k) frames later the sound starts and lasts a quarter of the period."""
    x = [floor] * (first + n * period)
    for k in range(n):
        start = first + k * period + delay_of(k)
        for i in range(start, min(start + period // 4, len(x))):
            x[i] = level
    return x


class Onsets(unittest.TestCase):
    def test_constant_delay(self):
        x = recording(4, 11025, 44100, lambda k: 4000)
        r = lm.onsets(x, 11025, 44100)
        self.assertEqual([o.delay for o in r], [4000] * 4)

    def test_the_grid_index_counts_the_seconds(self):
        x = recording(3, 11025, 44100, lambda k: 100)
        self.assertEqual([o.index for o in lm.onsets(x, 11025, 44100)], [0, 1, 2])

    def test_a_note_without_sound_has_no_delay(self):
        x = recording(3, 11025, 44100, lambda k: 100)
        for i in range(11025 + 44100, 11025 + 2 * 44100):
            x[i] = 0.0
        self.assertEqual([o.delay for o in lm.onsets(x, 11025, 44100)], [100, None, 100])

    def test_the_noise_floor_is_not_an_onset(self):
        x = recording(2, 11025, 44100, lambda k: 500, level=0.5, floor=0.001)
        self.assertEqual([o.delay for o in lm.onsets(x, 11025, 44100)], [500, 500])

    def test_the_previous_notes_tail_is_not_this_notes_onset(self):
        # a tail reaching into the next grid slot: the sound was already there at the note, no valid onset
        x = recording(2, 11025, 44100, lambda k: 500)
        for i in range(11025 + 44100 - 10, 11025 + 44100 + 200):
            x[i] = 0.5
        self.assertIsNone(lm.onsets(x, 11025, 44100)[1].delay)

    def test_silence_returns_nothing_valid(self):
        self.assertEqual([o.delay for o in lm.onsets([0.0] * 100000, 11025, 44100)], [None, None])


class Summary(unittest.TestCase):
    def test_classes_by_parity_of_the_second(self):
        x = recording(6, 11025, 44100, lambda k: 600 if k & 1 else 4000)
        s = lm.summary(lm.onsets(x, 11025, 44100), classes=2)
        self.assertEqual(s[0]["median"], 4000)
        self.assertEqual(s[1]["median"], 600)
        self.assertEqual(s[0]["n"], 3)
        self.assertEqual(s[1]["n"], 3)

    def test_spread_and_missing(self):
        delays = [4000, 4002, 4004, 4000]
        x = recording(4, 11025, 44100, lambda k: delays[k])
        for i in range(11025 + 3 * 44100, len(x)):
            x[i] = 0.0
        s = lm.summary(lm.onsets(x, 11025, 44100), classes=1)
        self.assertEqual(s[0]["n"], 3)
        self.assertEqual(s[0]["missing"], 1)
        self.assertEqual((s[0]["min"], s[0]["max"]), (4000, 4004))

    def test_ms_uses_the_rate(self):
        self.assertAlmostEqual(lm.frames_to_ms(44100, 44100), 1000.0)
        self.assertAlmostEqual(lm.frames_to_ms(480, 48000), 10.0)


class Stamps(unittest.TestCase):
    def test_a_stamp_before_a_note_on_belongs_to_it(self):
        ev = [(1.0, "c5 cin3 f5 10 20"), (1.0, "c2 cin9 90 3c 66")]
        r = lm.stamped_notes(ev)
        self.assertEqual(len(r), 1)
        self.assertEqual(r[0].stamp, 0x10 | 0x20 << 7)
        self.assertEqual(r[0].note, 0x3c)

    def test_a_note_without_a_stamp_is_reported_as_such(self):
        # the original's Live button: no F5 at all (E400)
        r = lm.stamped_notes([(2.0, "c2 cin9 90 3c 66")])
        self.assertIsNone(r[0].stamp)

    def test_note_off_and_velocity_zero_are_not_notes(self):
        ev = [(1.0, "c5 cin3 f5 01 00"), (1.0, "c2 cin8 80 3c 00"), (1.1, "c5 cin3 f5 02 00"), (1.1, "c2 cin9 90 3c 00")]
        self.assertEqual(lm.stamped_notes(ev), [])

    def test_the_stamp_stays_valid_for_following_notes_of_the_same_write(self):
        # one F5 per stamp value per write (protocol.md): a chord shares it
        ev = [(1.0, "c5 cin3 f5 05 00"), (1.0, "c2 cin9 90 3c 66"), (1.0, "c2 cin9 90 40 66")]
        self.assertEqual([n.stamp for n in lm.stamped_notes(ev)], [5, 5])

    def test_a_stamp_from_an_earlier_time_does_not_carry_over(self):
        ev = [(1.0, "c5 cin3 f5 05 00"), (1.0, "c2 cin9 90 3c 66"), (2.0, "c2 cin9 90 3c 66")]
        self.assertEqual([n.stamp for n in lm.stamped_notes(ev)], [5, None])

    def test_lead_difference_wraps_at_14_bits(self):
        self.assertEqual(lm.stamp_diff(100, 16380), 104)
        self.assertEqual(lm.stamp_diff(16380, 100), -104)

    def test_lead_per_class_compares_stamp_with_the_grid(self):
        # class 0 stamps 4000 after the note's frame, class 1 1024 before it; the difference is what E400 predicts
        notes = [lm.Note(float(k), 60, (11025 + k * 44100 + (4000 if not k & 1 else -1024)) % 16384) for k in range(6)]
        s = lm.stamp_classes(notes, 11025, 44100, classes=2)
        self.assertEqual(lm.stamp_diff(s[0], s[1]), 5024)

    def test_lead_per_class_needs_stamps(self):
        notes = [lm.Note(float(k), 60, None) for k in range(4)]
        self.assertEqual(lm.stamp_classes(notes, 11025, 44100, classes=2), {0: None, 1: None})


if __name__ == "__main__":
    unittest.main()
