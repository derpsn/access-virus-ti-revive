"""Tests for the changelog part of scripts/site/build.py: each version shows only its own release text.
Run: python3 -I -m unittest discover -s scripts/tests"""
import importlib.util
import os
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("sitebuild", os.path.join(HERE, "..", "site", "build.py"))
sitebuild = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sitebuild)

HEAD = "**TI Revive 0.9.0** - Total Integration for the Access Virus TI.\n\n"


class Summary(unittest.TestCase):
    def test_older_texts_repeat_earlier_versions(self):
        body = HEAD + "**New in 0.9.0: drops.**\n\n- Drop a file.\n\n**New in 0.8.9: knobs.**\n\n- Knobs.\n"
        self.assertEqual(sitebuild.summary({"tag_name": "v0.9.0", "body": body}), "- Drop a file.")

    def test_current_texts_end_with_links_and_the_notice(self):
        # since 0.8.35 a release text holds only its own version, then a pointer to earlier ones and the notice
        body = (HEAD + "**New in 0.9.0: drops.**\n\n- Drop a file.\n\nTested on a Virus TI2 Desktop.\n\n"
                "Earlier versions: their own release pages, and the [changelog](x).\n\n"
                "Not affiliated with Access Music or Kemper. Contains no Access software.\n")
        self.assertEqual(sitebuild.summary({"tag_name": "v0.9.0", "body": body}),
                         "- Drop a file.\n\nTested on a Virus TI2 Desktop.")


if __name__ == "__main__":
    unittest.main()
