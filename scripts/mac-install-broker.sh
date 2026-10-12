#!/bin/bash
# mac-install-broker.sh - install vtibroker as an on-demand LaunchAgent for the current user (macOS).
# launchd owns the broker socket /tmp/vtibroker-<uid>.sock and starts the broker on the first connect from
# TI Revive's libVirusUSB.dylib; the broker exits after 30 s without clients and launchd starts it again on demand.
# Running in the user's GUI session, the broker's MIDI port (VirusTIMDP..., E100) is visible to the DAW.
# usage: mac-install-broker.sh [path/to/vtibroker]   (default: ~/tirevive/build/vtibroker)
#        mac-install-broker.sh --uninstall
set -e
LABEL=com.tirevive.vtibroker
PLIST="$HOME/Library/LaunchAgents/$LABEL.plist"
DOMAIN="gui/$(id -u)"
launchctl bootout "$DOMAIN/$LABEL" 2>/dev/null || true
if [ "$1" = "--uninstall" ]; then
    rm -f "$PLIST"
    echo "removed $LABEL"
    exit 0
fi
BIN="${1:-$HOME/tirevive/build/vtibroker}"
[ -x "$BIN" ] || { echo "no broker at $BIN"; exit 1; }
SOCK="/tmp/vtibroker-$(id -u).sock"
pkill -x vtibroker 2>/dev/null || true
rm -f "$SOCK"
mkdir -p "$HOME/Library/LaunchAgents"
cat > "$PLIST" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key><string>$LABEL</string>
    <key>ProgramArguments</key><array><string>$BIN</string></array>
    <key>Sockets</key>
    <dict>
        <key>Listeners</key>
        <dict>
            <key>SockPathName</key><string>$SOCK</string>
            <key>SockPathMode</key><integer>384</integer>
        </dict>
    </dict>
    <key>StandardErrorPath</key><string>/tmp/vtibroker.log</string>
    <key>ProcessType</key><string>Interactive</string>
</dict>
</plist>
PLIST
launchctl bootstrap "$DOMAIN" "$PLIST"
echo "installed $LABEL ($BIN, socket $SOCK)"
