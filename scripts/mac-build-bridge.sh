#!/bin/bash
# mac-build-bridge.sh - build TI Revive's macOS stage 2 parts on the Mac and install the bridge for the current user
# (findings/design-macos-bridge.md):
#   "Virus TI (TI Revive).vst3"  arm64 VST3 bridge, Contents/Resources/vtihelper = x86_64 helper (runs under Rosetta)
#   libVirusUSB.dylib            x86_64, loaded by the original plug-in inside the helper (stage 1, E099-E103)
#   vtibroker                    native broker (make -C src/broker mac; LaunchAgent: scripts/mac-install-broker.sh)
# usage (on the Mac): mac-build-bridge.sh <repo dir with src/> [--install]
#   needs: Xcode Command Line Tools, VST3 pluginterfaces in <repo>/../vst3sdk/pluginterfaces (MIT,
#   git clone https://github.com/steinbergmedia/vst3_pluginterfaces vst3sdk/pluginterfaces)
set -euo pipefail
REPO="$(cd "${1:?repo dir}" && pwd)"
VST3SDK="${VST3SDK:-$REPO/../vst3sdk}"
OUT="${OUT:-$REPO/../build}"
# a stale MacOSX27.0.sdk in the Command Line Tools breaks ld (E098): pick the newest SDK that works
if [ -z "${SDKROOT:-}" ]; then
    for s in /Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk $(xcrun --show-sdk-path); do
        [ -d "$s" ] && { export SDKROOT="$s"; break; }
    done
fi
P="$VST3SDK/pluginterfaces"
mkdir -p "$OUT"
CXX="clang++ -std=c++17 -O2 -Wall -Wno-unused-parameter -I $VST3SDK -I $REPO/src/mac/common"

echo "== helper (x86_64)"
$CXX -arch x86_64 -I "$REPO/src/mac/helper" -o "$OUT/vtihelper" "$REPO/src/mac/helper/vtihelper.cpp" \
    -fobjc-arc -x objective-c++ "$REPO/src/mac/helper/vtihelper_editor.mm" -x none \
    "$P/base/funknown.cpp" "$P/base/coreiids.cpp" -framework CoreFoundation -framework Cocoa

echo "== bridge (arm64)"
$CXX -arch arm64 -bundle -fvisibility=hidden -o "$OUT/tirbridge" "$REPO/src/mac/bridge/tirbridge_vst3.cpp" \
    -fobjc-arc -x objective-c++ "$REPO/src/mac/bridge/tirbridge_view.mm" -x none \
    "$P/base/funknown.cpp" "$P/base/coreiids.cpp" -framework CoreFoundation -framework Cocoa

echo "== libVirusUSB.dylib (x86_64)"
clang++ -arch x86_64 -dynamiclib -std=c++17 -O2 -Wall \
    -install_name "/Library/Application Support/Access Music/Virus TI/Common/libVirusUSB.dylib" \
    -o "$OUT/libVirusUSB.dylib" "$REPO/src/mac/libvirususb/VirusClient.cpp" -framework CoreAudio -framework CoreFoundation

echo "== bundle"
B="$OUT/Virus TI (TI Revive).vst3"
rm -rf "$B"
mkdir -p "$B/Contents/MacOS" "$B/Contents/Resources"
cp "$OUT/tirbridge" "$B/Contents/MacOS/Virus TI (TI Revive)"
cp "$OUT/vtihelper" "$B/Contents/Resources/vtihelper"
cat > "$B/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key><string>English</string>
    <key>CFBundleExecutable</key><string>Virus TI (TI Revive)</string>
    <key>CFBundleIdentifier</key><string>io.github.derpsn.tirevive.bridge.vst3</string>
    <key>CFBundleName</key><string>Virus TI (TI Revive)</string>
    <key>CFBundlePackageType</key><string>BNDL</string>
    <key>CFBundleShortVersionString</key><string>0.5.0</string>
    <key>CFBundleVersion</key><string>0.5.0</string>
    <key>CFBundleSignature</key><string>????</string>
</dict>
</plist>
PLIST
echo "BNDL????" > "$B/Contents/PkgInfo"
codesign --force --sign - "$B/Contents/Resources/vtihelper"
codesign --force --sign - "$B"
echo "built $B"

if [ "${2:-}" = "--install" ]; then
    D="$HOME/Library/Audio/Plug-Ins/VST3"
    mkdir -p "$D"
    rm -rf "$D/Virus TI (TI Revive).vst3"
    cp -R "$B" "$D/"
    L="/Library/Application Support/Access Music/Virus TI/Common/libVirusUSB.dylib"
    if [ -w "$L" ]; then cat "$OUT/libVirusUSB.dylib" > "$L"; else echo "note: $L not writable, library not updated"; fi
    echo "installed to $D"
fi
