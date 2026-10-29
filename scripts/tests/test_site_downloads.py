"""Tests for the download counter of scripts/site/build.py: GitHub's download counts of the installer files of every
release, summed at build time (the site is rebuilt daily, site.yml).
Run: python3 -I -m unittest discover -s scripts/tests"""
import importlib.util
import os
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("sitebuild", os.path.join(HERE, "..", "site", "build.py"))
sitebuild = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sitebuild)


def rel(tag, counts, pre=False):
    return {"tag_name": tag, "prerelease": pre, "assets": [{"name": n, "download_count": c} for n, c in counts]}


class Downloads(unittest.TestCase):
    def test_counts_the_installers_of_every_release(self):
        rs = [rel("v0.9.0", [("TI-Revive-0.9.0-setup.exe", 5), ("TI-Revive-0.9.0.msi", 2),
                             ("TI-Revive-0.9.0-mac.zip", 7), ("TI-Revive-0.9.0-linux.tar.gz", 1)]),
              rel("v0.9.0-rc1", [("TI-Revive-0.9.0-mac.zip", 3)], pre=True)]
        self.assertEqual(sitebuild.downloads(rs), 18)

    def test_skips_tools_and_sources(self):
        rs = [rel("v0.9.0", [("TI-Revive-0.9.0-mac.zip", 4), ("tirevive-usbinfo.ps1", 50),
                             ("libusb-1.0.30.tar.bz2", 9)])]
        self.assertEqual(sitebuild.downloads(rs), 4)

    def test_no_assets_no_count(self):
        self.assertEqual(sitebuild.downloads([{"tag_name": "v0.1.0"}]), 0)

    def test_the_download_section_shows_it(self):
        rs = [rel("v0.9.0", [("TI-Revive-0.9.0-mac.zip", 1234)])]
        self.assertEqual(sitebuild.downloads_line(rs),
                         '<p class="small downloads">Downloaded 1,234 times so far (all versions, counted by GitHub).</p>')
        self.assertEqual(sitebuild.downloads_line([]), "")


if __name__ == "__main__":
    unittest.main()
