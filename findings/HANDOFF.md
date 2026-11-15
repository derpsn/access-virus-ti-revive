# HANDOFF - current state

_The current state only, rewritten on every update (not appended): round, machines, who waits on whom, open tasks,
NEXT. At most 150 lines; the Stop hook (`.claude/hooks/handoff-check.sh`) refuses a longer file. What happened and
how goes into the dated log [`log/2026-11.md`](log/2026-11.md) (newest first); older logs are in [`log/`](log/).
Device/protocol knowledge goes into [`evidence.md`](evidence.md). Update this file in the same commit as the work._

Last update: 2026-11-15 morning: live-later 160, Linux MIDI ports, hil-win tuned VM (E537), update check + bug report done; dev build installed on Linux.

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

- libusb reader stall handling (E503) at kernel level with 8 transfers pending (level 2, needs sudo); sim held.
- Windows broker error 433 = device gone: unit test only (WC4 skipped in the VM).
- Windows setup unplug step (E504): never run with a Virus on the Access driver (win10 snapshot `pre-i36-access`
  could do it).
- Mac "Virus TI MIDI": the input shows up (M1); nothing played through it (no keyboard at the MIDI In here).
- Unlink on a real Keyboard/Polar: sim only (E525); on Linux, Windows (W3) and our Desktop the link lines are fine.
- Mac pkg refusing macOS < 14.4: no old Mac here.
- Full list: `python3 -I scripts/evidence.py list --no-hardware`.

## Round 0.8.42 candidates (decided unless marked proposed)

User 2026-11-15 on the order: Mac push later; live-later 160 explained, go; MIDI ports: Windows part only after a
talk with the user (Linux may start); hil-win on the tuned VM: yes; update check + bug report also in the plug-in
(proposal to the user first); #39 latency note: show the text before posting; proposed sim items: discuss when there.

- live-later at 160 frames: CODE DONE 2026-11-15 (LIVE_LATER_FRAMES 160, test first; log line prints the value).
  Open: the same keystamp test on the Mac (armed track, Monitor In, keystamp 500 ms, klog K and onset steps); the
  texts' "at most about 1.3 ms late (measured on a Mac)" is the 128 result: re-measure, then fix CC/README/site.
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
  form with "TI Revive <v>, <OS>" in Versions; tirevive-cc update-check. Linux: real check OK (latest 0.8.41).
  Not run: Windows (curl.exe via CreateProcess; mingw syntax only), macOS, the GUI by eye. README/site/helptext done.
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

1. Linux has the DEV build installed (plug-in 7220de21 from a1153d9, broker 1f2fc540, CC + tirevive-cc from
   1435b04; 0.8.41 release backed up in ~/.vst3-backup/0.8.41-release). User smoke test 2026-11-15 PASSED: Options
   8th row visible (8 rows fit, no scroll: right), switching it on ran a real check (update.txt latest 0.8.41);
   plug-in ? menu "Prepare a Bug Report..." saved the logs and opened the form; plug-in played, no crash.
2. #39 decided (user 2026-11-15): NO automatic latency switch ("the original also had this issue and live mode is for
   that"); only the once-only LCD hint when recording played notes without Live Mode: CODE DONE (recordhint.h, test
   first; help text, README, site). Not seen in a DAW yet (needs Live: Mac). Any #39 text: show the user first.
3. v0.8.42-rc1 is out as pre-release (tag 5a83f34, CI 43645282349, 7 assets incl. the mac-libusb zip); wait for #37's
   report. build.yml "New in 0.8.42" = rc1 text: rewrite before the final (live-later 160, Linux MIDI ports, update
   check, bug report).
4. What's New scrollbar (user 2026-11-15: 0.8.41's notes overlapped the buttons): CODE DONE (InfoScroll in
   infopage.h, test first; newest version that fits nowhere at 9 pt -> whole at 11 pt + scrollbar: wheel, track click,
   thumb drag). User 2026-11-15: "scrollbars work".
5. Windows MIDI ports: CODE WRITTEN (user OK: in-box API only, read-only, installed only where supported; design in
   design-midi-ports.md "Implementation"): helper src/midiports-win (MSVC, never built yet), vtikeyport_win.c +
   midifeed.h + umpconv.h (tests), MSI condition, CI step. CI 43694600597 (1db7d3e, 0.8.42) ALL GREEN, helper
   compiled. win11 VM: build 26300.9974 (26H2), midisrv there (Manual), NO System32 Windows.Devices.Midi2.dll ->
   can't test yet; no API in Windows Update either. User: test with the SDK in the VM: helper switch
   VTI_MIDI2_ALLOW_SDK=1 + Preview 11 DLL by hand: after the fix (5e510e4, one device per port + retries) WinMM
   inputs "Virus TI Synth" / "Virus TI MIDI", no outputs, note/CC/SysEx through both (fed by hand); #1047 hang seen
   (pre-fix Windows). Open: real keys/DIN traffic; Windows' own API (end Nov). README/site written. VM off, clean.
6. Later (user): Mac push of a package + live-later 160 keystamp test there; Windows MIDI ports only after a talk.
7. Waiting on reporters (table above); check issues only together with the user.
