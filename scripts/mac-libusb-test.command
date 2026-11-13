#!/bin/bash
# TI Revive libusb-audio test (branch libusb-audio, E511): the TI Revive service takes the Virus' audio itself
# instead of Apple's USB audio driver and Core Audio, as on Linux. Needs the test build of this branch installed.
# It needs root (macOS lets only root take the Virus from Apple's driver), so it asks for your password.
#
# 1. Quit your DAW and the TI Revive Control Center. 2. Double-click this file. 3. Start your DAW (its audio device:
# NOT the Virus TI, e.g. the Mac's built-in output) and load TI Revive Virus Control. 4. When done: quit the DAW, then
# Ctrl-C in this window. The Virus goes back to Apple's driver and the plug-in uses Core Audio again from its next start.
BROKER="/Library/Application Support/TI Revive/vtibroker"
OPT="$HOME/Library/Application Support/virus-ti-native"
LOG="$HOME/Desktop/tirevive-libusb-broker.log"
cleanup() {
    rm -f "$OPT/mac-usb-audio.on"
    echo
    echo "Stopped. The plug-in uses Core Audio again from its next start. Log: $LOG"
}
trap cleanup EXIT
if [ ! -x "$BROKER" ]; then
    echo "TI Revive is not installed ($BROKER missing)."
    exit 1
fi
if pgrep -x vtibroker >/dev/null; then # the normal service exits 30 s after its last client; this test needs it gone
    echo "Stopping the running TI Revive service (quit your DAW first if it is open)..."
    pkill -x vtibroker
    sleep 1
fi
mkdir -p "$OPT" && touch "$OPT/mac-usb-audio.on"
echo "Starting the TI Revive service with its own Virus audio. macOS asks for your password."
echo "Leave this window open; Ctrl-C ends the test."
sudo VTI_LIBUSB_AUDIO=1 "$BROKER" -v -stay 2>&1 | tee "$LOG"
