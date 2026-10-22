#!/bin/bash
# hil-mac.command - the automated hardware run of scripts/hil.sh on a Mac (findings/testing.md): the installed plug-in
# in nativehost (no DAW), C4 every second on part 1, Out 1 recorded, graded by hil_check.py --folder. Covers MC1 (cold
# start of the broker), MC2 (reopen after the broker's idle exit, twice), MC3 (export, paced to the Virus, playback
# after it) and H6 (the broker killed while playing). Not covered: a DAW's behaviour (Live, Logic), the editor.
# Double-click it in Finder (Terminal): over ssh the plug-in hears only silence - macOS gives an ssh session no
# Microphone access (E103). The first run asks for Microphone access for Terminal: allow it.
# Needs next to it: nativehost, hil_check.py, timing_analysis.py (the CI artifact TI-Revive-hil-mac).
# Rule 2: sends only what the plug-in sends in a DAW (start-up, the part states, notes), never stores anything.
set -uo pipefail
here=$(cd "$(dirname "$0")" && pwd)
plugin="/Library/Audio/Plug-Ins/VST3/TI Revive Virus Control.vst3"
host="$here/nativehost"
out="$here/results-$(date +%Y%m%d-%H%M%S)"
fails=0

finish() { echo; read -r -p "Press Enter to close this window. " _; exit "$1"; }

[ -x "$host" ] || { echo "no nativehost next to this script ($here)"; finish 2; }
[ -d "$plugin" ] || { echo "TI Revive is not installed ($plugin missing)"; finish 2; }
ioreg -p IOUSB -l 2>/dev/null | grep -q '"idVendor" = 4926' || { echo "no Virus TI on USB"; finish 2; }
# the broker serves one owner: a DAW with the plug-in would make this run wait (and the run would take its Virus)
if pgrep -x -i 'Live|Logic Pro|Logic Pro X|REAPER|Bitwig Studio|nativehost|TI Revive Control Center' >/dev/null; then
    echo "a DAW, the Control Center or nativehost is running: close it first"; finish 2
fi
mkdir -p "$out"
summary="$out/summary.txt"
say() { echo "$*" | tee -a "$summary"; }

# the broker exits 30 s after its last client; the plug-in starts it again on the next connect
broker_gone() {
    local t=0
    while pgrep -x vtibroker >/dev/null; do
        [ $t -ge 120 ] && { say "  broker still running after 120 s"; return 1; }
        sleep 2; t=$((t + 2))
    done
}

# run_item NAME SECONDS [nativehost args...]: play and record (macOS has no timeout(1): a watchdog instead)
run_item() {
    local name=$1 secs=$2; shift 2
    "$host" "$plugin" --notes --seconds "$secs" --wav "$out/$name.wav" "$@" >"$out/$name.out" 2>"$out/$name.err" &
    local pid=$!
    (sleep $((secs + 60)); kill "$pid" 2>/dev/null) &
    local dog=$!
    wait "$pid"; local rc=$?
    kill "$dog" 2>/dev/null; wait "$dog" 2>/dev/null
    [ $rc -eq 0 ] || say "  nativehost exit $rc"
    return $rc
}

say "TI Revive hardware run (Mac) $(date '+%Y-%m-%d %H:%M:%S'), macOS $(sw_vers -productVersion) $(uname -m)"
say "plug-in $(stat -f %Sm -t '%Y-%m-%d %H:%M' "$plugin/Contents/MacOS/"* 2>/dev/null | head -1), results in $out"
say "about 5 minutes; keep the Virus on and this Mac awake"

say "H1 cold start of the broker"
broker_gone && run_item H1 20 || fails=$((fails + 1))
say "H2.1 reopen after the broker's idle exit"
broker_gone && run_item H2-1 20 || fails=$((fails + 1))
say "H2.2 reopen after the broker's idle exit"
broker_gone && run_item H2-2 20 || fails=$((fails + 1))
say "H3 export: offline from 12 s to 36 s, stopped until 40 s, playback to 54 s"
run_item H3 54 --offline 12 --offline-end 36 --notes-off 36:40 || fails=$((fails + 1))
say "H6 broker killed at 20 s while playing, playback to 60 s"
run_item H6 60 &
item=$!
sleep 20; pkill -TERM -x vtibroker
wait $item || fails=$((fails + 1))

say ""
if python3 -c 'import sys; sys.exit(sys.version_info < (3, 6))' 2>/dev/null; then
    python3 -I "$here/hil_check.py" --folder "$out" | tee -a "$summary"
    [ "${PIPESTATUS[0]}" -eq 0 ] || fails=$((fails + 1))
    if grep -q "^H1: FAIL" "$summary"; then
        say "H1 silent? System Settings > Privacy & Security > Microphone: allow Terminal, then run this again"
    fi
else
    say "no Python 3 here: send the folder $out to be graded"
fi
say "$([ $fails -eq 0 ] && echo "done, nothing failed" || echo "$fails step(s) failed") - results in $out"
finish $((fails > 0))
