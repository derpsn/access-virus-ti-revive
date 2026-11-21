#!/bin/bash
# Sets up the files of YOUR OWN Access "Virus TI Software Suite ... .pkg" that TI Revive's plug-in reads - WITHOUT
# installing the Access software: the .pkg is unpacked and only Virus Control.bundle (skin, parameter descriptions)
# and firmware.bin (ROM banks) go to /Library/Application Support/Access Music/Virus TI/Common, the manuals to
# /Library/Documentation/Access Music/Virus TI and the factory patch libraries to ~/Documents/Access Music/Virus TI/
# Patches (only files that are not there yet). Nothing of Access is shipped with TI Revive.
# It also parks drivers that would block the Virus, and offers to remove the original Access plug-ins (Intel-only,
# not used any more since TI Revive 0.8.0).
# Put the Access .pkg next to this script, or drop it onto the Terminal window when asked.
set -euo pipefail
cd "$(dirname "$0")"
PKG=$(ls -1 Virus\ TI\ Software\ Suite*.pkg 2>/dev/null | head -1 || true)
if [ -z "$PKG" ]; then
    echo "Drag your Access 'Virus TI Software Suite ... .pkg' into this window and press Return:"
    read -r PKG
    PKG="${PKG%\'}"; PKG="${PKG#\'}"; PKG="${PKG//\\ / }"
fi
[ -f "$PKG" ] || { echo "not found: $PKG"; exit 1; }
WORK=$(mktemp -d /tmp/tirevive-access.XXXXXX)
trap 'rm -rf "$WORK"' EXIT
# A full Access installation (with its driver package) leaves drivers behind that macOS still runs through Rosetta on
# Apple Silicon; they hold the Virus' USB interfaces, so TI Revive cannot open it (broker: "claim interface 3:
# LIBUSB_ERROR_ACCESS", plug-in: "not connected", E134). TI Revive replaces all of them.
OLD_DRIVERS=()
for f in "/Library/Audio/MIDI Drivers/Virus TI MIDI Driver.plugin" "/Library/Audio/Plug-Ins/HAL/de.access-music.virus_ti.plugin" \
         "/Library/Audio/Plug-Ins/HAL/de.access-music.virus_ti.driver" "/Library/Extensions/AccessVirusTI.kext"; do
    [ -e "$f" ] && OLD_DRIVERS+=("$f")
done
if [ ${#OLD_DRIVERS[@]} -gt 0 ]; then
    echo "The old Access drivers are installed on this Mac:"
    printf '    %s\n' "${OLD_DRIVERS[@]}"
    echo "They cannot work on Apple Silicon, but they block the Virus for TI Revive (\"Virus TI not connected\")."
    printf "Disable them now (moved to /Library/Application Support/Access Music/Virus TI/Drivers disabled by TI Revive)? [Y/n] "
    read -r ans
    case "$ans" in
    [nN]*) echo "Left in place - TI Revive will probably not be able to use the Virus." ;;
    *) BAK="/Library/Application Support/Access Music/Virus TI/Drivers disabled by TI Revive"
       for f in "${OLD_DRIVERS[@]}"; do
           sudo mkdir -p "$BAK$(dirname "$f")"
           sudo rm -rf "$BAK$f"
           sudo mv "$f" "$BAK$f"
       done
       echo "Done. Please RESTART your Mac afterwards so macOS stops the old drivers." ;;
    esac
fi
# Other Virus USB drivers take the same interfaces (E134: Aura Plugins' Virus Editor driver). Only one driver can use
# the Virus at a time: offer to park it (restorable), so TI Revive can connect.
AURA="/Library/Audio/MIDI Drivers/AURA Plugins Virus TI USB Midi.plugin"
if [ -e "$AURA" ]; then
    echo "Aura Plugins' Virus TI USB MIDI driver is installed. It holds the Virus' USB interfaces, so TI Revive cannot"
    echo "connect while it is active (only one driver can use the Virus at a time)."
    printf "Park it now (moved to /Library/Application Support/Aura driver (parked); move it back to use Aura again)? [y/N] "
    read -r ans
    case "$ans" in
    [yY]*) sudo mkdir -p "/Library/Application Support/Aura driver (parked)"
           sudo mv "$AURA" "/Library/Application Support/Aura driver (parked)/"
           echo "Parked. Please RESTART your Mac afterwards." ;;
    *) echo "Left active - disable it before using \"TI Revive\"." ;;
    esac
fi
echo "Unpacking $PKG ..."
pkgutil --expand-full "$PKG" "$WORK/x" >/dev/null
SRC="$WORK/x/Core_components.pkg/Payload/Library/Application Support/Access Music/Virus TI/Common"
[ -d "$SRC/Virus Control.bundle" ] && [ -f "$SRC/firmware.bin" ] ||
    { echo "unexpected installer layout (no Virus Control.bundle / firmware.bin) - is it the Virus TI Software Suite?"; exit 1; }
COMMON="/Library/Application Support/Access Music/Virus TI/Common"
echo "Copying the Virus TI files (your administrator password is needed) ..."
sudo mkdir -p "$COMMON"
sudo rm -rf "$COMMON/Virus Control.bundle"
sudo cp -R "$SRC/Virus Control.bundle" "$COMMON/"
sudo cp "$SRC/firmware.bin" "$COMMON/"
# the look of Access' Control Center for TI Revive Control Center: only its images and layout files (its program is
# 32-bit and does not run on current macOS)
CCRES="$WORK/x/Core_components.pkg/Payload/Applications/Access Music/Virus TI/Virus Control Center.app/Contents/Resources"
if [ -d "$CCRES" ]; then
    sudo rm -rf "$COMMON/Virus Control Center Resources"
    sudo mkdir -p "$COMMON/Virus Control Center Resources"
    sudo find "$CCRES" -maxdepth 1 -type f \( -iname '*.png' -o -iname '*.xml' \) -exec cp {} "$COMMON/Virus Control Center Resources/" \;
fi
DOCS="$WORK/x/Additional_items.pkg/Payload/Library/Documentation/Access Music/Virus TI"
if [ -d "$DOCS" ]; then
    sudo mkdir -p "/Library/Documentation/Access Music"
    sudo rm -rf "/Library/Documentation/Access Music/Virus TI"
    sudo cp -R "$DOCS" "/Library/Documentation/Access Music/"
fi
# the Access installer creates Documents/Access Music as root: nothing could be written there (backups, libraries,
# the copy below; E182). Only what root owns is given back to you.
if [ -d "$HOME/Documents/Access Music" ] && [ -n "$(find "$HOME/Documents/Access Music" -user root -print -quit 2>/dev/null)" ]; then
    echo "Documents/Access Music belongs to the administrator (made by the Access installer): giving it back to you."
    sudo find "$HOME/Documents/Access Music" -user root -exec chown "$(id -un):$(id -gn)" {} +
fi
LIB="$WORK/x/Additional_items.pkg/Payload/Library/Application Support/Access Music/Virus TI/Patches"
MINE="$HOME/Documents/Access Music/Virus TI/Patches"
if [ -d "$LIB" ]; then # factory patch libraries; your own files stay untouched
    (cd "$LIB" && find . -type f) | while read -r f; do
        [ -e "$MINE/$f" ] || { mkdir -p "$(dirname "$MINE/$f")"; cp "$LIB/$f" "$MINE/$f"; }
    done
fi
# The original Access plug-ins are Intel-only and not used any more (TI Revive 0.7 ran them under Rosetta); a native
# DAW lists them as unusable. TI Revive 0.7 parked them in its own folder.
ORIG=()
for f in "/Library/Audio/Plug-Ins/VST3/Virus TI.vst3" "/Library/Audio/Plug-Ins/VST/Virus TI.vst" \
         "/Library/Audio/Plug-Ins/VST/Virus TI Snow.vst" "/Library/Audio/Plug-Ins/Components/Virus TI.component" \
         "/Library/Audio/Plug-Ins/Components/VirusTI.component" "/Library/Audio/Plug-Ins/Components/Virus TI Snow.component" \
         "/Library/Application Support/TI Revive/Access plug-ins" \
         "/Library/Application Support/Access Music/Virus TI/Common/libVirusUSB.dylib"; do
    [ -e "$f" ] && ORIG+=("$f")
done
if [ ${#ORIG[@]} -gt 0 ]; then
    echo
    echo "The original Access plug-ins (Intel-only) from an earlier installation are still on this Mac:"
    printf '    %s\n' "${ORIG[@]}"
    printf "TI Revive no longer uses them. Remove them? (the Virus TI files above stay) [Y/n] "
    read -r ans
    case "$ans" in
    [nN]*) echo "Left in place." ;;
    *) for f in "${ORIG[@]}"; do sudo rm -rf "$f"; done
       echo "Removed." ;;
    esac
fi
echo
echo "Done. In your DAW use \"TI Revive\" (VST3, or the Audio Unit in Logic / GarageBand)."
echo "Allow your DAW to use the microphone (System Settings > Privacy & Security > Microphone): macOS gives"
echo "the Virus' audio only to apps with that permission."
