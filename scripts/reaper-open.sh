#!/usr/bin/env bash
# reaper-open.sh - opens a REAPER project with the installed plug-in under gdb for SECONDS (default 40) and fails when
# REAPER crashes meanwhile (a regression check for host-side crashes, e.g. the two-instance reopen of round 0.8.42).
#   scripts/reaper-open.sh PROJECT.RPP [SECONDS]      (REAPER from $REAPER or ~/.local/opt/dl/reaper/...)
# Exit 0: no crash; 1: crash (backtrace in PROJECT.gdb.txt); 2: usage. REAPER opens on the desktop (needs $DISPLAY).
# Rule 2: opening a project sends what a DAW sends at a project load (the plug-in states), never stores.
set -euo pipefail
[ $# -ge 1 ] && [ -f "$1" ] || { echo "usage: $0 PROJECT.RPP [SECONDS]" >&2; exit 2; }
rpp=$(realpath "$1"); secs=${2:-40}
reaper=${REAPER:-$HOME/.local/opt/dl/reaper/reaper_linux_x86_64/REAPER/reaper}
log="${rpp%.*}.gdb.txt"
timeout -s INT "$secs" gdb -q -batch -ex "set pagination off" -ex "handle SIGPIPE nostop noprint" -ex run \
    -ex "bt 12" --args "$reaper" -nosplash -new "$rpp" >"$log" 2>&1 || true
if grep -q "received signal SIG\(SEGV\|ABRT\|BUS\|ILL\)" "$log"; then
    echo "CRASH: $(grep -m1 'received signal' "$log") - see $log"
    exit 1
fi
echo "no crash in ${secs} s ($(grep -c 'editor opened' "$log") editor(s) opened)"
