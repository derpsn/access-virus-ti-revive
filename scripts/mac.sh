#!/usr/bin/env bash
# mac.sh - drive the macOS test machine (Apple Silicon) over SSH, like vm.sh for the Windows VMs.
# Host and user come from the local, untracked ~/.config/tirevive/mac.env:  MAC_HOST=192.168.x.y  MAC_USER=name
# usage: mac.sh ssh [cmd...] | run 'shell code' | push LOCAL REMOTE | pull REMOTE LOCAL | recon
set -euo pipefail
# MAC=<name> picks a second Mac from ~/.config/tirevive/mac-<name>.env (e.g. MAC=bigsur, an Intel test Mac on 11.7)
if [ -n "${MAC:-}" ]; then
    [ -f ~/.config/tirevive/mac-"$MAC".env ] || { echo "no ~/.config/tirevive/mac-$MAC.env"; exit 1; }
    . ~/.config/tirevive/mac-"$MAC".env
else
    [ -f ~/.config/tirevive/mac.env ] && . ~/.config/tirevive/mac.env
fi
: "${MAC_HOST:?set MAC_HOST in ~/.config/tirevive/mac.env}" "${MAC_USER:?set MAC_USER in ~/.config/tirevive/mac.env}"
KEY=~/.ssh/mac_re
SSH=(ssh -i "$KEY" -o StrictHostKeyChecking=accept-new -o LogLevel=ERROR "$MAC_USER@$MAC_HOST")
case "${1:-}" in
  ssh)   shift; "${SSH[@]}" "$@" ;;
  run)   shift; "${SSH[@]}" "bash -lc $(printf '%q' "$*")" ;;
  push)  scp -i "$KEY" -r "$2" "$MAC_USER@$MAC_HOST:$3" ;;
  pull)  scp -i "$KEY" -r "$MAC_USER@$MAC_HOST:$2" "$3" ;;
  recon) "${SSH[@]}" 'bash -s' < "$(dirname "$0")/mac-recon.sh" ;;
  *) echo "usage: mac.sh ssh [cmd] | run 'code' | push L R | pull R L | recon"; exit 1 ;;
esac
