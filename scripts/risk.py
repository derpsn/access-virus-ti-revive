#!/usr/bin/env python3
"""risk.py - which core checklist items a test round needs, from the files the round changed.

Every changed file gets a risk class from scripts/checklists/risk.json (findings/testing.md):
  A  logic covered by unit tests, docs, tools that are not shipped: no manual items
  B  I/O, timing, threads, the send path: the rule's core items; on Linux scripts/hil.sh runs the automated ones
  C  platform code, installers, drivers, sleep/wake, rule-2 guards: the rule's core items on that platform by hand
A file no rule names counts as class C on every platform with every core item, so a new folder is never skipped
by accident. The round's own items (what the changes need beyond the core) are still written by hand.

  python3 scripts/risk.py                      report for the changes since main (committed and not)
  python3 scripts/risk.py --base v0.8.27       ... since another ref
  python3 scripts/risk.py --spec OUT.json --round 0.8.28
                                               also write a round spec skeleton for checklist.py
  python3 scripts/risk.py FILE...              report for these paths
"""
import argparse
import fnmatch
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ORDER = "ABC"
TITLES = {"linux": "Linux", "vm": "Windows 11 VM", "win": "Windows PC", "mac": "macOS"}  # core.json's sections


def classify(path, rmap):
    """the first rule whose pattern matches path; unknown files fall back to class C everywhere"""
    for r in rmap["rules"]:
        if any(fnmatch.fnmatchcase(path, p) for p in r["paths"]):
            return r
    return {"class": "C", "platforms": list(rmap["platforms"]), "core": list(rmap["core_keys"]),
            "why": "no rule names this file", "fallback": True}


def round_no(version):
    """the round's number for the rotation: the patch level of the version (0.8.28 -> 28), None if there is none"""
    parts = version.split("-")[0].split(".")
    return int(parts[-1]) if len(parts) == 3 and parts[-1].isdigit() else None


def select(files, rmap, round_no=None):
    """per platform: highest class, core items by hand, automated (scripts/hil.sh), rotated out, files.
    Rotation (rmap["rotation"]): core items that never found anything are left out for class B changes except in
    every n-th round; class C changes always get them."""
    keys = rmap["core_keys"]
    rot = rmap.get("rotation", {})
    due = round_no is None or round_no % rot.get("every", 1) == 0
    plats, top = {}, "A"
    for f in sorted(set(files)):
        r = classify(f, rmap)
        top = max(top, r["class"], key=ORDER.index)
        if r["class"] == "A":
            continue
        for p in r.get("platforms", []):
            e = plats.setdefault(p, {"class": "A", "keys": set(), "rotated": set(), "files": []})
            e["class"] = max(e["class"], r["class"], key=ORDER.index)
            for k in r.get("core", []):
                (e["rotated"] if r["class"] == "B" and not due and k in rot.get("keys", []) else e["keys"]).add(k)
            e["files"].append(f)
    for p, e in plats.items():
        auto = set(rmap.get("automated", {}).get(p, []))
        e["automated"] = [k for k in keys if k in e["keys"] | e["rotated"] and k in auto]
        e["manual"] = [k for k in keys if k in e["keys"] and k not in auto]
        e["rotated"] = [k for k in keys if k in e["rotated"] and k not in e["keys"] and k not in auto]
        del e["keys"]
    return {"class": top, "platforms": plats}


def spec(sel, rmap, core, rnd):
    """a round spec for checklist.py: the core items by hand per platform, sections in the core list's order"""
    ids, sections = [], []
    by_section = {s["id"]: s for s in core["sections"]}
    for p, sec_id in rmap["platforms"].items():
        e = sel["platforms"].get(p)
        if not e or not e["manual"]:
            continue
        ids += [it["id"] for it in by_section.get(sec_id, {"items": []})["items"] if it.get("key") in e["manual"]]
        sections.append({"id": sec_id, "title": TITLES.get(sec_id, p), "where": "", "prep": [], "items": []})
    # checklist.py shows the sections in this order; keep the core list's
    order = [s["id"] for s in core["sections"]]
    sections.sort(key=lambda s: order.index(s["id"]) if s["id"] in order else len(order))
    ids.sort(key=lambda i: [it["id"] for s in core["sections"] for it in s["items"]].index(i))
    auto = ["%s: %s" % (TITLES.get(rmap["platforms"][p], p), ", ".join(e["automated"]))
            for p, e in sel["platforms"].items() if e["automated"]]
    lead = "Core items run automated by scripts/hil.sh (%s)." % "; ".join(auto) if auto else ""
    return {"round": rnd, "core": "core.json", "core_items": ids, "lead": lead, "rules": [], "sections": sections}


def changed(base):
    """files changed since the merge base with base, committed or not, plus untracked files"""
    git = ["git", "-C", os.path.join(HERE, "..")]
    mb = subprocess.run(git + ["merge-base", base, "HEAD"], capture_output=True, text=True, check=True).stdout.strip()
    out = subprocess.run(git + ["diff", "--name-only", mb], capture_output=True, text=True, check=True).stdout
    new = subprocess.run(git + ["ls-files", "--others", "--exclude-standard"], capture_output=True, text=True,
                         check=True).stdout
    return [f for f in (out + new).splitlines() if f]


MUTATION_EXT = (".h", ".hpp", ".c", ".cpp")


def mutation_targets(files):
    """changed code that vtitests builds (native/src, the broker headers it includes): what mutation.py should change
    this round, so a surviving mutant shows code the round's tests do not pin down (findings/testing.md)"""
    out = []
    for f in sorted(set(files)):
        if not f.endswith(MUTATION_EXT):
            continue
        if f.startswith("native/src/") or (f.startswith("src/broker/") and f.endswith(".h")):
            out.append(f)
    return out


def report(sel, files, rmap):
    lines = ["%d changed files, highest class %s" % (len(set(files)), sel["class"])]
    for f in sorted(set(files)):
        r = classify(f, rmap)
        lines.append("  %s  %s  (%s)" % (r["class"], f, r.get("why", "")))
    if not sel["platforms"]:
        lines.append("No core items needed: unit tests and CI are enough for these changes.")
    for p, e in sel["platforms"].items():
        lines.append("%s: class %s; by hand: %s; automated (hil.sh): %s%s" % (
            p, e["class"], ", ".join(e["manual"]) or "-", ", ".join(e["automated"]) or "-",
            "; rotated out this round: " + ", ".join(e["rotated"]) if e["rotated"] else ""))
    mut = mutation_targets(files)
    if mut:
        lines.append("Mutation run for the changed code (on a copy of the tree, see findings/testing.md):")
        lines.append("  python3 -I scripts/mutation.py --tree COPY --build COPY/build " + " ".join(mut))
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("files", nargs="*")
    ap.add_argument("--base", default="main")
    ap.add_argument("--spec")
    ap.add_argument("--round", default="X")
    a = ap.parse_args()
    with open(os.path.join(HERE, "checklists", "risk.json"), encoding="utf-8") as f:
        rmap = json.load(f)
    files = a.files or changed(a.base)
    sel = select(files, rmap, round_no(a.round))
    print(report(sel, files, rmap))
    if a.spec:
        with open(os.path.join(HERE, "checklists", "core.json"), encoding="utf-8") as f:
            core = json.load(f)
        with open(a.spec, "w", encoding="utf-8") as f:
            json.dump(spec(sel, rmap, core, a.round), f, indent=1)
            f.write("\n")
        print("spec written to " + a.spec)


if __name__ == "__main__":
    sys.exit(main())
