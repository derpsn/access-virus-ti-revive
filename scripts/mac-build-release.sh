#!/bin/bash
# mac-build-release.sh - build the macOS release of TI Revive on a Mac (Xcode; the release workflow's macOS runner):
#   TI-Revive-<version>-mac.zip = TI Revive.pkg (plug-in "TI Revive Virus Control" as VST3 + Audio Unit, universal;
#     the broker) + "Set up Access Virus TI files.command" + "Uninstall TI Revive.command" + README-mac.txt
# usage: mac-build-release.sh <repo dir> <version> [out dir]
# needs: Xcode (the plug-in: native/, VST3 SDK submodule; the AudioUnitSDK is fetched by CMake), curl (libusb).
# 0.8.0: no bridge, Rosetta helper or libVirusUSB any more (scripts/mac-build-bridge.sh builds them for reference).
set -euo pipefail
REPO="$(cd "${1:?repo dir}" && pwd)"
VERSION="${2:?version}"
OUT="$(mkdir -p "${3:-$REPO/../release}" && cd "${3:-$REPO/../release}" && pwd)"
export VERSION MACOSX_DEPLOYMENT_TARGET=14.4
# a stale MacOSX27.0.sdk in some Command Line Tools installs breaks ld (E098): use a working SDK
if [ -z "${SDKROOT:-}" ]; then
    for s in /Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk $(xcrun --show-sdk-path); do
        [ -d "$s" ] && { export SDKROOT="$s"; break; }
    done
fi
BUILD="$OUT/build"
rm -rf "$BUILD" && mkdir -p "$BUILD"

echo "== libusb (static, built for macOS $MACOSX_DEPLOYMENT_TARGET; Homebrew's targets the build machine's macOS)"
LIBUSB_VER=1.0.30
LIBUSB="$OUT/libusb-$LIBUSB_VER-macos$MACOSX_DEPLOYMENT_TARGET"
if [ ! -f "$LIBUSB/lib/libusb-1.0.a" ]; then
    T=$(mktemp -d)
    curl -fsSL "https://github.com/libusb/libusb/releases/download/v$LIBUSB_VER/libusb-$LIBUSB_VER.tar.bz2" -o "$T/libusb.tar.bz2"
    tar -xjf "$T/libusb.tar.bz2" -C "$T"
    (cd "$T/libusb-$LIBUSB_VER" && ./configure -q --disable-shared --enable-static --prefix="$LIBUSB" \
        CFLAGS="-arch arm64 -O2 -mmacosx-version-min=$MACOSX_DEPLOYMENT_TARGET" >/dev/null && make -s -j4 install >/dev/null)
    rm -rf "$T"
fi
echo "== broker (static libusb)"
make -C "$REPO/src/broker" mac-static OUT="$BUILD" LIBUSB_PREFIX="$LIBUSB"
otool -L "$BUILD/mac/vtibroker" | grep -q libusb && { echo "broker still links a libusb dylib"; exit 1; }
codesign --force --sign - "$BUILD/mac/vtibroker"

echo "== plug-in TI Revive Virus Control: VST3 + Audio Unit (arm64 + x86_64)"
cmake -S "$REPO/native" -B "$BUILD/native" -G Xcode -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 ${SDKROOT:+-DCMAKE_OSX_SYSROOT="$SDKROOT"} >/dev/null
cmake --build "$BUILD/native" --config Release --target VirusTINative VirusTINative_AU VirusTINative_AU_Snow tirevive-cc TIReviveControlCenter -- -quiet
VST3="$BUILD/native/VST3/Release/TI Revive Virus Control.vst3"
AU="$BUILD/native/AU/Release/TI Revive Virus Control.component"
AU_SNOW="$BUILD/native/AU/Release/TI Revive Virus Control (Snow).component" # the Snow unit (E374)
codesign --force --deep --sign - "$VST3"
codesign --force --deep --sign - "$AU"
codesign --force --deep --sign - "$AU_SNOW"
CC_BIN="$BUILD/native/bin/Release/tirevive-cc" # the Control Center command line (read-only)
codesign --force --sign - "$CC_BIN"
CCAPP="$BUILD/native/ccapp/TI Revive Control Center.app" # the Control Center app
codesign --force --deep --sign - "$CCAPP"

echo "== payload"
ROOT="$BUILD/root"
SUP="$ROOT/Library/Application Support"
mkdir -p "$ROOT/Library/Audio/Plug-Ins/VST3" "$ROOT/Library/Audio/Plug-Ins/Components" "$SUP/TI Revive" "$ROOT/Applications"
# 0.8.0: only the native plug-in (VST3 + Audio Unit, the codes / class ids of the classic bridge, which it replaces)
cp -R "$VST3" "$ROOT/Library/Audio/Plug-Ins/VST3/"
cp -R "$AU" "$AU_SNOW" "$ROOT/Library/Audio/Plug-Ins/Components/"
cp "$BUILD/mac/vtibroker" "$SUP/TI Revive/vtibroker"
cp "$CC_BIN" "$SUP/TI Revive/tirevive-cc"
cp -R "$CCAPP" "$ROOT/Applications/"
cp "$REPO/src/installer-mac/Uninstall TI Revive.command" "$REPO/src/installer-mac/Set up Access Virus TI files.command" \
    "$SUP/TI Revive/"
chmod -R go-w "$ROOT"

echo "== pkg"
pkgbuild --analyze --root "$ROOT" "$BUILD/components.plist" >/dev/null
# never relocate the bundle onto another copy with the same identifier (e.g. a development install in ~/Library), and
# always write it: with the default version check the installer kept an installed bundle of the same version (a test
# build over another one of 0.8.29 kept yesterday's plug-ins, only the broker was new - E305)
n=$(/usr/libexec/PlistBuddy -c "Print" "$BUILD/components.plist" | grep -c "RootRelativeBundlePath" || true)
for ((i = 0; i < n; i++)); do
    for key in BundleIsRelocatable BundleIsVersionChecked; do
        /usr/libexec/PlistBuddy -c "Delete :$i:$key" "$BUILD/components.plist" 2>/dev/null || true
        /usr/libexec/PlistBuddy -c "Add :$i:$key bool false" "$BUILD/components.plist"
    done
done
STAGE="$BUILD/TI-Revive-$VERSION-mac"
mkdir -p "$STAGE"
pkgbuild --root "$ROOT" --component-plist "$BUILD/components.plist" --identifier io.github.derpsn.tirevive \
    --version "$VERSION" --scripts "$REPO/src/installer-mac/scripts" --install-location / "$STAGE/TI Revive.pkg"
cp "$REPO/src/installer-mac/Set up Access Virus TI files.command" "$REPO/src/installer-mac/Uninstall TI Revive.command" \
    "$REPO/src/installer-mac/README-mac.txt" "$REPO/LICENSE" "$REPO/THIRD-PARTY-NOTICES.txt" "$STAGE/"
chmod +x "$STAGE"/*.command
(cd "$BUILD" && ditto -c -k --keepParent "TI-Revive-$VERSION-mac" "$OUT/TI-Revive-$VERSION-mac.zip")
echo "built $OUT/TI-Revive-$VERSION-mac.zip"
