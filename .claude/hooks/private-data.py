#!/usr/bin/env python3
"""Keeps private data out of everything that goes public: commits, pushes and GitHub posts.

Private things (correspondence, the maintainer's machines, accounts, backups, personal folders) belong in the
git-ignored work/HANDOFF-private.md, never in the repository, its history, issues or release notes.

Two layers, one rule set:
  git hooks   --git pre-commit | commit-msg FILE | pre-push REMOTE URL   (exit 1 stops git)
              Installed into the repository's hooks folder with --install. They see every commit, however it is
              made, and pre-push checks each new commit on its own, because a line added and removed again still
              ends up in the public history.
  Claude Code PreToolUse (Bash), JSON on stdin (exit 2 blocks the call): gh posts that never pass through git
              (issue/PR comments, PR and release texts, --body/--body-file/-F/--notes-file, gh api fields) and the
              message of an inline `git commit -m`.

Rules: generic ones below (home folders with a real user name, private IP addresses, e-mail addresses), plus the
personal words in work/private-patterns.txt (git-ignored, one per line; "re:" starts a regular expression; # starts
a comment). That file is private on purpose: listing the words here would publish them. PRIVATE_PATTERNS points to
another file. PRIVATE_DATA_OK=1 lets a deliberate case through; only the user decides that.
Only added lines are checked, so text that is already public does not block unrelated work.
"""
import json
import os
import re
import shlex
import stat
import subprocess
import sys

ZERO = "0" * 40
# The hook's own source and its test fixtures contain example paths and addresses on purpose.
EXEMPT_FILES = {".claude/hooks/private-data.py", "scripts/tests/test_private_data_hook.py"}

PLACEHOLDER_USERS = {"runner", "user", "u", "you", "me", "name", "username", "shared", "public", "default",
                     "example"}
NOT_EMAIL_TLDS = {"png", "jpg", "jpeg", "gif", "svg", "webp", "ico", "pdf"}
ALLOWED_EMAIL = re.compile(r"(noreply|derpsn|^git@github\.com$|@example\.(com|org)$)", re.I)

GENERIC = [
    ("home folder with a user name", re.compile(r"(?<![\w.])/home/([a-z_][a-z0-9_.-]*)")),
    ("macOS home folder with a user name", re.compile(r"(?<![\w.])/Users/([A-Za-z][\w.-]*)")),
    ("Windows home folder with a user name", re.compile(r"\b[A-Za-z]:\\{1,2}Users\\{1,2}([A-Za-z][\w.-]*)", re.I)),
    ("private IP address", re.compile(
        r"(?<![\d.])(?:192\.168|10\.\d{1,3}|172\.(?:1[6-9]|2\d|3[01]))\.\d{1,3}\.\d{1,3}(?![\d.])")),
    ("e-mail address", re.compile(r"[A-Za-z0-9._%+-]+@[A-Za-z0-9-]+(?:\.[A-Za-z0-9-]+)*\.([A-Za-z]{2,})\b")),
]

ADVICE = ("Move it to work/HANDOFF-private.md, or write it without the private part (~/ instead of a home folder, "
          "'the maintainer's NAS' instead of names). A deliberate case needs the user's OK, then PRIVATE_DATA_OK=1.")


def git(*args):
    r = subprocess.run(["git", *args], capture_output=True, text=True, errors="replace")
    return r.stdout if r.returncode == 0 else ""


def default_pattern_file():
    common = git("rev-parse", "--path-format=absolute", "--git-common-dir").strip()
    if common:
        return os.path.join(os.path.dirname(common.rstrip("/")), "work", "private-patterns.txt")
    root = os.environ.get("CLAUDE_PROJECT_DIR") or os.getcwd()
    return os.path.join(root, "work", "private-patterns.txt")


def load_personal(path):
    rules = []
    try:
        with open(path, encoding="utf-8") as f:
            lines = f.read().splitlines()
    except OSError:
        return rules
    for line in lines:
        word = line.strip()
        if not word or word.startswith("#"):
            continue
        rules.append(re.compile(word[3:] if word.startswith("re:") else re.escape(word), re.I))
    return rules


def find(text, personal):
    """Returns (rule, match) for every private thing in one line of text."""
    hits = []
    for name, rx in GENERIC:
        for m in rx.finditer(text):
            if name == "e-mail address":
                if m.group(1).lower() in NOT_EMAIL_TLDS or ALLOWED_EMAIL.search(m.group(0)):
                    continue
            elif "IP" not in name and m.group(1).lower() in PLACEHOLDER_USERS:
                continue
            hits.append((name, m.group(0)))
    for rx in personal:
        for m in rx.finditer(text):
            hits.append(("personal word from private-patterns.txt", m.group(0)))
    return hits


def added_lines(diff):
    """(file, line) for each added line of a unified diff."""
    path = None
    for line in diff.splitlines():
        if line.startswith("+++ "):
            path = line[6:] if line.startswith("+++ b/") else None
        elif line.startswith("+") and path is not None:
            yield path, line[1:]


def scan_diff(diff, personal, where=""):
    out = []
    for path, line in added_lines(diff):
        if path in EXEMPT_FILES:
            continue
        for rule, text in find(line, personal):
            out.append(f"{where}{path}: {rule}: {text}")
    return out


def scan_text(text, personal, where):
    return [f"{where}: {rule}: {hit}" for line in text.splitlines() for rule, hit in find(line, personal)]


def report(findings, code):
    if not findings or os.environ.get("PRIVATE_DATA_OK") == "1":
        return 0
    print("Private data check: this would make private data public:", file=sys.stderr)
    for f in findings[:30]:
        print("  " + f, file=sys.stderr)
    if len(findings) > 30:
        print(f"  ... and {len(findings) - 30} more", file=sys.stderr)
    print(ADVICE, file=sys.stderr)
    return code


# ---- git hooks ----

def commits_to_push(local, remote):
    known = remote != ZERO and subprocess.run(["git", "cat-file", "-e", remote + "^{commit}"],
                                              capture_output=True).returncode == 0
    revs = git("rev-list", local, "^" + remote) if known else git("rev-list", local, "--not", "--remotes")
    return revs.split()


def git_mode(args, personal):
    kind = args[0] if args else ""
    if kind == "pre-commit":
        return report(scan_diff(git("diff", "--cached", "-U0", "--no-color", "--no-ext-diff"), personal), 1)
    if kind == "commit-msg":
        with open(args[1], encoding="utf-8", errors="replace") as f:
            msg = "\n".join(l for l in f.read().splitlines() if not l.startswith("#"))
        return report(scan_text(msg, personal, "commit message"), 1)
    if kind == "pre-push":
        findings = []
        for line in sys.stdin.read().splitlines():
            parts = line.split()
            if len(parts) != 4 or parts[1] == ZERO:  # malformed, or a branch deletion
                continue
            for c in commits_to_push(parts[1], parts[3]):
                short = c[:9]
                findings += scan_text(git("log", "-1", "--format=%B", c), personal, f"commit {short} message")
                if len(git("rev-list", "--parents", "-1", c).split()) <= 2:  # merges: their own commits count
                    findings += scan_diff(git("diff-tree", "-p", "-U0", "--no-color", "--root", "--no-commit-id",
                                              c), personal, f"commit {short} ")
        return report(findings, 1)
    print(f"private-data.py: unknown git mode {kind!r}", file=sys.stderr)
    return 0


def install():
    hooks = git("rev-parse", "--path-format=absolute", "--git-path", "hooks").strip()
    if not hooks:
        print("private-data.py --install: not in a git repository", file=sys.stderr)
        return 1
    os.makedirs(hooks, exist_ok=True)
    me = os.path.abspath(__file__)
    for name in ("pre-commit", "commit-msg", "pre-push"):
        path = os.path.join(hooks, name)
        with open(path, "w") as f:
            f.write("#!/bin/sh\n# Installed by .claude/hooks/private-data.py --install: no private data in public.\n"
                    f'exec python3 -I "{me}" --git {name} "$@"\n')
        os.chmod(path, os.stat(path).st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
        print("installed " + path)
    return 0


# ---- Claude Code PreToolUse ----

BODY_FILE_FLAGS = {"--body-file", "-F", "--notes-file", "--input"}
BODY_FLAGS = {"--body", "-b", "--notes", "-n", "--title", "-t", "-m", "--message", "-f", "--field", "--raw-field"}


def segments(command):
    """The simple commands of a shell line, split at ; & | (quotes respected)."""
    lex = shlex.shlex(command, posix=True, punctuation_chars=";&|")
    lex.whitespace_split = True
    seg = []
    try:
        for tok in lex:
            if tok and set(tok) <= set(";&|"):
                if seg:
                    yield seg
                seg = []
            else:
                seg.append(tok)
    except ValueError:  # unbalanced quotes: the shell will complain, not us
        return
    if seg:
        yield seg


def read_file(path):
    try:
        with open(os.path.expanduser(path), encoding="utf-8", errors="replace") as f:
            return f.read()
    except OSError:
        return ""


def texts_to_check(seg):
    """The texts a gh or git commit command would publish (file contents, not their paths)."""
    while seg and re.match(r"^[A-Za-z_][A-Za-z0-9_]*=", seg[0]):  # leading VAR=value
        seg = seg[1:]
    if not seg:
        return []
    is_gh = seg[0] == "gh"
    is_commit = seg[0] == "git" and "commit" in seg[1:3]
    if not (is_gh or is_commit):
        return []
    out = []
    i = 1
    while i < len(seg):
        tok = seg[i]
        flag, eq, val = tok.partition("=")
        if flag in BODY_FILE_FLAGS | BODY_FLAGS and (is_gh or flag in ("-m", "--message")):
            arg = val if eq else (seg[i + 1] if i + 1 < len(seg) else "")
            if not eq:
                i += 1
            if flag in BODY_FILE_FLAGS:
                key, kv, v = arg.partition("=")
                if kv and flag == "-F":  # gh api -F key=@file or key=value
                    out.append(read_file(v[1:]) if v.startswith("@") else v)
                elif arg and arg != "-":
                    out.append(read_file(arg))
            else:
                out.append(arg)
        elif is_gh and not tok.startswith("-"):
            out.append(tok)
        i += 1
    return out


def claude_mode(personal):
    try:
        command = json.load(sys.stdin).get("tool_input", {}).get("command", "")
    except (ValueError, AttributeError):
        return 0
    if not command or not re.search(r"(^|[\s;&|(])(gh|git)\s", command):
        return 0
    findings = []
    for seg in segments(command):
        for text in texts_to_check(seg):
            findings += scan_text(text, personal, " ".join(seg[:2]))
    if "<<" in command and re.search(r"(^|[\s;&|(])gh\s", command):  # body from a heredoc
        findings += scan_text(command.split("<<", 1)[1].split("\n", 1)[-1], personal, "gh heredoc body")
    return report(findings, 2)


def main(argv):
    if argv[:1] == ["--install"]:
        return install()
    personal = load_personal(os.environ.get("PRIVATE_PATTERNS") or default_pattern_file())
    if argv[:1] == ["--git"]:
        return git_mode(argv[1:], personal)
    return claude_mode(personal)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
