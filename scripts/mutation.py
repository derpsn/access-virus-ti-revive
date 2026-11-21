#!/usr/bin/env python3
"""mutation.py - how much the unit tests really check (findings/testing.md): change one operator in a source file,
rebuild vtitests, run it; a mutant the tests still pass ("survived") marks code whose behaviour no test pins down.

  python3 -I scripts/mutation.py --tree COPY --build BUILD FILE... [--max N] [--json OUT]
COPY is a copy of the repository (never the working tree: an interrupted run would leave a mutant behind), BUILD a
configured build folder of that copy; FILE paths are relative to COPY, e.g. native/src/device/dumppacer.h.
Mutations: < <= > >= == != && || true/false, + 1 / - 1. A mutant that does not compile is not counted.
"""
import argparse
import json
import os
import re
import subprocess
import sys
import time

SWAP = {"<": "<=", "<=": "<", ">": ">=", ">=": ">", "==": "!=", "!=": "==", "&&": "||", "||": "&&",
        "true": "false", "false": "true", "+ 1": "- 1", "- 1": "+ 1"}
# binary operators need spaces around them (keeps templates, -> and #include <...> out); words need word bounds
PATTERN = re.compile(r"(?<= )(<=|>=|==|!=|&&|\|\||<|>)(?= )|\b(true|false)\b|(?<= )([+-] 1)\b")
STRING = re.compile(r'"(?:\\.|[^"\\])*"' + r"|'(?:\\.|[^'\\])'")


def mutants(text):
    """every mutation place in text: {line (1-based), pos (offset in text), old, new}"""
    out, off = [], 0
    for no, line in enumerate(text.splitlines(keepends=True), 1):
        code = line.split("//", 1)[0]
        if not code.lstrip().startswith("#"):
            blank = STRING.sub(lambda m: " " * len(m.group(0)), code)
            for m in PATTERN.finditer(blank):
                old = m.group(0)
                out.append({"line": no, "pos": off + m.start(), "old": old, "new": SWAP[old]})
        off += len(line)
    return out


def apply(text, m):
    return text[:m["pos"]] + m["new"] + text[m["pos"] + len(m["old"]):]


def run(cmd, timeout):
    try:
        return subprocess.run(cmd, capture_output=True, timeout=timeout).returncode
    except subprocess.TimeoutExpired:
        return "timeout"


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("files", nargs="+")
    ap.add_argument("--tree", required=True)
    ap.add_argument("--build", required=True)
    ap.add_argument("--max", type=int, default=0, help="at most N mutants per file (evenly spread)")
    ap.add_argument("--json")
    a = ap.parse_args()
    if os.path.realpath(a.tree) == os.path.realpath(os.path.join(os.path.dirname(__file__), "..")):
        sys.exit("mutation.py: --tree must be a copy, not the working tree")
    build = ["cmake", "--build", a.build, "--target", "vtitests", "-j", str(os.cpu_count() or 4)]
    if run(build, 1800) != 0:
        sys.exit("mutation.py: the unmutated copy does not build")
    # after the build: a fresh folder has none before; multi-config, single-config (bin/) or an old flat layout
    tests = next((p for p in (os.path.join(a.build, *d, "vtitests") for d in (("bin", "Release"), ("bin",), ()))
                  if os.path.exists(p)), os.path.join(a.build, "vtitests"))
    if run([tests], 600) != 0:
        sys.exit("mutation.py: the unmutated copy's tests fail")
    results = []
    for f in a.files:
        path = os.path.join(a.tree, f)
        with open(path, encoding="utf-8") as fh:
            orig = fh.read()
        ms = mutants(orig)
        if a.max and len(ms) > a.max:
            ms = [ms[i * len(ms) // a.max] for i in range(a.max)]
        try:
            for m in ms:
                with open(path, "w", encoding="utf-8") as fh:
                    fh.write(apply(orig, m))
                t0 = time.time()
                if run(build, 900) != 0:
                    verdict = "no build"
                else:
                    rc = run([tests], 120)
                    verdict = "survived" if rc == 0 else "killed"
                r = dict(file=f, line=m["line"], old=m["old"], new=m["new"], verdict=verdict, s=round(time.time() - t0))
                results.append(r)
                print("%-8s %s:%d %s -> %s" % (verdict, f, m["line"], m["old"], m["new"]), flush=True)
        finally:
            with open(path, "w", encoding="utf-8") as fh:
                fh.write(orig)
    run(build, 1800)  # leave the copy's build as the source is
    for f in a.files:
        rs = [r for r in results if r["file"] == f and r["verdict"] != "no build"]
        k = sum(r["verdict"] == "killed" for r in rs)
        print("%s: %d of %d killed (%s)" % (f, k, len(rs), "%.0f %%" % (100.0 * k / len(rs)) if rs else "-"))
    rs = [r for r in results if r["verdict"] != "no build"]
    k = sum(r["verdict"] == "killed" for r in rs)
    print("mutation score: %d of %d killed (%s)" % (k, len(rs), "%.0f %%" % (100.0 * k / len(rs)) if rs else "-"))
    if a.json:
        with open(a.json, "w", encoding="utf-8") as fh:
            json.dump(results, fh, indent=1)


if __name__ == "__main__":
    main()
