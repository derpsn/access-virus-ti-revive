#!/usr/bin/env bash
# latency-native.sh - the same measurement as latency-orig.sh for TI Revive, on Linux with the real Virus (E400, #30):
# nativehost plays a C4 at the start of every second, Out 1 comes back as a WAV. The plug-in's lead can be overridden
# with --a (VTI_NATIVE_A, a developer switch that stays in this process): --a 0 shows what a note costs without the
# lead, which is the floor for a live path, while nothing in the plug-in has to change for it.
#
#   scripts/latency-native.sh [--blocks "32 64 128 256 512"] [--seconds 30] [--a N] [--rate 44100|48000] [--tag TEXT] [--out DIR]
set -uo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
plugin="$HOME/.vst3/TI Revive Virus Control.vst3"
host="$repo/build-native/bin/Release/nativehost"
blocks="32 64 128 256 512"; seconds=30; a=3072; tag=""; override=0; rate=44100
out="$repo/work/latency/native-$(date +%Y%m%d-%H%M%S)"
while [ $# -gt 0 ]; do
    case "$1" in
        --blocks) blocks=$2; shift 2 ;;
        --seconds) seconds=$2; shift 2 ;;
        --a) a=$2; override=1; shift 2 ;;
        --rate) rate=$2; shift 2 ;;
        --tag) tag=-$2; shift 2 ;;
        --out) out=$2; shift 2 ;;
        *) echo "unknown: $1" >&2; exit 2 ;;
    esac
done
[ -x "$host" ] || { echo "no nativehost at $host" >&2; exit 2; }
mkdir -p "$out"
for block in $blocks; do
    name="b$block$tag"
    # latency_frames() in virusplugin.h at the Virus' rate (reported_frames, stamprule.h; since 0.8.39 no 256 floor)
    reported=$(( a + block + ${VTI_NATIVE_MARGIN:-512} ))
    if [ "$override" = 1 ]; then
        VTI_NATIVE_A=$a timeout $((seconds + 60)) "$host" "$plugin" --notes --block "$block" --rate "$rate" --seconds "$seconds" --wav "$out/$name.wav" >"$out/$name.out" 2>"$out/$name.err"
    else
        timeout $((seconds + 60)) "$host" "$plugin" --notes --block "$block" --rate "$rate" --seconds "$seconds" --wav "$out/$name.wav" >"$out/$name.out" 2>"$out/$name.err"
    fi
    echo "=== block $block, A=$a: reported $reported frames"
    python3 -I "$repo/scripts/latency_measure.py" wav "$out/$name.wav" --first 0 --period "$rate" --classes 1 --skip 10 --reported "$reported" \
        | tee "$out/$name.wav.txt" | grep '^class'
    sleep 3 # the broker's idle exit is not looped here: one client after the other (E394)
done
echo "files: $out"
