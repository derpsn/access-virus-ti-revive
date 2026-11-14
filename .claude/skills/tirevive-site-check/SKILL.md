---
name: tirevive-site-check
description: Check every factual claim on the TI Revive website (scripts/site/build.py) and in the README against findings/evidence.md, HANDOFF and the test results, and fix what is outdated. Use before every site rebuild or release, after a test round (VM, bare metal, macOS, Linux), after new evidence entries, and whenever someone asks whether the website or README is still correct.
---

# Check the website and README against the evidence

The website text lives in `scripts/site/build.py` (`page_landing`, `features_section`, constants `TESTS`,
`COVERAGE`, `COVERAGE_DATE`); `site/src/` only holds the frame, the style sheet and screenshots. The README carries
the same claims and must say the same thing (the texts are kept identical). A claim is correct only if a source in
the repository supports it *today*. Newer evidence wins over older evidence.

## 1. Collect the claims

Read `features_section()` and `page_landing()` completely, then the matching README parts (features, timing table,
CPU table, sidechain, limits, roadmap/status table). List every claim that could go stale:

- numbers: timing table (notes on the grid, jitter, max, drift), CPU, editor speed, unit tests, coverage, rate limits,
  resampler error, block times
- "tested on / not yet on" statements: test matrix (model x OS), sidechain, ASIO, DAWs and their versions
- known issues and limits ("not yet", "being worked on", "not fed by the plug-in")
- requirements: OS versions, Windows 10, macOS version, Linux distributions
- "fixed after a user's report" and version numbers in the text

## 2. Find the source of each claim

For each claim search, newest first:

1. `findings/evidence.md` (search by number, model, OS, "bare metal", "real PC", "VM")
2. `findings/HANDOFF.md` (current state, open items, what is not verified on hardware) and the newest log in
   `findings/log/` (test round results)
3. `native/docs/status.md`, `native/docs/architecture.md`
4. `scripts/checklists/`: `core.json` (the core items every round runs, merged into each round by
   `scripts/checklist.py`), `round-*.json` (the round's own items: e.g. B1 = sidechain on bare metal) and
   `baseline-*.json` (baseline rounds against a release)
5. `native/tests` for the test count (`ctest --test-dir build-native -N | tail -1` if built)

Record per claim: source (E-number or file:line), date, setup. Watch the setup closely:

- **VM vs bare metal.** The win11 VM (QEMU passthrough) has known artefacts (iso OUT corruption E083/E142, missing
  notes E141). Numbers on the site should come from a real PC when one exists; say which it is.
- **Build / version.** A number from 0.8.0 is stale when a later round measured the same thing again.
- **DAW version.** Write only the version the evidence names ("Ableton Live" when it names none).
- **Model.** Only "tested" for the model that was actually used; user reports are "works for users".

## 3. Judge

Mark each claim: `ok` (newest source agrees), `stale` (a newer source has a different number or result), `wrong`
(contradicted, e.g. a limit that a later feature removed), `unsourced` (no source found). Report the list to the
human as a table before editing: claim, where (file:line), status, source.

## 4. Fix

- Change `scripts/site/build.py` and the README together, same wording and numbers.
- `unsourced`: do not invent a source. Ask the human, or remove/soften the claim.
- Keep the site style: one page, no marketing sentences, no long dashes, no emojis, no mono caps labels, no glows,
  no arrows, no underlines; footnotes only when they are still true for the new number.
- Build to check: `python3 scripts/site/build.py --out <scratchpad>/site` and grep the output for the new values.
- Do not deploy. The site is rebuilt by hand (`gh workflow run site.yml`) only when the human says so.
- Note the check in `findings/HANDOFF.md` and commit with the other changes.
- Before a release: the plug-in's own version (`project(... VERSION x.y.z)` in `native/CMakeLists.txt`) must equal the
  release. Hosts show it (Live's log: "v0.8.23" in the 0.8.24/0.8.25 test builds, E222 - it was not bumped for 0.8.24).
