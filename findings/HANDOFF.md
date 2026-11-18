# HANDOFF - current state

_The current state only, rewritten on every update (not appended): round, machines, who waits on whom, open tasks,
NEXT. At most 150 lines; the Stop hook (`.claude/hooks/handoff-check.sh`) refuses a longer file. What happened and
how goes into the dated log [`log/2026-11.md`](log/2026-11.md) (newest first); older logs are in [`log/`](log/).
Device/protocol knowledge goes into [`evidence.md`](evidence.md). Update this file in the same commit as the work._

Last update: 2026-11-18: audit fixes 1-31 of 34 done and reviewed (NEXT 5b); rc6 out for #43; #42 option snow-idle-slow (E550, E551); branding cleanup parked (NEXT 8).

## Where things stand

- **0.8.41 is released and Latest** (2026-11-14, user "Ship it"): tag v0.8.41 on main 266cb46 = merge 6e4c632
  (main 5fc470c + round e5f84e8) + one commit that drops main's rc8 preview block from the README (it survived the
  merge without a conflict: check that at every round merge). Release CI 43625111066 green, 6 assets. Site rebuilt
  by hand (site.yml 43625667337): Download 0.8.41, no preview box, 753 tests, coverage 96.4 %.
- **Round 0.8.42** (branch `round-0.8.42` from origin/main 266cb46 + round-0.8.41 docs + `libusb-audio` +
  `libusb-catalina`; the newest files are here). First item done in code: the **libusb package** (user 2026-11-14:
  normal Mac package stays as it is; a separate package for Macs where Apple's driver crackles; "we dont test the
  catalina build. This needs to be done from the user that had the issue" = #37 tests, we do not):
  `LIBUSB_AUDIO=1 MAC_MIN=10.15 MAC_ARCHS=x86_64 PLUGIN_MIN=10.15 scripts/mac-build-release.sh` ->
  TI-Revive-<v>-mac-libusb.zip (plain component pkg, README-mac-libusb.txt); its postinstall (marker `libusb-audio`
  in the pkg scripts) installs the LaunchDaemon io.github.derpsn.tirevive.usbaudio (root, launchd socket
  /tmp/vtibroker-<uid>.sock 0666, VTI_OWNER_UID, -stay) and mac-usb-audio.on; the normal pkg removes a leftover
  service; the uninstaller too. Broker: VTI_OWNER_UID (owneruid.h vti_owner_env), clients other than the owner/root
  refused on macOS (vti_client_allowed), give-back without the abort (macgiveback.h, E535 (6)), gap timers reset at
  audio stop. Plug-in: mac-usb-audio is no Options switch any more (cc::mac_usb_audio()). CI: build.yml job
  `mac-libusb` (macos-15-intel) + release asset. CI 43644097519 (ff629fd, version 0.8.42) ALL GREEN incl. mac and
  mac-libusb (its package checks: plain pkg, marker, no UTI, CC min 10.15). Not run on any Mac yet.
- Test round of 0.8.41: all passed (details in log/2026-11.md, checklist spec scripts/checklists/round-0.8.41.json).
- Our TI2 Desktop: on **Linux** USB (2026-11-14 evening). User flashed ROM Z on it in early November (E500).
- Linux: 0.8.41 release installed (plug-in 184badb2, broker 728db830, Control Center 0.8.41).
- win11 VM: 0.8.41 release installed (/quiet exit 0), shut down. hil-win on the tuned VM 2026-11-15 (E537): run 1
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
| #30 | knob automation in Logic fixed in rc2 (E542): CONFIRMED by Chris 13:47 | - (done) | 2026-11-15 |
| #30 | REOPENED: Virus audio 5.6 ms early in Logic (E540, E541, E545): SOLVED by rc3 per #41 (his log: Logic read 3085, re-announced 2816, 269 samples = 5.6 ms at 48k; not his Snow, corrected in #41) | reporter, see #41; close #30 and #41 with the user once #41 is answered | 2026-11-15 |
| #31 | Win10 VirtualBox, Snow, setup with the Access driver | reporter: setup unplug step report; 0.8.41 note | 2026-11-14 |
| #32 | (no change since 11-10) | reporter | before 11-10 |
| #33 | Intel Mac 0.8.39: Logic distorted until a buffer change; log clean on the way in, not a slip (E532) | reporter: Logic output device, distortion on the unit's outputs?, 0.8.41 log | 2026-11-14 |
| #34 | TI2/Keyboard macOS crackle, bank sync failed 11.9 s (E529) | reporter: new log with 0.8.41 | 2026-11-14 |
| #36 | Windows Cubase bank sync 0/128 | reporter: BC2001.syx, new log with 0.8.41 | 2026-11-14 |
| #37 | rc1 libusb: NO crackle (E543); fixes in rc3 (E544; our Mac: normal pkg fine, libusb pkg untested here): MIDI ports via a helper in the user session, latency re-announce + log (offset hypothesis: confirmed for #30 by #41's log, still open for #37/#33) | reporter: test rc3 (MIDI ports, offset size, log; posted issuecomment-6896731886) | 2026-11-15 |
| #38 | macOS 27 Live: AU/editor crash = SOLVED by the reporter (Access .pkg was missing; plug-in opens; Virus not connected yet) | reporter: note after connecting the Virus, then close (issuecomment-6948868845) | 2026-11-17 |
| #40 | (ours) macOS libusb audio as an option? | #39 reporter; #37 = first real data point (works) | 2026-11-13 |
| #41 | (the #30 reporter, Snow, Logic) rc3: bus recording on the grid; log + project attached (new take not measured by us). Log also: RAM backup failed, cannot write ~/Documents/... .tmp (nothing written, safe) | reporter: Logic's Documents Folder permission in Privacy & Security (issuecomment-6900513133); 3085 -> 2816 log = E546 | 2026-11-15 |
| #43 | original TI, OS 5.1.7, macOS: `tirevive-cc restore` stops at a fixed slot ("another patch after the restore"). CAUSE (E549): byte 10 (patch flags, E156) is not kept by the Virus on a RAM store, the read-back compared it; Rom A slot 7 = 0x76. FIX on round-0.8.42 (controlcenter.cpp patch_difference: byte 10 left out, a real mismatch names the byte; 3 new tests); original checked with REA (stores byte 10 unchanged, no read-back). Not run on hardware. rc6 RELEASED (pre-release, tag e84ded1, CI 44121551201 all 9 jobs green, 7 assets, 20:45 UTC); reply posted. Mutation on controlcenter.cpp not done (the copy ../tirevive-mut fails unmutated, rebuild there) | reporter: test rc6 (posted issuecomment-6960600649) | 2026-11-17 |
| #42 | Snow, M4 Pro, macOS 15.8.2: vtibroker ~33 % CPU idle; cause E547 (Snow idle stream on bulk EP 0x85, ~3300/s). rc5 TESTED by the reporter (E550): ~830/s, 15-19 %, knobs fine; 33 sync reports in 6 min (asked if audible). Reply with why + how the original does it (E548) posted (issuecomment-6957394873). NEW (user 2026-11-17): option snow-idle-slow (off by default, Control Center Options; 4 ms after 1 s quiet; broker reads the flag file every 2 s; tests first; not run on a Snow). Sim confirmed from the Snow's USB chip code (E551, S147): idle packet armed on every read, no command switches it off | reporter: sync reports audible?; us: build with the option to them when CI is free | 2026-11-17 |

#30 follow-up (Logic arrange meters of multi-timbral tracks): reproduced on our Mac = Logic's display (E538), answered 2026-11-15 (issuecomment-6892430267), stays closed. #39 closed by the reporter (doubled notes = Local On + Monitor In, E530/E531). #30 closed by us 2026-11-14 with the
user: all works, Snow meters visible in the plug-in (E536). 2026-11-15 check: no new replies; a fork
(insolentconstruct) syncing our commits shows as "added a commit that references" events, nothing to do. Quiet-issue rule: 7 days after our
last request, close with the AGENTS.md message, only in a joint check with the user: #28 closed 2026-11-16 (issuecomment-6924172903), the
others from 2026-11-21 (#40 is ours: no closing; #32: look up its date first).

## Not verified on hardware

Full list: `python3 -I scripts/evidence.py list --no-hardware` (libusb stall level 2, Windows error 433, setup
unplug step on the Access driver, Mac "Virus TI MIDI" played through, Keyboard/Polar unlink, Mac pkg refusing < 14.4).

## Round 0.8.42 candidates (decided unless marked proposed)

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

1. Mac: test build CI 43708448485 (9a003ee, pkg db140751) installed 2026-11-15; the three new Control Center pages
   (nerd mode, OPTIONS, BACKUP & RESTORE with the real RAM banks) look right on macOS AND Windows (win11 VM, rc2, the
   simulator's virtual TI2 over USB/IP: user "works"). New nerd mode screenshot (Mac, 13:44, real traffic) in docs/images
   + site (860x612, not-affiliated notice under it). Site not redeployed (by hand at release).
   2026-11-17 (user): every screenshot caption in README + site now says "TI Revive ships no Access files; it draws this
   look at runtime from the skin in your own Access installer" (header/hero: longer version). Captions-only commit 7d1de54 fast-forwarded onto main (user's
   choice), site.yml 44021683300 green: LIVE. Round merge later: same text on both sides.
2. #30 knob automation in Logic (E542): fix 2 WORKS on our Mac. v0.8.42-rc2 RELEASED as pre-release (tag on a164a98,
   run 43713472544 green, 7 assets); Chris CONFIRMED rc2 (13:47 UTC). His Logic project came by email (dropped):
   uploaded 17:54: measured on our Mac (E545): exact here, his take 5.6 ms early -> his setup (Snow?); rc3 log asked.
   Our Mac: Logic Recording Delay was set to -269 for the test: user to set it back to 0 (not confirmed yet).
3. v0.8.42-rc3 out (#37 and #30 wait). build.yml "New in 0.8.42" + What's New entry written for rc2 (all round items).
4. Windows MIDI ports: real path waits for Windows' own API (end of Nov update) in the VM: installer must then add the
   helper by itself; real key/DIN traffic untested (no keyed Virus here).
5. Open, low: stray single sample 2^-6 after record start in Logic (E541): not reproduced in nativehost (10/10 zero, both
   output-delay modes); next only if it shows again (Logic 48k series). Proposed sim items (discuss one at a time).
5a. #37 fixes (E544) on round-0.8.42, CI 43727531536 all green (no tag): libusb package MIDI ports (launchctl asuser helper,
   `vtibroker -midiports UID`) and the latency watch (UI timer, `the host reads latency` log line). Next: user back ->
   our Mac CHECKED (rc3 normal pkg 511b0958): ports as before, Logic reads the right latency (2432 = 1408+512+512),
   no re-announce (E544); diag.off had been set on the Mac, removed. libusb install on our Mac only with the user;
   rc3 released for #37 (user 2026-11-15: post it for him to test, Catalina not testable here).
5b. AUDIT 2026-11-17 (user: fix every point, update after each): [`audit-2026-11-17.md`](audit-2026-11-17.md), 34 items; one at a time, test first, done only after /ponytail:ponytail-review. DONE 1-31 (fff1a52.., 822 unit + 1 vlatests + 197 script tests; #15, #28 (driver.log now in Program Files\TI Revive) need a win11 VM MSI run before release, validator 141/141; nothing run on hardware, Mac or DAW yet). NEXT: #32 MemorySync reply checksum, then 33.. in order.
6. Linux: DEV build installed (plug-in from a1153d9; release backed up in ~/.vst3-backup/0.8.41-release); Mac: test
   build CI 43696948011 installed; TI2 on the Mac's USB.
7. Waiting on reporters (table above); check issues only together with the user.
8. PARKED (owner 2026-11-17, come back later): branding cleanup (logos in screenshots, plug-in/ASIO names, repo/site
   name, wording). Audit, file list and order in work/HANDOFF-private.md; nothing started.
