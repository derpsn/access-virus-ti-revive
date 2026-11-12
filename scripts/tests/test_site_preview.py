"""Tests for the preview box of scripts/site/build.py: a pre-release newer than the latest release gets a box for
testers in the download section; it goes away by itself when the final release is out.
Run: python3 -I -m unittest discover -s scripts/tests"""
import importlib.util
import os
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("sitebuild", os.path.join(HERE, "..", "site", "build.py"))
sitebuild = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sitebuild)


def rel(tag, when, pre=False, draft=False):
    return {"tag_name": tag, "published_at": when + "T12:00:00Z", "prerelease": pre, "draft": draft,
            "html_url": "https://github.com/derpsn/access-virus-ti-revive/releases/tag/" + tag}


LATEST = rel("v0.9.0", "2026-12-01")


class Preview(unittest.TestCase):
    def test_no_prerelease_no_box(self):
        self.assertEqual(sitebuild.preview_box(LATEST, [LATEST]), "")

    def test_older_prerelease_no_box(self):
        # the rc of the version that is out now: the final replaced it
        rs = [LATEST, rel("v0.9.0-rc1", "2026-11-28", pre=True)]
        self.assertEqual(sitebuild.preview_box(LATEST, rs), "")

    def test_draft_no_box(self):
        rs = [rel("v0.9.1-rc1", "2026-12-05", pre=True, draft=True), LATEST]
        self.assertEqual(sitebuild.preview_box(LATEST, rs), "")

    def test_newer_prerelease_gets_a_box(self):
        rc = rel("v0.9.1-rc2", "2026-12-05", pre=True)
        box = " ".join(sitebuild.preview_box(LATEST, [rc, rel("v0.9.1-rc1", "2026-12-03", pre=True), LATEST]).split())
        self.assertIn("Preview: 0.9.1-rc2 for testers", box) # the newest rc only
        self.assertNotIn("rc1", box)
        self.assertIn("5 Dec 2026", box)
        self.assertIn('href="' + rc["html_url"] + '"', box)
        self.assertIn("template=test_report.yml", box)
        self.assertIn("uninstall the preview before installing the final 0.9.1", box)
        self.assertIn("The regular download stays 0.9.0 until then.", box)

    def test_new_sentence_from_the_table(self):
        rc = rel("v0.9.1-rc1", "2026-12-05", pre=True)
        old = dict(sitebuild.PREVIEW_NEW)
        try:
            sitebuild.PREVIEW_NEW.clear()
            self.assertNotIn("New:", sitebuild.preview_box(LATEST, [rc, LATEST]))
            sitebuild.PREVIEW_NEW["0.9.1"] = "a **bold** thing."
            self.assertIn("New: a <strong>bold</strong> thing. ", sitebuild.preview_box(LATEST, [rc, LATEST]))
        finally:
            sitebuild.PREVIEW_NEW.clear()
            sitebuild.PREVIEW_NEW.update(old)

    def test_the_text_matches_the_readme(self):
        # README and site say the same (the README block is removed by hand at the final release)
        with open(os.path.join(HERE, "..", "..", "README.md")) as f:
            readme = " ".join(l[2:] if l.startswith("> ") else l for l in f.read().split("\n")) # quote blocks
        readme = " ".join(readme.split())
        for v, new in sitebuild.PREVIEW_NEW.items():
            if "**Preview: %s-rc" % v in readme:
                self.assertIn("New: " + new, readme)


if __name__ == "__main__":
    unittest.main()
