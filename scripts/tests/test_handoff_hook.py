"""Tests for .claude/hooks/handoff-check.sh (Stop hook: no turn ends with commits after the last HANDOFF commit).
Run: python3 -I -m unittest discover -s scripts/tests"""
import json
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
HOOK = os.path.join(HERE, "..", "..", ".claude", "hooks", "handoff-check.sh")


# The hook is a bash script for the maintainers' Linux machine; the Windows CI runner's bash cannot run it.
@unittest.skipIf(sys.platform == "win32", "bash hook, not used on Windows")
class HandoffHook(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.repo = self.tmp.name
        self.git("init", "-q")
        os.makedirs(os.path.join(self.repo, "findings"))
        self.commit("findings/HANDOFF.md", "HANDOFF: start")

    def tearDown(self):
        self.tmp.cleanup()

    def git(self, *args):
        env = dict(os.environ, GIT_AUTHOR_NAME="t", GIT_AUTHOR_EMAIL="t@t", GIT_COMMITTER_NAME="t",
                   GIT_COMMITTER_EMAIL="t@t")
        subprocess.run(["git", "-C", self.repo, *args], check=True, env=env, capture_output=True)

    def commit(self, path, message):
        with open(os.path.join(self.repo, path), "a") as f:
            f.write(message + "\n")
        self.git("add", path)
        self.git("commit", "-q", "-m", message)

    def run_hook(self, active=False):
        event = {"hook_event_name": "Stop", "stop_hook_active": active, "cwd": self.repo}
        r = subprocess.run(["bash", HOOK], input=json.dumps(event), capture_output=True, text=True,
                           cwd=self.repo, env=dict(os.environ, CLAUDE_PROJECT_DIR=self.repo))
        self.assertEqual(r.returncode, 0, r.stderr)
        return json.loads(r.stdout) if r.stdout.strip() else None

    def test_handoff_is_newest_commit_allows(self):
        self.commit("code.c", "fix")
        self.commit("findings/HANDOFF.md", "HANDOFF: fix")
        self.assertIsNone(self.run_hook())

    def test_commit_after_handoff_blocks(self):
        self.commit("code.c", "fix the thing")
        out = self.run_hook()
        self.assertEqual(out["decision"], "block")
        self.assertIn("1 commit", out["reason"])
        self.assertIn("fix the thing", out["reason"])

    def test_block_reason_asks_for_the_same_commit(self):
        self.commit("code.c", "fix")
        self.assertIn("into the work commit itself", self.run_hook()["reason"])

    def test_long_handoff_blocks(self):
        with open(os.path.join(self.repo, "findings", "HANDOFF.md"), "w") as f:
            f.write("line\n" * 151)
        self.git("commit", "-q", "-am", "HANDOFF: long")
        out = self.run_hook()
        self.assertEqual(out["decision"], "block")
        self.assertIn("151 lines (limit 150)", out["reason"])
        self.assertNotIn("since the last HANDOFF update", out["reason"])

    def test_handoff_at_the_limit_allows(self):
        with open(os.path.join(self.repo, "findings", "HANDOFF.md"), "w") as f:
            f.write("line\n" * 150)
        self.git("commit", "-q", "-am", "HANDOFF: full")
        self.assertIsNone(self.run_hook())

    def test_uncommitted_changes_allow(self):
        with open(os.path.join(self.repo, "code.c"), "w") as f:
            f.write("wip\n")
        self.assertIsNone(self.run_hook())

    def test_second_stop_after_block_allows(self):
        self.commit("code.c", "fix")
        self.assertIsNone(self.run_hook(active=True))

    def test_not_a_repo_allows(self):
        other = tempfile.TemporaryDirectory()
        self.addCleanup(other.cleanup)
        self.repo = other.name
        self.assertIsNone(self.run_hook())


if __name__ == "__main__":
    unittest.main()
