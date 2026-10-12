#!/usr/bin/env python3
"""Run the current portable control-client code without linking Mac audio APIs.

The implementation is extracted verbatim from VirusClient.cpp, not reimplemented.
This checks control transport only; it does not validate the Mac dylib or audio.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, help="alternate VirusClient.cpp for a negative control")
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    source = (args.source or here.parent / "VirusClient.cpp").read_text()
    markers = ["static void vlog(", "// ------------------------------------------------------------------------------------------- broker client",
               "enum { MSG_HELLO", "static char g_device;"]
    for marker in markers:
        if source.count(marker) != 1:
            raise ValueError("expected one control-source marker: " + marker)
    positions = [source.index(marker) for marker in markers]
    if positions != sorted(positions):
        raise ValueError("control-source markers are out of order")
    headers = "\n".join("#include <" + name + ">" for name in [
        "cerrno", "cstdarg", "cstdint", "cstring", "cstdio", "ctime", "mutex",
        "pthread.h", "unistd.h", "sys/socket.h", "sys/un.h"])
    implementation = headers + "\n" + source[positions[0]:positions[1]] + source[positions[2]:positions[3]]
    with tempfile.TemporaryDirectory(prefix="tirevive-control-") as temp:
        fragment = Path(temp) / "control.cpp"
        fragment.write_text(implementation)
        binary = Path(temp) / "control-test"
        compiler = os.environ.get("CXX", "c++")
        subprocess.run([compiler, "-std=c++17", "-pthread", "-O2",
                        "-DTIREVIVE_CONTROL_TEST_SOURCE=" + json.dumps(str(fragment)),
                        str(here / "test_control_transactions.cpp"), "-o", str(binary)], check=True)
        return subprocess.run([str(binary)], timeout=15).returncode


if __name__ == "__main__":
    raise SystemExit(main())
