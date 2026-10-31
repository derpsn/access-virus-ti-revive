# Instructions for AI coding agents

You are working on **TI Revive**: it makes the Access Virus TI (Desktop, Polar, Keyboard; the Snow per user reports) work
on Windows 11, macOS and Linux without the old Access drivers. Since 0.8.0 its plug-in is "TI Revive Virus Control"
(`native/`), a rewrite that looks and works like the original "Virus Control" and loads the Access files from the
user's own installer at runtime.
Read this file completely before changing anything. Human contributors: see [CONTRIBUTING.md](CONTRIBUTING.md).
The findings are free to use elsewhere; the maintainers would be happy about a mention (credit line in the README,
"Using our findings").

## Hard rules (never break these)

1. **No Access material in the repository**: no Access binaries, installers, plug-ins, firmware images, skin PNGs,
   XML resources, patch banks, and no decompiled or disassembled Access code (not even excerpts in comments).
   Describe behaviour in your own words; function addresses/offsets are fine. If a task seems to need Access
   material in git, stop and ask the human. The one exception (decided by the maintainer, 2026-10-15, widened to
   the website 2026-10-16): screenshots of the running plug-in and Control Center for the README (`docs/images/`)
   and the project website (`site/`), always next to the "not affiliated" notice and never as the site's logo or
   branding - never the skin files themselves.
2. **Do not risk the user's hardware.** The one thing that can brick a Virus is its firmware, so the line is drawn
   there (write policy decided by the maintainer, 2026-10-31):
   - **Never:** trigger firmware/OS updates or anything on that path (bootloader/DFU entry, flash erase/program);
     send USB vendor requests or SysEx that are not *known*, i.e. seen from the original Access software in a
     capture or clearly identified as harmless by the firmware analysis - no "let's see what happens" commands;
     interrupt a store on purpose (power-off or USB pull mid-write).
   - **Allowed:** known writes - edit-buffer parameters, storing patches to RAM banks, multis, global settings,
     REMOTE templates. The worst case is lost user data, so a shipped feature that overwrites stored data backs it
     up first (or asks), and a store is one user action, never a loop (flash wear: no store per knob move or
     automation step).
   - **Own Virus first:** writes whose effect survives a power cycle (global settings, MIDI/USB routing) are tried
     on our own Virus and written up as evidence before a shipped feature sends them: a wrong one can make the
     Virus look dead over USB until it is fixed on the front panel.
   Keep MIDI rates bounded (a parameter flood can hang the Virus' audio until a power cycle — see evidence on
   automation limits). Never send the Multi parameter "Original Patch" (`72 00 03`): issue #7 reports that it
   sets all 16 edit buffers to Init at the next power-up (2/2 on their TI2, not reproduced by us; TI Revive never
   sends it, E209).
3. **Evidence before claims.** Do not invent protocol details. Every claim about the device or the Access software
   needs a source (capture, log, hardware test, static analysis) and goes into `findings/evidence.md` with a
   confidence level. If you are guessing, say so in the PR and mark the entry `open` or `LOW`.
4. **Never claim a test you did not run.** State exactly what was built and what was tested on which hardware.
   "Compiles" is not "works".
5. **No secrets, no personal data** in code, logs or commits (user names, serial numbers, home paths from logs).
6. **No history rewriting** on shared branches (no force pushes to `main`), no mass reformatting, no unrelated
   refactors in a bug-fix PR.

## Map of the repository

| Path | What it is |
|---|---|
| `findings/spec.md` | the USB/MIDI protocol of the Virus TI as far as it is known |
| `findings/evidence.md` | numbered observations E001.. with source and confidence — search here first |
| `findings/claims.md` | community claims and how they were checked |
| `findings/design*.md` | design per platform (Windows, macOS bridge, Linux, MIDI ports) |
| `findings/testing.md` | testing strategy: risk classes per path (`scripts/risk.py`), the automated hardware run (`scripts/hil.sh`), checklist rotation, mutation score |
| `findings/HANDOFF.md` | the maintainers' working log: current state, open problems, next steps |
| `native/` | **the plug-in since 0.8.0**: "TI Revive Virus Control" (VST3, all platforms) and the Windows ASIO driver "TI Revive Virus Audio"; `native/docs/architecture.md`, `native/docs/protocol.md` (timing, stamp clock), `native/docs/formats.md` |
| `src/broker/` | device access + broker: owns the Virus' USB interfaces, serves plug-in clients (WinUSB on Windows, libusb on macOS/Linux) |
| `src/wrapper/` | the classic Windows plug-in of 0.7.x (emulation of the Access driver interface `vtiemu`, VST3 wrapper, ASIO wrapper); not shipped since 0.8.0, kept for reference |
| `src/inf/`, `src/installer/` | Windows driver package (WinUSB) and MSI |
| `src/mac/` | macOS, the classic 0.5-0.7.x path (not shipped since 0.8.0, kept for reference): `libvirususb/` (replacement libVirusUSB), `bridge/` (VST3/AU bridge; since 0.8.0 the native plug-in is the VST3 and the Audio Unit), `helper/` (Rosetta host of the original plug-in) |
| `src/installer-mac/` | macOS package and helper scripts (driver conflicts, Access installer) |
| `scripts/` | diagnostics (`tirevive-usbinfo.ps1`), capture/decode tools, VM and build scripts |

## Before you change code

1. Search `findings/evidence.md` and `findings/HANDOFF.md` for the symptom (error text, USB request, model name).
   Many problems are known: e.g. another driver holding the interfaces (`LIBUSB_ERROR_ACCESS`, macOS
   `UsbExclusiveOwner = MIDIServer`, Windows `VIRUSUSB` service), wrong model detection (Snow vs TI, E115), part
   output not on USB ("connected but silent").
2. Find the path involved: since 0.8.0 the native plug-in (`native/`) on all platforms, the macOS Audio Unit
   included (`native/au/`); reports about "Virus TI (TI Revive)" VST3 or the Rosetta helper come from 0.7.x or older.
3. Read the code around the change and match its style: C/C++, short functions, comments that explain *why*.
4. **Unsure how the original Virus Control does something? Look it up, do not guess.** Use the REA MCP (skill
   `reverse-engineer-anything`) for static analysis of the user's own Access files (e.g. `Virus Control.dll`), not
   ad-hoc disassembly in a shell. Then implement that behaviour in the most efficient and reliable way, with
   performance (no extra work on the audio thread) and safety (rule 2) in mind. Rule 1 still applies: the repo gets
   the behaviour in your own words plus function addresses (an evidence entry), never decompiled code.

## Building and checking

- Windows parts: cross-compile on Linux with mingw-w64: `make -C src/broker`, `make -C src/wrapper`. MSI: WiX on
  Windows (`src/installer/build.ps1`).
- macOS parts: on a Mac, the broker with `make -C src/broker mac` and the plug-in as below; the package with
  `scripts/mac-build-release.sh <repo> <version>`. (`scripts/mac-build-bridge.sh` builds the 0.7.x bridge and
  Rosetta helper, for reference only.)
- Linux: the broker builds with `make -C src/broker` (libusb).
- Native plug-in (all platforms): `git submodule update --init --recursive native/external/vst3sdk`, then
  `cmake -S native -B build-native && cmake --build build-native && ctest --test-dir build-native` (MSVC on Windows,
  Xcode on macOS; see `native/README.md`). Never commit Access files: the plug-in loads them at runtime.
- CI (`.github/workflows/build.yml`) builds releases only on version tags; build locally for PRs.
- Test rounds: `python3 scripts/risk.py --round X.Y.Z` says which core checklist items the changes need (risk class
  A/B/C per path, `scripts/checklists/risk.json`); `scripts/hil.sh` runs the Linux ones on the real Virus
  (`findings/testing.md`). A new folder needs a rule in `risk.json` (a unit test checks it).
- Without the hardware you can still: build, run the static checks you have, and reason from logs. Say that you did
  exactly that.

## Helping a user test their setup

Use the `tirevive-test` skill (`.claude/skills/tirevive-test/`): safety rules first (no firmware updates, no writing
stored patches, no untested SysEx, a disposable DAW project), then the test stages from USB detection to project
recall, and a report with pass / fail / not tested per stage for the "Test report" issue template. Untested models
(Virus TI Snow) and setups are exactly where these reports are needed.

## Working from a user's report

Use the `tirevive-diagnose` skill (`.claude/skills/tirevive-diagnose/`) or follow it by hand: collect the platform
logs listed in CONTRIBUTING.md, match the lines against known evidence, and answer with the cause, the evidence
entry and concrete steps. Quote log lines exactly.

## Preparing a pull request

Use the `tirevive-pr` skill (`.claude/skills/tirevive-pr/`) or its checklist:

- [ ] one focused change; no Access material; no personal data
- [ ] builds for the platforms it touches (say which you built)
- [ ] tested on hardware, or clearly marked "not tested on hardware"
- [ ] new device/protocol knowledge added to `findings/evidence.md` (next free E-number, date, source, confidence)
- [ ] README updated if users see different behaviour
- [ ] PR description: problem, cause (with evidence), change, test, and one line that an AI assistant helped

## Tone in issues and PRs

Be concrete and friendly, base answers on the logs, and add a short note at the end when an answer was written with
AI assistance.
