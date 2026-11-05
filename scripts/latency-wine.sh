#!/usr/bin/env bash
# latency-wine.sh - the original plug-in under Wine on this Linux PC, the Virus on the same USB path as TI Revive (E403):
# the 0.7.x wrapper (src/wrapper/vtiwrap.c) loads the user's Access "Virus TI.vst3" from the Wine prefix and answers its
# driver calls through the broker, because the Access USB driver cannot load under Wine. So this measures the original's
# logic without the VM's audio bursts, NOT the Access driver. vtihost plays a C4 every second, odd seconds flagged live.
#
#   scripts/latency-wine.sh [--blocks "32 64 128 256 512"] [--seconds 34] [--quiet 10] [--play-at 18] [--live alt|all|none]
#                           [--tag TEXT] [--out DIR] [--prefix WINEPREFIX]
set -uo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
blocks="32 64 128 256 512"; seconds=34; quiet=10; playat=18; live=alt; tag=""
prefix="$HOME/.wine"
out="$repo/work/latency/wine-$(date +%Y%m%d-%H%M%S)"
while [ $# -gt 0 ]; do
    case "$1" in
        --blocks) blocks=$2; shift 2 ;;
        --seconds) seconds=$2; shift 2 ;;
        --quiet) quiet=$2; shift 2 ;;
        --play-at) playat=$2; shift 2 ;;
        --live) live=$2; shift 2 ;;
        --tag) tag=-$2; shift 2 ;;
        --out) out=$2; shift 2 ;;
        --prefix) prefix=$2; shift 2 ;;
        *) echo "unknown: $1" >&2; exit 2 ;;
    esac
done
host="$repo/work/build/vtihost.exe"
wrapper="$repo/work/build/linux-wine/VirusTI_TIRevive.vst3"
[ -f "$host" ] && [ -f "$wrapper" ] || { echo "build first: make -C $repo/src/wrapper" >&2; exit 2; }
mkdir -p "$out"
export WINEPREFIX="$prefix" WINEDEBUG=-all
# The original reads its flash at open; the broker refuses those vendor requests (rule 2), so the wrapper answers them
# from this table, taken from a capture of the original on the same Virus in win10ref. Local, git-ignored, not rebuilt
# when it exists.
table="$repo/work/latency/flash-reads.txt"
if [ ! -s "$table" ]; then
    cap=$(ls "$repo"/captures/lat-orig-*-b256-altq.pcapng 2>/dev/null | head -1)
    [ -n "$cap" ] || { echo "no capture of the original for $table: run scripts/latency-orig.sh first" >&2; exit 2; }
    python3 -I "$repo/scripts/latency_measure.py" flash "$cap" "$(sed -n 's/^addr=//p' "${cap%.pcapng}.meta")" > "$table"
fi
export VTI_FLASH_REPLAY="Z:$(printf '%s' "$table" | tr / '\\')"
# Wine comes over TCP, which the broker only opens with VTI_BROKER_TCP=1 (it then makes ~/.config/tirevive/broker.token,
# 0600). One stop of the normal broker and one restart at the end, not per run: never loop broker stops (E394).
broker="$HOME/.local/lib/tirevive/vtibroker"
restore() {
    [ -n "${bp:-}" ] && kill "$bp" 2>/dev/null
    sleep 2
    systemctl --user start tirevive-broker.socket 2>/dev/null
}
systemctl --user stop tirevive-broker.socket tirevive-broker.service 2>/dev/null
sleep 3
VTI_BROKER_TCP=1 "$broker" >"$out/broker.log" 2>&1 &
bp=$!
trap restore EXIT
sleep 3
skip=$quiet
for block in $blocks; do
    name="b$block$tag"
    (
        cd "$out" || exit 1
        VTI_BLOCK=$block VTI_LIVE=$live VTI_PLAY_AT=$playat \
            timeout $((seconds + 120)) wine "$host" "Z:$(printf '%s' "$wrapper" | tr / '\\')" "$seconds" "$name.wav" "$quiet" >"$name.log" 2>&1
    )
    reported=$(sed -n 's/^latency \([0-9]*\) samples.*/\1/p' "$out/$name.log" | head -1)
    echo "=== block $block live=$live under Wine: the plug-in reports ${reported:-?} samples"
    if [ -s "$out/$name.wav" ]; then
        python3 -I "$repo/scripts/latency_measure.py" wav "$out/$name.wav" --first 11025 --skip "$skip" ${reported:+--reported "$reported"} \
            | tee "$out/$name.wav.txt" | grep -E '^(note|class)' | awk '/^note/ && $6!="-" {printf "%s:%s ", $2, $6} /^class/ {print ""; print} END {print ""}' | sed '/^$/d'
    else
        echo "no WAV; last lines of the log:"; grep -v CVirusBank "$out/$name.log" | tail -5 | cut -c1-140
    fi
    sleep 3
done
echo "files: $out"
