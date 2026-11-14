# HANDOFF - current state

_The current state only, rewritten on every update (not appended): round, machines, who waits on whom, open tasks,
NEXT. At most 150 lines; the Stop hook (`.claude/hooks/handoff-check.sh`) refuses a longer file. What happened and
how goes into the dated log [`log/2026-11.md`](log/2026-11.md) (newest first); older logs are in [`log/`](log/).
Device/protocol knowledge goes into [`evidence.md`](evidence.md). Update this file in the same commit as the work._

Last update: 2026-11-14 morning (#39: Live placement measured E518, reply with 3 options posted; #37 libusb test in Logic asked; E517 MIDI In; fresh context from here).

## Round

- **v0.8.41-rc7** pre-release 2026-11-13 20:45 UTC (tag on 2e0fed9: live-note stamps from the timeline, E512),
  release CI 43503582943 all 8 jobs green, 6 assets, Latest stays 0.8.40; main 6cf8493 preview box; Linux rc7
  installed. #39 asked to test rc7 (issuecomment-6864528067). Mac: rc7 installed 11-14, Live
  placement test done there (E518).
- **rc7 bug, fixed on the round (2026-11-13 late):** without the user's Access files rc7 crashes the DAW in process()
  (the E512 change put `out_->live_s.store` under an `if` without braces; out_ is null there). Test-first: ctest
  no_access_files (nativehost, empty HOME) segfaulted, passes with the braces. Why unnoticed: plug-in sources got no
  -Wall (relative path in the warning loop), now on; Linux -Werror clean, Clang 4 non-fatal warnings left. Must be in
  the next rc/final; the three reporters have Access files (not hit).
- **v0.8.41-rc6** pre-release 2026-11-13 19:28 UTC (tag on 8e9cf20: macOS slip restart E510), release CI
  43490921423 all green (the macOS branch compiles), 6 assets; main 4f0f061 preview box; Linux rc6 installed
  (plug-in 4cc924bc). Catalina build of rc6: catalina-test 77aa389 (round merged), dev-mac 43491758791 green,
  artifact dev-mac-catalina (x86_64, minos 10.15, CC 10.15). #37 reply with both links posted. Mac fake-slip test DONE (E510): restart path runs, new marker, rc6 pkg = installed.
- **macOS without Apple's audio driver (option A, E511):** vtiaudioprobe on our Mac (macOS 27) WORKS as root: the
  Virus taken from usbaudiod, 20.5 s streamed with the broker's Linux code, C4 on USB 1, slip share 0.01-0.03, given
  back cleanly; as user LIBUSB_ERROR_ACCESS. Needs -usb (Global Mode 3 + Output Select 10, as the plug-in's start).
  Open: Catalina (kernel AppleUSBAudio) via the #37 reporter (probe in the Catalina artifact, dev-mac 43495785623);
  discussion with the three reporters in #40; decision after the #37 report (user). Design for 0.8.42+.
- **v0.8.41-rc5** pre-release 2026-11-13 18:50 UTC (tag on 7f6d1be, release CI 43484666513 all 8 jobs green,
  6 assets; Latest stays 0.8.40). main aee9e11 = preview-box lines only, site 43486622822 green, live page shows
  "Preview: 0.8.41-rc5". Linux: rc5 installed by install.sh (plug-in 5726305b, broker 2e75801c). Open: rc5 reply
  on #30 (draft with the user), win11 silent install, Mac install command.
- Round **0.8.41** on branch `round-0.8.41` (pushed). Released before: **v0.8.41-rc4** pre-release
  (2026-11-13 08:48 UTC, tag on 24c056e, release CI 43395373253 all green, 6 assets); **0.8.40 stays Latest**.
  Site live with "Preview: 0.8.41-rc4" (main b9baa60 = box lines only, site run 43396293872).
- In 0.8.41: experimental lower latency (option `low-latency`, off by default, E496), Control Center Options page,
  slip detector + automatic restart (`slip-restart`, on by default, E488), freeze alert page (E493), burst counter
  (E490), Mac pkg refuses macOS < 14.4, plug-in log on by default (`diag.off` switches it off), Rise Message 4 per
  fetched patch (E499/E502), LCD/sync page names each fetched patch, Windows broker error 433 = gone,
  libusb reader stall handling (E503), Windows setup unplug step (E504), and since rc3 (not in a build yet):
  E497 fix d7a0d1f (user 2026-11-12: "put E497 in 0.8.41") - a re-sync ping sent during a marker scan pass keeps
  its 5 s (device/markerscan.h, test_markerscan.cpp; the two race tests failed on the old logic first); CI run
  43369222561 (2026-11-13, all green) has it. Since then (user 2026-11-13: "Into 0.8.41 ... like the original"):
  transport restarts like the original (E505, #30 count-in arp): F2 from ppq + 256, 73 00 23 vv on the next tick,
  F8 grid rephased, FC at Stop, the original's jump limits (transport.h, test_transport.cpp, nativehost --count-in).
- Branch `catalina-test` (worktree `../virus-ti-re-catalina`, e91ef17): untested x86_64 / macOS 10.15 build for
  #37 only; dev-mac run 43278510379 green, link posted on #37. Never merged into main.

## Machines and the Virus

- Our TI2 Desktop: on the **Mac** USB bus (2026-11-14, Live placement test); back to Linux for hil.sh. User flashed ROM Z on it in early November (E500).
- Linux: rc4 CI build (run 43377561345) installed (plug-in b18c102b, broker 2e75801c), `hil.sh` ALL PASS.
- win11 VM: rc4 CI MSI installed (/quiet, exit 0, DisplayVersion 0.8.41, no Virus attached), shut down.
- win10 VM: reverted to snapshot `pre-i36` (0.8.36); snapshots `pre-i36`, `pre-i36-access` kept.
- Mac (macOS 27.2.1): **rc7** package installed 2026-11-14 06:46 (by the user; ~/Downloads/TI-Revive-0.8.41-rc7). Live 12 Suite there; keystamp in
  ~/tirevive-test/keystamp, starter ~/Downloads/'Live placement test.command'; sets ~/Desktop/placement-A/B/C. live-notes option back on; klog.on, noteprobe.on still set (harmless).

## Open issues (as of 2026-11-14 morning; check GitHub only together with the user)

| Issue | Topic | Waits on | Last from us |
|---|---|---|---|
| #26 | Snow, Linux REAPER timing / clicks | reporter (REAPER audio system, pw-jack test, rc3) | 2026-11-12 |
| #28 | numbers 100-127 moved 5 px | reporter (confirm, close on yes) | 2026-11-09 |
| #30 | Snow, Logic: rc4 "nailed it"; arp after 1-bar count-in confirmed (18:00, E505); only the plug-in window's meters (Logic's are fine) | us: rc5 with the Snow meter fix (E509), reply when the release is out | 2026-11-13 |
| #31 | Win10 VirtualBox, Snow, setup with Access drivers | reporter (rc3 setup with the unplug step) | 2026-11-12 |
| #32 | (no change since 11-10) | reporter | before 11-10 |
| #33 | Intel Mac: Ableton fine, Logic distorted | reporter (rc1: Logic log, rate, buffer) | 2026-11-10 |
| #34 | TI2/Keyboard macOS Ableton crackle, right channel at full scale | reporter (2 Outs, 44.1 kHz, no hub, record input 3/4) | 2026-11-12 |
| #36 | Windows Cubase bank sync 0/128 | reporter (rc2: display, hub/cable, 3 checks) | 2026-11-11 |
| #37 | Catalina: crackle = byte-slipped audio (E510); libusb probe run 2 (E511 Later): 1000/1000 iso packets ok and real data, but only an idle floor on USB 1, no C4; AppleUSBAudio stays attached next to libusb; interface 1 without driver afterwards | reporter: heard the C4s on the Virus' outputs? Virus ok in Logic since? then the libusb test in Logic from his download (asked 11-14, issuecomment-6872527194): broker log + plug-in log | 2026-11-14 |
| #38 | macOS 27 Live: editor / AU crash | reporter (crash report, Access files) | 2026-11-10 |
| #39 | TI1 Keyboard in Ableton Live. Settled: the unit's pace and shares (E515, E516 6), the original on Win 10 in Live 10 as VST2 showed almost the same although it makes both 'AbLi' calls (E516 7, 8), so his symptom is the unit, not us. Measured on our Mac (E518): with monitor In a played take comes back 54.6 ms late by default, 0 with Live Mode on. His three logs: Live Mode off throughout. Posted 11-14 (issuecomment-6872942887): the measurement, did he ever try the Live button, his view on 3 remedies (hint only / played notes take the lead while recording / report the short latency while playing). | reporter: Live button tried? which remedy | 2026-11-14 |
| #40 | (ours) macOS: TI Revive takes the Virus' audio itself (libusb, E511) as an option? asks #30/#37/#39 reporters | reporters (audio interface use, admin rights, macOS/DAW) | 2026-11-13 |

Every open issue waits on its reporter. Quiet-issue rule: 7 days after our last request, close with the AGENTS.md
message, only in a joint check with the user (by the dates above: #28 from 2026-11-16, #33 and #38 from 2026-11-17; #32: look up its date first).

## Not verified on hardware yet

- E503 libusb reader stall handling (5660a93): unit tests only; macOS broker not built locally.
- Windows broker error 433 = device gone (5af74bd): mingw build + unit test only.
- Windows setup unplug step (c6313b4, E504): the unplug path never exercised (no Virus on the Access driver).
- E497 fix: DONE on the TI2 (E506): 0 of 239 re-syncs missed in 60 sessions (mixed build, both parts 0).
- Transport restarts (E505): DONE on the TI2 for wire + note timing (count-in, between ticks, low-latency on,
  FC; rc3 baseline sends F2 00 00). The arpeggiator itself not heard: sim model (asked) and Logic on the Mac.
- Rule 2 incident (sim session, 2026-11-13 07:11:20): a sim nativehost run reached the real broker for 1.4 s and
  sent the default state + close sequence (no store); no overlap with our runs; sim now uses m4/run.py only.
- `python3 -I scripts/evidence.py list --no-hardware` lists the evidence entries marked this way.

## Open decisions for the user

- Restart of the killed sim plan runs (A/B extra) only with the user's OK and streaming decoders (memory).

## To do before the final 0.8.41

- [ ] README + site: new features, Intel Mac line only once #33 confirms Logic; remove the README "Preview" block.
- [ ] `tirevive-site-check` over the whole round.
- [ ] Full test round on all three platforms: `python3 scripts/risk.py --round 0.8.41` (class C everywhere; Mac by
      hand: sleep), `scripts/hil.sh` on Linux, checklist artifact via `scripts/checklist.py`.
- [ ] Release text heredoc in `.github/workflows/build.yml` ("New in 0.8.41") checked before the tag.
- [ ] #34 second session "bank sync failed in 11.9 s" (RAM B -1) after "paused 2.6 s for notes": not looked into.
- [ ] #30 note at the Play moment (sim S126, model): the note-on is stamped one tick before the F2 restart, the arp
      then stays off grid by half ticks (10 of 10); count-in case on grid (5 of 5). REA: the original has the same
      order (MED). Waits on the user: keep like the original (proposed) or restart at the block start (0.8.42 cand.,
      model then TI2). Also open: B2 note-on without a stamp (shared stamp?). Log 2026-11-13 afternoon.
- [ ] Library scan skips patches as "damaged or unknown format" (#36 log: BC2001 36, Incubation V3 97): check the
      format.

## Round 0.8.42 candidates (decided or proposed)

- Decided: opt-in update check in the Control Center; "Prepare bug report" button (no telemetry).
- Decided (user 2026-11-13, #30): the plug-in's level meters in Logic (details asked on #30). The count-in arp
  moved into 0.8.41 (E505).
- Proposed (sim list): freeze guard (73 00 10 vv vs stream rate), single re-sync after a detected +64 (E492/E498),
  burst guard, fewer broker resets (E495), lower safety-net floor (option B).

## Standing notes

- Coverage: `scripts/coverage.py` (recipe and threshold in `findings/testing.md`). Mutation: `risk.py` names the
  changed C/C++ files to run `scripts/mutation.py` on.
- Never `nativehost --stress*` on the real Virus at small blocks (E438). Never `virsh screenshot` with a Virus in a VM.
- One VM at a time (30 GB host).

## NEXT

00. START HERE (fresh context 2026-11-14). Waiting on reporters: #39 (Live button? remedy 1/2/3), #37 (libusb test in
    Logic, two questions), #30 (Snow meters in rc5), #40 views. Check issues only together with the user.
0a. E518 remedy, after #39's view and the user's call. My recommendation: hint now (helptext.cpp Live Mode text +
    README), the 'short latency while playing' variant as a feature later (untested: hosts re-sync on a latency change).
0b. Open from E518 (4): played notes in rc7 are spaced up to +-128 frames in Live on the Mac (E512 aimed lower, E513
    saw +-64 on the libusb path). Look at live_stamp_tl with the Mac's diag.log/klog of 11-14 06:54-07:06.
0c. Open from E517 (sim S133): (1) TI Revive's MIDI port passes only cable 1; a keyboard on the Virus' MIDI In (cable
    0) reaches no DAW while the plug-in is connected: gap vs the original's second port, decide with the user. (2)
    Compare how the original's driver and our broker keep reads pending on EP 0x85 (sim: a 20 ms read gap delays keys
    to 30 ms). (3) Compare the key globals each plug-in sends (0x3B, 0x40, 0x41, 0x46, 0x3E, 0x42-0x45). (4) TI2 run
    with a USB-MIDI interface on MIDI In (sim's plan in S133): asked the user 11-14 whether one is here, no answer yet.
0d. rc8: the rc7 crash fix without Access files (dbf8bed) is only on the round; user's call when to build it.
    Sim repo history (raw firmware bytes in an old commit of the private repo): user's call on a force push.
1. rc4 posted on #30 (issuecomment-6848455763). The reporter answered 11-13 15:46: latency solved, meters open.
2. User on bare-metal Windows testing rc4; afterwards read their plug-in log, and their call on the S126 fix.
3. Wait for the reporters; next joint issue check with the user.
4. Then the "To do before the final 0.8.41" list.
