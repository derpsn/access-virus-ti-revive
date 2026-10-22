#!/usr/bin/env python3
"""hil_check.py - pass/fail of one run of the automated hardware check (scripts/hil.sh, findings/testing.md).

nativehost plays C4 at the start of every second (--notes) through the plug-in and records Out 1; this script checks
the recording against that grid and the plug-in's log lines (stderr) against a clean start:
  python3 -I scripts/hil_check.py --log RUN.err [--wav RUN.wav --seconds 20] [--offline RUN.out] [--sleep --from T] [--gap A:B]
  python3 -I scripts/hil_check.py --folder DIR   (a whole Windows / Mac run: hil-win.ps1, hil-mac.command)
Exit 0 when every check passes, 1 otherwise. The limits come from clean runs on the TI Desktop (2026-10-19:
jitter 0.02 ms, max 1.5 ms off the grid, drift -3..-31 ppm, first note after 5-8 s).
"""
import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import timing_analysis  # noqa: E402

ON_GRID_S = 0.020  # an onset this close to the grid is a note; farther = an extra sound
FIRST_NOTE_S = 12.0  # start-up (connect, state upload, readiness probe) plus the bank cache check
MAX_DEV_MS = 5.0
MAX_JITTER_MS = 1.0
MAX_DRIFT_PPM = 100.0


def grid(onsets, period, duration, since=0.0, gaps=()):
    """onsets (s) against a grid of one note per period: notes lost, extra sounds, deviation, jitter, drift.
    Sounds before the first two notes a period apart are ignored (a release tail of the run before), and with since
    everything before that time (a suspend and the catch-up after the wake). Grid slots inside gaps ((from, to) in s:
    a planned pause without notes) are not counted as lost."""
    r = {"count": 0, "first_s": None, "last_s": None, "missing": 0, "offgrid": 0, "maxdev_ms": 0.0,
         "jitter_ms": 0.0, "drift_ppm": 0.0, "duration": duration, "period": period, "since": since}
    onsets = [t for t in onsets if t >= since]
    if not onsets:
        return r
    phases = sorted(t % period for t in onsets)
    p = phases[len(phases) // 2]
    dev = [((t - p + period / 2) % period) - period / 2 for t in onsets]
    on = [t for t, d in zip(onsets, dev) if abs(d) < ON_GRID_S]
    start = next((t for t in on if any(abs(u - t - period) < ON_GRID_S for u in on)), None)
    if start is None:
        return r
    notes = [t for t in on if t >= start]
    r["offgrid"] = sum(1 for t, d in zip(onsets, dev) if t > start and abs(d) >= ON_GRID_S)
    ks = [round((t - start) / period) for t in notes]
    paused = sum(1 for k in range(ks[-1] + 1) if any(a <= start + k * period < b for a, b in gaps))
    r.update(count=len(notes), first_s=start, last_s=notes[-1], missing=ks[-1] + 1 - paused - len(set(ks)))
    if len(notes) >= 2:
        n = len(notes)
        sk, st = sum(ks), sum(notes)
        skk, skt = sum(k * k for k in ks), sum(k * t for k, t in zip(ks, notes))
        b = (n * skt - sk * st) / (n * skk - sk * sk)
        a = (st - b * sk) / n
        res = [t - (a + b * k) for k, t in zip(ks, notes)]
        r["maxdev_ms"] = max(abs(x) for x in res) * 1000
        r["jitter_ms"] = (sum(x * x for x in res) / n) ** 0.5 * 1000
        r["drift_ppm"] = (b / period - 1) * 1e6
    return r


def grid_verdicts(r):
    """(name, ok, detail) per check of a grid() result"""
    first = r["first_s"]
    return [
        ("start: first note within %g s" % FIRST_NOTE_S, first is not None and first <= r["since"] + FIRST_NOTE_S,
         "first note at %s s%s" % ("-" if first is None else "%.2f" % first,
                                   " (judged from %g s)" % r["since"] if r["since"] else "")),
        ("notes: none lost", r["count"] > 0 and r["missing"] == 0, "%d notes, %d lost" % (r["count"], r["missing"])),
        ("notes: no extra notes", r["offgrid"] == 0, "%d sounds off the grid" % r["offgrid"]),
        ("timing: every note within %g ms of the grid" % MAX_DEV_MS, r["count"] > 1 and r["maxdev_ms"] <= MAX_DEV_MS,
         "max %.2f ms" % r["maxdev_ms"]),
        ("timing: jitter below %g ms" % MAX_JITTER_MS, r["count"] > 1 and r["jitter_ms"] <= MAX_JITTER_MS,
         "%.2f ms" % r["jitter_ms"]),
        ("timing: drift below %g ppm" % MAX_DRIFT_PPM, r["count"] > 1 and abs(r["drift_ppm"]) <= MAX_DRIFT_PPM,
         "%+.0f ppm" % r["drift_ppm"]),
        ("end: notes until the end", r["last_s"] is not None and r["last_s"] >= r["duration"] - 2.5 * r["period"],
         "last note at %s s of %g" % ("-" if r["last_s"] is None else "%.2f" % r["last_s"], r["duration"])),
    ]


def log_verdicts(text, sleep=False):
    """(name, ok, detail) for the plug-in's log of one run; after a sleep the stood-still restart is expected"""
    ready = re.search(r"Virus ready after ([\d.]+) s", text)
    out = [("log: Virus ready", bool(ready), ready.group(0) if ready else "no \"Virus ready after\" line"),
           ("log: sync marker", "sync marker at frame" in text and "no sync marker" not in text,
            "no sync marker" if "no sync marker" in text else "")]
    if not sleep:
        still = text.count("audio stood still")
        out.append(("log: audio never stood still", still == 0, "%d times" % still))
    return out


def offline_verdicts(out):
    """the export ran at the Virus' pace (about real time), not as fast as the host could"""
    m = re.search(r"offline part: ([\d.]+) s of audio in ([\d.]+) s wall time", out)
    if not m:
        return [("export: paced to the Virus", False, "no \"offline part\" line")]
    ratio = float(m.group(1)) / max(float(m.group(2)), 1e-6)
    return [("export: paced to the Virus", 0.8 <= ratio <= 1.25, "%.2fx real time" % ratio)]


def check_run(log, wav=None, seconds=20.0, offline=None, sleep=False, since=0.0, gaps=()):
    """every verdict of one run: the log, the recording against the grid, the export's pace"""
    with open(log, encoding="utf-8", errors="replace") as f:
        checks = log_verdicts(f.read(), sleep)
    if wav:
        x, rate = timing_analysis.read_wav(wav)
        checks += grid_verdicts(grid(timing_analysis.onsets(x, rate), 1.0, seconds, since, gaps))
    if offline:
        with open(offline, encoding="utf-8", errors="replace") as f:
            checks += offline_verdicts(f.read())
    return checks


# the items of the Windows and Mac runs (scripts/hil-win.ps1, scripts/hil-mac.command; hil.sh plays the same on
# Linux): length in s, how each is judged. H3 renders from 12 to 36 s and stops until 40 s; H6 kills the broker at 20 s
ITEMS = {
    "H1": {"seconds": 20},
    "H2-1": {"seconds": 20},
    "H2-2": {"seconds": 20},
    "H3": {"seconds": 54, "offline": True, "gaps": [(36.0, 40.0)]},
    "H6": {"seconds": 60, "since": 34.0},
}


def grade_folder(folder):
    """(all passed, report lines) for a results folder with NAME.err / .wav / .out per item of ITEMS"""
    lines, ok = [], True
    for name, item in ITEMS.items():
        p = os.path.join(folder, name)
        if not os.path.exists(p + ".err") or not os.path.exists(p + ".wav"):
            lines.append("%s: FAIL (not run)" % name)
            ok = False
            continue
        checks = check_run(p + ".err", p + ".wav", item["seconds"], p + ".out" if item.get("offline") else None,
                           since=item.get("since", 0.0), gaps=item.get("gaps", ()))
        lines += ["  %s  %s%s" % ("pass" if c else "FAIL", n, " (%s)" % d if d else "") for n, c, d in checks]
        passed = all(c for _, c, _ in checks)
        lines.append("%s: %s" % (name, "pass" if passed else "FAIL"))
        ok = ok and passed
    lines.append("ALL PASS" if ok else "FAILED")
    return ok, lines


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--folder", help="grade every item of a Windows / Mac results folder (ITEMS)")
    ap.add_argument("--log", help="the plug-in's log of the run (nativehost stderr)")
    ap.add_argument("--wav")
    ap.add_argument("--seconds", type=float, default=20.0)
    ap.add_argument("--offline", help="nativehost stdout of an --offline run")
    ap.add_argument("--sleep", action="store_true", help="the run included a suspend")
    ap.add_argument("--from", dest="since", type=float, default=0.0, help="judge only the notes from this time on")
    ap.add_argument("--gap", action="append", default=[], help="A:B - a planned pause without notes (s)")
    a = ap.parse_args()
    if a.folder:
        ok, lines = grade_folder(a.folder)
        print("\n".join(lines))
        return 0 if ok else 1
    if not a.log:
        ap.error("--log or --folder")
    checks = check_run(a.log, a.wav, a.seconds, a.offline, a.sleep, a.since,
                       [tuple(float(v) for v in g.split(":")) for g in a.gap])
    for name, ok, detail in checks:
        print("  %s  %s%s" % ("pass" if ok else "FAIL", name, " (%s)" % detail if detail else ""))
    return 0 if all(ok for _, ok, _ in checks) else 1


if __name__ == "__main__":
    sys.exit(main())
