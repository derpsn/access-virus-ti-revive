# Testing strategy: risk classes, automated hardware run, rotation, mutation score

_Since 2026-10-19 (round 0.8.28). Goal: much less manual testing per round without losing the bugs it found._

## Why

Before this, every test round ran the five core items (cold start, reopen, export, Virus off/on, sleep) by hand
on every system plus the round's own items: about 60-90 minutes per system. The unit tests (vtitests, ctest)
pin down logic, but they cannot show what only the device, the OS or a DAW does: USB re-enumeration, sleep and
wake, the Virus' stamp clock, a host's offline render. So testing has four layers, and each change only goes
as far up as its risk needs:

| Layer | What it proves | Who runs it | Time |
|---|---|---|---|
| 1. Unit tests (`ctest`) | logic: protocol encoding, pacing, state, decoding, the rule-2 guard | CI, every build | seconds |
| 2. Automated hardware run (`scripts/hil.sh`) | the real Virus on Linux: start-up, reopen, export pacing, timing on the grid, sleep | Claude, Linux PC | ~4 min (+3 with sleep) |
| 3. Targeted checklist items | what layer 2 cannot reach: other platforms, DAWs, the editor, a power cycle | the user, by risk class | ~15-20 min per system |
| 4. Pre-release testers | other models (Snow), DAWs, Intel Macs | users via the "Test report" issue | async |

## Risk classes (`scripts/checklists/risk.json`, `scripts/risk.py`)

Risk = how likely a bug is (code touched, complexity, bug history) x how bad it would be (hardware safety,
silence, timing, lost projects). Every path in the repository has a rule; the first matching rule wins.

- **A**: logic covered by unit tests (model, Access files, editor model, Control Center decoding), docs, site,
  tests and tools that are not shipped, the 0.7.x code. **No manual core items.**
- **B**: the send path, timing, audio, the plug-in's processor, the broker's shared core. **The rule's core
  items**: on Linux the automated run, on the other systems by hand (rotated items only every 3rd round).
- **C**: platform code (WinUSB, libusb, CoreMIDI layers, popups, AU, ASIO), installers, drivers, sleep/wake,
  the rule-2 guards (sendguard, automation limit, send budget), build files and the SDK. **The rule's core items by
  hand on that platform, never rotated.**
- A file no rule names counts as **C on every platform with every core item** (a unit test makes sure every
  tracked file has a rule, so this only happens to a new folder).

Use at the start of a round's checklist:

```
python3 scripts/risk.py --round 0.8.28                       # report for the changes since main
python3 scripts/risk.py --round 0.8.28 --spec scripts/checklists/round-0.8.28.json
```

The spec gets `"core_items"` (only the needed core items; `checklist.py` filters `core.json` by it) and one
empty section per system that needs hand items. The round's own items (what the changes need beyond the core:
a new menu, a new page) are still written by hand, for the changed feature only.

## Automated hardware run (`scripts/hil.sh`)

The installed plug-in in `nativehost` (no DAW), C4 at the start of every second on part 1, Out 1 recorded.
`scripts/hil_check.py` checks the plug-in's log and the recording:

- log: "Virus ready after", "sync marker at frame", no "no sync marker", no "audio stood still" (except after
  a sleep);
- notes: first note within 12 s, none lost, no sounds off the grid, notes until the end;
- timing: every note within 5 ms of a fitted grid, jitter below 1 ms, drift below 100 ppm;
- export: the offline part runs at 0.8-1.25x real time (paced to the Virus, E204); after a 4 s stop (real-time
  blocks without notes, like a DAW after a render) playback is on the grid again (the E243 kind of fault: the
  stamp clock shifted after an export). Going from the render straight into notes put the first one 12-14 ms
  late until nativehost waited one block period at the switch (E286: the test host's fault, not the plug-in's).

Items: H1 cold start of the broker (LC1 without the power cycle), H2 two reopens after the broker's idle exit
(LC2), H3 export, a stop and playback after it (LC3), H5 with `--sleep` a 45 s suspend while playing (LC5; needs
sudo for `rtcwake`, the user runs it; the suspend goes through `systemctl suspend` and H5 fails when the PC did not
really sleep: the first run with `rtcwake -m mem`, 2026-10-20, never slept, the NVIDIA driver refused, it tested a
USB reset; the rerun slept 48 s and passed, E290), H6 the broker killed while playing (reconnect, set-up, marker, notes on the
grid from 34 s on; E284). Not covered: LC4 (Virus off and on; a switchable mains plug would make
it scriptable), REAPER/PipeWire, the editor. Limits from clean runs on the TI Desktop, 2026-10-19: jitter
0.02-0.26 ms, max 1.4 ms off the grid, drift -31..+1 ppm, first note after 5-8 s.

Rule 2: the run sends what the plug-in sends when a DAW opens a project (start-up messages, the part states,
notes); it replaces the edit buffers like opening a project, never stores anything. Close the DAW first (the
script refuses to run while REAPER, Bitwig, Ardour or another nativehost is running).

```
cmake --build build-native --target nativehost
scripts/hil.sh                 # LC1-LC3, results in work/hil/<time>/summary.txt
scripts/hil.sh --sleep         # plus LC5 (asks for sudo)
```

### Start-up compared with the original (added 2026-10-22, E321)

What the host sends to the Virus when the plug-in starts, original against ours, message by message. Two usbmon
captures of a cold start without notes (the broker gone first): the original in win10ref (pass the Virus through,
`scripts/capture.sh orig-<name> 60`, then open the plug-in), ours on Linux (`capture.sh ours-<name> 45`, then
`nativehost <plug-in> --seconds 30`). Then:

```
python3 -I scripts/startup_diff.py captures/orig-<name>.pcapng captures/ours-<name>.pcapng
```

It condenses both from the first SysEx to the end of the bank check and prints what only the original sends (`-`)
and what only we send (`+`). Known differences are counted, not judged (`IGNORE` in the script, each with its reason:
this computer's USB IO setting, our readiness probe, our song position at the timeline anchor). The first comparison
found the missing "Configuring Virus TI" message and the wrong multi dump (E321). The original's repeated bank info
requests for banks 2-4 are a side effect of its timer (E322) and counted as known. On the fixed build: SAME START-UP. Run it in rounds that touch the start-up (initialize, state, MemorySync, the
broker's device open); the reference capture is captures/orig-startup-lcd-20261022 (original 5.1.7, TI2).

### The same run on Windows and macOS (added 2026-10-22, not yet run on hardware)

`scripts/hil-win.ps1` (started with `hil-win.cmd`) and `scripts/hil-mac.command` play the items H1, H2 x2, H3 and H6
with the installed plug-in in `nativehost` and write `results-<time>/` next to themselves (NAME.err = the plug-in's
log, NAME.out, NAME.wav, summary.txt). `hil_check.py --folder DIR` grades such a folder on any system (`ITEMS`; a
unit test checks that both runners play the item lengths it expects); the runners call it themselves when a Python 3
is there. The CI test build makes them as the artifacts `TI-Revive-hil-windows` and `TI-Revive-hil-mac` (`nativehost`
plus the scripts; steps that may fail, never part of the release). Both must run in the desktop session: on the Mac an
ssh session gets no Microphone access and the plug-in records silence (E103; the first double-click asks for
Microphone access for Terminal), on Windows the broker's shared memory is per session (E047; for the VM a scheduled
task with /IT). H6 on Windows kills the broker hard (Stop-Process), like a crash. Until both have passed once on the
Virus, `risk.json` keeps cold/reopen/export as hand items on Windows and macOS.

## Pre-releases for testers

A tag `v0.8.28-rc1` builds everything like a release tag and creates a GitHub release marked pre-release, with a
note on top (test with a disposable project, report with the "Test report" issue template). The packages carry the
plain version 0.8.28, because MSI versions must be numeric: on Windows a tester uninstalls the pre-release before
the final 0.8.28 (never reinstall over the same version: a forced reinstall removed the WinUSB driver, E166). The website skips pre-releases on the
landing page and marks them in the changelog. Use it when a round's changes need setups we do not have (the Snow,
other DAWs, Intel Macs); the round's own checklist stays on our machines.

## Yield of the core items (rounds 0.8.23-0.8.27)

What the hand-run core items found, from HANDOFF and evidence (the checklist marks themselves live in the
artifacts' databases):

| Core item | Findings | Sources |
|---|---|---|
| sleep (LC5/MC5/BC5) | 4 real faults: Mac ring stopped (0.8.23), Windows silent after wake (baseline), Linux no marker after the stall path (0.8.24), Linux 48 kHz after wake (0.8.26) | E207, E210, E212/E213, E241 |
| cold start (LC1/WC1/MC1/BC1) | 1: a false "audio stood still" at connect (baseline) | E209 (5), E212 |
| export (LC3/WC3/MC3/BC3) | 1: on the Mac the stamp clock moved after an export (found in MC4 after MC3) | E243, E244 |
| reopen (LC2/WC2/MC2/BC2) | none (6/6 reopens after idle exit with the marker on the first ping, every round pass) | E209 (2), E212, E215, E216 |
| Virus off/on (LC4/WC4/MC4/BC4) | none in the plug-in (WC4 needs a libvirt re-attach in the VM; BC4 "minor issues on the first Play", not reproduced) | E212, E215, E216 |

The round's own items found far more (menus, popups, Control Center ownership, the Wavetable list, a duplicated
track in REAPER: E181, E183, E202 and the L/M/W FAILs of rounds 0.8.19-0.8.22), always in the code the round had
changed. So: sleep stays by hand wherever its code is touched (class C), reopen and power rotate (every 3rd round
for class B), and the round's own items stay targeted at the changed feature.

Recount after every few rounds: add the round's FAILs to this table and move items in or out of the rotation in
`risk.json`.

## Mutation score (`scripts/mutation.py`)

Line coverage (vticore 94 %) only says that code ran during the tests, not that a test would notice a wrong
result. `mutation.py` changes one operator at a time (`<` to `<=`, `&&` to `||`, `true` to `false`, `+ 1` to `- 1`)
in a copy of the tree, rebuilds vtitests and runs it: a mutant the tests still pass is behaviour no test pins
down. Run it now and then on the class B/C logic, not in CI (each mutant is a rebuild).

```
git worktree add ../tirevive-mut HEAD          # a copy: an interrupted run must not leave a mutant behind
ln -s "$PWD/native/external/vst3sdk" ../tirevive-mut/native/external/vst3sdk
cmake -S ../tirevive-mut/native -B ../tirevive-mut/build && \
python3 -I scripts/mutation.py --tree ../tirevive-mut --build ../tirevive-mut/build \
    native/src/device/sendguard.h native/src/device/dumppacer.h --json work/mutation.json
```

Results: see "Mutation runs" below; a surviving mutant gets a test (or a note why the change is harmless).

## Mutation runs

**2026-10-19, first run** (copy of b7b9ed0^, 19 headers of native/src/device plus the broker's ctrlguard.h and
resetguard.h, 221 mutants, ~50 s each): **164 of 221 killed (74 %)**.

| File | Killed | Note |
|---|---|---|
| sendguard.h, ctrlguard.h, presencewatch.h | 100 % | the rule-2 guards are pinned down |
| dumppacer.h, cablerule.h, stamprule.h | 91-93 % | 4 survivors in stamprule.h:32 (`\|\|` to `&&`) |
| offlinepace.h, automation.h, resetguard.h | 71-80 % | boundaries (`>=` vs `>`) not tested |
| stallwatch.h, usbmidi.h, startprogress.h | 57-69 % | usbmidi.h: packet length bounds (25-32) and the SysEx end (52) |
| transport.h, gapfill.h, resampler.h | 43-50 % | resampler.h: index arithmetic (63-65) and bounds |
| **sendbudget.h** | **17 %** | the MIDI byte budget (rule 2: flood protection) is barely checked by tests |
| keepalive.h, broker.h, sidechain.h | 0-25 % | broker.h survivors are default member values |

**sendbudget.h after its new tests (2026-10-19):** 2 of 5 killed; the 3 survivors are equivalent (`now > last`
vs `>=` adds 0 tokens at equal times, `tokens > burst` vs `>=` and `tokens < 0` vs `<=` give the same value at the
boundary), so every mutant that changes behaviour is killed. One new test also found a real flaw (an earlier time
was granted twice; harmless with steady_clock, fixed).

**2026-10-20, second run, batch 1** (copy of af9e21f, the send path: stamprule.h, usbmidi.h, dumppacer.h and the
round's new audiogate.h, hostmidi.h, gapfill.h; 110 mutants): 87 killed (79 %). Each of the 23 survivors checked in the
code: 16 were missing tests, now added (test_stamprule: ping-shaped SysEx that is not Access, too short or not F0;
test_usbmidi: note-off 0x80, no read past the message when packing, a real-time byte inside a SysEx and a 1-byte
system common message; test_gapfill: the MAX_FILL and SLACK boundaries) - all 16 killed in a rerun of exactly those
mutants. 7 are equivalent: dumppacer.h:37 and :46 (same value at the boundary), hostmidi.h:16 and :25 (`> 0` vs
`>= 0` gives 0 both ways), audiogate.h:39 (the constructor always sets `ok_`), usbmidi.h:27 `< 0xF0` (F0 is taken by
the branch above) and usbmidi.h:68 (a SysEx buffer is always a multiple of 3 long there, so the +3 boundary is never
met). **Result: 103 of 110 killed (94 %), every surviving mutant equivalent.** The first run's survivor list was
lost with its scratch folder.

**2026-10-20, second run, batch 2** (same copy plus batch 1's tests: offlinepace.h, automation.h, stallwatch.h,
startprogress.h, transport.h, resampler.h, keepalive.h, sidechain.h, the broker's resetguard.h; 64 mutants): 38 killed
(59 %). Of the 26 survivors 14 were missing tests, now added and all killed in a rerun: resetguard - a second reset
in the same second is skipped (E136's case, the most important gap); resampler - frames_needed is exactly the range
process() reads (NaN outside it, both edges read) and process() writes n outputs, not n + 1; stallwatch - a stream
from frame 0 and exactly LIMIT_MS; transport - a first block that already plays at 0 and exactly the 0.01 tolerance;
keepalive - an FE due exactly at the block end, and one exactly a period ahead; automation - a full queue refuses
instead of overwriting the oldest. 10 are equivalent: offlinepace.h:34, startprogress.h:19/21/38, keepalive.h:27 (the
same value at the boundary), stallwatch.h:41 and sidechain.h:41 (initial values the first call or setup() overwrites),
resetguard.h:19 (`fd > 0`: fd 0 only with stdin closed), resampler.h:32 (the Kaiser window's value at its edge,
~1e-5 of a tap) and :73 (a 41st Bessel term). 2 change only memory past the filter table (resampler.h:28/36 write a
0 one float past its end at the last phase): a unit test cannot see that, a sanitizer build would (not run).
**Result: 52 of 64 killed (81 %); all behaviour-changing mutants killed.** Both batches together: 155 of 174 killed
(89 %), 17 equivalent, 2 sanitizer-only.

Note: several survivors were reads past a message's end (usbmidi_pack); a plain unit test sees them only when the
bytes past the end are known, so the test leaves a marker in the vector's spare capacity.
