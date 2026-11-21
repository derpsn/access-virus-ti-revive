# HANDOFF - current state

_The current state only, rewritten on every update (not appended): round, machines, who waits on whom, open tasks,
NEXT. At most 150 lines; the Stop hook (`.claude/hooks/handoff-check.sh`) refuses a longer file. What happened and
how goes into the dated log [`log/2026-11.md`](log/2026-11.md) (newest first); older logs are in [`log/`](log/).
Device/protocol knowledge goes into [`evidence.md`](evidence.md). Update this file in the same commit as the work._

Last update: 2026-11-21: release 0.8.43 started (final CI 44890937850, mutation run, site check); joint issue check, #37 rc2 AU does not load in Logic on Catalina (stale AU cache after the rename suspected, reply posted). 2026-11-20: **v0.8.43-rc2** tagged for the #50 reporter (RAM cache of Virus 2 fixed, E563); rc1 log: the second Virus' Core Audio start fails (kIOReturnError, follows the start order; hub bandwidth suspected, LOW). Round fixes today: start-up slip in silence (#37, E561), Core Audio pick in diag.log + "- Virus N" (#50, E562). 0.8.42 RELEASED 2026-11-19.

## Where things stand

- **0.8.42 is released and Latest** (2026-11-19 19:37 UTC). Round test 2026-11-19 all pass (checklist
  https://claude.ai/artifact/RyeeFb5mxzh8MsfSPMte7J, spec scripts/checklists/round-0.8.42.json; L5 Snow option not
  testable, W1 VM drift accepted); found and fixed in the round: E554 (REAPER crash, two instances + open window) and
  E555 (slot cycling); two Viruses tested on Linux with the sim's virtual TI2. main 902c294 = merge (main 7d1de54 +
  round a0450bf, tree = round head), push needed PRIVATE_DATA_OK=1 once (user OK: audit item 11 in 15141e7 quotes
  example paths with user 'x'). Tag v0.8.42 on fdb35d8. Its release CI 44598273038: all 8 build jobs green, the
  RELEASE JOB FAILED (backticks in the notes heredoc ran as a command; the sparse checkout lacked scripts/libusb.sh
  since audit #7); fixed on main 9049eec (build.yml only). Moving the tag was refused by the permission check, so
  the release was created BY HAND from that run's artifacts (gh release create v0.8.42 --verify-tag, the notes from
  the fixed heredoc, libusb 1.0.30 verified by libusb_verify, tirevive-usbinfo.ps1): 7 assets, Latest. The next
  tag runs the fixed job (not yet seen green). Site rebuilt (site.yml 44600240159): Download 0.8.42, several
  Viruses in Features + Recent releases, 838 tests, coverage 95.5 %; site.yml no longer runs on release events (the tag's deploy is refused by the
  Pages environment, red at every release; user OK 2026-11-19).
- Release installed: Linux (install.sh, plug-in 94257c93, broker c280b8e6), win11 VM (setup.exe /quiet exit 0, VST3
  b5cd51c9, VM shut down). Mac: work/rel-0.8.42/out/macx/TI-Revive-0.8.42-release (pkg 1f8f888c + INSTALL-COMMAND
  + Install test build.command) to push with scripts/mac.sh push when the Mac answers; the user installs.
- Round 0.8.43: branch round-0.8.43 from origin/main 5db2486, native VERSION 0.8.43. Plan agreed with the user
  2026-11-19: Mac push only when needed; #46 in README "Tested with" + site matrix (done).
- Our TI2 Desktop: in the win11 VM (attached 2026-11-19 evening, user; Linux broker socket stopped). Back: VM=win11
  scripts/vm.sh detach, then systemctl --user start tirevive-broker.socket. User flashed ROM Z on it in early November (E500).
- Linux: CI build 44588173936 (0.8.42, 42f0a12: E554 + E555 + stats label) installed 2026-11-19 (plug-in 4b9563c5, broker c280b8e6). Virus back on Linux after the VM test (node plugdev, socket active).
- win11 VM: CI build 44588173936 (42f0a12) installed 2026-11-19 (VST3 7eafc6fc; tirevive-hil task now runs C:\vti\0842\hil) (/quiet exit 0), shut down; hil-win 2026-11-19: all pass except H2-2 drift +126 ppm (VM clock, as before). hil-win on the tuned VM 2026-11-15 (E537): run 1
  4/5 (H1 silent, first session after Virus power-on + attach, audio read hung), run 2 4/5 (H2-1 drift +112 ppm
  only); probe answered every time, notes <= 0.70 ms. TUNED 2026-11-14 (E533): vCPUs pinned to P-cores
  4-7, emulator 2-3, topology 1/2/2, no emulated sound / SPICE USB redirect / balloon, evmcs+avic off, ISOs off;
  guest High performance, USB selective suspend off (AC). Backup: work/vm/win11-before-tuning-20261114.xml.
  2026-11-19: emulated sound card BACK (ich9 -> SPICE audio, user wanted sound in the VM; from the next VM start);
  before hil-win timing runs consider removing it again (backup without it: work/vm/win11-before-sound-20261119.xml).
  Before VM timing tests: `sudo cpupower frequency-set -g performance` (user; on since 2026-11-15 08:10).
  win10 VM: snapshot `pre-i36` (0.8.36). No spare USB controller for VFIO (user: no extra hardware for now).
- Mac (macOS 27.2.1): CI build 43620201798 installed (= 0.8.41 except texts). The RELEASE package is ready locally in
  work/rel-0.8.41/mac-push/TI-Revive-0.8.41-release (pkg 643f73d4, INSTALL-COMMAND + "Install test build.command");
  the Mac did not answer over SSH: push it to ~/Downloads when it is awake (scripts/mac.sh push), the user installs.
  Option files there: live-later.on (left from M2), klog.on, noteprobe.on, live-notes.on, diag.on.
- Private-data check (user 2026-11-15): `.claude/hooks/private-data.py` runs as git pre-commit/commit-msg/pre-push
  hook (installed here with `--install`, worktrees share it) and as Claude PreToolUse hook for `gh` posts; personal
  words in the git-ignored work/private-patterns.txt. A block means: move it to work/HANDOFF-private.md. 33 unit tests (93633f8: heredocs count only for gh stdin bodies).
- Branches: `libusb-audio` and `libusb-catalina` (worktree ../virus-ti-re-catalina) are merged into round-0.8.42 and
  done (dev-mac there = the Catalina build; on the round dev-mac stays the normal build), `catalina-test` (old).

## Open issues (checked with the user 2026-11-17; check GitHub only together with the user)

| Issue | Topic | Waits on | Last from us |
|---|---|---|---|
| #30 | knob automation in Logic fixed in rc2 (E542, confirmed); REOPENED audio 5.6 ms early in Logic (E540, E541, E545) SOLVED by rc3 per #41 (his log: Logic read 3085, re-announced 2816, 269 samples = 5.6 ms at 48k) | reporter, see #41; close #30 and #41 with the user once #41 is answered | 2026-11-15 |
| #32 | (no change since 11-10) | reporter | before 11-10 |
| #47 | NEW 2026-11-18: Snow "connected but not found" in Ableton; form says Windows 11 / TI Desktop, text macOS 27.0.1 / Snow; no logs, no version (E134 or E115 pattern?) | reporter: platform, model, version, Access/AURA, diag.log, broker log, ioreg, MIDI Drivers ls (issuecomment-7003657269) | 2026-11-19 |
| #37 | libusb pkg on Catalina, round 2026-11-19 (E561): (1) dropouts at the first run = byte-slip restarts at the stream start on the libusb path too (fix in round 0.8.43, NEXT 3a); (2) out of sync after dragging the playhead: sync error + read pull-backs + safety net in the log, cause open; (3) automation: 128 steps expected, gaps open; (4)(6) MIDI ports stay / Virus stays off Apple's driver = by design of the libusb service; (5) no crackle | reporter: playhead recovers by itself (2026-11-20); out of sync after reopening the project until a buffer change - log asked, automation questions open (issuecomment-7043987936). 2026-11-20 rc2 libusb: Logic "Failed to load Audio Unit 'TI Revive V'"; log: no plug-in line after the install, CC + broker rc2 fine; rc2 AU checked (x86_64, min 10.15, codes kept). Suspected (LOW, not seen): Logic's stale AU cache still points at the removed "TI Revive Virus Control.component" (rename, E557 only on macOS 27). Asked to clear ~/Library/Caches/AudioUnitCache + com.apple.audiounits.cache, rescan; rename reason explained; promised: installer clears the cache if that fixes it (issuecomment-7058689290) | 2026-11-21 |
| #38 | macOS 27 Live: AU/editor crash = SOLVED by the reporter (Access .pkg was missing; plug-in opens; Virus not connected yet) | reporter: note after connecting the Virus, then close (issuecomment-6948868845) | 2026-11-17 |
| #40 | (ours) macOS libusb audio as an option? | #39 reporter; #37 = first real data point (works) | 2026-11-13 |
| #41 | (the #30 reporter, Snow, Logic) rc3: bus recording on the grid; log + project attached (new take not measured by us). Log also: RAM backup failed, cannot write ~/Documents/... .tmp (nothing written, safe) | reporter: Logic's Documents Folder permission in Privacy & Security (issuecomment-6900513133); 3085 -> 2816 log = E546 | 2026-11-15 |
| #43 | original TI, OS 5.1.7, macOS: `tirevive-cc restore` stops at a fixed slot ("another patch after the restore"). CAUSE (E549): byte 10 (patch flags, E156) is not kept by the Virus on a RAM store, the read-back compared it; Rom A slot 7 = 0x76. FIX on round-0.8.42 (controlcenter.cpp patch_difference: byte 10 left out, a real mismatch names the byte; 3 new tests); original checked with REA (stores byte 10 unchanged, no read-back). Not run on hardware. rc6 RELEASED (pre-release, tag e84ded1, CI 44121551201 all 9 jobs green, 7 assets, 20:45 UTC); reply posted. Mutation on controlcenter.cpp not done (the copy ../tirevive-mut fails unmutated, rebuild there) | reporter: test rc6 (posted issuecomment-6960600649) | 2026-11-17 |
| #42 | Snow, M4 Pro, macOS 15.8.2: vtibroker ~33 % CPU idle; cause E547 (Snow idle stream on bulk EP 0x85, ~3300/s). rc5 TESTED by the reporter (E550): ~830/s, 15-19 %, knobs fine; 33 sync reports in 6 min (asked if audible). Reply with why + how the original does it (E548) posted (issuecomment-6957394873). NEW (user 2026-11-17): option snow-idle-slow (off by default, Control Center Options; 4 ms after 1 s quiet; broker reads the flag file every 2 s; tests first; not run on a Snow). Sim confirmed from the Snow's USB chip code (E551, S147): idle packet armed on every read, no command switches it off 2026-11-18 reporter: no dropouts or timing problems in a quick Ableton test | reporter: 0.8.42 with the option on, CPU (issuecomment-7020102746) | 2026-11-19 |
| #52 | NEW 2026-11-20: Intel iMac 2017, Ventura 13.7.8 (its last macOS), TI2 Desktop: asks for a macOS 13 build. 14.4 is our package gate (mac-build-release.sh), Ventura has UniformTypeIdentifiers | reporter: DAW, Access software still installed? (issuecomment-7050158233); rc4 mac-ventura test + diag.log; us: nothing, untested by us (no Ventura Mac) | 2026-11-20 |
| #51 | NEW 2026-11-20: Windows 11, Apollo Twin as DAW interface (two clocks, ~90 ppm): click every ~2 min = read pulled back when err passes the 512 margin (E564) | reporter: setx VTI_NATIVE_MARGIN 1024 test + full diag.log (issuecomment-7044953063); us: TimelineFollow fix in a round | 2026-11-20 |
| #50 | 2026-11-20: two TI2s on macOS. Silent second Virus = USB hub on a TB dock (fixed by the Mac's own ports; E563). Virus 2 bank re-read persists with rc2 on the Mac, fix confirmed on Linux | reporter: ls of the cache folders (issuecomment-7047279049) + rc3 log (two opens, issuecomment-7047803240) | 2026-11-20 |
| #48 (dup #44) | M1, Sequoia, Ableton, TI2 Keyboard, 0.8.42: distorted after reopening a saved project, only through the Mac (unit outputs silent = parts on USB), output device a UMC1820 (two clocks), buffer change worse, project reload at 11:40 local fixed it. Log of 07:34: transport clean like #33 (E532), no AURA conflict, one full-scale window before a reload (E559); cause open | reporter: new log covering 11:35-11:40, UMC1820 sample rate (issuecomment-7038034557) | 2026-11-20 |
| #45 | NEW: two Viruses (TI Keyboard + TI2 Desktop) at once, one plug-in instance each, in Live. TI Revive: one Virus only (first found). Original 5.1.7 plug-in: device list, one owner instance per Virus (E553, static; driver side unknown). Replies posted (not yet, later round, asked OS + can they test: issuecomment-6980767270; follow-up with E553, asked if they used two with the original: issuecomment-6981406654) 2026-11-18 reporter (French): two editors in Ableton were always buggy with the original; will beta test; asked OS again in French (issuecomment-6984586414). DECIDED: maintainer posted 2026-11-18 it is on the list, first test build to them by Fri/Sat (2026-11-20/21) IN WORK (round-0.8.42, user 2026-11-18: one broker per Virus, assignment like the original, all 3 OS, numbered MIDI ports, ASIO stays Virus 1): devslot.h/winslot.h (-slot N, -N names, key = USB port), slotpick.h (owner per Virus, empty slot 2+ given up after 3 s, project record TIRevive/Virus), RAM cache virus-N, Mac Core Audio by USB serial in UID (UNVERIFIED), systemd tirevive-broker@N. Review findings 1-7 fixed (findings/audit-2026-11-18-multidevice.md); dev build installed on Linux with tirevive-broker@2-4 (systemd starts broker 2: checked), hil.sh ALL PASS (H1, H2x2, H3, H6). Never run with two Viruses; mismatch question not done | reporter: test 0.8.42 with both Viruses (released 2026-11-19, issuecomment-7020102331) | 2026-11-19 |

#49 (2026-11-20): test report, all works on 0.8.41, TI2 Polar, M1 MacBook Pro, Sequoia 15, FL Studio 26.1.7, through a USB-C hub; thanked, timing check asked (no reply needed); in README "Tested with" + site matrix (round-0.8.43); could be closed with the user. 2026-11-21 joint check: only #37 had a new reply.
#30 follow-up (Logic arrange meters of multi-timbral tracks): reproduced on our Mac = Logic's display (E538), answered 2026-11-15 (issuecomment-6892430267), stays closed. #39 closed by the reporter (doubled notes = Local On + Monitor In, E530/E531). #46 (2026-11-18): user report, all works on 0.8.41 with a TI2 Polar, Mac mini M2 Pro, macOS 26.4.1, Logic Pro 13, 44.1k/128; thanked + closed (issuecomment-6987596263); in README "Tested with" and the site matrix (round-0.8.43). #30 closed by us 2026-11-14 with the user: all works, Snow meters visible in the plug-in (E536). 2026-11-15 check: no new replies; a fork
(insolentconstruct) syncing our commits shows as "added a commit that references" events, nothing to do. Quiet-issue rule: 7 days after our
last request, close with the AGENTS.md message, only in a joint check with the user: #28 closed 2026-11-16 (issuecomment-6924172903), the
others from 2026-11-21 (#40 is ours: no closing; #32: look up its date first).

## Not verified on hardware

Full list: `python3 -I scripts/evidence.py list --no-hardware` (libusb stall level 2, Windows error 433, setup
unplug step on the Access driver, Mac "Virus TI MIDI" played through, Keyboard/Polar unlink, Mac pkg refusing < 14.4).

## Still open from round 0.8.42 (the full list moved to log/2026-11.md, 2026-11-19)

- Linux MIDI ports: no message seen through them yet (Desktop has no keys; the sim's virtual Keyboard could).
- Update check: not run on Windows (curl.exe via CreateProcess). hil-win (LOW): H1's hung first audio read after
  power-on + attach; drift limit 100 ppm vs 11 s items.
- Proposed (sim list): freeze guard (73 00 10 vv vs stream rate), single re-sync after a detected +64 (E492/E498),
  burst guard, fewer broker resets (E495), lower safety-net floor.

## Standing notes

- Weekly releases: one round per week, one test run (risk.py, hil.sh, checklist artifact via scripts/checklist.py). Raise native/CMakeLists.txt VERSION at every round start.
- Coverage: `scripts/coverage.py` (recipe in `findings/testing.md`). Mutation: `risk.py` names the files for
  `scripts/mutation.py`.
- Never `nativehost --stress*` on the real Virus at small blocks (E438). Never `virsh screenshot` with a Virus in a VM.
- One VM at a time (30 GB host). The tirevive-hil task in win11 runs hil-win in the console session; Live or the
  Control Center open in the VM make it stop ("close first").
- Xvfb grabs of the Control Center stay black (GTK, no WM): check its look on the user's desktop.
- Stop hook (handoff-check.sh + handoff-turn.py): a turn that edits the repo, commits, pushes, posts on GitHub or
  starts CI without touching this file is stopped once to update it.

## NEXT

00. #50: rc2 cache fix WORKS on Linux (sim repro, E563) but not on the reporter's Mac (Logic AU). Asked for an ls of
   cache + cache/virus-2 (issuecomment-7047279049). v0.8.43-rc3 out (CI 44792570714 green, 7 assets; diagnostics:
   build, RAM cache used, failed saves), offered on #50; waiting on the reporter's ls + rc3 log. v0.8.43-rc4 (d06c250, adds mac-ventura for #52) tagged, CI 44815783941 green, 8 assets (Ventura job: installer gate 13.0, plug-in minos 11.0); offered on #52, waiting on the reporter's test + diag.log. Repro cleaned up (vhci detached, server stopped). rc1/rc2 untested on our TI2 for the slip fix.
0. RELEASE 0.8.43 IN WORK (2026-11-21): TI2 back on Linux (VM was off, socket active); risk.py: class C all OS (hil cold/reopen/export, Mac power+sleep by hand); mutation run on the 20 files in 4 worktrees ../tirevive-mut43-1..4 (433bd17, logs work/mutation-0.8.43-*.log; mutation.py fixed for bin/vtitests, 5bbb2ce); final CI 44890937850 from 5bbb2ce running; site check DONE 2026-11-21 (old name in site Windows card + README macOS, tests 848, several Viruses/#50, hubs, silent slip E561, Burn/OS in README, FL Studio, Ventura/libusb packages named; release text + What's New (infopage.h, compiles into the plug-in, not yet built) soften the Virus 2 bank cache claim; row 6 left: #51 TI1 or TI2 unknown). DONE: final CI ALL GREEN; Linux CI build installed (plug-in a603d89b, broker 0fa955fd); hil.sh ALL PASS + fake slip restart ~1 s (ratio path; marker line not shown on our TI2) = E565; Mac pkg 1516da99 pushed to ~/Downloads/TI-Revive-0.8.43-final (user installs, then sleep + power by hand). Mutation run STOPPED for the hardware runs (copies reset), restart after hil-win. hil-win in the VM: 3/5 twice, VM noise accepted by the user (log 2026-11-21); VM shut down, Virus back on Linux, socket active. Mutation run restarted. Mac test FOUND E566 (a waiting instance played the Virus along: Core Audio opened while trying empty slots); fixed 7632771 (core_audio_allowed, test, 849 pass), CI 44898579947 green, Mac pkg aba91689 pushed (~/Downloads/TI-Revive-0.8.43-e566). Mac on 1516da99: hil-mac ALL PASS, sleep + power OK (user). E566 fix CONFIRMED by the user on the Mac (several instances) + hil-mac ALL PASS on aba91689 (results-20261121-123031). Mutation run DROPPED for this round (user 2026-11-21; copies reset). Linux hil.sh on the E566 build skipped: the change is macOS-only code (#ifdef __APPLE__), Linux behaviour unchanged. Release text + What's New: E566 line; Ventura backticks removed from the notes heredoc (0.8.42 release job failure cause). Next: merge, tag, release job, site rebuild, installs; compile infopage.h, checklist page, merge/tag. Original order: Virus back to Linux (VM=win11
   scripts/vm.sh detach; systemctl --user start tirevive-broker.socket) + restart the VM for its sound card; risk.py
   --round 0.8.43 -> hil.sh (cold, reopen, export) + Mac sleep/wake and power by hand + checklist page
   (scripts/checklist.py); mutation run on the files risk.py names; tirevive-site-check (texts changed with the
   rename; live site still says "looks and works like" until rebuilt); final CI from the round head; merge, tag, release
   (first run of the fixed release job), site rebuild on main.
1. Mac: the 0.8.42 release push is obsolete (the Mac has the 0.8.43 test package, pkg 22ae6de3).
2. Quiet issues #26 #31 #33 #36 CLOSED 2026-11-21 (joint check, standard message); next: #41 from 11-22, #30 together with #41 (its last request 11-15); #32 #40 are ours. Still to read:
   replies from #45 (two real Viruses: first real-world test), #42 (snow-idle-slow CPU), #44 (recall), #47, #43, #37.
3. Round 0.8.43 (started): the fixed release job (9049eec) runs for the first time at its tag - check it; rest of
   the round to discuss with the user (Windows MIDI ports, test automation, Linux MIDI with the sim's Keyboard).
   3a. Start-up slip in silence (#37, E561): DONE in code (slipwatch.h fires on the marker in the slipped layout;
   checked on real slipped/normal captures; 844 unit tests pass). Still to do: hardware check on our TI2 on Linux
   with VTI_NATIVE_FAKE_SLIP=1 (expect "byte slip, E488, marker - restarting the stream (1)" within ~2 s of the
   ping, then "sync marker at frame"); covered by the round's hil run once the TI2 is back from the VM.
   3b. Wish (#39, timemachine70 2026-11-20, answered issuecomment-7039532288): resize the plug-in window by dragging
   (today only right-click > Size 100-200 %, viruseditor.h canResize false); a later round, not 0.8.43.
   3c. #50 (E562): DONE in code: Core Audio pick/open outcome into diag.log ("audio input: ..."), " - Virus N" after
   the model with several Viruses. Mac compile only in CI (not built locally); fix core_audio_pick itself from the
   reporter's answer / the next log. rc1 log: pick by order works, the second start fails (E563); if not the hub,
   look at AudioDeviceStart on two Virus devices in one process. RAM cache per Virus fixed in rc2.
3d. #51 (E564): keep the timeline drift away from the read margin with two clocks (TimelineFollow STEP/HOLD vs
   timing_m 512; unit test first); resampler drift-follow is the long-term option. Not started.
4. Open from the round: the 53 ms stall on the sim's Virus 2 (sim wants a rerun with VTISIM_WARMUP=1); two real
   Viruses, and two on Windows/macOS, untested (Mac Core Audio pick by USB serial unverified); Live on macOS may host
   waiting instances in another process ("another program's plug-in" lines, not checked); W2's largest ASIO buffer not
   in the log; Windows MIDI ports wait for Windows' own MIDI Services (end of November update).
5. Proposals accepted 2026-11-19 (test automation): hil-mac started over SSH with `open` (try once), MIDI port checks
   in hil.sh / hil-mac, one install script for test builds, REAPER runs via scripts/reaper-open.sh (started), smart
   plug for H4 (hil.sh --power exists; user decides on the hardware).
6. Branding cleanup DONE and TESTED on round-0.8.43 (user 2026-11-19; list in work/HANDOFF-private.md; details
   log/2026-11.md): screenshots masked ("TI Revive", Lato Black), repo derpsn/ti-revive (+ derpsn.github.io redirect,
   main 42528a1), Info page footer, wording, Burn/OS gone (user: "cc looks clean"), RENAME plug-in "TI Revive", ASIO
   "TI Revive Audio", MIDI "TI Revive Synth"/"MIDI"; internal ids kept (owner.h names, class ids, AU codes/bundle ids,
   CoreMIDI/Windows MIDI ids). Old projects reopen: REAPER Linux E556, Logic + Live Mac E557 (MIDI input to choose
   again), Live Windows E558 incl. sound through TI Revive Audio. What's New + release text name these tests.
   CI 44615094701 (f3d612e) all green: Mac has its package (22ae6de3), win11 VM its setup.exe (d45048c3), Linux the
   local dev build. Not needed now (user): new Control Center screenshots without Burn/OS.
7. Our TI2 is in the win11 VM (see Where things stand); the VM's new sound card appears at the next VM start.
