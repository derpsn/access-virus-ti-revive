# HANDOFF - current state

_The current state only, rewritten on every update (not appended): round, machines, who waits on whom, open tasks,
NEXT. At most 150 lines; the Stop hook (`.claude/hooks/handoff-check.sh`) refuses a longer file. What happened and
how goes into the dated log [`log/2026-11.md`](log/2026-11.md) (newest first); older logs are in [`log/`](log/).
Device/protocol knowledge goes into [`evidence.md`](evidence.md). Update this file in the same commit as the work._

Last update: 2026-11-13 night (rc7 tagged: live-note stamps E512 for #39; rc6 out; libusb-audio branch paused).

## Round

- **v0.8.41-rc7** pre-release 2026-11-13 20:45 UTC (tag on 2e0fed9: live-note stamps from the timeline, E512),
  release CI 43503582943 all 8 jobs green, 6 assets, Latest stays 0.8.40; main 6cf8493 preview box; Linux rc7
  installed. #39 asked to test rc7 (issuecomment-6864528067). Mac: still rc6 (verify E512 there with keystamp+klog).
- **v0.8.41-rc6** pre-release 2026-11-13 19:28 UTC (tag on 8e9cf20: macOS slip restart E510), release CI
  43490921423 all green (the macOS branch compiles), 6 assets; main 4f0f061 preview box; Linux rc6 installed
  (plug-in 4cc924bc). Catalina build of rc6: catalina-test 77aa389 (round merged), dev-mac 43491758791 green,
  artifact dev-mac-catalina (x86_64, minos 10.15, CC 10.15). #37 reply with both links posted. Mac fake-slip test DONE (E510): restart path runs, new marker, rc6 pkg = installed.
- **macOS without Apple's audio driver (option A, E511):** vtiaudioprobe on our Mac (macOS 27) WORKS as root: the
  Virus taken from usbaudiod, 20.5 s streamed with the broker's Linux code, C4 on USB 1, slip share 0.01-0.03, given
  back cleanly; as user LIBUSB_ERROR_ACCESS. Needs -usb (Global Mode 3 + Output Select 10, as the plug-in's start).
  Open: Catalina (kernel AppleUSBAudio) via the #37 reporter (probe in the Catalina artifact, dev-mac 43495785623);
  discussion with the three reporters in #40; decision after the #37 report (user). Design for 0.8.42+.
- **Branch `libusb-audio`** (user 2026-11-13: test build now, do not wait for #37/#40): broker side done
  (VTI_BROKER_AUDIO on macOS, runtime only as root with VTI_LIBUSB_AUDIO=1; socket + ring named after and chowned to
  SUDO_UID, owneruid.h with tests; vti_open captures the device before MIDI on macOS in that mode; Makefile mac
  targets); plug-in accepts a root broker; plug-in side done (option mac-usb-audio, AudioIn::use_broker_ring, start/
  restart/rate via the broker, no replug; 723 tests). Paused for #39 (user: live-stamp fix + key-pacing analysis
  first). scripts/mac-libusb-test.command (stops the normal broker - no launchd agent since 0.8, the plug-in
  spawns it -, sets mac-usb-audio.on, runs sudo VTI_LIBUSB_AUDIO=1 vtibroker -v -stay, log on the Desktop, option
  off again at the end). CI 43505792317: mac green, native-linux/windows red (owneruid strtol on 32-bit long,
  risk.json lacked the .command) -> fixed 5865382, CI 43506997752 running. Then test on our Mac.
  Second test Mac (Big Sur MacBook): gone since 2026-11-13 night, not used for testing any more (user).
- First libusb run on our Mac (CI 43506997752 package, 22:13): broker captured the Virus and streamed, but the
  plug-in got no audio (audio 0): macOS refuses fchown on POSIX shm (EINVAL), the ring stayed root's 0600; and the
  plug-in's audio off/on released the interfaces, auto-detach re-attached Apple's driver = re-enumeration (MIDI cut,
  recovered). Fixed on the branch: ring made as the user (seteuid, stale ring unlinked), auto-detach off after the
  capture, explicit give-back at close (206ee80). Second try WORKS (E513): sound in Live, 0 drops, marker, keystamp
  onsets 24000 +-64 frames apart, give-back to usbaudiod ok. Big Sur probe clean (E511) -> #37's zeros are
  Catalina/machine-specific. User: libusb test version for #37:
  VTI_USBAUDIO_DIAG=1 per-second raw packet stats (probe sets it), branch libusb-catalina = catalina-test +
  libusb-audio, dev-mac build (package + probe + test command). Old MacBook too slow for a DAW (user).
  libusb-catalina f1bfee1 (merge, Makefile MAC_ARCH_FLAGS + vtiusbaudio_libusb.c), dev-mac 43512216308 running.
- Round **0.8.41** on branch `round-0.8.41` (pushed); rc1-rc7 pre-releases, **0.8.40 stays Latest**.
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
- Branch `catalina-test` (worktree `../virus-ti-re-catalina`): x86_64 / macOS 10.15 build for #37, now rc6 code
  + vtiaudioprobe (dev-mac 43495785623). Never merged into main.

## Machines and the Virus

- Our TI2 Desktop: on **Linux** again since 2026-11-13 night (user); before that the Mac and the Big Sur MacBook. User flashed ROM Z on it in early November (E500).
- Linux: rc4 CI build (run 43377561345) installed (plug-in b18c102b, broker 2e75801c), `hil.sh` ALL PASS.
- win11 VM: rc4 CI MSI installed (/quiet, exit 0, DisplayVersion 0.8.41, no Virus attached), shut down.
- win10 VM: reverted to snapshot `pre-i36` (0.8.36); snapshots `pre-i36`, `pre-i36-access` kept.
- Mac (macOS 27.2.1): rc6 package INSTALLED 2026-11-13 21:00 (VST3 22e2d9fc, broker 4558a47e = payload); before: rc4 CI package (2026-11-13 09:42 CET; VST3 b9674444, AU 04b9bae3, broker 4558a47e
  = the package payload). The count-in arp test is left to the #30 reporter (user 2026-11-13).

## Open issues (as of 2026-11-12 late; check GitHub only together with the user)

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
| #37 | Catalina: crackle = byte-slipped audio (E510); probe: data empty on Catalina but clean on our Big Sur MacBook (E511) = Catalina/machine-specific | reporter (stays on rc6 Catalina build) | 2026-11-13 |
| #38 | macOS 27 Live: editor / AU crash | reporter (crash report, Access files) | 2026-11-10 |
| #39 | TI1 Keyboard: (1) our live stamps fixed in rc7 (E512); (2) firmware pacing: his normal/soft/Fantom takes show soft playing ~ Fantom, pairs 6.0 ms apart (E512 (3)) | us: reply (draft with the user), then reporter (rc7 feel) | 2026-11-13 |
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

0a. Live-stamp fix (E512): in rc7 (user: "make a new RC and let him test", #39 has the setup); also verify on our Mac
   (keystamp + klog: onsets 24000 frames apart). libusb-audio branch paused (plug-in side committed 300cdd4).
0. #39 (user 2026-11-13: investigate before replying to #39 and #30): E507 = their log (no unit requests while
   playing), sim S127/S128 (unit holds keys only behind a dump reply), Mac run1 (Live on built-in output, two clocks)
   and run2 (Virus as Live's device) with native/tools/keystamp.c + scripts/als_notes.py: 120/120 notes within
   0.29 ms each, no drift/step/2-bar effect. Live audio-device switches reproduce the reporter's three session-1
   moments (1+23..25 safety-net moves). Reply posted on #39 (issuecomment-6860658701, with keystamp build line
   pinned to 9e2632f, SDK fallback tested on our Mac); analyse their zip with als_notes.py when it comes.
   #30 reply posted (issuecomment-6860709832: meters + screenshot, arp after count-in). Data: work/latency/i39-keystamp.
   TI2 is on the Mac USB now, not Linux. klog is still switched on on the Mac (harmless, diagnostic).
1. rc4 posted on #30 (issuecomment-6848455763). The reporter answered 11-13 15:46: latency solved, meters open.
2. User on bare-metal Windows testing rc4; afterwards read their plug-in log, and their call on the S126 fix.
3. Wait for the reporters; next joint issue check with the user.
4. Then the "To do before the final 0.8.41" list.
