---
name: tirevive-pr
description: Prepare a TI Revive pull request the way the maintainers expect - rule check (no Access material), build, hardware test notes, evidence entry, README update and PR description. Use before committing or opening a pull request in this repository.
---

# Prepare a TI Revive pull request

Work through the steps in order and report the result of each one to the human.

## 1. Rule check (stop if any fails)

- `git diff --stat` and `git status`: only files that belong to this change.
- No Access material: no binaries, firmware, PNG/XML resources, patch banks, decompiled Access code
  (`git diff | grep -iE "^\+.*(decompil|0x1800[0-9a-f]{5}.*\{)"` is a quick hint, not proof — read the diff).
- No personal data: user names, home paths from logs, serial numbers.
- No unrelated reformatting or refactoring.

## 2. Build what changed

- `native/` (the shipped plug-in and ASIO driver since 0.8.0): `cmake -S native -B build-native && cmake --build
  build-native && ctest --test-dir build-native` (needs the submodule `native/external/vst3sdk`); Windows needs
  MSVC, macOS Xcode.
- `src/broker/`: `make -C src/broker` (mingw-w64 for Windows, libusb on Linux/macOS).
- `src/mac/`: on a Mac, `scripts/mac-build-bridge.sh <repo> --install`.
- `src/wrapper/`: 0.7.x reference code, not shipped since 0.8.0; build it (`make -C src/wrapper`) only if the change
  touches it.
- Installer scripts: run their syntax check (`bash -n`, PowerShell parser) at least.
Record which builds you ran and their result. If a platform could not be built, say so.

## 3. Test

- Fixes and features in `native/`: write the failing test first, then the code; keep the coverage.
- With hardware: describe the setup (platform, Virus model, DAW, TI Revive version) and the steps and results.
- Without hardware: write "not tested on hardware" and explain what a tester should check.
- Never send untested vendor requests or SysEx to a real Virus; no firmware/flash operations.

## 4. Document

- New knowledge about the device or the Access software: add a `### Ennn` section to `findings/evidence.md`
  with the next free number (format at the top of that file): the claim with numbers and conditions, then the
  fields Date, Pointer, Method, Confidence (`HIGH` directly observed, `MED` strong inference, `LOW` plausible,
  `open`; per part when the parts differ) and Hardware (`yes - <model>, <OS>, <build>`, `no` for a fix that only
  passed unit tests, `n/a` for static analysis). Correct earlier entries with a new entry ("corrects Ennn") and set
  `Status: corrected by Ennn` on the old one instead of editing history away. Then
  `python3 -I scripts/evidence.py index` and `python3 -I scripts/evidence.py check` (must pass).
- Code change in `native/src` or the broker headers: `python3 scripts/risk.py` prints the mutation run for the
  changed files; a surviving mutant gets a test. Line coverage must stay >= 95 % (`scripts/coverage.py --min 95`,
  CI job `coverage`).
- Changed user-visible behaviour: update `README.md` (installation, "Good to know", status table); notable features
  also go on the website (`scripts/site/build.py`, see the `tirevive-site-check` skill).

## 5. Pull request description

Fill `.github/PULL_REQUEST_TEMPLATE.md` (Problem, Cause with the E-number, Change, Tested, Checklist); GitHub
puts it into the description. Under Tested name the builds you ran and the hardware setup, or "not tested on
hardware". One line says that an AI assistant helped.

Keep the PR to one problem. Small, well-explained changes get merged fastest.

## Maintainers

Fixes go onto the current round branch (`round-X.Y.Z`), not into one release each; hotfixes are the exception.
Keep `findings/HANDOFF.md` (current state, at most 150 lines) current in the same commit as each step; the details
of what happened go to the dated log in `findings/log/`. Commit as `derpsn`.
