# HANDOFF - current state

_The current state only, rewritten on every update (not appended): round, machines, who waits on whom, open tasks,
NEXT. At most 150 lines; the Stop hook (`.claude/hooks/handoff-check.sh`) refuses a longer file. What happened and
how goes into the dated log [`log/2026-11.md`](log/2026-11.md) (newest first); older logs are in [`log/`](log/).
Device/protocol knowledge goes into [`evidence.md`](evidence.md). Update this file in the same commit as the work._

Last update: 2026-11-15 afternoon: Control Center Access-look pages done (Linux/sim), #30 reopened + measured (E541), Mac round tasks done.

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
- Branches: `libusb-audio` and `libusb-catalina` (worktree ../virus-ti-re-catalina) are merged into round-0.8.42 and
  done (dev-mac there = the Catalina build; on the round dev-mac stays the normal build), `catalina-test` (old).

## Open issues (checked with the user 2026-11-14 evening; check GitHub only together with the user)

| Issue | Topic | Waits on | Last from us |
|---|---|---|---|
| #26 | Snow, Linux REAPER timing | reporter: pw-jack test; 0.8.41 note | 2026-11-14 |
| #28 | numbers 100-127 moved 5 px | reporter: confirm, close on yes | 2026-11-09 |
| #30 | ALSO: Virus knob -> Logic automation only with the plug-in window open (E542, fix: own gesture timer, untested in Logic). | | |
| #30 | REOPENED: Virus audio a few ms early in Logic (E540); NOT reproduced on our Mac/Logic/TI2 at 48k, 128/512, LL on (E541: 0.00 ms) | Chris: 15 ms, buffer 32, LL on -> exact here at 48k AND 44.1k (E541); left: Snow or his project; asked for his Logic project | 2026-11-15 |
| #31 | Win10 VirtualBox, Snow, setup with the Access driver | reporter: setup unplug step report; 0.8.41 note | 2026-11-14 |
| #32 | (no change since 11-10) | reporter | before 11-10 |
| #33 | Intel Mac 0.8.39: Logic distorted until a buffer change; log clean on the way in, not a slip (E532) | reporter: Logic output device, distortion on the unit's outputs?, 0.8.41 log | 2026-11-14 |
| #34 | TI2/Keyboard macOS crackle, bank sync failed 11.9 s (E529) | reporter: new log with 0.8.41 | 2026-11-14 |
| #36 | Windows Cubase bank sync 0/128 | reporter: BC2001.syx, new log with 0.8.41 | 2026-11-14 |
| #37 | Catalina: libusb test WORKED (E535); 0.8.42-rc1 libusb package (installer, no Terminal) posted | reporter: install rc1 libusb zip, crackle?, after a restart?, Save logs | 2026-11-14 |
| #38 | macOS 27 Live: AU/editor crash, not reproduced | reporter: crash report + log with 0.8.41 | 2026-11-14 |
| #40 | (ours) macOS libusb audio as an option? | #39 reporter; #37 = first real data point (works) | 2026-11-13 |

#30 follow-up (Logic arrange meters of multi-timbral tracks): reproduced on our Mac = Logic's display (E538), answered 2026-11-15 (issuecomment-6892430267), stays closed. #39 closed by the reporter (doubled notes = Local On + Monitor In, E530/E531). #30 closed by us 2026-11-14 with the
user: all works, Snow meters visible in the plug-in (E536). 2026-11-15 check: no new replies; a fork
(insolentconstruct) syncing our commits shows as "added a commit that references" events, nothing to do. Quiet-issue rule: 7 days after our
last request, close with the AGENTS.md message, only in a joint check with the user: #28 from 2026-11-16, the
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

1. Mac: test build CI 43708448485 (9a003ee, pkg db140751) installed 2026-11-15; the three new Control Center pages
   (nerd mode, OPTIONS, BACKUP & RESTORE with the real RAM banks) look right on macOS (user + screenshots). Open: the
   Windows look in the VM; new README/site nerd mode screenshot (docs/images/control-center-nerd-mode.png is old).
2. #30 knob automation in Logic (E542): fix written (plug-in's own gesture timer); next: CI build -> Logic test on our Mac
   (Latch, window closed, turn cutoff on the Virus) -> new RC in #30.
   #30 (reopened): early audio waits on the reporter's settings (E540/E541: not reproduced on our Mac/Logic/TI2 at 48k). If 44.1 kHz:
   measure that on our Mac; Snow: only the reporter. New RC promised once fixed.
3. v0.8.42-rc1 pre-release out; wait for #37. build.yml "New in 0.8.42" = rc1 text: rewrite before the final (update
   check + bug report, Linux/Windows MIDI ports, What's New scrollbar, Control Center pages, MIDI port texts).
4. Windows MIDI ports: real path waits for Windows' own API (end of Nov update) in the VM: installer must then add the
   helper by itself; real key/DIN traffic untested (no keyed Virus here).
5. Open, low: stray single sample 2^-6 after record start (E541); proposed sim items (discuss one at a time).
6. Linux: DEV build installed (plug-in from a1153d9; release backed up in ~/.vst3-backup/0.8.41-release); Mac: test
   build CI 43696948011 installed; TI2 on the Mac's USB. #28 quiet-issue limit 2026-11-16 (joint check).
7. Waiting on reporters (table above); check issues only together with the user.
