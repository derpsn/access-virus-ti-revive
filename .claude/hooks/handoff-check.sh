#!/usr/bin/env bash
# Claude Code Stop hook for findings/HANDOFF.md (the current state, findings/log/ holds the history):
# 1. a turn may not end while commits sit after the last commit that touched HANDOFF.md, so the state never falls
#    behind the work (resumable sessions). Uncommitted work does not count: mid-task turns end with a dirty tree.
# 2. HANDOFF.md may not grow past MAX_LINES: it is the state, not a journal (finished items go to findings/log/).
# Any failure (no git, no repo, no HANDOFF) allows the stop: a broken hook must never trap a session.
MAX_LINES=150
event=$(cat)

# Second stop after a block: allow it, else a refused update would loop forever.
if printf '%s' "$event" | grep -q '"stop_hook_active"[[:space:]]*:[[:space:]]*true'; then
    exit 0
fi

cd "${CLAUDE_PROJECT_DIR:-.}" 2>/dev/null || exit 0
[ -f findings/HANDOFF.md ] || exit 0
reason=""

last=$(git log -1 --format=%H -- findings/HANDOFF.md 2>/dev/null)
if [ -n "$last" ]; then
    count=$(git rev-list --count "$last..HEAD" 2>/dev/null) || count=0
    if [ "$count" -gt 0 ]; then
        newest=$(git log -1 --format=%s HEAD)
        [ "$count" -eq 1 ] && word=commit || word=commits
        reason="$count $word since the last HANDOFF update (newest: $newest). Update findings/HANDOFF.md (state, open items, NEXT; details go to findings/log/) and commit it. Next time put the HANDOFF change into the work commit itself instead of a separate HANDOFF-only commit."
    fi
fi

lines=$(wc -l < findings/HANDOFF.md 2>/dev/null) || lines=0
if [ "$lines" -gt "$MAX_LINES" ]; then
    reason="${reason:+$reason }findings/HANDOFF.md has $lines lines (limit $MAX_LINES): it holds the current state only. Move finished or historical items to the dated log in findings/log/ and keep HANDOFF short."
fi

# 3. this turn changed state (repo edits, commits, pushes, GitHub posts, CI runs) without touching HANDOFF.md:
#    a /clear must find the current state there (user 2026-11-14). Checked from the transcript, see handoff-turn.py.
if [ -z "$reason" ]; then
    turn=$(printf '%s' "$event" | python3 -I "$(dirname "$0")/handoff-turn.py" 2>/dev/null) || turn=""
    reason="$turn"
fi

[ -n "$reason" ] || exit 0
python3 -I -c 'import json, sys; print(json.dumps({"decision": "block", "reason": sys.argv[1]}))' "$reason"
