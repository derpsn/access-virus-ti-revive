#!/usr/bin/env bash
# latency-orig.sh - measure the original plug-in in the win10ref VM, without hands (E400, #30): vtihost plays a C4 every
# second, the odd seconds flagged as played live (VTI_LIVE=alt); usbmon records the wire here on the Linux host, the
# plug-in's output comes back as a WAV. Needs win10ref running with the Virus attached (VM=win10ref scripts/vm.sh attach).
#
#   scripts/latency-orig.sh [--blocks "32 64 128 256 512"] [--seconds 45] [--quiet S] [--stopped] [--play-at S] [--live all|alt|none]
#                           [--out DIR] [--tag TEXT] [--no-capture]
# Prints per run: the latency the plug-in reports, then scripts/latency_measure.py for the WAV and the capture.
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
export VM=win10ref
vm() { "$repo/scripts/vm.sh" "$@"; }
blocks="32 64 128 256 512"; seconds=45; stopped=0; live=alt; tag=""; capture=1; quiet=0; playat=""
out="$repo/work/latency/$(date +%Y%m%d-%H%M%S)"
while [ $# -gt 0 ]; do
    case "$1" in
        --blocks) blocks=$2; shift 2 ;;
        --seconds) seconds=$2; shift 2 ;;
        --quiet) quiet=$2; shift 2 ;;
        --stopped) stopped=1; shift ;;
        --play-at) playat=$2; shift 2 ;;
        --live) live=$2; shift 2 ;;
        --out) out=$2; shift 2 ;;
        --tag) tag=-$2; shift 2 ;;
        --no-capture) capture=0; shift ;;
        *) echo "unknown: $1" >&2; exit 2 ;;
    esac
done
mkdir -p "$out"
skip=$(( (seconds + quiet) / 2 )) # the plug-in's start-up (drops notes) is over by then; checked against the "note" lines

vm ps 'New-Item -ItemType Directory -Force C:\ref\lat | Out-Null' >/dev/null
vm push "$repo/work/build/vtihost.exe" 'C:/ref/lat/vtihost.exe' >/dev/null
# the console session: the Access plug-in needs its message loop and windows there (same as the vti-shot task)
vm ps '$a = New-ScheduledTaskAction -Execute "cmd.exe" -Argument "/c C:\ref\lat\run.cmd"; $p = New-ScheduledTaskPrincipal -UserId vti -LogonType Interactive -RunLevel Highest; Register-ScheduledTask -TaskName vti-lat -Action $a -Principal $p -Force | Out-Null' >/dev/null

for block in $blocks; do
    state=""
    if [ "$stopped" = 1 ]; then state=-stopped; fi
    name="b$block$state$tag"
    {
        printf '@echo off\r\ncd /d C:\\ref\\lat\r\nset VTI_BLOCK=%s\r\n' "$block"
        [ "$live" = none ] || printf 'set VTI_LIVE=%s\r\n' "$live"
        [ "$stopped" = 1 ] && printf 'set VTI_STOPPED=1\r\n'
        [ -n "$playat" ] && printf 'set VTI_PLAY_AT=%s\r\n' "$playat"
        printf 'del %s.done 2>nul\r\n' "$name"
        printf 'vtihost.exe "C:\\Program Files\\Common Files\\VST3\\Virus TI.vst3" %s %s.wav %s > %s.log 2>&1\r\n' "$seconds" "$name" "$quiet" "$name"
        printf 'echo done > %s.done\r\n' "$name"
    } > "$out/run.cmd"
    vm push "$out/run.cmd" 'C:/ref/lat/run.cmd' >/dev/null
    capname="lat-orig-$(basename "$out")-$name"
    if [ "$capture" = 1 ]; then
        "$repo/scripts/capture.sh" "$capname" $((seconds + 20)) > "$out/$name.capture.txt" 2>&1 &
        cap=$!
        sleep 2
    fi
    vm ps 'Start-ScheduledTask vti-lat' >/dev/null
    for _ in $(seq 1 $((seconds + 60))); do
        sleep 1
        vm ps "Test-Path C:\\ref\\lat\\$name.done" 2>/dev/null | grep -q True && break
    done
    vm pull "C:/ref/lat/$name.wav" "$out/" >/dev/null
    vm pull "C:/ref/lat/$name.log" "$out/" >/dev/null
    [ "$capture" = 1 ] && { wait "$cap" || true; }

    reported=$(sed -n 's/^latency \([0-9]*\) samples.*/\1/p' "$out/$name.log" | head -1)
    echo "=== block $block$state live=$live: the plug-in reports ${reported:-?} samples"
    python3 -I "$repo/scripts/latency_measure.py" wav "$out/$name.wav" --first 11025 --skip "$skip" ${reported:+--reported "$reported"} \
        | tee "$out/$name.wav.txt" | grep '^class'
    if [ "$capture" = 1 ]; then
        addr=$(sed -n 's/^addr=//p' "$repo/captures/$capname.meta")
        python3 -I "$repo/scripts/latency_measure.py" usb "$repo/captures/$capname.pcapng" "$addr" --first 11025 \
            | tee "$out/$name.usb.txt" | grep -E '^class'
    fi
done
echo "files: $out"
