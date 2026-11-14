#!/usr/bin/env python3
"""coverage.py - line coverage of native/src by the unit tests (vtitests), with a threshold (findings/testing.md).

Builds a separate coverage build (Debug, --coverage -O0), runs vtitests with an empty HOME (no Access files, no user
state), reads gcov's JSON for vticore and vtitests and merges the hits per source line: header-only code runs from
the test objects, so a line counts as covered when any object ran it. Reports the total and the weakest files.

  python3 -I scripts/coverage.py                       build in build-cov/, run, report
  python3 -I scripts/coverage.py --min 95              ... and exit 1 below 95 %
  python3 -I scripts/coverage.py --no-build --json OUT reuse build-cov/ as built, write per-file numbers

Never point --build at build-native or pass FETCHCONTENT_BASE_DIR=build-native/_deps: the coverage flags would land
in that build's cache (old log, 0.8.8 notes). Only Linux/GCC is supported (gcov --json-format).
"""
import argparse
import glob
import gzip
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.realpath(os.path.join(HERE, ".."))
SRC = os.path.join(ROOT, "native", "src")
OBJ_DIRS = ["vticore.dir", "vtitests.dir"]
CLEAN_ENV = ["XDG_CONFIG_HOME", "WINEPREFIX", "VTI_ACCESS_DIR"]


def merge(reports, src_root=SRC):
    """gcov JSON reports -> {source path: {line: hit?}} for files under src_root; a line is hit if any report ran it"""
    lines = {}
    for rep in reports:
        cwd = rep.get("current_working_directory", "")
        for f in rep.get("files", []):
            path = os.path.realpath(os.path.join(cwd, f["file"]))
            if not path.startswith(src_root + os.sep):
                continue
            per = lines.setdefault(path, {})
            for ln in f.get("lines", []):
                no = ln["line_number"]
                per[no] = per.get(no, False) or ln.get("count", 0) > 0
    return lines


def summary(lines, src_root=SRC):
    """(total covered, total lines, [(relative path, covered, lines)] weakest first)"""
    files = []
    for path, per in lines.items():
        if per:
            files.append((os.path.relpath(path, src_root), sum(per.values()), len(per)))
    files.sort(key=lambda t: (t[1] / t[2], t[0]))
    return sum(f[1] for f in files), sum(f[2] for f in files), files


def gcov_reports(build):
    out = []
    for d in OBJ_DIRS:
        objdir = os.path.join(build, "CMakeFiles", d)
        for gcda in glob.glob(os.path.join(objdir, "**", "*.gcda"), recursive=True):
            r = subprocess.run(["gcov", "--json-format", "--stdout", gcda], capture_output=True,
                               cwd=os.path.dirname(gcda), env=dict(os.environ, LC_ALL="C"))
            if r.returncode != 0:
                sys.exit("gcov failed on %s: %s" % (gcda, r.stderr.decode(errors="replace")[:300]))
            data = r.stdout
            if data[:2] == b"\x1f\x8b":
                data = gzip.decompress(data)
            for chunk in data.decode().splitlines():
                if chunk.strip():
                    out.append(json.loads(chunk))
    return out


def build_and_run(build):
    flags = "--coverage -O0"
    subprocess.run(["cmake", "-S", os.path.join(ROOT, "native"), "-B", build, "-DCMAKE_BUILD_TYPE=Debug",
                    "-DCMAKE_CXX_FLAGS=" + flags, "-DCMAKE_C_FLAGS=" + flags], check=True)
    subprocess.run(["cmake", "--build", build, "--target", "vtitests", "-j", str(os.cpu_count() or 4)], check=True)
    for gcda in glob.glob(os.path.join(build, "**", "*.gcda"), recursive=True):
        os.remove(gcda)
    home = tempfile.mkdtemp(prefix="vti-cov-home-")
    try:
        env = {k: v for k, v in os.environ.items() if k not in CLEAN_ENV}
        env.update(HOME=home, LC_ALL="C")
        exe = next((p for p in (os.path.join(build, "bin", "Debug", "vtitests"), os.path.join(build, "vtitests"))
                    if os.path.exists(p)), None)
        if not exe:
            sys.exit("coverage.py: vtitests not found in %s" % build)
        r = subprocess.run([exe], env=env, cwd=build)
        if r.returncode != 0:
            sys.exit("vtitests failed (exit %d): coverage not measured" % r.returncode)
    finally:
        shutil.rmtree(home, ignore_errors=True)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--build", default=os.path.join(ROOT, "build-cov"))
    ap.add_argument("--no-build", action="store_true", help="use the .gcda files already in --build")
    ap.add_argument("--min", type=float, default=0.0, help="exit 1 when the total is below this percentage")
    ap.add_argument("--json")
    ap.add_argument("--show", type=int, default=10, help="weakest files to list")
    a = ap.parse_args(argv)
    build = os.path.realpath(a.build)
    if os.path.basename(build) == "build-native":
        sys.exit("coverage.py: use a separate build folder, not build-native")
    if not a.no_build:
        build_and_run(build)
    covered, total, files = summary(merge(gcov_reports(build)))
    if not total:
        sys.exit("coverage.py: no coverage data under %s (run without --no-build first)" % build)
    pct = 100.0 * covered / total
    print("native/src line coverage: %.1f %% (%d of %d lines, %d files)" % (pct, covered, total, len(files)))
    for rel, c, n in files[:a.show]:
        print("  %5.1f %%  %s (%d/%d)" % (100.0 * c / n, rel, c, n))
    if a.json:
        with open(a.json, "w", encoding="utf-8") as fh:
            json.dump({"percent": round(pct, 2), "covered": covered, "lines": total,
                       "files": {rel: [c, n] for rel, c, n in files}}, fh, indent=1)
    if pct < a.min:
        print("BELOW THE THRESHOLD %.1f %%" % a.min)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
