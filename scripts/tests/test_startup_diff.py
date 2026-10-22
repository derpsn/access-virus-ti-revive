"""Tests for scripts/startup_diff.py (compare the start-up messages of two usbmon captures, E321)."""
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import startup_diff  # noqa: E402

HDR = bytes([0xF0, 0x00, 0x20, 0x33, 0x01, 0x10])


def usb_midi(msg, cable=2):
    """a SysEx as USB-MIDI 4-byte packets (CIN 4 = start/continue, 5/6/7 = end with 1/2/3 bytes)"""
    out = b""
    i = 0
    while len(msg) - i > 3:
        out += bytes([cable << 4 | 4]) + msg[i:i + 3]
        i += 3
    rest = msg[i:]
    return out + bytes([cable << 4 | (4 + len(rest))]) + rest + bytes(3 - len(rest))


def sx(*b):
    return HDR + bytes(b) + b"\xf7"


class Decode(unittest.TestCase):
    def test_sysex_over_several_transfers(self):
        m = sx(0x73, 0x00, 0x7A, 0x03)
        data = usb_midi(m)
        msgs = startup_diff.decode([(1.0, "0x06", data[:4]), (1.1, "0x06", data[4:])])
        self.assertEqual(msgs, [(1.0, m)])

    def test_two_cables_interleaved(self):
        a, b = sx(0x73, 0x00, 0x22, 0x00), sx(0x74, 0x00, 0x7E, 0x07)
        pa, pb = usb_midi(a, 2), usb_midi(b, 3)
        data = pa[:4] + pb[:4] + pa[4:] + pb[4:]
        self.assertEqual([m for _, m in startup_diff.decode([(0.0, "0x06", data)])], [a, b])

    def test_short_messages_kept(self):
        self.assertEqual(startup_diff.decode([(0.0, "0x06", bytes([0x2B, 0xB1, 0x07, 0x64]))]),
                         [(0.0, bytes([0xB1, 0x07, 0x64]))])


class Tokens(unittest.TestCase):
    def test_runs_and_names(self):
        multi = HDR + bytes([0x11, 0x00, 0x00]) + bytes(257) + b"\xf7"
        single = lambda p: HDR + bytes([0x10, 0x00, p]) + bytes(514) + b"\xf7"  # noqa: E731
        msgs = [sx(0x73, 0x00, 0x7A, 0x03), sx(0x74, 0x00, 0x7E, 0x07), multi] + \
               [sx(0x72, p, 0x22, p) for p in range(16)] + [single(p) for p in range(16)] + \
               [bytes([0xFE]), sx(0x39, 0x01)]
        t = startup_diff.tokens([(0.0, m) for m in msgs])
        self.assertEqual(t[0], "73 00 7a=03")
        self.assertEqual(t[1], "74 00 7e=07")
        self.assertTrue(t[2].startswith("multi 267 B "))
        self.assertEqual(t[3], "72 xx 22 (part channels 1-16)")
        self.assertEqual(t[4], "single dumps to the edit buffers x16")
        self.assertEqual(t[5], "39 01 (bank info request)")
        self.assertEqual(len(t), 6)  # active sensing etc. left out

    def test_notes_between_the_single_dumps_do_not_split_the_run(self):
        single = lambda p: HDR + bytes([0x10, 0x00, p]) + bytes(514) + b"\xf7"  # noqa: E731
        msgs = [single(0), bytes([0x90, 60, 100]), bytes([0xFE]), single(1), bytes([0x80, 60, 0]), single(2)]
        self.assertEqual(startup_diff.tokens([(0.0, m) for m in msgs]), ["single dumps to the edit buffers x3"])

    def test_the_multi_content_counts(self):
        a = HDR + bytes([0x11, 0x00, 0x00]) + bytes(257) + b"\xf7"
        b = HDR + bytes([0x11, 0x00, 0x00, 1]) + bytes(256) + b"\xf7"
        self.assertNotEqual(startup_diff.tokens([(0, a)]), startup_diff.tokens([(0, b)]))


class Window(unittest.TestCase):
    def test_from_the_first_sysex_to_the_end_of_the_bank_check(self):
        msgs = [(0.5, bytes([0xFE])), (1.0, sx(0x73, 0x00, 0x7A, 0x03)), (2.0, sx(0x39, 0x04)),
                (2.1, sx(0x74, 0x00, 0x29, 0x01)), (9.0, sx(0x73, 0x00, 0x22, 0x00))]
        w = startup_diff.window(msgs, max_s=60)
        self.assertEqual([t for t, _ in w], [1.0, 2.0, 2.1])

    def test_capped_without_a_bank_check(self):
        msgs = [(1.0, sx(0x73, 0x00, 0x7A, 0x03)), (30.0, sx(0x73, 0x00, 0x22, 0x00))]
        self.assertEqual(len(startup_diff.window(msgs, max_s=20)), 1)


class Compare(unittest.TestCase):
    def test_equal_and_different(self):
        same, lines = startup_diff.compare(["a", "b", "c"], ["a", "b", "c"])
        self.assertTrue(same)
        same, lines = startup_diff.compare(["a", "x", "b", "c"], ["a", "b", "c", "y"])
        self.assertFalse(same)
        self.assertIn("- x", lines)
        self.assertIn("+ y", lines)

    def test_repeated_bank_info_requests_are_a_known_side_effect(self):
        # E322: the original asks again for a bank whose reply is still arriving (timer reset by any reply); the
        # repeat and its display message count as known, not as a difference
        orig = ["74 01 7e=07", "39 01 (bank info request)", "74 01 7e=07", "39 02 (bank info request)",
                "74 01 7e=07", "39 02 (bank info request)", "74 00 29=01"]
        ours = ["74 01 7e=07", "39 01 (bank info request)", "74 01 7e=07", "39 02 (bank info request)", "74 00 29=01"]
        same, lines = startup_diff.compare(orig, ours)
        self.assertTrue(same, lines)
        self.assertTrue(any("repeated bank info" in line for line in lines))

    def test_known_differences_are_listed_not_counted(self):
        same, lines = startup_diff.compare(["73 00 09=00", "a"], ["a"], ignore=["73 00 09="])
        self.assertTrue(same)
        self.assertTrue(any("ignored" in line for line in lines))


if __name__ == "__main__":
    unittest.main()
