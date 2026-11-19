# libusb.sh - the libusb the macOS and Linux brokers link statically, sourced by mac-build-release.sh,
# linux-build-release.sh and the release job (build.yml, LGPL source asset). One place for the version and the
# SHA-256 of its release tarball: it used to be downloaded and built without a check, and the libusb Mac package's
# vtibroker runs as root (audit 2026-11-17 #7). A new version: change both lines together.
LIBUSB_VER=1.0.30
LIBUSB_SHA256=fea36f34f9156400209595e300840767ab1a385ede1dc7ee893015aea9c6dbaf

# libusb_verify FILE: exit 0 only if FILE has the pinned SHA-256 (sha256sum on Linux, shasum on macOS)
libusb_verify() {
    [ -f "$1" ] || return 1
    if command -v sha256sum >/dev/null 2>&1; then
        echo "$LIBUSB_SHA256  $1" | sha256sum -c - >/dev/null
    else
        echo "$LIBUSB_SHA256  $1" | shasum -a 256 -c - >/dev/null
    fi
}

# libusb_fetch FILE: download the release tarball to FILE and verify it; on a mismatch FILE is removed
libusb_fetch() {
    curl -fsSL "https://github.com/libusb/libusb/releases/download/v$LIBUSB_VER/libusb-$LIBUSB_VER.tar.bz2" -o "$1" &&
        libusb_verify "$1" || { echo "libusb-$LIBUSB_VER.tar.bz2: download failed or SHA-256 mismatch" >&2; rm -f "$1"; return 1; }
}
