# HANDOFF - current state

_The current state only, rewritten on every update (not appended): round, machines, who waits on whom, open tasks,
NEXT. At most 150 lines; the Stop hook (`.claude/hooks/handoff-check.sh`) refuses a longer file. What happened and
how goes into the dated log [`log/2026-11.md`](log/2026-11.md) (newest first); older logs are in [`log/`](log/).
Device/protocol knowledge goes into [`evidence.md`](evidence.md). Update this file in the same commit as the work._

Last update: 2026-11-19: round-0.8.43 started (VERSION 0.8.43; #46 in README/site). **0.8.42 RELEASED** (user "Ship it", no rc7); test round complete (details log/2026-11.md); replies posted on #45 #42 #44; Mac release push pending (Mac asleep). Quiet-issue closings due from 2026-11-21.

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
- Our TI2 Desktop: on **Linux** USB (2026-11-14 evening). User flashed ROM Z on it in early November (E500).
- Linux: CI build 44588173936 (0.8.42, 42f0a12: E554 + E555 + stats label) installed 2026-11-19 (plug-in 4b9563c5, broker c280b8e6). Virus back on Linux after the VM test (node plugdev, socket active).
- win11 VM: CI build 44588173936 (42f0a12) installed 2026-11-19 (VST3 7eafc6fc; tirevive-hil task now runs C:\vti\0842\hil) (/quiet exit 0), shut down; hil-win 2026-11-19: all pass except H2-2 drift +126 ppm (VM clock, as before). hil-win on the tuned VM 2026-11-15 (E537): run 1
  4/5 (H1 silent, first session after Virus power-on + attach, audio read hung), run 2 4/5 (H2-1 drift +112 ppm
  only); probe answered every time, notes <= 0.70 ms. TUNED 2026-11-14 (E533): vCPUs pinned to P-cores
  4-7, emulator 2-3, topology 1/2/2, no emulated sound / SPICE USB redirect / balloon, evmcs+avic off, ISOs off;
  guest High performance, USB selective suspend off (AC). Backup: work/vm/win11-before-tuning-20261114.xml.
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
| #26 | Snow, Linux REAPER timing | reporter: pw-jack test; 0.8.41 note | 2026-11-14 |
| #30 | knob automation in Logic fixed in rc2 (E542, confirmed); REOPENED audio 5.6 ms early in Logic (E540, E541, E545) SOLVED by rc3 per #41 (his log: Logic read 3085, re-announced 2816, 269 samples = 5.6 ms at 48k) | reporter, see #41; close #30 and #41 with the user once #41 is answered | 2026-11-15 |
| #31 | Win10 VirtualBox, Snow, setup with the Access driver | reporter: setup unplug step report; 0.8.41 note | 2026-11-14 |
| #32 | (no change since 11-10) | reporter | before 11-10 |
| #33 | Intel Mac 0.8.39: Logic distorted until a buffer change; log clean on the way in, not a slip (E532) | reporter: Logic output device, distortion on the unit's outputs?, 0.8.41 log | 2026-11-14 |
| #47 | NEW 2026-11-18: Snow "connected but not found" in Ableton; form says Windows 11 / TI Desktop, text macOS 27.0.1 / Snow; no logs, no version (E134 or E115 pattern?) | reporter: platform, model, version, Access/AURA, diag.log, broker log, ioreg, MIDI Drivers ls (issuecomment-7003657269) | 2026-11-19 |
| #36 | Windows Cubase bank sync 0/128 | reporter: BC2001.syx, new log with 0.8.41 | 2026-11-14 |
| #37 | rc3 libusb TESTED by the reporter 2026-11-18: notes in sync from the first start, no crackle; knobs as MIDI controller work in Remote mode (as the original); MIDI ports appear after loading the plug-in (by design only while the broker runs), but sometimes only after a Mac restart or Virus power cycle (cause unknown; their log had the broker start scrolled out). Log: 7 "audio arrives scrambled" restarts, 3 sync errors (asked if audible). Offset hypothesis: confirmed for #30, still open for #33 | reporter: grep "MIDI port" broker log when ports are missing, DIN MIDI In test, dropout heard? (issuecomment-6984585900); user 2026-11-18: decide on ports staying up while a Virus is connected (CPU cost, #42) only after that log; maintainer nudged 2026-11-18 19:43 | 2026-11-18 |
| #38 | macOS 27 Live: AU/editor crash = SOLVED by the reporter (Access .pkg was missing; plug-in opens; Virus not connected yet) | reporter: note after connecting the Virus, then close (issuecomment-6948868845) | 2026-11-17 |
| #40 | (ours) macOS libusb audio as an option? | #39 reporter; #37 = first real data point (works) | 2026-11-13 |
| #41 | (the #30 reporter, Snow, Logic) rc3: bus recording on the grid; log + project attached (new take not measured by us). Log also: RAM backup failed, cannot write ~/Documents/... .tmp (nothing written, safe) | reporter: Logic's Documents Folder permission in Privacy & Security (issuecomment-6900513133); 3085 -> 2816 log = E546 | 2026-11-15 |
| #43 | original TI, OS 5.1.7, macOS: `tirevive-cc restore` stops at a fixed slot ("another patch after the restore"). CAUSE (E549): byte 10 (patch flags, E156) is not kept by the Virus on a RAM store, the read-back compared it; Rom A slot 7 = 0x76. FIX on round-0.8.42 (controlcenter.cpp patch_difference: byte 10 left out, a real mismatch names the byte; 3 new tests); original checked with REA (stores byte 10 unchanged, no read-back). Not run on hardware. rc6 RELEASED (pre-release, tag e84ded1, CI 44121551201 all 9 jobs green, 7 assets, 20:45 UTC); reply posted. Mutation on controlcenter.cpp not done (the copy ../tirevive-mut fails unmutated, rebuild there) | reporter: test rc6 (posted issuecomment-6960600649) | 2026-11-17 |
| #42 | Snow, M4 Pro, macOS 15.8.2: vtibroker ~33 % CPU idle; cause E547 (Snow idle stream on bulk EP 0x85, ~3300/s). rc5 TESTED by the reporter (E550): ~830/s, 15-19 %, knobs fine; 33 sync reports in 6 min (asked if audible). Reply with why + how the original does it (E548) posted (issuecomment-6957394873). NEW (user 2026-11-17): option snow-idle-slow (off by default, Control Center Options; 4 ms after 1 s quiet; broker reads the flag file every 2 s; tests first; not run on a Snow). Sim confirmed from the Snow's USB chip code (E551, S147): idle packet armed on every read, no command switches it off 2026-11-18 reporter: no dropouts or timing problems in a quick Ableton test | reporter: 0.8.42 with the option on, CPU (issuecomment-7020102746) | 2026-11-19 |
| #44 | NEW: M1, Sequoia, Ableton, TI2 Keyboard, 0.8.41: distorted sound after reopening a saved project (gone after several reopens or a plug-in reload; like #33, E532), patches sometimes gone after a reload (audit #21-#24 area, not in rc6). AURA editor installed (E134 conflict?). No log | reporter: diag.log, AURA driver parked + restart, buffer-change test, rc6 test (issuecomment-6975482074); 0.8.42 with the recall fixes posted (issuecomment-7020103207) | 2026-11-19 |
| #45 | NEW: two Viruses (TI Keyboard + TI2 Desktop) at once, one plug-in instance each, in Live. TI Revive: one Virus only (first found). Original 5.1.7 plug-in: device list, one owner instance per Virus (E553, static; driver side unknown). Replies posted (not yet, later round, asked OS + can they test: issuecomment-6980767270; follow-up with E553, asked if they used two with the original: issuecomment-6981406654) 2026-11-18 reporter (French): two editors in Ableton were always buggy with the original; will beta test; asked OS again in French (issuecomment-6984586414). DECIDED: maintainer posted 2026-11-18 it is on the list, first test build to them by Fri/Sat (2026-11-20/21) IN WORK (round-0.8.42, user 2026-11-18: one broker per Virus, assignment like the original, all 3 OS, numbered MIDI ports, ASIO stays Virus 1): devslot.h/winslot.h (-slot N, -N names, key = USB port), slotpick.h (owner per Virus, empty slot 2+ given up after 3 s, project record TIRevive/Virus), RAM cache virus-N, Mac Core Audio by USB serial in UID (UNVERIFIED), systemd tirevive-broker@N. Review findings 1-7 fixed (findings/audit-2026-11-18-multidevice.md); dev build installed on Linux with tirevive-broker@2-4 (systemd starts broker 2: checked), hil.sh ALL PASS (H1, H2x2, H3, H6). Never run with two Viruses; mismatch question not done | reporter: test 0.8.42 with both Viruses (released 2026-11-19, issuecomment-7020102331) | 2026-11-19 |

#30 follow-up (Logic arrange meters of multi-timbral tracks): reproduced on our Mac = Logic's display (E538), answered 2026-11-15 (issuecomment-6892430267), stays closed. #39 closed by the reporter (doubled notes = Local On + Monitor In, E530/E531). #46 (2026-11-18): user report, all works on 0.8.41 with a TI2 Polar, Mac mini M2 Pro, macOS 26.4.1, Logic Pro 13, 44.1k/128; thanked + closed (issuecomment-6987596263); in README "Tested with" and the site matrix (round-0.8.43). #30 closed by us 2026-11-14 with the user: all works, Snow meters visible in the plug-in (E536). 2026-11-15 check: no new replies; a fork
(insolentconstruct) syncing our commits shows as "added a commit that references" events, nothing to do. Quiet-issue rule: 7 days after our
last request, close with the AGENTS.md message, only in a joint check with the user: #28 closed 2026-11-16 (issuecomment-6924172903), the
others from 2026-11-21 (#40 is ours: no closing; #32: look up its date first).

## Not verified on hardware

Full list: `python3 -I scripts/evidence.py list --no-hardware` (libusb stall level 2, Windows error 433, setup
unplug step on the Access driver, Mac "Virus TI MIDI" played through, Keyboard/Polar unlink, Mac pkg refusing < 14.4).

## Round 0.8.42 items (shipped in 0.8.42 unless NEXT 4 lists them as open)

User 2026-11-15 on the order: Mac push later; live-later 160 explained, go; MIDI ports: Windows part only after a
talk with the user (Linux may start); hil-win on the tuned VM: yes; update check + bug report also in the plug-in
(proposal to the user first); #39 latency note: show the text before posting; proposed sim items: discuss when there.

- live-later 160 on the Mac (E539, 5 runs): like 128, played notes up to one 64-frame step apart (only run 1 all on
  one); grid-edge reading wrong; texts "at most about 1.3 ms" kept. BACK TO 128 (user 2026-11-15), test first.
- Linux MIDI ports: CODE DONE 2026-11-15: vtikeyport_linux.c (straight /dev/snd/seq ioctls, no alsa-lib), client
  "Virus TI", port 0 "Virus TI MIDI" (cable 0), port 1 "Virus TI Synth" (cable 1) = the kernel driver's names (E016),
  read-only; seqevent.h + test_seqevent first. Seen: ports in aconnect -l (inputs only). NOT seen: a message through
  them (Desktop has no keys, no keyboard at the DIN In; sim's virtual Keyboard could). README/site texts after that.
- MIDI ports Windows: talk with the user first (Windows
  MIDI Services virtual devices on Win11 25H2+, design-midi-ports.md option A; option C, a forwarding VST3, for
  Windows 10).
- FIRST (code written, see Where things stand): the libusb package; open: CI build of both Mac jobs, our Mac
  keeps the NORMAL package (install it there, check it is unchanged), then an rc1 pre-release with the libusb zip
  for #37 (promised there). Unverified: launchd socket activation as root on 10.15, SockPathMode 438, the
  postinstall's pkill of a running user broker. mac-libusb-test.command stays a dev tool (not in any package).
- Update check + bug report: CODE DONE 2026-11-15 (user: "the update features fits"): cc/updatecheck.{h,cpp} (test
  first, test_updatecheck; mutation-checked by hand), option "update-check" (off; 8th row), Control Center checks at
  start / when switched on, once a day, system curl (Linux: wget fallback), update.txt in plugin_dir; plug-in reads
  it only: LCD note once per release + "Download TI Revive X" in ?; "Prepare a Bug Report..." = save logs + GitHub
  form with "TI Revive <v>, <OS>" in Versions; tirevive-cc update-check. Real check OK on Linux and macOS (latest
  0.8.41, 2026-11-15); GUI checked by the user on both. Not run: Windows (curl.exe via CreateProcess).
- #39: no latency switch (user 2026-11-15); the recording hint instead, done.
- Windows VM: hil-win on the tuned VM DONE (E537): the probe miss is gone, no A/B needed. Open (LOW): H1's hung
  first audio read after power-on + attach; watch for it in the next hil-win, and drift limit 100 ppm vs 11 s items.
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

1. Mac (only when needed, user 2026-11-19): push the 0.8.42 release (work/rel-0.8.42/out/macx/TI-Revive-0.8.42-release, pkg 1f8f888c) with
   scripts/mac.sh push when the Mac answers; the user installs; check the installed files. Until then it has the
   tested CI build 44588173936 (same code, older texts).
2. Joint issue check from 2026-11-21: quiet-issue closings (#26 #31 #33 #36; #41 from 11-22; #32 #40 are ours);
   replies from #45 (two real Viruses: first real-world test), #42 (snow-idle-slow CPU), #44 (recall), #47, #43, #37.
3. Round 0.8.43 (started): the fixed release job (9049eec) runs for the first time at its tag - check it; rest of
   the round to discuss with the user (Windows MIDI ports, test automation, Linux MIDI with the sim's Keyboard).
4. Open from the round: the 53 ms stall on the sim's Virus 2 (sim wants a rerun with VTISIM_WARMUP=1); two real
   Viruses, and two on Windows/macOS, untested (Mac Core Audio pick by USB serial unverified); Live on macOS may host
   waiting instances in another process ("another program's plug-in" lines, not checked); W2's largest ASIO buffer not
   in the log; Windows MIDI ports wait for Windows' own MIDI Services (end of November update).
5. Proposals accepted 2026-11-19 (test automation): hil-mac started over SSH with `open` (try once), MIDI port checks
   in hil.sh / hil-mac, one install script for test builds, REAPER runs via scripts/reaper-open.sh (started), smart
   plug for H4 (hil.sh --power exists; user decides on the hardware).
6. Our Mac: Logic Recording Delay back to 0 (user 2026-11-19).
7. Branding cleanup IN WORK (user 2026-11-19; list in work/HANDOFF-private.md): plug-in name "TI Revive", audio
   "TI Revive Audio", MIDI "TI Revive MIDI" etc.; Burn/OS Updater out of the Control Center; screenshots DONE (idea A,
   user 2026-11-19): scripts/site/mask_screenshots.py covers Access' logos, "TI Revive" (Lato Black, orange TI) in the
   top logo spot, only the published PNGs (the plug-in shows the user's skin as before); repo RENAMED to
   derpsn/ti-revive 2026-11-19, old site path forwarded by the new repo derpsn/derpsn.github.io, links changed on the
   round (log/2026-11.md); open: old /issues/45 link answered 404 (recheck), live site keeps the old canonical URL
   until rebuilt from main, Google Search Console: new address (user); Info page footer
   "Not affiliated ..." (kInfoFooter, test infopage_footer_notice, 839 pass) uncommitted, screenshot needs the Virus on;
