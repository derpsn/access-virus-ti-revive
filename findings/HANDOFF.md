# HANDOFF - current state

_The current state only, rewritten on every update (not appended): round, machines, who waits on whom, open tasks,
NEXT. At most 150 lines; the Stop hook (`.claude/hooks/handoff-check.sh`) refuses a longer file. What happened and
how goes into the dated log [`log/2026-11.md`](log/2026-11.md) (newest first); older logs are in [`log/`](log/).
Device/protocol knowledge goes into [`evidence.md`](evidence.md). Update this file in the same commit as the work._

Last update: 2026-11-14 late evening: 0.8.41 Latest; #37 libusb audio works on Catalina, #30 closed; next round 0.8.42.

## Where things stand

- **0.8.41 is released and Latest** (2026-11-14, user "Ship it"): tag v0.8.41 on main 266cb46 = merge 6e4c632
  (main 5fc470c + round e5f84e8) + one commit that drops main's rc8 preview block from the README (it survived the
  merge without a conflict: check that at every round merge). Release CI 43625111066 green, 6 assets. Site rebuilt
  by hand (site.yml 43625667337): Download 0.8.41, no preview box, 753 tests, coverage 96.4 %.
- **Where the newest files are:** after the merge, `round-0.8.41` got HANDOFF/log commits only (74c8832 on); its tree
  is main's plus those docs, but it lacks main's merge commit. Start round 0.8.42 as a branch from `origin/main`, then
  `git merge origin/round-0.8.41` (docs only) so this HANDOFF comes along. Push the new branch with an explicit
  refspec (`<b>:refs/heads/<b>`), check origin/main unchanged.
- Test round of 0.8.41: checklist https://claude.ai/artifact/7j2tvTcAc19iNynNjfnSwM (spec
  scripts/checklists/round-0.8.41.json). Linux L1-L4 + LC4 pass (hil.sh ALL PASS); Mac hil-mac ALL PASS, M1-M3,
  MC4, MC5 pass; VM W2, W3 pass, WC4 skipped, W1 passed by the user as VM-related (E533). Found and fixed in the
  round: the Control Center Options page cut off its 7th row (cc/optionslayout.h, b7b6779). M2 (E534): live-later
  halves the played-note spread (<= 1.3 ms instead of 2.7 ms) but is not exact; texts made honest, code unchanged.
- Contents of 0.8.41: build.yml "New in 0.8.41" (release page) and the What's New entry (native/src/editor/infopage.h).

## Machines and the Virus

- Our TI2 Desktop: on **Linux** USB (2026-11-14 evening). User flashed ROM Z on it in early November (E500).
- Linux: 0.8.41 release installed (plug-in 184badb2, broker 728db830, Control Center 0.8.41).
- win11 VM: 0.8.41 release installed (/quiet exit 0), shut down. TUNED 2026-11-14 (E533): vCPUs pinned to P-cores
  4-7, emulator 2-3, topology 1/2/2, no emulated sound / SPICE USB redirect / balloon, evmcs+avic off, ISOs off;
  guest High performance, USB selective suspend off (AC). Backup: work/vm/win11-before-tuning-20261114.xml.
  Before VM timing tests: `sudo cpupower frequency-set -g performance` (user). hil-win on the tuned VM: not run.
  win10 VM: snapshot `pre-i36` (0.8.36). No spare USB controller for VFIO (user: no extra hardware for now).
- Mac (macOS 27.2.1): CI build 43620201798 installed (= 0.8.41 except texts). The RELEASE package is ready locally in
  work/rel-0.8.41/mac-push/TI-Revive-0.8.41-release (pkg 643f73d4, INSTALL-COMMAND + "Install test build.command");
  the Mac did not answer over SSH: push it to ~/Downloads when it is awake (scripts/mac.sh push), the user installs.
  Option files there: live-later.on (left from M2), klog.on, noteprobe.on, live-notes.on, diag.on.
- Branches: `libusb-audio` (Mac libusb path, merge main before its next build), `libusb-catalina` (worktree
  ../virus-ti-re-catalina, the #37 test build, dev-mac 43512216308), `catalina-test` (old). Local scratch branch
  `release-0.8.41` (merge worktree in this session's scratchpad) can be deleted.

## Open issues (checked with the user 2026-11-14 evening; check GitHub only together with the user)

| Issue | Topic | Waits on | Last from us |
|---|---|---|---|
| #26 | Snow, Linux REAPER timing | reporter: pw-jack test; 0.8.41 note | 2026-11-14 |
| #28 | numbers 100-127 moved 5 px | reporter: confirm, close on yes | 2026-11-09 |
| #31 | Win10 VirtualBox, Snow, setup with the Access driver | reporter: setup unplug step report; 0.8.41 note | 2026-11-14 |
| #32 | (no change since 11-10) | reporter | before 11-10 |
| #33 | Intel Mac 0.8.39: Logic distorted until a buffer change; log clean on the way in, not a slip (E532) | reporter: Logic output device, distortion on the unit's outputs?, 0.8.41 log | 2026-11-14 |
| #34 | TI2/Keyboard macOS crackle, bank sync failed 11.9 s (E529) | reporter: new log with 0.8.41 | 2026-11-14 |
| #36 | Windows Cubase bank sync 0/128 | reporter: BC2001.syx, new log with 0.8.41 | 2026-11-14 |
| #37 | Catalina: libusb test build WORKS (no crackle 64-1024, one slip at 512 healed by the stream restart); exit: give-back to Apple fails + libusb refcnt assertion | us: a test build that installs like the normal package (no Terminal) before 0.8.42; user promised official Catalina support in 0.8.42 | 2026-11-14 |
| #38 | macOS 27 Live: AU/editor crash, not reproduced | reporter: crash report + log with 0.8.41 | 2026-11-14 |
| #40 | (ours) macOS libusb audio as an option? | #39 reporter; #37 = first real data point (works) | 2026-11-13 |

#39 closed by the reporter (doubled notes = Local On + Monitor In, E530/E531). #30 closed by us 2026-11-14 with the
user: all works, Snow meters visible in the plug-in (evidence entry still to write). Quiet-issue rule: 7 days after our
last request, close with the AGENTS.md message, only in a joint check with the user: #28 from 2026-11-16, the
others from 2026-11-21 (#40 is ours: no closing; #32: look up its date first).

## Not verified on hardware

- libusb reader stall handling (E503) at kernel level with 8 transfers pending (level 2, needs sudo); sim held.
- Windows broker error 433 = device gone: unit test only (WC4 skipped in the VM).
- Windows setup unplug step (E504): never run with a Virus on the Access driver (win10 snapshot `pre-i36-access`
  could do it).
- Mac "Virus TI MIDI": the input shows up (M1); nothing played through it (no keyboard at the MIDI In here).
- Unlink on a real Keyboard/Polar: sim only (E525); on Linux, Windows (W3) and our Desktop the link lines are fine.
- Mac pkg refusing macOS < 14.4: no old Mac here.
- Full list: `python3 -I scripts/evidence.py list --no-hardware`.

## Round 0.8.42 candidates (decided unless marked proposed)

- live-later at 160 frames (half a 64-frame step; E534 reading: the stamp sits on the Virus' 64-frame grid edge),
  then the same keystamp test on the Mac (armed track, Monitor In, keystamp 500 ms, klog K and onset steps).
- MIDI ports: Linux first (ALSA sequencer virtual ports from the broker, keyport.h filter), then Windows (Windows
  MIDI Services virtual devices on Win11 25H2+, design-midi-ports.md option A; option C, a forwarding VST3, for
  Windows 10).
- FIRST: macOS libusb audio path as an opt-in for old macOS, official Catalina package (x86_64, 10.15) without the
  Terminal steps; test build to #37 before the release. Fix the exit give-back (attach fails, then libusb
  refcnt assertion; vticore.c vti_close), the cosmetic `usb block late` after a reconnect, and make
  mac-libusb-test.command refuse a non-test broker. Evidence entry for #37's run (log 2026-11-14 late evening).
- Opt-in update check in the Control Center; "Prepare bug report" button (no telemetry).
- #39: report the short latency while played notes come in (Live re-syncs).
- Windows VM: hil-win on the tuned VM; if fresh-broker sessions still miss the probe, A/B against 0.8.40 (E533).
- Proposed (sim list): freeze guard (73 00 10 vv vs stream rate), single re-sync after a detected +64 (E492/E498),
  burst guard, fewer broker resets (E495), lower safety-net floor.

## Standing notes

- Weekly releases: one round per week, one test run (risk.py, hil.sh, checklist artifact via scripts/checklist.py).
- Coverage: `scripts/coverage.py` (recipe in `findings/testing.md`). Mutation: `risk.py` names the files for
  `scripts/mutation.py`.
- Never `nativehost --stress*` on the real Virus at small blocks (E438). Never `virsh screenshot` with a Virus in a VM.
- One VM at a time (30 GB host). The tirevive-hil task in win11 runs hil-win in the console session; Live or the
  Control Center open in the VM make it stop ("close first").
- Xvfb grabs of the Control Center stay black (GTK, no WM): check its look on the user's desktop.
- Stop hook (handoff-check.sh + handoff-turn.py): a turn that edits the repo, commits, pushes, posts on GitHub or
  starts CI without touching this file is stopped once to update it.

## NEXT

1. Mac awake: push the 0.8.41 release package (see Machines), the user installs it.
2. Start round 0.8.42 from `origin/main` + merge `origin/round-0.8.41` (see "Where things stand"), pick the first items with the user
   (first: the libusb path + Catalina package for #37, then live-later 160 + its Mac test, Linux MIDI ports).
3. Waiting on reporters (table above); check issues only together with the user.
