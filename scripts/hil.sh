#!/usr/bin/env bash
# hil.sh - the Linux core checklist items as an automated hardware run (findings/testing.md): the installed plug-in in
# nativehost (no DAW), C4 every second on part 1, Out 1 recorded and checked by hil_check.py against the grid and
# the plug-in's log. Covers LC1 (cold start of the broker), LC2 (reopen after the broker's idle exit, twice), LC3
# (export, paced to the Virus, playback after it), with --power LC4 (Virus off and on) and with --sleep LC5
# (suspend with the plug-in loaded), H6 the broker killed while playing. Not covered: a DAW's behaviour (REAPER, PipeWire), the editor.
#   scripts/hil.sh [--plugin PATH.vst3] [--host nativehost] [--out DIR] [--sleep] [--power CMD]
# --power CMD (or TIREVIVE_POWER): a command that switches the Virus' mains with "off" / "on" as its argument
# (e.g. a script calling a switchable plug's local API); then H4 = LC4 runs too. Not tried with a real plug yet.
# Rule 2: sends only what the plug-in sends in a DAW (start-up, the part states, notes); it replaces the Virus' edit
# buffers like opening a project does, never stores anything. --sleep suspends this PC for 45 s (sudo rtcwake sets
# the alarm, systemctl suspend sleeps; H5 fails when the PC did not really sleep).
# Exit 0 when every item passes.
set -uo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
plugin="$HOME/.vst3/TI Revive Virus Control.vst3"
host="$repo/build-native/bin/Release/nativehost"
out="$repo/work/hil/$(date +%Y%m%d-%H%M%S)"
sleep_test=0
power=${TIREVIVE_POWER:-}
while [ $# -gt 0 ]; do
    case $1 in
        --plugin) plugin=$2; shift 2 ;;
        --host) host=$2; shift 2 ;;
        --out) out=$2; shift 2 ;;
        --sleep) sleep_test=1; shift ;;
        --power) power=$2; shift 2 ;;
        *) sed -n '2,15p' "$0"; exit 2 ;;
    esac
done

[ -x "$host" ] || { echo "no nativehost at $host (cmake --build build-native --target nativehost)"; exit 2; }
[ -d "$plugin" ] || { echo "no plug-in at $plugin"; exit 2; }
grep -qs 133e /sys/bus/usb/devices/*/idVendor || { echo "no Virus TI on USB"; exit 2; }
# the broker serves one owner: a DAW with the plug-in would make this run wait (and the run would take its Virus).
# Steam's game launcher is called "reaper" too: only processes outside Steam count
daw_running() {
    local pid
    for pid in $(pgrep -x -i 'reaper|bitwig-studio|ardour[0-9]*|nativehost'); do
        case "$(readlink -f /proc/"$pid"/exe 2>/dev/null)" in */Steam/*) ;; *) return 0 ;; esac
    done
    return 1
}
if daw_running; then
    echo "a DAW or nativehost is running: close it first"; exit 2
fi
mkdir -p "$out"
summary="$out/summary.txt"
fails=0
say() { echo "$*" | tee -a "$summary"; }

# the broker exits 30 s after its last client (IDLE_EXIT_S); the next connect starts it fresh via systemd
broker_gone() {
    local t=0
    while pgrep -x vtibroker >/dev/null; do
        [ $t -ge 120 ] && { say "  broker still running after 120 s"; return 1; }
        sleep 2; t=$((t + 2))
    done
}

# seconds this PC has been suspended since boot: CLOCK_BOOTTIME counts the sleep, CLOCK_MONOTONIC does not
slept_s() { python3 -c 'import time; print(int(time.clock_gettime(time.CLOCK_BOOTTIME) - time.monotonic()))'; }

# run NAME SECONDS [nativehost args...]: play, record, check
run() {
    local name=$1 secs=$2; shift 2
    timeout $((secs + 60)) "$host" "$plugin" --notes --seconds "$secs" --wav "$out/$name.wav" "$@" \
        >"$out/$name.out" 2>"$out/$name.err"
    local rc=$?
    [ $rc -eq 0 ] || say "  nativehost exit $rc"
    return $rc
}

fail() { say "$1: FAIL"; fails=$((fails + 1)); }

check() {
    local name=$1; shift
    if python3 -I "$repo/scripts/hil_check.py" --log "$out/$name.err" --wav "$out/$name.wav" "$@" | tee -a "$summary" &&
        [ "${PIPESTATUS[0]}" -eq 0 ]; then
        say "$name: pass"
    else
        fail "$name"
    fi
}

say "TI Revive hardware run $(date -Is), plug-in $(stat -c %y "$plugin/Contents/x86_64-linux/"*.so 2>/dev/null | cut -c1-19)"
say "results in $out"

say "H1 cold start of the broker (LC1 without the Virus power cycle)"
if broker_gone && run H1 20; then check H1 --seconds 20; else fail H1; fi

for i in 1 2; do
    say "H2.$i reopen after the broker's idle exit (LC2)"
    if broker_gone && run "H2-$i" 20; then check "H2-$i" --seconds 20; else fail "H2-$i"; fi
done

# a DAW stops after a render and plays again later: 4 s of real-time blocks without notes between the two (going
# straight from the render into notes is fine too since nativehost waits one block at the switch, E286)
say "H3 export: offline from 12 s to 36 s, stopped until 40 s, playback to 54 s (LC3; E243: the clock after an export)"
if run H3 54 --offline 12 --offline-end 36 --notes-off 36:40; then
    check H3 --seconds 54 --offline "$out/H3.out" --gap 36:40
else
    fail H3
fi

if [ -n "$power" ]; then
    say "H4 Virus off and on while playing (LC4, mains switched by: $power)"
    run H4 75 & pid=$!
    sleep 20; $power off >>"$out/H4.power" 2>&1
    sleep 10; $power on >>"$out/H4.power" 2>&1
    # the Virus boots (~10 s), the plug-in sets up again: judged from 25 s after switching on
    if wait $pid; then check H4 --seconds 75 --sleep --from 55; else fail H4; fi
fi

# the broker exits while the plug-in plays (a crash, an update, a restart; systemd starts it again on the next
# connect): the plug-in reconnects, sets up the Virus again and plays on (review 2026-10-19 H3)
say "H6 broker killed at 20 s while playing, playback to 60 s"
run H6 60 & pid=$!
sleep 20; pkill -TERM -x vtibroker
# judged from 34 s on: reconnect, flash read (~3 s), state and probe (~4 s), marker
if wait $pid; then check H6 --seconds 60 --from 34; else fail H6; fi

if [ $sleep_test = 1 ]; then
    say "H5 suspend for 45 s with the plug-in playing (LC5)"
    if sudo -v; then
        run H5 150 & pid=$!
        sleep 25
        # through systemd, not `rtcwake -m mem`: that skips nvidia-suspend.service, the NVIDIA driver refuses and the
        # kernel is back after 0.7 s (2026-10-20: H5 passed on a USB reset, not a sleep). rtcwake only sets the alarm.
        slept0=$(slept_s)
        sudo rtcwake -m no -s 45 >"$out/H5.rtcwake" 2>&1 && systemctl suspend >>"$out/H5.rtcwake" 2>&1
        t=0
        while [ $(( $(slept_s) - slept0 )) -lt 30 ] && [ $t -lt 90 ]; do sleep 1; t=$((t + 1)); done
        slept=$(( $(slept_s) - slept0 ))
        if [ $slept -ge 30 ]; then say "  pass  suspend: the PC slept ${slept} s"
        else say "  FAIL  suspend: the PC slept only ${slept} s (see H5.rtcwake, journalctl 'PM: suspend')"; fails=$((fails + 1)); fi
        # judged from 30 s after the wake on: the plug-in sets up again (stood-still restart), the host catches up
        if wait $pid; then check H5 --seconds 150 --sleep --from 100; else fail H5; fi
    else
        say "H5: not run (no sudo)"; fails=$((fails + 1))
    fi
fi

say "$( [ $fails -eq 0 ] && echo "ALL PASS" || echo "$fails item(s) FAILED")"
exit $((fails > 0))
