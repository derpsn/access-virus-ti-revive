#!/usr/bin/env python3
"""evidence.py - check, index and query findings/evidence.md (one "### Ennn" section per observation).

Entry format (findings/evidence.md explains it at the top):

  ### E505

  The claim, in our own words (one or more paragraphs).

  - **Date:** 2026-11-13
  - **Pointer:** capture + frame, binary SHA256 prefix + address, or file + line
  - **Method:** command or procedure
  - **Confidence:** HIGH / MED / LOW / open, with a short note what each part rests on
  - **Hardware:** yes - TI2 Desktop, Linux, build 1a2b3c4  |  no  |  n/a (static analysis, docs)
  - **Status:** corrected by E510   (only when a later entry corrects this one)

Rules the check enforces: ids ascending without duplicates (known gaps excepted), ISO dates, every required field,
a level word in Confidence, Hardware from E505 on, and a "corrected by" Status wherever a later entry says
"corrects Ennn". The topic index (findings/evidence-index.md) is generated from the entries and must be current.

  python3 -I scripts/evidence.py check            exit 1 with a list of problems
  python3 -I scripts/evidence.py index            rewrite findings/evidence-index.md
  python3 -I scripts/evidence.py list --open      entries with an open part (also --level LOW|MED, --no-hardware,
                                                  --topic sync, --corrected)
"""
import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
EVIDENCE = os.path.join(ROOT, "findings", "evidence.md")
INDEX = os.path.join(ROOT, "findings", "evidence-index.md")

FIELDS = ["Date", "Pointer", "Method", "Confidence", "Hardware", "Status"]
REQUIRED = ["Date", "Pointer", "Method", "Confidence"]
HARDWARE_FROM = 505          # entries before this one were written before the Hardware field existed
KNOWN_GAPS = {89, 90, 131}   # numbers never used (E131 is named in the old log but was never written down)
LEVELS = ["LOW", "MED", "HIGH"]  # lowest first

HEAD = re.compile(r"^### E(\d{3,})\s*$")
FIELD = re.compile(r"^- \*\*(%s):\*\* ?(.*)$" % "|".join(FIELDS))
CORRECTS = re.compile(r"\b(?:corrects|revises)\s+E(\d{3,})\b|\bE(\d{3,}) corrected\b", re.I)

# Topic -> pattern over claim + pointer + method. A match puts the entry under the topic; an entry may be in several.
TOPICS = [
    ("Firmware analysis (8051, TUSB3200A, DSP56300)",
     r"firmware\.bin|8051|TUSB3200|DSP56|bootloader|DFU|flash (?:erase|program)"),
    ("USB transport: endpoints, iso streams, stalls, errors",
     r"\bEP 0x|endpoint|\biso\b|isochronous|LIBUSB_ERROR|WinUsb|libusb|error 31|stall|usbmon|URB"),
    ("SysEx and parameter protocol",
     r"SysEx|\bF0 00 20 33|\bCC\b|NRPN|parameter page|edit buffer|\b7[0-9A-F] [0-9A-F]{2} [0-9A-F]{2}\b"),
    ("Patch banks and start-up sync", r"\bbank|RAM [A-D]\b|ROM [A-Z]\b|memsync|start-up sync|bank sync|patch name|dump"),
    ("Timing, stamps, latency, host blocks",
     r"stamp|latency|jitter|timeline|marker 511|block size|safety net|drift|late notes?|\bsync reports?"),
    ("Sound problems: crackle, silence, dropouts", r"crackl|\bclicks?\b|silent|silence|dropout|\bdrops?\b|glitch|distort"),
    ("Access software (Virus Control, Control Center, installer)",
     r"Virus Control|Control Center|Access installer|MSI|CAInstaller|vtiaudio|vtimidi|VirusUSB|skin|editor\.xml|remotedevice"),
    ("Windows", r"Windows|Win1[01]|\bWDM\b|\bINF\b|pnputil|registry|\bMSI\b|Pester|WinUSB|bug ?check"),
    ("macOS and Audio Unit",
     r"macOS|\bMac\b|Catalina|Sonoma|Sequoia|Core ?MIDI|Core Audio|\bAU\b|Audio Unit|Rosetta|Logic"),
    ("Linux", r"\bLinux\b|ALSA|snd-usb-audio|udev|PipeWire|JACK|Wine"),
    ("Models: Snow, Polar, Keyboard, TI2", r"\bSnow\b|\bPolar\b|Keyboard|\bTI2\b|\bTI 2\b|model detection"),
    ("DAWs", r"REAPER|Ableton|\bLive 1\d|Logic \d|Logic Pro|Cubase|FL Studio|Bitwig|Studio One|Pro Tools"),
    ("User reports (GitHub issues, forums)", r"(?<![\w&])#\d{1,3}\b|Reddit|reporter|forum"),
    ("Safety (rule 2): sends that can hurt the Virus",
     r"rule 2|brick|flood|hung its audio|power cycle|Original Patch|sendguard"),
]


def parse(text):
    """entries in file order: {id, claim, fields{}, line}; lines before the first heading are the preamble"""
    entries, cur = [], None
    for no, line in enumerate(text.splitlines(), 1):
        m = HEAD.match(line)
        if m:
            cur = {"id": int(m.group(1)), "claim": [], "fields": {}, "line": no}
            entries.append(cur)
            continue
        if cur is None:
            continue
        f = FIELD.match(line)
        if f:
            cur["fields"][f.group(1)] = f.group(2).strip()
        elif line.strip() and not cur["fields"]:
            cur["claim"].append(line.strip())
        elif line.strip():
            cur.setdefault("stray", []).append(no)
    for e in entries:
        e["claim"] = "\n".join(e["claim"])
    return entries


def levels(conf):
    """level words named in a Confidence text, lowest first (MEDIUM counts as MED), and whether a part is open"""
    found = {"MED" if w == "MEDIUM" else w for w in re.findall(r"\b(HIGH|MEDIUM|MED|LOW)\b", conf)}
    return [lv for lv in LEVELS if lv in found], bool(re.search(r"\bopen\b", conf, re.I))


def corrections(entries):
    """{corrected id: [ids of the later entries that say they correct it]}"""
    out = {}
    for e in entries:
        text = e["claim"] + " " + e["fields"].get("Confidence", "")
        for m in CORRECTS.finditer(text):
            target = int(m.group(1) or m.group(2))
            if target < e["id"] and e["id"] not in out.get(target, []):
                out.setdefault(target, []).append(e["id"])
    return out


def check(entries):
    errs, prev, seen = [], 0, set()
    for e in entries:
        n, f = e["id"], e["fields"]
        tag = "E%03d (line %d)" % (n, e["line"])
        if n in seen:
            errs.append(tag + ": duplicate id")
        elif n <= prev:
            errs.append(tag + ": not ascending (after E%03d)" % prev)
        else:
            missing = [g for g in range(prev + 1, n) if g not in KNOWN_GAPS]
            if missing:
                errs.append(tag + ": numbers skipped: " + ", ".join("E%03d" % g for g in missing))
        seen.add(n)
        prev = max(prev, n)
        if not e["claim"]:
            errs.append(tag + ": no claim text")
        for k in REQUIRED:
            if not f.get(k):
                errs.append(tag + ": no " + k)
        if f.get("Date") and not re.fullmatch(r"\d{4}-\d{2}-\d{2}", f["Date"]):
            errs.append(tag + ": Date is not YYYY-MM-DD: " + f["Date"])
        if f.get("Confidence"):
            lv, op = levels(f["Confidence"])
            if not lv and not op and "per item" not in f["Confidence"]:
                errs.append(tag + ": Confidence names no level (HIGH/MED/LOW/open)")
        if n >= HARDWARE_FROM and not re.match(r"(yes|no|n/a)\b", f.get("Hardware", "")):
            errs.append(tag + ": Hardware must start with yes / no / n/a (required from E%03d)" % HARDWARE_FROM)
        if e.get("stray"):
            errs.append(tag + ": text after the fields (lines %s)" % ", ".join(map(str, e["stray"])))
    status = {e["id"]: e["fields"].get("Status", "") for e in entries}
    for target, by in sorted(corrections(entries).items()):
        if target not in status:
            errs.append("E%03d is corrected by %s but does not exist" % (target, ", ".join("E%03d" % b for b in by)))
            continue
        for b in by:
            if "E%03d" % b not in status[target]:
                errs.append("E%03d: Status must say 'corrected by E%03d' (E%03d says it corrects it)" % (target, b, b))
    return errs


def title(claim, width=110):
    """first sentence of the claim, cut at a word boundary"""
    t = re.split(r"(?<=[A-Za-z0-9)])\. ", claim.replace("\n", " "), maxsplit=1)[0].strip()
    if len(t) > width:
        t = t[:width].rsplit(" ", 1)[0] + " ..."
    return t


def topics_of(e):
    text = " ".join([e["claim"], e["fields"].get("Pointer", ""), e["fields"].get("Method", "")])
    return [name for name, pat in TOPICS if re.search(pat, text)]


def marks(e, corr):
    lv, op = levels(e["fields"].get("Confidence", ""))
    out = []
    if lv and lv[0] != "HIGH":
        out.append(lv[0])
    if op:
        out.append("open")
    if e["fields"].get("Hardware", "").startswith("no"):
        out.append("not on hardware")
    if e["id"] in corr:
        out.append("corrected by " + ", ".join("E%03d" % b for b in corr[e["id"]]))
    return out


def render_index(entries):
    corr = corrections(entries)
    out = ["# Evidence index", "",
           "_Generated by `python3 -I scripts/evidence.py index` from [evidence.md](evidence.md); do not edit by "
           "hand. Topics come from keyword patterns, so an entry can sit under several topics or miss one: search "
           "evidence.md as well._", "",
           "Entries: %d. Marks after an entry: its lowest confidence level when below HIGH, `open` when a part is "
           "still open, `not on hardware` and later corrections." % len(entries), ""]
    grouped = [(name, [e for e in entries if name in topics_of(e)]) for name, _ in TOPICS]
    grouped.append(("Other", [e for e in entries if not topics_of(e)]))
    for name, rows in grouped:
        if not rows:
            continue
        out += ["## %s (%d)" % (name, len(rows)), ""]
        for e in rows:
            m = marks(e, corr)
            out.append("- [E%03d](evidence.md#e%03d) %s%s" % (e["id"], e["id"], title(e["claim"]),
                                                              " (" + "; ".join(m) + ")" if m else ""))
        out.append("")
    return "\n".join(out)


def select(entries, a):
    corr = corrections(entries)
    for e in entries:
        f = e["fields"]
        lv, op = levels(f.get("Confidence", ""))
        if a.open and not op:
            continue
        if a.level and (not lv or LEVELS.index(lv[0]) > LEVELS.index(a.level)):
            continue
        if a.no_hardware and not f.get("Hardware", "").startswith("no"):
            continue
        if a.corrected and e["id"] not in corr:
            continue
        if a.topic and not any(a.topic.lower() in t.lower() for t in topics_of(e)):
            continue
        yield e


def read(path):
    with open(path, encoding="utf-8") as fh:
        return fh.read()


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--file", default=EVIDENCE)
    ap.add_argument("--index-file", default=INDEX)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("check")
    sub.add_parser("index")
    ls = sub.add_parser("list")
    ls.add_argument("--open", action="store_true")
    ls.add_argument("--level", choices=["LOW", "MED"])
    ls.add_argument("--no-hardware", action="store_true")
    ls.add_argument("--corrected", action="store_true")
    ls.add_argument("--topic")
    a = ap.parse_args(argv)
    entries = parse(read(a.file))
    if a.cmd == "check":
        errs = check(entries)
        if not os.path.exists(a.index_file) or read(a.index_file) != render_index(entries):
            errs.append("evidence-index.md is out of date: run python3 -I scripts/evidence.py index")
        for x in errs:
            print(x)
        print("%d entries, %d problems" % (len(entries), len(errs)))
        return 1 if errs else 0
    if a.cmd == "index":
        with open(a.index_file, "w", encoding="utf-8", newline="\n") as fh:
            fh.write(render_index(entries))
        print("wrote %s (%d entries)" % (a.index_file, len(entries)))
        return 0
    n = 0
    for e in select(entries, a):
        n += 1
        print("E%03d  %-40s  %s" % (e["id"], e["fields"].get("Confidence", "")[:40], title(e["claim"], 90)))
    print("%d entries" % n)
    return 0


if __name__ == "__main__":
    sys.exit(main())
