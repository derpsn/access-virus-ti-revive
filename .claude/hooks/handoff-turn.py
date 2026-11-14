#!/usr/bin/env python3
"""Stop hook helper for findings/HANDOFF.md: did this turn change state without touching HANDOFF?

Called by handoff-check.sh with the Stop event on stdin. Reads the transcript (JSONL) from the last real user prompt
on. A turn "changed state" when it edited a file in the repository (Edit/Write/NotebookEdit; not the scratchpad,
memory or other folders) or ran an outward/recording command (git commit/push/tag/merge, gh issue/pr
comment/create/close/edit, gh release, gh workflow run, gh run rerun). HANDOFF counts as touched when a tool call
names findings/HANDOFF.md (Edit/Write path, or a Bash command naming it that is not just a read). Prints a reason when
state changed and HANDOFF was not touched, nothing otherwise. Any error prints nothing: a broken hook must never trap
a session (user 2026-11-14: "after every finished task check if handoff is updated so in case I do a clear
everything is already set where to start").
"""
import json
import os
import re
import sys

OUTWARD = re.compile(
    r"\bgit\s+(commit|push|tag|merge)\b"
    r"|\bgh\s+(issue|pr)\s+(comment|create|close|reopen|edit|merge)\b"
    r"|\bgh\s+release\s+(create|edit|delete)\b"
    r"|\bgh\s+workflow\s+run\b"
    r"|\bgh\s+run\s+rerun\b"
)
# a command that only reads HANDOFF (cat/grep/sed -n ... HANDOFF.md) does not count as an update
READ_ONLY = re.compile(r"^\s*(cd [^;&|]*[;&|]+\s*)?(cat|grep|sed -n|head|tail|wc|awk|less)\b[^>;&|]*HANDOFF\.md[^>;&|]*$")


def is_prompt(row):
    if row.get("type") != "user":
        return False
    c = row.get("message", {}).get("content")
    if isinstance(c, str):
        return True
    if isinstance(c, list):
        return not any(isinstance(x, dict) and x.get("type") == "tool_result" for x in c)
    return False


def tool_uses(rows):
    for r in rows:
        if r.get("type") != "assistant":
            continue
        c = r.get("message", {}).get("content")
        if isinstance(c, list):
            for x in c:
                if isinstance(x, dict) and x.get("type") == "tool_use":
                    yield x.get("name") or "", x.get("input") or {}


def check(rows, project):
    start = max((i for i, r in enumerate(rows) if is_prompt(r)), default=-1)
    project = os.path.realpath(project)
    handoff = os.path.join(project, "findings", "HANDOFF.md")
    changed, touched = [], False
    for name, inp in tool_uses(rows[start + 1:]):
        if name in ("Edit", "Write", "NotebookEdit", "MultiEdit"):
            path = os.path.realpath(inp.get("file_path") or inp.get("notebook_path") or "")
            if path == handoff:
                touched = True
            elif path.startswith(project + os.sep):
                changed.append(os.path.relpath(path, project))
        elif name == "Bash":
            cmd = inp.get("command") or ""
            if "HANDOFF.md" in cmd and not READ_ONLY.search(cmd):
                touched = True
            m = OUTWARD.search(cmd)
            if m:
                changed.append(re.sub(r"\s+", " ", m.group(0)))
    if changed and not touched:
        seen = list(dict.fromkeys(changed))[:6]
        return ("This turn changed state (" + ", ".join(seen) + ") but did not touch findings/HANDOFF.md. "
                "Check that HANDOFF still says where to start after a /clear (round, open items, who waits on whom, "
                "NEXT; details go to findings/log/), then update and commit it. If nothing in it needs to change, "
                "say so in one line and stop.")
    return ""


def main():
    try:
        event = json.load(sys.stdin)
        transcript = event.get("transcript_path")
        if not transcript or not os.path.exists(transcript):
            return
        with open(transcript, encoding="utf-8") as f:
            rows = [json.loads(line) for line in f if line.strip()]
        print(check(rows, os.environ.get("CLAUDE_PROJECT_DIR") or event.get("cwd") or "."), end="")
    except Exception:
        pass


if __name__ == "__main__":
    main()
