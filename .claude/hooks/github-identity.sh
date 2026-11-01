#!/usr/bin/env bash
# PreToolUse (Bash): before anything talks to GitHub, make sure it runs as derpsn.
# This machine has two GitHub accounts in gh (derpsn and another one); git uses gh as its credential helper, so the
# active gh account decides who pushes. Blocks (exit 2) instead of switching: a switch is global and would change
# the account for other terminals too.
set -u
WANT=derpsn

cmd=$(python3 -c 'import json,sys; print(json.load(sys.stdin).get("tool_input",{}).get("command",""))' 2>/dev/null) || exit 0
[ -n "$cmd" ] || exit 0

# which commands reach GitHub (or create commits that will)
net='(^|[;&|({ ]|\$\()(gh |git ([^;&|]* )?(push|pull|fetch|clone|ls-remote|remote update|submodule update)|(curl|wget)[^;&|]*github)'
commit='(^|[;&|({ ]|\$\()git ([^;&|]* )?(commit|merge|tag|rebase|cherry-pick|am|revert)( |$)'
echo "$cmd" | grep -Eq "$net|$commit" || exit 0

block() { echo "GitHub identity check: $1" >&2; exit 2; }

if [ -n "${GH_TOKEN:-}${GITHUB_TOKEN:-}" ]; then
    who=$(gh api user --jq .login 2>/dev/null) || block "GH_TOKEN/GITHUB_TOKEN is set and its account could not be read. Unset it or make sure it belongs to $WANT."
    [ "$who" = "$WANT" ] || block "GH_TOKEN/GITHUB_TOKEN belongs to '$who', not $WANT. Unset it."
fi

active=$(gh config get -h github.com user 2>/dev/null)
[ "$active" = "$WANT" ] || block "the active gh account is '${active:-none}', not $WANT. Ask the user to run: gh auth switch -h github.com -u $WANT"

dir="${CLAUDE_PROJECT_DIR:-.}"
name=$(git -C "$dir" config user.name 2>/dev/null)
[ "$name" = "$WANT" ] || block "git user.name is '${name:-unset}', not $WANT (commits would carry the wrong identity). Ask the user to fix the git config."

exit 0
