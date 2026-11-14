#!/usr/bin/env python3
"""als_notes.py - where did a steady note grid land in an Ableton Live recording? (#39, native/tools/keystamp.c)

Reads a Live set (.als = gzip XML), takes the MIDI clips (arrangement and session), and lines the recorded note-ons
up with an even grid of --interval-ms: the residual of each note against the grid (first note = 0) shows a constant
offset as 0, a drift as a slope and a step as a jump. The tempo comes from the set (manual tempo) unless --bpm is
given. With --send keystamp.csv the reference is each note's real send time instead of the grid (so the sender's
own wake-up jitter drops out), and its lateness is shown next to it.

usage: python3 -I scripts/als_notes.py SET.als [--interval-ms 500] [--bpm 120] [--send keystamp.csv] [--clip N]
"""
import argparse
import csv
import gzip
import sys
import xml.etree.ElementTree as ET


def tempo(root):
    for t in root.iter("Tempo"):
        m = t.find("Manual")
        if m is not None:
            return float(m.get("Value"))
    return None


def clips(root):
    """[(name, clip start in beats, [(time in beats, key)])] for every MIDI clip with notes"""
    out = []
    for c in root.iter("MidiClip"):
        start = float(c.get("Time", "0"))
        name_el = c.find("Name")
        name = name_el.get("Value") if name_el is not None else ""
        loop_start = 0.0
        ls = c.find("Loop/LoopStart")
        if ls is not None:
            loop_start = float(ls.get("Value"))
        notes = []
        for kt in c.iter("KeyTrack"):
            key_el = kt.find("MidiKey")
            key = int(key_el.get("Value")) if key_el is not None else -1
            for ev in kt.iter("MidiNoteEvent"):
                if ev.get("IsEnabled", "true") == "true":
                    notes.append((float(ev.get("Time")) - loop_start, key))
        if notes:
            out.append((name, start, sorted(notes)))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("als")
    ap.add_argument("--interval-ms", type=float, default=500.0)
    ap.add_argument("--bpm", type=float)
    ap.add_argument("--send")
    ap.add_argument("--clip", type=int, default=-1, help="which clip (default: the one with the most notes)")
    a = ap.parse_args()

    root = ET.fromstring(gzip.open(a.als).read())
    bpm = a.bpm or tempo(root)
    if not bpm:
        sys.exit("no tempo in the set, give --bpm")
    found = clips(root)
    if not found:
        sys.exit("no MIDI clip with notes in the set")
    for i, (name, start, notes) in enumerate(found):
        print(f"clip {i}: '{name}' at beat {start:.3f}, {len(notes)} notes")
    name, start, notes = found[a.clip] if a.clip >= 0 else max(found, key=lambda c: len(c[2]))

    ms_per_beat = 60000.0 / bpm
    times = [(start + t) * ms_per_beat for t, _ in notes]
    keys = [k for _, k in notes]
    # with the send log the reference is when each note really left the sender (sent_us), not the schedule
    sent = {}
    if a.send:
        with open(a.send, newline="") as f:
            for row in csv.DictReader(f):
                sent[int(row["index"])] = (float(row["sent_us"]) / 1000, float(row["late_us"]) / 1000)

    def ref(idx):
        return sent[idx][0] if idx in sent else idx * a.interval_ms

    t0 = times[0]
    rows, missing = [], 0
    last_idx = -1
    for t, k in zip(times, keys):
        idx = round((t - t0) / a.interval_ms)
        if idx == last_idx:
            print(f"  extra note at {t - t0:.1f} ms (key {k}), skipped")
            continue
        missing += max(0, idx - last_idx - 1)
        last_idx = idx
        rows.append((idx, k, t - t0 - (ref(idx) - ref(0))))
    if rows and rows[0][0] != 0:
        print("  note: the first recorded note is not the first sent one; residuals are relative to it")

    print(f"\ntempo {bpm} BPM, grid {a.interval_ms} ms, {len(rows)} notes, {missing} missing")
    print(" index  key  at grid (s)  residual (ms)  step (ms)" + ("  sender late (ms)" if sent else ""))
    prev = None
    for idx, k, r in rows:
        step = "" if prev is None else f"{r - prev:+9.2f}"
        extra = f"  {sent[idx][1]:8.2f}" if idx in sent else ""
        print(f" {idx:5d}  {k:3d}  {idx * a.interval_ms / 1000:10.2f}  {r:+12.2f}  {step:>9}{extra}")
        prev = r

    # summary: least-squares slope (drift), largest step, first 2 bars vs the rest
    n = len(rows)
    if n >= 3:
        xs = [idx * a.interval_ms / 1000 for idx, _, _ in rows]
        ys = [r for _, _, r in rows]
        mx, my = sum(xs) / n, sum(ys) / n
        sxx = sum((x - mx) ** 2 for x in xs)
        slope = sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / sxx if sxx else 0.0
        steps = [(rows[i][0], ys[i] - ys[i - 1]) for i in range(1, n)]
        big = max(steps, key=lambda s: abs(s[1]))
        bar_ms = 4 * ms_per_beat
        early = [r for idx, _, r in rows if idx * a.interval_ms < 2 * bar_ms]
        late = [r for idx, _, r in rows if idx * a.interval_ms >= 2 * bar_ms]
        print(f"\ndrift {slope:+.3f} ms per s, residual range {min(ys):+.2f}..{max(ys):+.2f} ms, "
              f"largest step {big[1]:+.2f} ms before note {big[0]}")
        if early and late:
            print(f"first 2 bars: mean {sum(early) / len(early):+.2f} ms ({len(early)} notes); "
                  f"after: mean {sum(late) / len(late):+.2f} ms ({len(late)} notes)")


if __name__ == "__main__":
    main()
