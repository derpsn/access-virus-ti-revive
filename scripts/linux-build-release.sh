#!/bin/bash
# linux-build-release.sh - build the Linux release of TI Revive (findings/design-linux.md):
#   TI-Revive-<version>-linux.tar.gz = install.sh, uninstall.sh, README-linux.txt,
#     bin/vtibroker (native, libusb linked statically), native/TI Revive Virus Control.vst3 (the plug-in),
#     systemd/ (user socket + service), udev/70-tirevive-virus-ti.rules
# (0.8.0: no Wine-side files - the classic plug-in under yabridge is replaced by the native plug-in)
# usage: linux-build-release.sh <repo dir> <version> [out dir]
# needs: gcc, make, curl, bzip2, cmake + the native plug-in's build dependencies (or NATIVE_PREBUILT)
set -euo pipefail
REPO="$(cd "${1:?repo dir}" && pwd)"
VERSION="${2:?version}"
OUT="$(mkdir -p "${3:-$REPO/work/release}" && cd "${3:-$REPO/work/release}" && pwd)"
BUILD="$OUT/build-linux"
rm -rf "$BUILD" && mkdir -p "$BUILD"

echo "== libusb (static, without udev: the broker finds the Virus through sysfs)"
LIBUSB_VER=1.0.30
LIBUSB="$OUT/libusb-$LIBUSB_VER-linux-static"
if [ ! -f "$LIBUSB/lib/libusb-1.0.a" ]; then
    T=$(mktemp -d)
    curl -fsSL "https://github.com/libusb/libusb/releases/download/v$LIBUSB_VER/libusb-$LIBUSB_VER.tar.bz2" -o "$T/libusb.tar.bz2"
    tar -xjf "$T/libusb.tar.bz2" -C "$T"
    (cd "$T/libusb-$LIBUSB_VER" && ./configure -q --disable-shared --enable-static --disable-udev --prefix="$LIBUSB" \
        CFLAGS="-O2 -fPIC" >/dev/null && make -s -j4 install >/dev/null)
    rm -rf "$T"
fi
echo "== broker"
B="$REPO/src/broker"
gcc -O2 -Wall -Wextra -DTIREVIVE_VERSION="\"$VERSION\"" -I"$LIBUSB/include/libusb-1.0" -o "$BUILD/vtibroker" "$B/vtibroker_posix.c" "$B/vticore.c" \
    "$B/vtiusbaudio_libusb.c" "$B/vtikeyport_linux.c" "$B/vticore_midi.c" "$LIBUSB/lib/libusb-1.0.a" -lpthread
! ldd "$BUILD/vtibroker" | grep -q libusb || { echo "broker still links libusb dynamically"; exit 1; }
NATIVE=""
CC_BIN="" # tirevive-cc, the Control Center command line (native/tools)
CCAPP_BIN="" # the Control Center app (native/ccapp)
if [ -n "${NATIVE_PREBUILT:-}" ]; then # built elsewhere (the release workflow builds it on an older Ubuntu, glibc)
    NATIVE="$NATIVE_PREBUILT"
    [ -f "$NATIVE/Contents/x86_64-linux/TI Revive Virus Control.so" ] || { echo "no native plug-in in $NATIVE"; exit 1; }
    CC_BIN="${NATIVE_CC:-}"
    CCAPP_BIN="${NATIVE_CCAPP:-}"
elif [ -f "$REPO/native/CMakeLists.txt" ]; then # the native plug-in "TI Revive Virus Control" (native/, Linux VST3)
    echo "== native plug-in"
    cmake -S "$REPO/native" -B "$BUILD/native" -DCMAKE_BUILD_TYPE=Release >/dev/null
    cmake --build "$BUILD/native" --target VirusTINative tirevive-cc TIReviveControlCenter -j4 >/dev/null
    NATIVE="$BUILD/native/VST3/Release/TI Revive Virus Control.vst3"
    CC_BIN="$BUILD/native/bin/Release/tirevive-cc"
    CCAPP_BIN="$BUILD/native/ccapp/TIReviveControlCenter"
    strip "$NATIVE/Contents/x86_64-linux/TI Revive Virus Control.so"
fi

echo "== package"
NAME="TI-Revive-$VERSION-linux"
P="$BUILD/$NAME"
I="$REPO/src/installer-linux"
[ -n "$NATIVE" ] || { echo "the native plug-in is required (native/ or NATIVE_PREBUILT)"; exit 1; }
mkdir -p "$P/bin" "$P/systemd" "$P/udev"
install -m 0755 "$BUILD/vtibroker" "$P/bin/"
[ -f "$CC_BIN" ] || { echo "tirevive-cc missing (NATIVE_CC with NATIVE_PREBUILT)"; exit 1; }
install -m 0755 -s "$CC_BIN" "$P/bin/tirevive-cc"
[ -f "$CCAPP_BIN" ] || { echo "TIReviveControlCenter missing (NATIVE_CCAPP with NATIVE_PREBUILT)"; exit 1; }
install -m 0755 -s "$CCAPP_BIN" "$P/bin/tirevive-control-center"
install -m 0644 "$I/tirevive-broker.socket" "$I/tirevive-broker.service" "$P/systemd/"
install -m 0644 "$I/70-tirevive-virus-ti.rules" "$P/udev/"
install -m 0755 "$I/install.sh" "$I/uninstall.sh" "$P/"
mkdir -p "$P/native" && cp -R "$NATIVE" "$P/native/"
sed "s/@VERSION@/$VERSION/g" "$I/README-linux.txt" > "$P/README-linux.txt"
install -m 0644 "$REPO/LICENSE" "$REPO/THIRD-PARTY-NOTICES.txt" "$P/"
tar -C "$BUILD" -czf "$OUT/$NAME.tar.gz" "$NAME"
echo "$OUT/$NAME.tar.gz"
