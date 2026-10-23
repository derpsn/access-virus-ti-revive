#!/bin/sh
# TI Revive for Linux - installer (run as your normal user, from the unpacked TI-Revive-<version>-linux folder).
#
#   ./install.sh [path to "Virus TI Software Suite ... 64-bit.msi"]
#
# What it does (findings/design-linux.md):
#   1. udev rule so you (not only root) may use the Virus over USB          (asks for your password once, sudo)
#   2. TI Revive broker in ~/.local/lib/tirevive + systemd user socket (starts on demand, exits when idle)
#   3. the files of your own Access "Virus TI Software Suite" .msi that the plug-in reads (skin + device description,
#      ROM banks, manuals, factory patch libraries) - unpacked with msiextract (msitools) or Wine, NOT installed
#   4. the plug-in "TI Revive Virus Control" (Linux VST3, no Wine/yabridge) into ~/.vst3; the classic plug-in of
#      TI Revive 0.7 ("Virus TI (TI Revive)" through yabridge) is removed - the new one opens its projects
# Wine is only needed to unpack the .msi when msiextract is missing.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
PREFIX="${WINEPREFIX:-$HOME/.wine}"
LIB="$HOME/.local/lib/tirevive"
UNITS="${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user"
CONF="${XDG_CONFIG_HOME:-$HOME/.config}/tirevive"
VST3_DIR="$PREFIX/drive_c/Program Files/Common Files/VST3"
ACCESS="$HOME/.local/share/tirevive/access/Access Music/Virus TI" # Common/ (Virus Control.dll, firmware.bin), Documentation/

say() { printf '\n== %s\n' "$*"; }
die() { printf '\nERROR: %s\n' "$*" >&2; exit 1; }

CC_EXE="Virus Control Center.exe" # optional: the Control Center app takes its look from it
find_msi() { # $1 = the path given on the command line, else the usual places
    if [ -n "${1:-}" ]; then printf '%s\n' "$1"; return; fi
    for d in "$HERE" "$HOME/Downloads" "$HOME/Desktop" "$HOME"; do
        for f in "$d"/*Virus*TI*64-bit*.msi; do [ -f "$f" ] && printf '%s\n' "$f" && return; done
    done
    return 0 # nothing found is not an error here (set -e)
}
unpack_msi() { # $1 = .msi, $2 = empty folder; fails without msiextract or Wine
    if command -v msiextract >/dev/null 2>&1; then
        echo "unpacking $1 (msiextract)"
        (cd "$2" && msiextract "$1" >/dev/null)
    elif command -v wine >/dev/null 2>&1; then
        echo "unpacking $1 (Wine, administrative extraction - nothing is installed)"
        cp "$1" "$2/access.msi"
        WINEPREFIX="$2/wineprefix" WINEDEBUG=-all wine msiexec /a "$(WINEPREFIX="$2/wineprefix" wine winepath -w "$2/access.msi" 2>/dev/null)" \
            /qn TARGETDIR="$(WINEPREFIX="$2/wineprefix" wine winepath -w "$2/x" 2>/dev/null)" >/dev/null 2>&1 || return 1
        WINEPREFIX="$2/wineprefix" wineserver -k 2>/dev/null || true # its files must go with the folder
    else
        return 1
    fi
}
# The .msi holds Common/ twice (E324, issue #15): the x64 files with the Control Center exe and, under Program Files,
# the 32-bit DLL without it. find's order depends on the file system, so take the copy that has the exe.
access_src() { # $1 = unpacked .msi -> its .../Access Music/Virus TI folder, empty if there is no Virus Control.dll
    find "$1" -path '*Access Music/Virus TI/Common/Virus Control.dll' | {
        best=""
        while IFS= read -r dll; do
            d=$(dirname "$(dirname "$dll")")
            [ -n "$best" ] || best=$d
            if [ -f "$d/Common/$CC_EXE" ]; then best=$d; break; fi
        done
        [ -z "$best" ] || printf '%s\n' "$best"
    }
}
# the tests (scripts/tests/test_install_linux.py) source this file for the functions above only
[ -z "${TIREVIVE_INSTALL_FUNCS_ONLY:-}" ] || return 0

[ "$(id -u)" -ne 0 ] || die "run install.sh as your normal user, not as root (it asks for sudo when needed)"

# --- 1. USB access (udev) ------------------------------------------------------------------------------------------
RULE=/etc/udev/rules.d/70-tirevive-virus-ti.rules
virus_accessible() { # a plugged-in Virus whose device node we may already open (rule present or other setup)
    for d in /sys/bus/usb/devices/*; do
        [ "$(cat "$d/idVendor" 2>/dev/null)" = 133e ] && [ "$(cat "$d/idProduct" 2>/dev/null)" = 0815 ] || continue
        node=$(printf '/dev/bus/usb/%03d/%03d' "$(cat "$d/busnum")" "$(cat "$d/devnum")")
        [ -r "$node" ] && [ -w "$node" ] && return 0
    done
    return 1
}
if ! cmp -s "$HERE/udev/70-tirevive-virus-ti.rules" "$RULE" 2>/dev/null && ! virus_accessible; then
    say "USB access for the Virus TI (udev rule, needs sudo)"
    sudo install -m 0644 "$HERE/udev/70-tirevive-virus-ti.rules" "$RULE"
    sudo udevadm control --reload
    sudo udevadm trigger --subsystem-match=usb --attr-match=idVendor=133e || true
fi

# --- 2. broker + systemd user socket --------------------------------------------------------------------------------
say "TI Revive broker"
mkdir -p "$LIB" "$UNITS" "$CONF"
install -m 0755 "$HERE/bin/vtibroker" "$LIB/vtibroker"
# the Control Center command line (read-only: device info, backup of the RAM banks, library export)
mkdir -p "$HOME/.local/bin" && install -m 0755 "$HERE/bin/tirevive-cc" "$HOME/.local/bin/tirevive-cc"
# ... and the same in a window, with a menu entry; it needs GTK 3 / gtkmm (the plug-in does not)
install -m 0755 "$HERE/bin/tirevive-control-center" "$HOME/.local/bin/tirevive-control-center"
APPS="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
mkdir -p "$APPS" && cat > "$APPS/tirevive-control-center.desktop" <<EOF_DESKTOP
[Desktop Entry]
Type=Application
Name=TI Revive Control Center
Comment=Access Virus TI: device info, backup of the RAM banks, patch library export
Exec=$HOME/.local/bin/tirevive-control-center
Terminal=false
Categories=AudioVideo;Audio;Utility;
EOF_DESKTOP
CC_MISSING=$(ldd "$HOME/.local/bin/tirevive-control-center" 2>/dev/null | awk '/not found/ {print $1}' | tr '\n' ' ')
if [ -n "$CC_MISSING" ]; then
    # issue #9 (Arch/CachyOS): without gtkmm the app closes at once ("spinning cursor, then gone")
    echo "NOTE: the Control Center app needs gtkmm 3 - missing: $CC_MISSING"
    echo "      Ubuntu 24.04+: sudo apt install libgtkmm-3.0-1t64 (older Debian/Ubuntu: libgtkmm-3.0-1v5);"
    echo "      Fedora: sudo dnf install gtkmm3.0; Arch/CachyOS/Manjaro: sudo pacman -S gtkmm3."
    echo "      The plug-in and tirevive-cc work without it."
fi
install -m 0644 "$HERE/systemd/tirevive-broker.socket" "$HERE/systemd/tirevive-broker.service" "$UNITS/"
rm -f "$CONF/broker.token" # TI Revive 0.7: the TCP socket for the plug-in under Wine is gone
if command -v systemctl >/dev/null 2>&1 && systemctl --user show-environment >/dev/null 2>&1; then
    if systemctl --user daemon-reload && systemctl --user enable tirevive-broker.socket >/dev/null &&
        systemctl --user restart tirevive-broker.socket; then
        echo "broker socket enabled (systemd user unit tirevive-broker.socket)"
    else
        echo "WARNING: could not enable the systemd user socket; start $LIB/vtibroker yourself before using the plug-in"
    fi
else
    echo "no systemd user session: start $LIB/vtibroker yourself before using the plug-in"
fi

# --- 3. the Access files ---------------------------------------------------------------------------------------------
say "Access Virus TI files"
OLD_COMMON="$PREFIX/drive_c/Program Files/Access Music/Virus TI/Common" # TI Revive 0.7 installed Access into Wine
if [ -f "$ACCESS/Common/Virus Control.dll" ] && [ -f "$ACCESS/Common/firmware.bin" ]; then
    echo "already set up in $ACCESS"
    [ -f "$ACCESS/Common/$CC_EXE" ] || [ ! -f "$OLD_COMMON/$CC_EXE" ] || cp "$OLD_COMMON/$CC_EXE" "$ACCESS/Common/"
    # installs before 0.8.32 could miss the exe (issue #15): take it from the .msi when it is at hand, else go on
    MSI=$(find_msi "${1:-}")
    if [ ! -f "$ACCESS/Common/$CC_EXE" ] && [ -n "$MSI" ] && [ -f "$MSI" ]; then
        TMP=$(mktemp -d)
        trap 'rm -rf "$TMP"' EXIT
        SRC=""
        unpack_msi "$MSI" "$TMP" && SRC=$(access_src "$TMP")
        if [ -n "$SRC" ] && [ -f "$SRC/Common/$CC_EXE" ]; then
            cp "$SRC/Common/$CC_EXE" "$ACCESS/Common/"
            echo "added $CC_EXE (the Control Center's Access look)"
        else
            echo "NOTE: could not take $CC_EXE from $MSI; the Control Center works, in its plain look"
        fi
    elif [ ! -f "$ACCESS/Common/$CC_EXE" ]; then
        echo "NOTE: $CC_EXE is missing, so the Control Center shows its plain look; for the Access look run"
        echo "      ./install.sh \"/path/to/Virus TI Software Suite 5.1.7.00 64-bit.msi\""
    fi
elif [ -f "$OLD_COMMON/Virus Control.dll" ] && [ -f "$OLD_COMMON/firmware.bin" ]; then
    mkdir -p "$ACCESS/Common"
    cp "$OLD_COMMON/Virus Control.dll" "$OLD_COMMON/firmware.bin" "$ACCESS/Common/"
    [ ! -f "$OLD_COMMON/$CC_EXE" ] || cp "$OLD_COMMON/$CC_EXE" "$ACCESS/Common/"
    echo "taken from the Access installation in the Wine prefix $PREFIX"
else
    MSI=$(find_msi "${1:-}")
    [ -n "$MSI" ] && [ -f "$MSI" ] || die "Access installer not found. Download \"Virus TI Software Suite 5.1.7.00 64-bit.msi\"
       (your own copy) and run: ./install.sh \"/path/to/Virus TI Software Suite 5.1.7.00 64-bit.msi\""
    TMP=$(mktemp -d)
    trap 'rm -rf "$TMP"' EXIT
    command -v msiextract >/dev/null 2>&1 || command -v wine >/dev/null 2>&1 ||
        die "to unpack the Access .msi install msitools (Ubuntu/Debian: sudo apt install msitools) or Wine, then run ./install.sh again"
    unpack_msi "$MSI" "$TMP" || die "could not unpack $MSI"
    SRC=$(access_src "$TMP") # .../Access Music/Virus TI
    [ -n "$SRC" ] || die "no Virus Control.dll in $MSI - is it the 64-bit \"Virus TI Software Suite\" .msi?"
    mkdir -p "$ACCESS/Common"
    cp "$SRC/Common/Virus Control.dll" "$SRC/Common/firmware.bin" "$ACCESS/Common/"
    [ ! -f "$SRC/Common/$CC_EXE" ] || cp "$SRC/Common/$CC_EXE" "$ACCESS/Common/"
    [ -d "$SRC/Documentation" ] && cp -R "$SRC/Documentation" "$ACCESS/"
    LIB_SRC=$(find "$TMP" -type d -path '*Access Music/Virus TI/Patches' | grep -v '/Common/' | head -1)
    if [ -n "$LIB_SRC" ]; then # factory patch libraries; existing files (your own libraries) stay untouched
        # the plug-in's rule (library_dir): an existing ~/Documents library folder, else the XDG documents folder
        # (localized, e.g. ~/Dokumente), else ~/Documents
        DOCS="$HOME/Documents/Access Music/Virus TI/Patches"
        XDG_DOCS=$(xdg-user-dir DOCUMENTS 2>/dev/null || true)
        if [ ! -d "$DOCS" ] && [ -n "$XDG_DOCS" ] && [ "$XDG_DOCS" != "$HOME" ] && [ "${XDG_DOCS#/}" != "$XDG_DOCS" ]; then
            DOCS="$XDG_DOCS/Access Music/Virus TI/Patches"
        fi
        (cd "$LIB_SRC" && find . -type f) | while read -r f; do
            [ -e "$DOCS/$f" ] || { mkdir -p "$(dirname "$DOCS/$f")"; cp "$LIB_SRC/$f" "$DOCS/$f"; }
        done
    fi
    echo "set up in $ACCESS (the Access software itself is not installed)"
fi
# the plug-in reads the Access files from there
mkdir -p "$CONF"
printf '%s\n' "$ACCESS/Common" > "$CONF/access-dir"

# --- 4. the plug-in --------------------------------------------------------------------------------------------------
say "plug-in TI Revive Virus Control"
mkdir -p "$HOME/.vst3"
# the classic plug-in of TI Revive 0.7 (through yabridge) and the test builds: the new plug-in opens their projects
rm -f "$VST3_DIR/Virus TI (TI Revive).vst3"
rm -rf "$HOME/.vst3/yabridge/Virus TI (TI Revive).vst3" "$HOME/.vst3/VirusTINative.vst3" "$HOME/.vst3/TI Revive Virus Control.vst3"
cp -R "$HERE/native/TI Revive Virus Control.vst3" "$HOME/.vst3/"
MISSING=$(ldd "$HOME/.vst3/TI Revive Virus Control.vst3/Contents/x86_64-linux/TI Revive Virus Control.so" 2>/dev/null |
    awk '/not found/ {print $1}' | tr '\n' ' ')
if [ -n "$MISSING" ]; then
    cat <<EOT
WARNING: libraries the plug-in needs are missing: $MISSING
Install them with your package manager (Ubuntu/Debian: libxcb-cursor0 libxcb-util1 libxkbcommon-x11-0 libcairo2
libpango-1.0-0 libpangocairo-1.0-0 libfontconfig1; Fedora: xcb-util-cursor xcb-util libxkbcommon-x11 cairo pango
fontconfig; Arch/CachyOS/Manjaro: xcb-util-cursor xcb-util libxkbcommon-x11 cairo pango fontconfig), then rescan
plug-ins.
EOT
fi

say "done. Rescan plug-ins in your DAW and load the VST3 \"TI Revive Virus Control\"."
