"""Tests for .claude/hooks/private-data.py (no private data in commits, pushes or GitHub posts).
Run: python3 -I -m unittest discover -s scripts/tests"""
import json
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
HOOK = os.path.join(HERE, "..", "..", ".claude", "hooks", "private-data.py")
ZERO = "0" * 40


class Base(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.repo = os.path.join(self.tmp.name, "repo")
        os.makedirs(os.path.join(self.repo, "work"))
        self.patterns = os.path.join(self.repo, "work", "private-patterns.txt")
        with open(self.patterns, "w") as f:
            f.write("# private words, one per line\n\nsecretname\nre:host-[0-9]+\n")
        self.git("init", "-q", "-b", "main")
        self.commit("a.txt", "start\n", "start")

    def tearDown(self):
        self.tmp.cleanup()

    def git(self, *args, check=True):
        env = dict(os.environ, GIT_AUTHOR_NAME="t", GIT_AUTHOR_EMAIL="t@t", GIT_COMMITTER_NAME="t",
                   GIT_COMMITTER_EMAIL="t@t")
        return subprocess.run(["git", "-C", self.repo, *args], check=check, env=env, capture_output=True,
                              text=True).stdout.strip()

    def write(self, path, text):
        with open(os.path.join(self.repo, path), "w") as f:
            f.write(text)

    def commit(self, path, text, message):
        self.write(path, text)
        self.git("add", path)
        self.git("commit", "-q", "--no-verify", "-m", message)
        return self.git("rev-parse", "HEAD")

    def run_hook(self, *args, stdin="", env_extra=None):
        env = dict(os.environ, CLAUDE_PROJECT_DIR=self.repo)
        env.pop("PRIVATE_PATTERNS", None)
        if env_extra:
            env.update(env_extra)
        return subprocess.run([sys.executable, "-I", HOOK, *args], input=stdin, capture_output=True, text=True,
                              cwd=self.repo, env=env)


class Rules(Base):
    """What counts as private: generic rules in the script, personal words from the ignored file."""

    def staged(self, text):
        self.write("b.txt", text)
        self.git("add", "b.txt")
        return self.run_hook("--git", "pre-commit")

    def test_clean_change_passes(self):
        self.assertEqual(self.staged("the Virus answers on endpoint 0x85\n").returncode, 0)

    def test_home_path_blocks(self):
        r = self.staged("log in /home/alice/virus/log.txt\n")
        self.assertEqual(r.returncode, 1)
        self.assertIn("b.txt", r.stderr)
        self.assertIn("/home/alice", r.stderr)

    def test_placeholder_homes_pass(self):
        self.assertEqual(self.staged("/home/runner/work /home/user/x /home/u/.local ~/x $HOME/x /home/<you>\n")
                         .returncode, 0)

    def test_mac_and_windows_homes_block(self):
        self.assertEqual(self.staged("/Users/bob/Library\n").returncode, 1)
        self.assertEqual(self.staged("C:\\Users\\bob\\AppData\n").returncode, 1)

    def test_shared_mac_and_windows_dirs_pass(self):
        self.assertEqual(self.staged("/Users/Shared/x C:\\Users\\Public\\x /Users/runner/x\n").returncode, 0)

    def test_private_ip_blocks(self):
        self.assertEqual(self.staged("ssh 192.168.1.20\n").returncode, 1)
        self.assertEqual(self.staged("host 172.20.0.5\n").returncode, 1)

    def test_version_numbers_pass(self):
        self.assertEqual(self.staged("macOS 10.15.7, OS 5.1.7.00, build 10.0.22631\n").returncode, 0)

    def test_email_blocks_but_project_and_noreply_pass(self):
        self.assertEqual(self.staged("mail me at jane.doe@mail.example.net\n").returncode, 1)
        self.assertEqual(self.staged("callmederpsn@proton.me 1+x@users.noreply.github.com a@example.com\n")
                         .returncode, 0)

    def test_personal_word_blocks_case_insensitive(self):
        r = self.staged("written by SecretName\n")
        self.assertEqual(r.returncode, 1)
        self.assertIn("private-patterns.txt", r.stderr)

    def test_personal_regex_blocks(self):
        self.assertEqual(self.staged("on host-42 today\n").returncode, 1)

    def test_comment_and_blank_lines_are_not_patterns(self):
        self.assertEqual(self.staged("# private words, one per line\n").returncode, 0)

    def test_removed_lines_do_not_count(self):
        self.commit("c.txt", "secretname\n", "old")
        self.write("c.txt", "clean\n")
        self.git("add", "c.txt")
        self.assertEqual(self.run_hook("--git", "pre-commit").returncode, 0)

    def test_missing_pattern_file_keeps_generic_rules(self):
        os.remove(self.patterns)
        self.assertEqual(self.staged("secretname\n").returncode, 0)
        self.assertEqual(self.staged("/home/alice/x\n").returncode, 1)

    def test_pattern_file_from_env(self):
        other = os.path.join(self.tmp.name, "p.txt")
        with open(other, "w") as f:
            f.write("otherword\n")
        self.write("b.txt", "otherword\n")
        self.git("add", "b.txt")
        self.assertEqual(self.run_hook("--git", "pre-commit", env_extra={"PRIVATE_PATTERNS": other}).returncode, 1)

    def test_override_env_lets_it_through(self):
        self.write("b.txt", "/home/alice/x\n")
        self.git("add", "b.txt")
        self.assertEqual(self.run_hook("--git", "pre-commit", env_extra={"PRIVATE_DATA_OK": "1"}).returncode, 0)


class GitHooks(Base):
    def test_commit_msg_blocks(self):
        msg = os.path.join(self.tmp.name, "MSG")
        with open(msg, "w") as f:
            f.write("Fix on host-7\n\n# a git comment line with secretname is ignored\n")
        self.assertEqual(self.run_hook("--git", "commit-msg", msg).returncode, 1)

    def test_commit_msg_ignores_git_comment_lines(self):
        msg = os.path.join(self.tmp.name, "MSG")
        with open(msg, "w") as f:
            f.write("Clean message\n# secretname in a comment\n")
        self.assertEqual(self.run_hook("--git", "commit-msg", msg).returncode, 0)

    def test_pre_push_scans_new_commits(self):
        base = self.git("rev-parse", "HEAD")
        head = self.commit("d.txt", "path /home/alice/x\n", "add d")
        r = self.run_hook("--git", "pre-push", "origin", "url",
                          stdin=f"refs/heads/main {head} refs/heads/main {base}\n")
        self.assertEqual(r.returncode, 1)
        self.assertIn("d.txt", r.stderr)

    def test_pre_push_scans_commit_messages(self):
        base = self.git("rev-parse", "HEAD")
        head = self.commit("d.txt", "clean\n", "message about secretname")
        r = self.run_hook("--git", "pre-push", "origin", "url",
                          stdin=f"refs/heads/main {head} refs/heads/main {base}\n")
        self.assertEqual(r.returncode, 1)

    def test_pre_push_new_branch_scans_only_its_own_commits(self):
        self.commit("old.txt", "/home/alice/old\n", "old leak already on the remote")
        self.git("update-ref", "refs/remotes/origin/main", "HEAD")
        self.git("checkout", "-q", "-b", "topic")
        head = self.commit("e.txt", "clean\n", "clean")
        r = self.run_hook("--git", "pre-push", "origin", "url",
                          stdin=f"refs/heads/topic {head} refs/heads/topic {ZERO}\n")
        self.assertEqual(r.returncode, 0, r.stderr)

    def test_pre_push_branch_delete_passes(self):
        r = self.run_hook("--git", "pre-push", "origin", "url",
                          stdin=f"(delete) {ZERO} refs/heads/topic {self.git('rev-parse', 'HEAD')}\n")
        self.assertEqual(r.returncode, 0)

    def test_install_writes_the_three_git_hooks(self):
        r = self.run_hook("--install")
        self.assertEqual(r.returncode, 0, r.stderr)
        hooks = self.git("rev-parse", "--git-path", "hooks")
        hooks = hooks if os.path.isabs(hooks) else os.path.join(self.repo, hooks)
        for name in ("pre-commit", "commit-msg", "pre-push"):
            path = os.path.join(hooks, name)
            self.assertTrue(os.access(path, os.X_OK), name)
            with open(path) as f:
                self.assertIn("private-data.py", f.read())


class ClaudeHook(Base):
    """PreToolUse (Bash): GitHub posts that do not go through git."""

    def bash(self, command):
        return self.run_hook(stdin=json.dumps({"tool_name": "Bash", "tool_input": {"command": command}}))

    def test_other_commands_pass(self):
        self.assertEqual(self.bash("ls /home/alice").returncode, 0)

    def test_gh_body_file_blocks(self):
        body = os.path.join(self.tmp.name, "reply.md")
        with open(body, "w") as f:
            f.write("Thanks! Logs are in /home/alice/logs\n")
        r = self.bash(f"gh issue comment 41 --body-file {body}")
        self.assertEqual(r.returncode, 2)
        self.assertIn("HANDOFF-private", r.stderr)

    def test_gh_short_body_file_flag_blocks(self):
        body = os.path.join(self.tmp.name, "reply.md")
        with open(body, "w") as f:
            f.write("seen on host-3\n")
        self.assertEqual(self.bash(f"gh pr create --title t -F {body}").returncode, 2)

    def test_gh_inline_body_blocks(self):
        self.assertEqual(self.bash('gh issue comment 41 --body "from secretname"').returncode, 2)

    def test_gh_clean_body_passes(self):
        body = os.path.join(self.tmp.name, "reply.md")
        with open(body, "w") as f:
            f.write("Thanks, rc3 fixed it.\n")
        self.assertEqual(self.bash(f"gh issue comment 41 --body-file {body}").returncode, 0)

    def test_gh_release_notes_file_blocks(self):
        body = os.path.join(self.tmp.name, "notes.md")
        with open(body, "w") as f:
            f.write("built on host-9\n")
        self.assertEqual(self.bash(f"gh release create v1 --notes-file {body}").returncode, 2)

    def test_gh_read_only_passes(self):
        self.assertEqual(self.bash("gh issue view 41 --json body").returncode, 0)

    def test_git_commit_inline_message_blocks(self):
        self.assertEqual(self.bash('git commit -m "fix from secretname"').returncode, 2)

    def test_bad_json_passes(self):
        self.assertEqual(self.run_hook(stdin="not json").returncode, 0)


if __name__ == "__main__":
    unittest.main()
