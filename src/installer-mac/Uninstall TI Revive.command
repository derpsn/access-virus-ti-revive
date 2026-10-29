#!/bin/bash
# Removes TI Revive. The Virus TI files it set up (/Library/Application Support/Access Music/Virus TI) and your patch
# libraries stay.
set -u
echo "Removing TI Revive (your administrator password is needed) ..."
launchctl bootout "gui/$(id -u)/io.github.derpsn.tirevive.vtibroker" 2>/dev/null   # agent of a 0.5.0 test build
sudo rm -f /Library/LaunchAgents/io.github.derpsn.tirevive.vtibroker.plist
sudo pkill -x vtibroker 2>/dev/null
sudo pkill -x vtihelper 2>/dev/null
sudo rm -rf "/Library/Audio/Plug-Ins/VST3/TI Revive Virus Control.vst3" "/Library/Audio/Plug-Ins/Components/TI Revive Virus Control.component" \
    "/Library/Audio/Plug-Ins/Components/TI Revive Virus Control (Snow).component" \
    "$HOME/Library/Audio/Plug-Ins/VST3/VirusTINative.vst3"
sudo rm -rf "/Library/Audio/Plug-Ins/VST3/Virus TI (TI Revive).vst3" \
    "/Library/Audio/Plug-Ins/Components/Virus TI (TI Revive).component" "/Library/Application Support/TI Revive" \
    "/Applications/TI Revive Control Center.app"
# TI Revive 0.7's replacement of Access' driver-package library
sudo rm -f "/Library/Application Support/Access Music/Virus TI/Common/libVirusUSB.dylib"
sudo pkgutil --forget io.github.derpsn.tirevive >/dev/null 2>&1
echo "TI Revive removed."
