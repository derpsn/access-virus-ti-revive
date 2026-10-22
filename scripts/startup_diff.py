#!/usr/bin/env python3
"""startup_diff.py - compare the start-up messages of two usbmon captures (scripts/capture.sh), e.g. the original
Virus Control in win10ref against TI Revive (E321: the comparison found the missing "Configuring Virus TI" message
and the wrong multi dump).

  python3 -I scripts/startup_diff.py ORIGINAL.pcapng OURS.pcapng [--max-s 40] [--ignore PREFIX]... [--all]

Reads what the host sends to the Virus on its MIDI endpoints (EP 0x05/0x06), from the first SysEx to the end of the
bank check (the message timer set back to 1 after the last bank info request) or --max-s seconds, condenses it
(the 16 part channels and the 16 single dumps to one line each, the multi with a hash of its bytes; notes, active
sensing and the keepalive left out) and prints the differences in order. Known differences (IGNORE) are counted but
not judged. Exit 0 when the sequences match. The device address comes from the capture's .meta file (capture.sh).
"""
import argparse
import difflib
import hashlib
import os
import subprocess
import sys

VIRUS = bytes([0xF0, 0x00, 0x20, 0x33, 0x01])
# known, accepted differences (prefixes of condensed lines):
IGNORE = [
    "73 00 09=",  # USB IO configuration: this computer's setting (the original sends its stored one, three times)
    "30 00 00 ",  # our readiness probe after the state (the original has none)
    "f2 ",  # our song position at the timeline anchor (timing design); the original sends F2 only at Play (E275)
]
# bytes per USB-MIDI code index number; 5 = one byte (or the end of a SysEx with one byte)
CIN_LEN = {0x2: 2, 0x3: 3, 0x4: 3, 0x5: 1, 0x6: 2, 0x7: 3, 0x8: 3, 0x9: 3, 0xA: 3, 0xB: 3, 0xC: 2, 0xD: 2,
           0xE: 3, 0xF: 1}


def decode(packets):
    """[(time, endpoint, data)] of USB-MIDI OUT transfers -> [(time, message)], SysEx put together per endpoint and
    cable (time = its first packet)"""
    open_sx, out = {}, []
    for t, ep, data in packets:
        for i in range(0, len(data) - 3, 4):
            p = data[i:i + 4]
            cin, key = p[0] & 0xF, (ep, p[0] >> 4)
            if p == b"\0\0\0\0" or cin not in CIN_LEN:
                continue
            b = p[1:1 + CIN_LEN[cin]]
            if cin == 0x4 or (cin in (0x5, 0x6, 0x7) and (key in open_sx or b[:1] == b"\xf0")):
                open_sx.setdefault(key, [t, bytearray()])[1].extend(b)
                if cin != 0x4:
                    t0, m = open_sx.pop(key)
                    out.append((t0, bytes(m)))
            else:
                out.append((t, bytes(b)))
    return out


def _label(m):
    if m[:5] != VIRUS or len(m) < 9:
        return m.hex(" ")
    kind = m[6]
    if kind == 0x11:
        return "multi %d B %s" % (len(m), hashlib.sha1(m).hexdigest()[:10])
    if kind == 0x10 and m[7] == 0x00:
        return "single dump to the edit buffer"
    if kind == 0x39:
        return "39 %02x (bank info request)" % m[7]
    if kind == 0x30 and len(m) >= 10:
        return "30 %02x %02x (single request)" % (m[7], m[8])
    if len(m) == 11:
        return "%02x %02x %02x=%02x" % (m[6], m[7], m[8], m[9])
    return "%s (%d B)" % (m[6:9].hex(" "), len(m))


def _skipped(m):
    return not m or m[0] in (0xF5, 0xF8, 0xFE) or (m[0] & 0xE0) == 0x80  # keepalive, clock, notes


def tokens(msgs):
    """condensed lines of the messages that matter for the start-up"""
    out, i = [], 0
    while i < len(msgs):
        m = msgs[i][1]
        if _skipped(m):
            i += 1
            continue
        lab = _label(m)
        if lab == "single dump to the edit buffer":  # notes in between do not end the run
            n = 0
            while i < len(msgs) and (_skipped(msgs[i][1]) or _label(msgs[i][1]) == lab):
                n += not _skipped(msgs[i][1])
                i += 1
            out.append("single dumps to the edit buffers x%d" % n)
            continue
        if all(i + p < len(msgs) and msgs[i + p][1][:5] == VIRUS and len(msgs[i + p][1]) == 11 and
               msgs[i + p][1][6:10] == bytes([0x72, p, 0x22, p]) for p in range(16)):
            out.append("72 xx 22 (part channels 1-16)")
            i += 16
            continue
        out.append(lab)
        i += 1
    return out


def window(msgs, max_s=40.0):
    """from the first SysEx to the message timer set back to 1 after the last bank info request (or max_s)"""
    start = next((t for t, m in msgs if m[:1] == b"\xf0"), None)
    if start is None:
        return []
    w = [(t, m) for t, m in msgs if start <= t <= start + max_s]
    last_info = max((k for k, (_, m) in enumerate(w) if m[:5] == VIRUS and len(m) > 6 and m[6] == 0x39), default=-1)
    if last_info >= 0:
        end = next((k for k in range(last_info + 1, len(w))
                    if w[k][1][:5] == VIRUS and len(w[k][1]) == 11 and w[k][1][6:10] == bytes([0x74, 0, 0x29, 1])),
                   last_info)
        w = w[:end + 1]
    return w


def compare(a, b, ignore=()):
    """(same, report lines): '- x' only in a (the original), '+ y' only in b (ours), '  z' in both"""
    lines = []
    for pre in ignore:
        na, nb = sum(t.startswith(pre) for t in a), sum(t.startswith(pre) for t in b)
        if na or nb:
            lines.append("ignored %r: %d in the original, %d in ours" % (pre, na, nb))
    a = [t for t in a if not any(t.startswith(p) for p in ignore)]
    b = [t for t in b if not any(t.startswith(p) for p in ignore)]
    diff = [d for d in difflib.ndiff(a, b) if not d.startswith("?")]
    lines += diff
    return not any(d[:1] in "-+" for d in diff), lines


def read_capture(path, addr=None):
    """[(time, endpoint, data)] of the host's MIDI OUT transfers (tshark; via cat because of AppArmor, ts.sh)"""
    if addr is None:
        meta = os.path.splitext(path)[0] + ".meta"
        with open(meta, encoding="utf-8") as f:
            addr = next(int(l.split("=")[1]) for l in f if l.startswith("addr="))
    flt = ("usb.device_address == %d && (usb.endpoint_address == 0x06 || usb.endpoint_address == 0x05) && "
           "usb.urb_type == 'S' && usb.data_len > 0" % addr)
    with open(path, "rb") as f:
        r = subprocess.run(["tshark", "-r", "-", "-Y", flt, "-T", "fields", "-e", "frame.time_relative", "-e",
                            "usb.endpoint_address", "-e", "usb.capdata"], stdin=f, capture_output=True, text=True)
    out = []
    for line in r.stdout.splitlines():
        p = line.split("\t")
        if len(p) == 3 and p[2]:
            out.append((float(p[0].replace(",", ".")), p[1], bytes.fromhex(p[2].replace(":", ""))))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("original")
    ap.add_argument("ours")
    ap.add_argument("--max-s", type=float, default=40.0)
    ap.add_argument("--ignore", action="append", default=None, help="extra prefixes to ignore")
    ap.add_argument("--all", action="store_true", help="also print the lines both have")
    a = ap.parse_args()
    seqs = [tokens(window(decode(read_capture(p)), a.max_s)) for p in (a.original, a.ours)]
    same, lines = compare(seqs[0], seqs[1], IGNORE + (a.ignore or []))
    print("original: %d lines, ours: %d lines" % (len(seqs[0]), len(seqs[1])))
    for line in lines:
        if a.all or not line.startswith("  "):
            print(line)
    print("SAME START-UP" if same else "DIFFERENT START-UP ('-' only in the original, '+' only in ours)")
    return 0 if same else 1


if __name__ == "__main__":
    sys.exit(main())
