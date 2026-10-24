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


def pe_header(bits):
    """The first bytes of a Windows image: MZ, the PE header at 0x40, optional header magic PE32+ (64) or PE32 (32)."""
    b = bytearray(0x80)
    b[0:2] = b"MZ"
    b[0x3C] = 0x40
    b[0x40:0x44] = b"PE\0\0"
    b[0x44:0x46] = (0x8664 if bits == 64 else 0x14C).to_bytes(2, "little")
    b[0x58:0x5A] = (0x20B if bits == 64 else 0x10B).to_bytes(2, "little")
    return bytes(b)


def tree(root, rel, files, bits=64):
    """An unpacked .msi folder: <root>/<rel>/Common/<files>; Virus Control.dll gets a PE header of `bits`, the
    others are empty (only names matter)."""
    common = os.path.join(root, rel, "Common")
    os.makedirs(common, exist_ok=True)
    for f in files:
        with open(os.path.join(common, f), "wb") as fh:
            fh.write(pe_header(bits) if f == "Virus Control.dll" else b"")
    return os.path.join(root, rel)


@unittest.skipIf(sys.platform == "win32", "the Linux installer's shell functions, not used on Windows")
class AccessSrc(unittest.TestCase):
    # the 5.1.7 x64 .msi unpacks Common/ twice (E324): the x64 one with the Control Center exe and a PE32 one
    # under Program Files without it; find's order depends on the file system, so the result must not
    def test_prefers_the_copy_with_the_control_center(self):
        with tempfile.TemporaryDirectory() as t:
            good = tree(t, "Access Music/Virus TI", ["Virus Control.dll", "firmware.bin", CC_EXE])
            tree(t, "Program Files/Access Music/Virus TI", ["Virus Control.dll", "firmware.bin"], bits=32)
            self.assertEqual(call("access_src", t), good)

    def test_choice_is_by_content_not_by_folder_name(self):
        with tempfile.TemporaryDirectory() as t:
            tree(t, "Access Music/Virus TI", ["Virus Control.dll", "firmware.bin"], bits=32)
            good = tree(t, "Program Files/Access Music/Virus TI", ["Virus Control.dll", "firmware.bin", CC_EXE])
            self.assertEqual(call("access_src", t), good)

    # only the 64-bit Virus Control.dll counts (E345), even next to the Control Center exe
    def test_the_64_bit_dll_wins_over_the_control_center(self):
        with tempfile.TemporaryDirectory() as t:
            tree(t, "Access Music/Virus TI", ["Virus Control.dll", "firmware.bin", CC_EXE], bits=32)
            good = tree(t, "Program Files/Access Music/Virus TI", ["Virus Control.dll", "firmware.bin"])
            self.assertEqual(call("access_src", t), good)

    # an administrative extract (Wine's msiexec /a) flattens both trees and keeps the 32-bit DLL: nothing usable
    def test_only_a_32_bit_dll_gives_nothing(self):
        with tempfile.TemporaryDirectory() as t:
            tree(t, "Access Music/Virus TI", ["Virus Control.dll", "firmware.bin", CC_EXE], bits=32)
            self.assertEqual(call("access_src", t), "")

    def test_without_any_control_center_a_dll_copy_is_still_used(self):
        with tempfile.TemporaryDirectory() as t:
            only = tree(t, "x/Access Music/Virus TI", ["Virus Control.dll", "firmware.bin"])
            self.assertEqual(call("access_src", t), only)

    def test_no_dll_gives_nothing(self):
        with tempfile.TemporaryDirectory() as t:
            tree(t, "Access Music/Virus TI", ["firmware.bin", CC_EXE])
            self.assertEqual(call("access_src", t), "")


@unittest.skipIf(sys.platform == "win32", "the Linux installer's shell functions, not used on Windows")
class Pe64(unittest.TestCase):
    def is64(self, data):
        with tempfile.TemporaryDirectory() as t:
            f = os.path.join(t, "Virus Control.dll")
            with open(f, "wb") as fh:
                fh.write(data)
            env = dict(os.environ, TIREVIVE_INSTALL_FUNCS_ONLY="1")
            return subprocess.run(["sh", "-c", '. "$1"; pe64 "$2"', "sh", INSTALL, f], env=env).returncode == 0

    def test_pe32_plus_is_64_bit(self):
        self.assertTrue(self.is64(pe_header(64)))

    def test_pe32_is_not(self):
        self.assertFalse(self.is64(pe_header(32)))

    def test_no_image_is_not(self):
        self.assertFalse(self.is64(b""))
        self.assertFalse(self.is64(b"text that is no Windows image at all" * 4))


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
