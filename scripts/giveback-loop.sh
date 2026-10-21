#!/bin/bash
# giveback-loop.sh - E312: repeated hand-backs of the Virus to snd-usb-audio on Linux. Each round runs vtiprobe (open +
# handshake + one RAM dump request, read-only for the synth), whose close is the broker's give-back (claim all five
# interfaces, port reset, release - giveback.h), then waits so snd-usb-audio probes the Virus and the reset guard's
# 5 s gap (E136) is over. Stops at the first round that cannot open the Virus or whose kernel log shows error -71.
# usage: giveback-loop.sh [rounds] [gap seconds] [vtiprobe]   (the broker's systemd socket is stopped meanwhile)
set -u
ROUNDS=${1:-30}
GAP=${2:-7}
PROBE=${3:-"$(cd "$(dirname "$0")/.." && pwd)/work/build/linux/vtiprobe"}
[ -x "$PROBE" ] || { echo "no vtiprobe at $PROBE (make -C src/broker)"; exit 1; }
systemctl --user stop tirevive-broker.socket tirevive-broker.service 2>/dev/null
trap 'systemctl --user start tirevive-broker.socket' EXIT
sleep 2
ok=0
for ((i = 1; i <= ROUNDS; i++)); do
    since=$(date '+%Y-%m-%d %H:%M:%S')
    if ! "$PROBE" >/dev/null 2>&1; then
        echo "$(date +%T) round $i: vtiprobe failed"
        journalctl -k --since "$since" --no-pager | grep -E "usb 1-|snd-usb-audio" | tail -6
        break
    fi
    sleep "$GAP"
    if journalctl -k --since "$since" --no-pager | grep -q -E "error -71|not accepting address|unable to enumerate"; then
        echo "$(date +%T) round $i: USB error after the give-back"
        journalctl -k --since "$since" --no-pager | grep -E "usb 1-|snd-usb-audio" | tail -6
        break
    fi
    ok=$i
    echo "$(date +%T) round $i ok"
done
echo "== $ok of $ROUNDS rounds without a USB error"
