"""Tests for scripts/coverage.py: merging gcov JSON reports per source line and the summary.
Run: python3 -I -m unittest discover -s scripts/tests"""
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import coverage  # noqa: E402

SRC = os.path.realpath(os.path.join(os.sep, "repo", "native", "src"))


def report(cwd, *files):
    return {"current_working_directory": cwd,
            "files": [{"file": name, "lines": [{"line_number": n, "count": c} for n, c in lines]}
                      for name, lines in files]}


class Merge(unittest.TestCase):
    def test_a_line_run_by_any_object_counts(self):
        # a header seen from vticore (line 2 not run) and from a test object (line 2 run)
        a = report(SRC, ("device/ring.h", [(1, 3), (2, 0)]))
        b = report(os.path.join(SRC, "..", "tests"), ("../src/device/ring.h", [(2, 5), (3, 0)]))
        lines = coverage.merge([a, b], SRC)
        self.assertEqual(lines, {os.path.join(SRC, "device", "ring.h"): {1: True, 2: True, 3: False}})

    def test_files_outside_native_src_are_ignored(self):
        r = report(SRC, ("/usr/include/c++/vector", [(1, 1)]), ("../tests/test_x.cpp", [(1, 1)]))
        self.assertEqual(coverage.merge([r], SRC), {})


class Summary(unittest.TestCase):
    def test_totals_and_weakest_first(self):
        lines = {os.path.join(SRC, "a.h"): {1: True, 2: True}, os.path.join(SRC, "b.cpp"): {1: True, 2: False}}
        covered, total, files = coverage.summary(lines, SRC)
        self.assertEqual((covered, total), (3, 4))
        self.assertEqual(files[0], ("b.cpp", 1, 2))

    def test_refuses_the_normal_build_folder(self):
        with self.assertRaises(SystemExit):
            coverage.main(["--build", os.path.join(os.sep, "x", "build-native"), "--no-build"])


if __name__ == "__main__":
    unittest.main()
