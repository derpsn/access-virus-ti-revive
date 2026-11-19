#!/bin/sh
# gui_test.sh - end-to-end checks of the plug-in's editor and the Control Center app in a virtual X display (Linux,
# registered with ctest). No Virus and no broker needed. Skipped (exit 77) without xvfb-run.
#   gui_test.sh duplicate NATIVEHOST PLUGIN.vst3   two editors at once, the first instance removed (E183)
#   gui_test.sh ccclose CONTROLCENTER XCLOSE       window size, then the close button (E185)
set -u
command -v xvfb-run >/dev/null 2>&1 || { echo "xvfb-run not installed: skipped"; exit 77; }
mode=$1
shift
out=$(mktemp)
trap 'rm -f "$out"' EXIT
fail() { cat "$out"; echo "FAIL: $*"; exit 1; }

case $mode in
duplicate)
    # REAPER crashed at a duplicated track: the second editor freed the first one's run loop adapter, and the host
    # called a freed handler. The duplicate must also take over the Virus when the original goes.
    plugin=$(cd "$2" && pwd) # the VST3 loader names the library after the bundle folder: no "/../.."
    # 7 s after the removal: a waiting instance tries to take over every 2 s, and the first one's teardown takes
    # longer on a busy machine - with 3 s the try fell after the end right after a build (4 of ~20 runs)
    timeout 90 xvfb-run -a "$1" "$plugin" --editor --editor2 --duplicate 3 --seconds 10 >"$out" 2>&1
    rc=$?
    if [ $rc -ne 0 ] && grep -q "no display or no editor" "$out" && grep -q "cannot read .*Access Music" "$out"; then
        cat "$out"
        echo "no Access files (the editor needs them, e.g. CI): skipped"
        exit 77
    fi
    [ $rc -eq 0 ] || fail "nativehost exited with $rc"
    # since #45 (several Viruses) the duplicate tries Virus 2 first and logs "this instance uses Virus 1" at the takeover
    grep -q -e "this one takes over the Virus" -e "this instance uses Virus 1" "$out" || fail "the duplicate did not take over"
    # the editor's open and close are in the log (E287: REAPER crashes only with the editor open)
    [ "$(grep -c "editor opened" "$out")" -ge 2 ] || fail "the two editors' opening is not logged"
    grep -q "editor closed" "$out" || fail "the first editor's close is not logged"
    ;;
ccclose)
    # the app crashed on closing (its timer outlived VSTGUI's platform layer), and its window kept the size it was
    # created with instead of the start page's
    # its own session bus: GTK runs one instance per session, and a second start (the test while the user's Control
    # Center is open) only hands over to that one and exits (2026-11-15)
    bus=""
    command -v dbus-run-session >/dev/null 2>&1 && bus="dbus-run-session --"
    $bus timeout 90 xvfb-run -a sh -c '
        "$1" >"$3" 2>&1 &
        app=$!
        sleep 4
        "$2" "TI Revive Control Center" --size >>"$3" 2>&1
        "$2" "TI Revive Control Center" >>"$3" 2>&1
        wait $app
        echo "app exit $?" >>"$3"' gui_test "$1" "$2" "$out"
    grep -q "^app exit 0$" "$out" || fail "the Control Center did not exit cleanly"
    want=$(sed -n 's/^TI Revive Control Center: window \([0-9]*x[0-9]*\)$/\1/p' "$out" | head -1)
    got=$(sed -n 's/^window [0-9a-f]* \([0-9]*x[0-9]*\)$/\1/p' "$out" | head -1)
    [ -n "$want" ] || fail "the app did not report its window size"
    [ "$want" = "$got" ] || fail "window $got, the page wants $want"
    ;;
*)
    echo "usage: gui_test.sh duplicate NATIVEHOST PLUGIN | ccclose CONTROLCENTER XCLOSE"
    exit 2
    ;;
esac
cat "$out"
echo "ok"
