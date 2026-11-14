# HANDOFF - current state

_The current state only, rewritten on every update (not appended): round, machines, who waits on whom, open tasks,
NEXT. At most 150 lines; the Stop hook (`.claude/hooks/handoff-check.sh`) refuses a longer file. What happened and
how goes into the dated log [`log/2026-11.md`](log/2026-11.md) (newest first); older logs are in [`log/`](log/).
Device/protocol knowledge goes into [`evidence.md`](evidence.md). Update this file in the same commit as the work._

Last update: 2026-11-14: #39 follow-up posted (E530/E531), issue closed by the reporter.

## Round

- **v0.8.41-rc8** pre-release 2026-11-14 (tag on 8bc8de7): rc7 crash fix without Access files + Live Mode hint
  (E518). Release CI 43567076613 all green, 6 assets, Latest stays 0.8.40; main 5fc470c preview box. Open: #39
  reply with the hint, together with the user.
- **On the round since rc8 (not in a build yet; next rc or final):** "Virus TI MIDI" input on macOS (E521/E523),
  libusb reader with 8 transfers pending + its fixes (E525-E527, last 2d69cc8), unlink like the original (E524,
  E526), option live-later (E519). hil.sh ALL PASS on the TI2 with the round broker (E526); Linux re-link by
  snd-usb-audio accepted and documented (E527).
- rc7 (11-13 20:45, 2e0fed9, E512 live stamps), rc6 (11-13 19:28, 8e9cf20, macOS slip restart E510; Catalina
  build of rc6 on catalina-test, dev-mac 43491758791) - details in log/2026-11.md.
- **macOS without Apple's audio driver (option A, E511):** vtiaudioprobe on our Mac (macOS 27) WORKS as root: the
  Virus taken from usbaudiod, 20.5 s streamed with the broker's Linux code, C4 on USB 1, slip share 0.01-0.03, given
  back cleanly; as user LIBUSB_ERROR_ACCESS. Needs -usb (Global Mode 3 + Output Select 10, as the plug-in's start).
  Open: Catalina (kernel AppleUSBAudio) via the #37 reporter (probe in the Catalina artifact, dev-mac 43495785623);
  discussion with the three reporters in #40; decision after the #37 report (user). Design for 0.8.42+.
- Round **0.8.41** on branch `round-0.8.41` (pushed); **0.8.40 stays Latest**; earlier rcs in log/2026-11.md.
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

## Machines and the Virus

- Our TI2 Desktop: back on **Linux** (2026-11-14, after hil.sh and two level-2 slots with it unplugged; the user
  switched it on again). User flashed ROM Z on it in early November (E500).
- Linux: rc8 CI build installed (plug-in 7e3bdd83, broker 2e75801c = rc8's, restored after the round-broker run).
- win11 VM: rc8 MSI installed (/quiet, exit 0, VST3 3d9f7dbf), shut down. win10 VM: snapshot `pre-i36` (0.8.36).
- Branch `catalina-test` (../virus-ti-re-catalina): x86_64 / 10.15 build for #37, never in main.
- Mac (macOS 27.2.1): rc7 installed; rc8 package + install command in ~/Downloads/TI-Revive-0.8.41-rc8 (user to
  install). Live 12 Suite, keystamp in ~/tirevive-test/keystamp; klog.on, noteprobe.on set (harmless).
- Branch `libusb-audio` (Mac libusb path, opt-in for old macOS): last merged round state before rc8; merge the
  round again before its next build.

## Open issues (as of 2026-11-14 morning; check GitHub only together with the user)

| Issue | Topic | Waits on | Last from us |
|---|---|---|---|
| #26 | Snow, Linux REAPER timing / clicks | reporter (REAPER audio system, pw-jack test, rc3) | 2026-11-12 |
| #28 | numbers 100-127 moved 5 px | reporter (confirm, close on yes) | 2026-11-09 |
| #30 | Snow, Logic: rc4 "nailed it"; arp after 1-bar count-in confirmed (18:00, E505); only the plug-in window's meters (Logic's are fine) | reporter: rc5 posted 11-13 18:53 (meters on a real Snow?) | 2026-11-13 |
| #31 | Win10 VirtualBox, Snow, setup with Access drivers | reporter (rc3 setup with the unplug step) | 2026-11-12 |
| #32 | (no change since 11-10) | reporter | before 11-10 |
| #33 | Intel Mac: Ableton fine, Logic distorted | reporter (rc1: Logic log, rate, buffer) | 2026-11-10 |
| #34 | TI2/Keyboard macOS Ableton crackle, right channel at full scale | reporter (2 Outs, 44.1 kHz, no hub, record input 3/4) | 2026-11-12 |
| #36 | Windows Cubase bank sync 0/128 | reporter (rc2: display, hub/cable, 3 checks; BC2001.syx asked 11-14) | 2026-11-14 |
| #37 | Catalina: crackle = byte-slipped audio (E510); libusb probe run 2 (E511 Later): 1000/1000 iso packets ok and real data, but only an idle floor on USB 1, no C4; AppleUSBAudio stays attached next to libusb; interface 1 without driver afterwards | reporter: heard the C4s on the Virus' outputs? Virus ok in Logic since? then the libusb test in Logic from his download (asked 11-14, issuecomment-6872527194): broker log + plug-in log | 2026-11-14 |
| #38 | macOS 27 Live: editor / AU crash | reporter (crash report, Access files) | 2026-11-10 |
| #39 | TI1 Keyboard in Live: settled as the unit's pace (E515-E518); doubled notes = Local On + Monitor In, reproduced with TI Revive and the original on the sim (E530, E531). Reporter closed it 11-14 10:51 after our 'middle ground' comment; our follow-up with the result posted 11-14 (issuecomment-6876775320). | nobody (closed) | 2026-11-14 |
| #40 | (ours) macOS: TI Revive takes the Virus' audio itself (libusb, E511) as an option? asks #30/#37/#39 reporters | #37 reporter answered 11-13 21:14: barely uses it as sound card, wants a unit/soundcard switch, admin rights ok, Logic on Catalina; #30/#39 reporters not yet | 2026-11-13 |

Every open issue waits on its reporter. Quiet-issue rule: 7 days after our last request, close with the AGENTS.md
message, only in a joint check with the user (by the dates above: #28 from 2026-11-16, #33 and #38 from 2026-11-17, #36 from 2026-11-21; #32: look up its date first).

## Not verified on hardware yet

- E503 libusb reader stall handling (5660a93): sim level 1 DONE (E522, C1-C7 held on the model: our broker on its
  virtual TI + stand-in libusb, STALL cases on 0x85/0x86/0x06); level 2 (usbip, real libusb)
  needs sudo with the user.
- Windows broker error 433 = device gone (5af74bd): mingw build + unit test only.
- Windows setup unplug step (c6313b4, E504): the unplug path never exercised (no Virus on the Access driver).
- E497 fix: DONE on the TI2 (E506): 0 of 239 re-syncs missed in 60 sessions (mixed build, both parts 0).
- Transport restarts (E505): DONE on the TI2 for wire + note timing (count-in, between ticks, low-latency on,
  FC; rc3 baseline sends F2 00 00). The arpeggiator itself not heard: sim model (asked) and Logic on the Mac.
- Rule 2 incident (sim session, 2026-11-13 07:11:20): a sim nativehost run reached the real broker for 1.4 s and
  sent the default state + close sequence (no store); no overlap with our runs; sim now uses m4/run.py only.
- `python3 -I scripts/evidence.py list --no-hardware` lists the evidence entries marked this way.

## Open decisions for the user

- None open. Decided 2026-11-14 (user): no pause of the bank sync for the unit's own keys (E520 (7)), like the
  original - keys stay late during a sync, none lost; the DAW-notes pause (E153) stays as it is. The extra plan B
  (s094c, tighter +64 rate) is not run: no longer round needed; the +64 re-sync stays a 0.8.42 candidate.
- E519/E524 (4) decided (user 11-14 "sounds good"): option live-later (off by default, +128 frames for played notes,
  stamprule.h LIVE_LATER_FRAMES, diag.cpp, 2 tests, README + site); not run on a Mac yet.

## To do before the final 0.8.41

- [ ] README + site: new features, Intel Mac line only once #33 confirms Logic; remove the README "Preview" block.
- [ ] `tirevive-site-check` over the whole round.
- [ ] Full test round on all three platforms: `python3 scripts/risk.py --round 0.8.41` (class C everywhere; Mac by
      hand: sleep), `scripts/hil.sh` on Linux, checklist artifact via `scripts/checklist.py`.
- [ ] Release text heredoc in `.github/workflows/build.yml` ("New in 0.8.41") checked before the tag.
- [x] #34 "bank sync failed in 11.9 s" looked into (E529): timing fits cut-short bank info replies (flaky USB), not
      the note pause; memsync now logs wrong-size / missing replies (MemorySync::log).
- [ ] MIDI In port "Virus TI MIDI" (user 11-14: "close the gap"): done on the round (keyport.h vti_dinport_filter
      as the original's "MIDI" entity, E521 (4); vtikeyport_mac.c second source "VTI0"; 4 tests; README + site).
      Mac build via dev-mac CI; not run yet (Mac check: the port shows up; no MIDI-In device here). Windows/Linux
      still have no key port at all.
- [ ] 8 reads pending per MIDI endpoint like the original (E521 (1); user 11-14: "yes and in this round"): done in
      vticore.c (async transfers, event thread, rxqueue.h + 8 tests; reader keeps the E503 rules). Sim S138 (E525):
      rules hold, keys a bit faster; faults fixed in ce60145 (S139 ok) and in the
      close-drain commit cb73ca7; S140 on cb73ca7: all clean on the model (E525). hil.sh ALL PASS on the TI2 (E526); the kernel's halt with 8 pending = level 2 (sim's M12, needs sudo).
- [ ] Unlink like the original (E524; user 11-14 "if it's like the original, yes"): linkrule.h + 3 tests; both
      brokers send raw 4E 73 52 00 when the last non-observer client leaves and at vti_close, and 4E 73 52 01 again
      before the next client write. Linux + mingw build clean; sim S138: keyed unit plays stand-alone after the
      close, next session fine, CC keeps the link (E525). TI2: link off/on lines, sessions after fine (E526).
- [x] Library skips (#36, E529): same counts as ours except BC2001/HS Extras = the reporter's .syx files (asked for
      BC2001.syx 11-14, issuecomment-6875745545); the log line names the reasons (skip_summary), vtimodel libs fixed.

## Round 0.8.42 candidates (decided or proposed)

- Mac audio (user 2026-11-14): Core Audio stays the main path; libusb as root (branch libusb-audio) only as an
  opt-in workaround for old unmaintained macOS, users accept sudo; no DriverKit for now (E528, log 2026-11-14).

- Decided: opt-in update check in the Control Center; "Prepare bug report" button (no telemetry).
- Decided (user 2026-11-14, #39): option 3, report the short latency while played notes come in (Live re-syncs).
- Decided (user 2026-11-13, #30): the plug-in's level meters in Logic (details asked on #30). The count-in arp
  moved into 0.8.41 (E505).
- Proposed (sim list): freeze guard (73 00 10 vv vs stream rate), single re-sync after a detected +64 (E492/E498),
  burst guard, fewer broker resets (E495), lower safety-net floor (option B).

## Standing notes

- Coverage: `scripts/coverage.py` (recipe and threshold in `findings/testing.md`). Mutation: `risk.py` names the
  changed C/C++ files to run `scripts/mutation.py` on.
- Never `nativehost --stress*` on the real Virus at small blocks (E438). Never `virsh screenshot` with a Virus in a VM.
- One VM at a time (30 GB host).
- Stop hook (handoff-check.sh + handoff-turn.py, 2026-11-14): a turn that edits the repo, commits, pushes, posts on
  GitHub or starts CI without touching this file is stopped once to update it.

## NEXT

00. Waiting on reporters: #37 (libusb test in Logic), #30 (Snow meters, rc5), #40 (#30/#39 views),
    #36 (BC2001.syx, asked 11-14).
    Check issues only together with the user.
0a. live-later on the Mac: switch it on in the CC, keystamp run like E518 A, spacing should be 24000 +-1 (E519).
0b. Level 2 done twice (E527). Linux re-link accepted, in README + site. One-probe fix 2d69cc8: sim stand-in OK
    (S144, at most 1 pending during a stall; kernel run not done, optional). Then the Mac: rc8 + "Virus TI MIDI" + live-later check with a round build
    (rc9 = everything since rc8 incl. E529's new log lines).
1. Then the "To do before the final 0.8.41" list.
