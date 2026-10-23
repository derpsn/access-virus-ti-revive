"""Tests for the Access file helpers of src/installer-linux/install.sh (sourced with TIREVIVE_INSTALL_FUNCS_ONLY=1,
so nothing is installed). Run: python3 -I -m unittest discover -s scripts/tests"""
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
INSTALL = os.path.join(HERE, "..", "..", "src", "installer-linux", "install.sh")
CC_EXE = "Virus Control Center.exe"


def call(func, *args, home=None):
    """Run one install.sh function in a fresh sh and return its stdout."""
    env = dict(os.environ, TIREVIVE_INSTALL_FUNCS_ONLY="1")
    if home:
        env["HOME"] = home
    return subprocess.run(["sh", "-c", '. "$1"; shift; "$@"', "sh", INSTALL, func, *args], env=env, check=True,
                          capture_output=True, text=True).stdout.strip()


def tree(root, rel, files):
    """An unpacked .msi folder: <root>/<rel>/Common/<files> (empty files; only names matter)."""
    common = os.path.join(root, rel, "Common")
    os.makedirs(common, exist_ok=True)
    for f in files:
        open(os.path.join(common, f), "w").close()
    return os.path.join(root, rel)


@unittest.skipIf(sys.platform == "win32", "the Linux installer's shell functions, not used on Windows")
class AccessSrc(unittest.TestCase):
    # the 5.1.7 x64 .msi unpacks Common/ twice (E324): the x64 one with the Control Center exe and a PE32 one
    # under Program Files without it; find's order depends on the file system, so the result must not
    def test_prefers_the_copy_with_the_control_center(self):
        with tempfile.TemporaryDirectory() as t:
            good = tree(t, "Access Music/Virus TI", ["Virus Control.dll", "firmware.bin", CC_EXE])
            tree(t, "Program Files/Access Music/Virus TI", ["Virus Control.dll", "firmware.bin"])
            self.assertEqual(call("access_src", t), good)

    def test_choice_is_by_content_not_by_folder_name(self):
        with tempfile.TemporaryDirectory() as t:
            tree(t, "Access Music/Virus TI", ["Virus Control.dll", "firmware.bin"])
            good = tree(t, "Program Files/Access Music/Virus TI", ["Virus Control.dll", "firmware.bin", CC_EXE])
            self.assertEqual(call("access_src", t), good)

    def test_without_any_control_center_a_dll_copy_is_still_used(self):
        with tempfile.TemporaryDirectory() as t:
            only = tree(t, "x/Access Music/Virus TI", ["Virus Control.dll", "firmware.bin"])
            self.assertEqual(call("access_src", t), only)

    def test_no_dll_gives_nothing(self):
        with tempfile.TemporaryDirectory() as t:
            tree(t, "Access Music/Virus TI", ["firmware.bin", CC_EXE])
            self.assertEqual(call("access_src", t), "")


@unittest.skipIf(sys.platform == "win32", "the Linux installer's shell functions, not used on Windows")
class FindMsi(unittest.TestCase):
    def test_argument_wins(self):
        self.assertEqual(call("find_msi", "/some/where/my.msi"), "/some/where/my.msi")

    def test_searches_downloads(self):
        with tempfile.TemporaryDirectory() as home:
            os.makedirs(os.path.join(home, "Downloads"))
            msi = os.path.join(home, "Downloads", "Virus TI Software Suite 5.1.7.00 64-bit.msi")
            open(msi, "w").close()
            self.assertEqual(call("find_msi", "", home=home), msi)

    def test_nothing_found_gives_nothing(self):
        with tempfile.TemporaryDirectory() as home:
            self.assertEqual(call("find_msi", "", home=home), "")


if __name__ == "__main__":
    unittest.main()
