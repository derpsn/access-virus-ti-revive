"""Tests for scripts/libusb.sh (audit 2026-11-17 #7): the libusb the macOS and Linux brokers link statically was
downloaded and built without a checksum (the vtibroker of the libusb Mac package runs as root). libusb_verify must
refuse a tarball whose SHA-256 is not the pinned one. Run: python3 -I -m unittest discover -s scripts/tests"""
import hashlib
import os
import subprocess
import tempfile
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
LIBUSB = os.path.join(HERE, "..", "libusb.sh")


def verify(path, sha=None):
    """libusb_verify on `path`, with the pinned hash or `sha` instead; returns the exit code."""
    pin = f'LIBUSB_SHA256={sha}; ' if sha else ""
    return subprocess.run(["bash", "-c", f'. "$1"; {pin}libusb_verify "$2"', "bash", LIBUSB, path],
                          capture_output=True, text=True).returncode


@unittest.skipIf(sys.platform == "win32", "bash script of the Linux and Mac release builds, not used on Windows")
class LibusbFetch(unittest.TestCase):
    def test_pinned_version_and_hash(self):
        out = subprocess.run(["bash", "-c", '. "$1"; echo "$LIBUSB_VER $LIBUSB_SHA256"', "bash", LIBUSB],
                             capture_output=True, text=True, check=True).stdout.split()
        self.assertEqual(out[0], "1.0.30")
        self.assertEqual(out[1], "fea36f34f9156400209595e300840767ab1a385ede1dc7ee893015aea9c6dbaf")

    def test_a_swapped_tarball_is_refused(self):
        with tempfile.NamedTemporaryFile(delete=False) as f:
            f.write(b"not libusb")
        try:
            self.assertNotEqual(verify(f.name), 0)
            self.assertEqual(verify(f.name, hashlib.sha256(b"not libusb").hexdigest()), 0)
        finally:
            os.unlink(f.name)

    def test_a_missing_file_is_refused(self):
        self.assertNotEqual(verify("/nonexistent/libusb.tar.bz2"), 0)


if __name__ == "__main__":
    unittest.main()
