#!/usr/bin/env python3
"""latency_measure.py - how long a note takes through a plug-in, and what its stamp says on the wire (E400, #30).

The test hosts (src/wrapper/vtihost.cpp: the original; native/tools/nativehost.cpp: TI Revive) play notes on a fixed
grid and record the plug-in's output, so the delay from the note's frame to the first sound is exact, without any
audio interface or hands. With VTI_LIVE=alt the odd seconds are flagged as played live: class 1 below.

  python3 -I scripts/latency_measure.py wav REC.wav --first 11025 [--period 44100] [--classes 2] [--skip 10]
                                                    [--reported 4352] [--threshold-db -30]
      per class: notes, missing, median/min/max delay in frames and ms; --reported also prints median - reported.
  python3 -I scripts/latency_measure.py usb CAPTURE.pcapng ADDR --first 11025 [--classes 2] [--first-index 0]
      note-ons to the Virus with their F5 stamp; per class the stamp relative to the grid. Only the difference between
      classes means anything (the anchor is unknown here). A class without stamps prints "no F5".
"""
import argparse
import array
import collections
import os
import re
import statistics
import subprocess
import sys
import wave

STAMP_MOD = 1 << 14  # F5 lo hi: 14-bit frame (protocol.md)
SAME_WRITE_S = 0.001  # F5 and its events go out together; a stamp older than this belongs to something else

Onset = collections.namedtuple("Onset", "index frame delay")
Note = collections.namedtuple("Note", "time note stamp")


def frames_to_ms(frames, rate):
    return frames * 1000.0 / rate


def onsets(x, first, period, threshold_db=-30.0):
    """The first sound after each grid frame. Only slots recorded completely; sound already there at the note's frame
    (the last note's tail) is no onset: delay None, like a note without any sound."""
    peak = max((abs(v) for v in x), default=0.0)
    limit = peak * 10 ** (threshold_db / 20.0)
    out = []
    k = 0
    while first + k * period + period <= len(x):
        frame = first + k * period
        delay = None
        if peak > 0 and abs(x[frame]) <= limit:
            for i in range(frame, frame + period):
                if abs(x[i]) > limit:
                    delay = i - frame
                    break
        out.append(Onset(k, frame, delay))
        k += 1
    return out


def summary(found, classes=2):
    """Per class (grid index modulo classes): n, missing, median, min, max of the delays."""
    out = {}
    for c in range(classes):
        all_of = [o for o in found if o.index % classes == c]
        d = [o.delay for o in all_of if o.delay is not None]
        out[c] = {"n": len(d), "missing": len(all_of) - len(d), "median": statistics.median(d) if d else None,
                  "min": min(d) if d else None, "max": max(d) if d else None}
    return out


def stamp_diff(a, b):
    """a - b on the 14-bit stamp circle, in [-8192, 8191]."""
    return (a - b + STAMP_MOD // 2) % STAMP_MOD - STAMP_MOD // 2


def stamped_notes(events):
    """events: (time, 'cN cinX aa bb cc') as usbdecode.py --events prints them, in order. Note-ons with the stamp of
    the same write, stamp None when there is none (the original's Live button sends no F5 at all, E400)."""
    out = []
    stamp, stamp_time = None, None
    for t, text in events:
        p = text.split()
        if len(p) < 3 or not p[1].startswith("cin"):
            continue
        b = [int(v, 16) for v in p[2:]]
        if b[0] == 0xF5 and len(b) >= 3:
            stamp, stamp_time = b[1] | b[2] << 7, t
        elif b[0] & 0xF0 == 0x90 and len(b) >= 3 and b[2] > 0:
            valid = stamp_time is not None and t - stamp_time <= SAME_WRITE_S
            out.append(Note(t, b[1], stamp if valid else None))
    return out


def stamp_classes(notes, first, period, classes=2, rate=44100, first_index=0):
    """Per class the median of (stamp - grid frame) on the stamp circle, None without stamps. The grid index comes from
    the note's time relative to the first note (first_index: its index), so dropped notes do not shift the classes."""
    per = {c: [] for c in range(classes)}
    if not notes:
        return {c: None for c in range(classes)}
    t0 = notes[0].time
    for n in notes:
        k = first_index + round((n.time - t0) * rate / period)
        if n.stamp is not None:
            per[k % classes].append((n.stamp - (first + k * period)) % STAMP_MOD)
    out = {}
    for c, v in per.items():
        if not v:
            out[c] = None
            continue
        rel = [stamp_diff(s, v[0]) for s in v]  # around the first value: a median across the wrap would be wrong
        out[c] = int(v[0] + statistics.median(rel)) % STAMP_MOD
    return out


def flash_reads(lines):
    """usbdecode.py lines -> [(address hex, 16 bytes hex)]: what the original read from the flash at open (E026, E040),
    each address once. scripts/latency-wine.sh answers these reads from this table under Wine (E403)."""
    out, seen, addr = [], set(), None
    for line in lines:
        m = re.search(r"setup (0x[0-9a-f]+) req=(\d+) wV=\S+ wI=(\d+) wL=(\d+) ([0-9a-f]*)\s*$", line)
        if not m:
            continue
        rt, req, wi, wl, data = m.group(1), int(m.group(2)), int(m.group(3)), int(m.group(4)), m.group(5)
        if rt in ("0x41", "0x40") and req == 1 and wi == 0x12 and wl == 3 and len(data) == 6:
            addr = data
        elif rt in ("0xc1", "0xc0") and req == 0x81 and wi == 0x30 and len(data) == 32 and addr is not None:
            if addr not in seen:
                seen.add(addr)
                out.append((addr, data))
            addr = None
    return out


def cmd_flash(a):
    decoder = os.path.join(os.path.dirname(os.path.abspath(__file__)), "usbdecode.py")
    text = subprocess.run([sys.executable, "-I", decoder, a.file, a.addr], capture_output=True, text=True).stdout
    for addr, data in flash_reads(text.splitlines()):
        print(addr, data)


def read_wav(path):
    """Loudest channel per frame, 0..1, and the rate. 16-bit PCM, what both test hosts write."""
    with wave.open(path, "rb") as w:
        if w.getsampwidth() != 2:
            sys.exit(f"{path}: expected 16-bit PCM")
        ch, rate = w.getnchannels(), w.getframerate()
        pcm = array.array("h")
        pcm.frombytes(w.readframes(w.getnframes()))
    if sys.byteorder == "big":
        pcm.byteswap()
    return [max(abs(pcm[i + c]) for c in range(ch)) / 32768.0 for i in range(0, len(pcm), ch)], rate


def cmd_wav(a):
    x, rate = read_wav(a.file)
    found = [o for o in onsets(x, a.first, a.period, a.threshold_db) if o.index >= a.skip]
    for o in found:
        print(f"note {o.index:3d} frame {o.frame:8d} delay {'-' if o.delay is None else o.delay}")
    for c, s in summary(found, a.classes).items():
        if s["median"] is None:
            print(f"class {c}: no sound in {s['missing']} notes")
            continue
        line = (f"class {c}: {s['n']} notes, {s['missing']} missing, median {s['median']:.0f} frames = "
                f"{frames_to_ms(s['median'], rate):.1f} ms at {rate}, min {s['min']}, max {s['max']}")
        if a.reported is not None:
            line += f", median - reported ({a.reported}) = {s['median'] - a.reported:.0f}"
        print(line)


def cmd_usb(a):
    decoder = os.path.join(os.path.dirname(os.path.abspath(__file__)), "usbdecode.py")
    text = subprocess.run([sys.executable, "-I", decoder, a.file, a.addr, "--events"], capture_output=True, text=True).stdout
    events = []
    for line in text.splitlines():
        m = re.match(r"\s*([\d.]+) ep=0x06\s+(.*)", line)
        if m:
            events.append((float(m.group(1)), m.group(2)))
    notes = stamped_notes(events)
    for n in notes:
        print(f"{n.time:10.6f} note {n.note} stamp {'none' if n.stamp is None else n.stamp}")
    s = stamp_classes(notes, a.first, a.period, a.classes, first_index=a.first_index)
    for c, v in s.items():
        print(f"class {c}: " + ("no F5" if v is None else f"stamp - grid = {v}"))
    if a.classes == 2 and None not in s.values():
        print(f"class 0 - class 1 = {stamp_diff(s[0], s[1])} frames")


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="mode", required=True)
    w = sub.add_parser("wav")
    w.add_argument("file")
    w.add_argument("--threshold-db", type=float, default=-30.0)
    w.add_argument("--skip", type=int, default=0)
    w.add_argument("--reported", type=int)
    u = sub.add_parser("usb")
    u.add_argument("file")
    u.add_argument("addr")
    u.add_argument("--first-index", type=int, default=0)
    for q in (w, u):
        q.add_argument("--first", type=int, required=True)
        q.add_argument("--period", type=int, default=44100)
        q.add_argument("--classes", type=int, default=2)
    f = sub.add_parser("flash", help="the table for VTI_FLASH_REPLAY, on stdout")
    f.add_argument("file")
    f.add_argument("addr")
    a = p.parse_args()
    {"wav": cmd_wav, "usb": cmd_usb, "flash": cmd_flash}[a.mode](a)


if __name__ == "__main__":
    main()
