# Moved into TI Revive (2026-10-15)

This code now lives in the public TI Revive repository as `native/` (history preserved, docs/migration.md) and ships
with TI Revive 0.8.0. The private repository virus-ti-native is a read-only backup. The current working log is TI
Revive's `findings/HANDOFF.md`; this file is the history of the native rewrite (stage 3).

# RESUME HERE (night 2026-10-13/14: stability + performance plan worked off without the Virus)

## Windows test build (2026-10-14)

Internal pre-release v0.1.0-test1 (private repo release, zip from scripts/package-windows.sh): plug-in + ASIO
driver, static C++ runtime, install/uninstall/collect-logs scripts (tested in the VM: install, reinstall, uninstall).
The user tests on a real Windows PC: A plug-in, B timing WAV (timing-16ths-120bpm.mid), C ASIO out/in, logs zip.
test1 results (real PC, Ableton): ASIO "no issues at all"; timing 304/304 notes, first at 2000.45 ms, 0 ppm drift,
threshold jitter 0.66 ms sd (upper bound: free-running oscillator phase, cross-correlation unusable), plug-in stats
0 drops / 0 timeline steps / err -256..184; big knobs invisible -> test2: images > 16384 px tiled (Direct2D limit).
test2: knobs fixed, ASIO clean. Open: user felt ~1 s from live MIDI in to sound on the real PC. Not reproduced:
Linux live notes 49 ms (= reported latency); VM (test3, ASIO 256, klog.on) 65-77 ms from sending to the Virus'
sound, felt snappy. Suspect the real PC's audio device setting (Windows driver at 2048 samples); next: real PC with
test3 + klog.on, ASIO device, read Ableton's "Overall Latency". (User: the real PC ran ASIO - not the cause.)
test4: notes during the bank sync play (sync pauses while notes arrive, 1.5 s); only the ~4 s start-up holds
note-ons (the Virus plays them 0.6-1.6 s late while it processes the start-up SysEx, TI Revive E144). A fresh
cache syncs for minutes (>1 min per bank) - before test4 all note-ons were dropped meanwhile. klog.on by install.
test4 on the real PC (user): "no issues". klog: every measurable note K 8072 (= S 5000 + A 3072), sound 64-78 ms
after sending; large/negative K only for fast passages (several notes matched to one onset - measurement artefact).
Likely cause of the earlier ~1 s: the first sessions on that PC ran the initial full sync (empty cache, minutes, the
Virus delays notes while answering dumps - E144) and dropped note-ons meanwhile; now the cache is complete and
the sync pauses for notes. Not proven: the sync result/duration/pauses now also go to diag.log (next build).

test5 (sync: 30 s pause budget, then 300 ms gaps; note on the sync page; sync lines in diag.log) - for the user's
empty-cache test on the real PC.

## First release - state (2026-10-14 evening)

Decisions (user): name "TI Revive Virus Control" (vendor TI Revive), ASIO "TI Revive Virus Audio", own class id,
Snow "not supported yet", no Control Center in release 1. Version 0.8.0 (= TI Revive release).
Done:
- host rates != 44.1 kHz (resampler; user: REAPER at 48 kHz works)
- publication check: upgrade_single rewritten from docs/formats.md (golden test); init sequence is data from the
  user's remotedevice.xml (no port); history scan clean except the developer's user name in early tool scripts
  (replaced by filter-repo at the move); identities clean
- installers: MSI fragment (tested in the VM: install/upgrade/uninstall), Mac pkg + Linux tarball additions, release
  workflow jobs, README - all as the kit in packaging/ti-revive/ (local TI Revive branch native-integration)
- migration rehearsed end to end (docs/migration.md)
- sync: notes play during the bank sync (pause, 30 s budget), start-up hold 4 s
Open:
1. Release candidate 0.8.0-rc2 BUILT (private pre-release v0.8.0-rc2; rc1 + init-state fix E145 + MSI same-version
   upgrades). Linux: user tested the init fix in REAPER (init every time). Open: user tests of rc2 on Windows / Mac
   (the Mac was not reachable today) / Linux install.sh.
   Mac rc2 (2026-10-14): pkg installs, removes the old test bundle; plug-in works. User heard a constant MIDI->sound
   delay in the FIRST instance after the install (as once on Windows test2); gone after re-inserting. Not
   reproduced in 3 later sessions with klog (re-insert, Virus as Ableton input = Virus-clocked host, Ableton restart):
   K 6024, sound 18-40 ms after sending every time. Both delay sessions ran without klog -> klog.on stays on the test
   machines (Mac, Windows test package) to catch it. Open, not reproducible.
   Release workflow rehearsed (private repo derpsn/tirevive-rehearsal, tag v0.8.0): all jobs green after patch 0008.
   rc3 = the release workflow's own output (rehearsal repo, tag v0.8.0 at branch rc3, run 38629368510): kit patches
   0001-0009 + native incl. the stats fix. Linux rc2 tested by the user (install.sh, REAPER 44.1 + 48 kHz). Next:
   user tests rc3 setup.exe on the Windows PC (download from derpsn/tirevive-rehearsal releases).
   rc3 user test on Windows (2026-10-15): the classic plug-in and its ASIO device next to the new ones confused;
   the plug-in under vendor TI Revive instead of Access Music. Decisions (user): 0.8.0 REPLACES the classic plug-in
   (native takes its class id: Windows/Linux wrapper id, macOS bridge id; moduleinfo Compatibility for the others;
   a state saved by the original loads, round trip identical), vendor stays TI Revive (trademark), the Access .msi
   is only unpacked (Windows msiexec /a, Linux msiextract or Wine; Mac unchanged: the AU needs the Access install).
   Patches 0010-0013: MSI without classic parts, new tirevive-access.ps1 (unpack, offer removal of an earlier full
   Access install, dead Access ASIO entry removed), Linux without Wine/yabridge, Mac without bridge VST3, README.
   VM tests passed (upgrade, setup.exe bundle replace, Access removal, extraction). Snow owners: stay on 0.7.4.
   rc4 = rehearsal repo tag v0.8.0 at branch rc4 (run 38683084205).
2. The user's go for the move into TI Revive (docs/migration.md), then tag v0.8.0
Later: Snow, OSC mixed-shape display, AU, Control Center, the user's empty-cache test.

## Path to public (decision 2026-10-14)

This repo stays PRIVATE as the workshop. TI Revive (public) stays the home for users, issues and releases; the native
plug-in keeps using its broker through the submodule. When the native plug-in is feature complete, its code moves
into the public TI Revive repo with full history (git filter-repo into a subfolder + merge), becomes the main
approach there, and the wrapper stays as a fallback until it can be retired.

Feature complete = all of:
- [x] sound, MIDI, editor, browser, bank sync, project state
- [x] timing + reported latency on Linux, macOS (E140, E141) and a real Windows PC (E143: 304/304 notes, no drift;
      the VM's missing notes were the VM)
- [x] stability / M4L LFOs / CPU
- [ ] Virus TI Snow (model, 4 parts, banks, Snow's own device files?) - needs a Snow tester (issue #4 user)
- [x] Virus as sound card on Windows (ASIO): own driver "Virus TI (native)", clean in Ableton on a real PC
      (user, test1). In the VM the output crackles: QEMU corrupts iso OUT (TI Revive E083, E142)
- [ ] Virus Control Center (Windows) - decide: port, launch the original, or drop
- [ ] remaining UI details (UI to-do list)
- [ ] publication check: rewrite parts ported from the decompile (e.g. upgrade_single, init sequence) as own
      implementations from the documented behaviour, neutral product name, scan the history for Access material

State: all committed and pushed. The Virus was switched off by the user during the night, so everything below that
needs the device is on the "tomorrow" list. No CI run triggered (manual only; Windows/macOS compile the new code
and the tests with their next build).

Done tonight (details: "Plan: stability and performance" below, commit messages):
- P1.1 ui_state_ race (own mutex) - P1.2 note-offs never dropped (event buffer 256, insertion sort, no allocation)
- P1.3 sanitizers: TSan found a broker close race (fd closed while the reader was in recv) -> fixed; TSan and
  ASan/UBSan runs of nativehost --stress(-params) --notes --editor clean
- P2.4 worker woken per block (1 ms poll gone) + P2.5 env flags read once: idle 5 % -> 1.4 %, stress 19 % -> 2.9 %
- P2.6 meta engine index - P2.7 partial repaints for value changes (56 -> 4 full repaints in 4 s of automation,
  all pages pixel-identical to full repaints) + displays clipped to their element (long envelopes drew onto the
  panel) - P2.8 one broker write per block
- page switch: static layers of 8 pages cached, bitmaps preloaded on idle: first visit 27-52 ms -> 10-13 ms
- P2b.9 unit tests: vtitests/ctest, 25 tests (USB-MIDI, state, conditions, dumps, automation limit incl. the
  3-LFO case, library, Access-file tests that skip without the files); in CI for all platforms

Tomorrow (needs the Virus / the user):
1. REAPER smoke test on Linux: sound, notes timing (batched broker writes, per-block wake), automation, editor
   (partial repaints, page switches), project save/load, link buttons.
2. nativehost --stress-params --notes with the Virus (4 min): no hang, CPU numbers with the device.
3. Long envelopes: compare the clipped envelope display with the original (tools/uicompare capa.py, Envelope
   Attack/Decay/Release 127).
4. (done 2026-10-14 on macOS: M4L LFO test stable, see below)
5. Then: UI to-do list (needs captures of the original), timing R5 + getLatencySamples, Snow issues #3/#4.

Device results 2026-10-14 (Virus on): nativehost --stress-params --notes --editor 4 min with page switches: no hang,
8 % of one core in total, audio drops only in the first 10 s (start-up). Note timing (nativehost --notes --wav,
onset per second): the night's changes do not change timing - but the session latency varies at start-up in old and
new builds alike: 69/70/71/105/118/203/207/236/237 ms over 12 runs (same timeline anchor err each time, so it is set
after the anchor - the Virus' own anchor on FE/F2?). Within a run it is stable apart from nativehost's own clock drift
(~2 ms/s with steps: nativehost is not paced by an audio device). This is the R5 item: needs the cause before
getLatencySamples can report a fixed latency. Tool: scratchpad onsets.py (phase of note onsets in each second).

R5 SOLVED (2026-10-14, Virus on): the sync ping's marker is a single sample on USB 2 (not ch 0/1) = the Virus'
stamp clock 0; stamps are now converted with it -> K = 7048 for every note in 6/6 sessions, WAV latency identical in
all sessions (was 70..240 ms), latency reported to the host (3328 frames at 512). Plus: All Notes Off + sustain off on
deactivate (the stress test left notes hanging), marker retry after "no marker" (not provoked yet). Next: user test in
REAPER (render a MIDI pattern, notes on the grid with delay compensation), Windows + macOS (Core Audio ring) check.

REAPER test 1 (2026-10-14): no sound / crackling, 3-4 playback starts until notes came, MIDI input less snappy.
diag.log: REAPER processes ahead after a playback start (anticipative FX: up to 457728 host frames per 10 s vs 441000
Virus frames, timeline up to -8548 frames ahead) -> the stamp-aligned read asked for frames not there yet (read margin
-7268) and the old re-anchor (|err| > 8192) sent FE/F2/ping again -> marker search -> random stamps until found.
Fix: with a marker the timeline jumps straight back to real time when |err| > 512 (the stamp clock is tied to the
ring, no FE/F2/ping needed), reads are clamped to available frames, A 2048 -> 1024 and the read margin smaller:
latency 2176 frames (49 ms at 512, was 75). nativehost --ahead 200@12 --ahead 200@16 (REAPER-like): marker kept,
K = 6024 for 24/24 notes, no gaps/jumps. Diagnostics: diag.on now also logs the R5 stats/marker lines to diag.log.
For exact timing in REAPER the Virus track should not use anticipative FX processing (track > Performance options).
(Since round 0.8.33 the jump needs a lasting error - 0.5 s beyond 512, a 2 s mean beyond 512, or beyond 4096 at
once - because an uneven host cadence made it jump back and forth: E337, device/timelinefollow.h.)

REAPER test 2 (2026-10-14, build bf34426): playback starts right away, live input snappy again (user); diag.log: one
marker, no re-ping, read margin >= 129, 22 timeline jumps after playback starts. Timing render (16ths at 120 BPM,
34 s, REAPER render, work/timing/REAPER Media/untitled.wav, scripts/timing_analysis.py): 256/256 onsets, jitter sd
0.01 ms, max |dev| 0.05 ms, drift 0 ppm, first onset 2000.00 ms (the original via TI Revive, E125: sd 0.06 ms, max
1.31 ms, first note late/second missing). R5 done apart from Windows/macOS checks.

Windows (win11 VM, 2026-10-14): the marker comes ~2 s after the ping (start-up dumps still being digested; each ping
resets the stamp clock) -> one ping at a time, 5 s window, newest marker wins (ef/latest commit). Marker found 4/4.
MIDI reaches the Virus later than on Linux (VM USB passthrough?): A=1024 -> K 6068..7437 (late, played on arrival),
A=2048 -> 12/16 exact, A=3072 -> 16/16 exact -> Windows default A=3072 (latency 4224 = 96 ms at 512, the original
reports 4352). To check on a real Windows PC (maybe less is enough) and macOS (Core Audio ring: marker?).

Windows build with A=3072 (CI 38536820337) in the VM: run 1 K = 8072 for 16/16, run 2 6x 8072 + 9x 8116 (one USB
frame = 44 frames slip of the ring against the Virus' clock mid-run, VM iso packet loss; 1 ms, not re-synced).

Ableton 11 in the win11 VM (user, 2026-10-14): plug-in loads, timing recording (16ths, 120 BPM, recorded in real
time and exported, C:\vti-new\timing.wav): the notes that sound are on the grid (sd 0.21 ms, max 1.83 ms, 2 ppm,
first 2004.47 ms) but 85/256 are missing in a strict pattern - 31 notes, 17 sixteenths silent, repeating identically
every 3 bars. User: the clip looks right, suspects the VM passthrough. Open: a random loss would not repeat
identically - recheck with REAPER in the VM / a real Windows PC. diag.log: one marker, no re-ping, margin >= 129;
Ableton's export processed 4x real time (1995 timeline jumps, handled).

macOS (Ableton 12, MacBook Air M3, CI 38545072011, 2026-10-14): marker found through the Core Audio ring (~1 s after
the ping), err 0 (the ring follows the host exactly), read margin 640, no drops. Timing recording (16ths, 120 BPM,
recorded in real time, ~/Desktop/timing.wav): 256/256 onsets, sd 0.02 ms, max 0.28 ms, 0 ppm, first onset 2000.00 ms.
nativehost over SSH gets silent Core Audio input (no microphone permission) - measure on the Mac in a DAW.
-> R5 works on Linux and macOS; Windows only tested in the VM (gap pattern, see above).

M4L LFO test (user, macOS Ableton 12, 2026-10-14): three Max for Live LFOs on Virus parameters, ~30-60 min: "all
stable". diag.log over ~40 min of sessions: 0 drops, process() max 122 us per block (~1 % of the block time), 0 blocks
over time, read margin >= 129, one marker per session. -> the forum question ("crashes with 3 M4L LFOs") is answered
for the native plug-in: stable, automation limited to 250 msg/s (each LFO ~80 updates/s).

Known limit: the automation -> state ring (pu_, 4096) drops values only if the worker stalls for seconds; then the
saved state could keep an older value until the next change.

# Earlier resume notes (2026-10-13 late evening)


State: all work committed and pushed (private + public). Native VST3 runs on Linux (REAPER), Windows (Ableton,
win11 VM) and macOS (Ableton 12, M3). Virus is on the Linux PC (broker socket active); win11 VM may still be running
(Virus detached, Ableton was open there). Diagnostics off (no ~/.local/share/virus-ti-native/diag.on).

Automation CPU (2026-10-13): nativehost --stress-params --notes, worker thread 12.4 % -> ~3 %. Three changes:
state record index in set_part_value ("path#part" -> record), coalesced drain_param_updates (latest value per
parameter, at most every 5 ms) and a per-message field index in RemoteDevice::update_dump (one value: 7.8 -> 0.4 us,
fast path checked against the full path for all 435 single-dump fields). Improvement plan: see "Plan: stability and
performance" below / chat 2026-10-13.

Waiting for the user:
1. (done 2026-10-14: HyperSaw sound + display confirmed by the user)
2. Snow issues #3 (Windows: Access kernel driver) / #4 (macOS: MIDIServer driver) - answers posted, waiting for
   the testers.

Next (own tasks):
- UI to-do list below ("UI to-do"): OSC classic mixed shapes (DLL wave tables), HP filter resonance, envelope 1 px,
  big rings on hover?, page-switch cost (cache rebuild 30-45 ms).
- Timing R5 + reporting the plug-in latency to the host (getLatencySamples).
- Start-up sync has priority again (user, 2026-10-14): the original's start-up page (InitialSyncProgress 0..126,
  progress bar) covers the editor and note-ons are held back while the bank check/sync runs (a few s; first start
  ~8 min). The "pause while playing" variant (8119d36) is gone. User (2026-10-14): everything working (sync screen,
  Ctrl/double-click reset, HyperSaw).
- Optional: skip the start-up bank check if the last one was recent (user asked; recommended to keep as is).
- Tools: nativehost --stress/--block, VTI_NATIVE_STATS, diag.on/diag.log, tools/uicompare (capture the original).

# Status

## 2026-10-12 (night)
- Repo created (private), submodules: external/ti-revive (TI Revive, public), external/vst3sdk (v3.8.1_build_84;
  base, cmake, pluginterfaces, public.sdk, vstgui4).
- Toolchain verified on Linux (Ubuntu 26.04): VST3 SDK + VSTGUI build, empty plug-in bundle builds.
- R0: trace recorder in the TI Revive broker (VTI_TRACE, scripts/vtitrace.py); first trace of the original (25 s,
  cached memory): F5 time stamps on cable 5, MIDI clock + CC + notes + SysEx on cable 2, control polls wIndex 1.
- R1 started: src/access (Access files from Virus Control.dll PE resources / Mac bundle, 445 files found in the Wine
  prefix), src/model (remotedevice.xml: 587 parameters, 128 value lists, 123 conditions, 12 message templates),
  tools/vtimodel (files/summary/params/encode). Encoding verified against the original for Global Master/Mode.
  docs/formats.md describes the XML semantics.
- automation.xml = the original's VST3 ParamIDs (2671 of 2672 + 32 aftertouch/pitchbend ids = the 2703 mapped);
  scripted automation of the original (vtihost VTI_PARAMS) confirms value = round(norm*127) and our encoding.
- editor.xml structure documented (36 control classes, 404 bitmaps, 20 top-level containers).
- R2 started: src/device/broker (C++ client of the TI Revive broker, POSIX), usbmidi packer, tools/virusctl
  (state / set <path> <value> / note).

## 2026-10-13 (night)
- Full-sync trace of the original (empty cache, 4 RAM banks, ~9 min): start-up sequence, bank info 39/14, single dump
  requests 30/10 at ~1 per second; docs/protocol.md, tools/msgsummary.py.
- virusctl state / set / note work against the Virus through the TI Revive broker (systemd socket).
- Dumps: RemoteDevice::decode_dump / update_dump / dump_size, vtimodel message|dump; checksum add7 verified, decode +
  re-encode byte-identical for all 528 recorded single dumps. Open: bank-info fingerprint function, multi dump checksum.
- Bank info (39/14) decoded from the decompile: 14-bit sum of dump bytes 13..521 minus 265 (512/512);
  replies come on EP 0x85 outside a plug-in session; virusctl sysex sends requests and saves the replies.
- Ghidra headless decompile of Virus Control.dll -> work/decomp/virus-control.c (tools/DecompileAll.java), for the
  fingerprint quirk and the remaining unknowns (39/14 compare, 74 01 7E poll, multi dump checksum).
- R2 sync engine: src/device/memsync (bank info -> fetch only programs whose fingerprint differs; strictly one request
  at a time - overlapping requests make the Virus truncate replies; ~1 s per request is the device's pace), cache in
  the original's file format; UsbMidiParser in usbmidi.h. tools/virussync: import of an Access cache + check of 4 banks
  = 3.4 s, 3 corrupted programs refetched and the bank restored byte-identical to the original's cache.
- Start-up multi dump = fixed table in the DLL (stale checksum, Virus accepts it); decompile pointers in protocol.md.

- Plug-in state = the start-up message list in the cache container (formats.md); vtihost VTI_SAVESTATE/LOADSTATE
  (TI Revive) captures the original's state. Original class ids + parameter list reproduced (2703 params, 2671/2671
  titles identical; automation.xml typo handled like the original).
- R3 started: plugin/ = "Virus TI (native)" (SingleComponentEffect, original buses, 2703 params, IMidiMapping, state in
  the original's format, parameter changes -> encoded messages -> broker, part dumps kept current). VST3 validator: 47
  passed / 0 failed. tools/nativehost: the original's state loads and getState returns it byte-identically; a
  Filter 1 Cutoff change updates exactly byte 49 + both checksums of the part 1 dump.

- R3: the native plug-in PLAYS the Virus on Linux: init messages + state upload, notes with F5 stamps, audio from
  the broker's shm ring (Out 1 = USB 1, Out 2 = USB 2, drift by frame drop/repeat). nativehost --notes --wav: init
  patch audible every second (peak ~0.09), ~170 ms latency for now.

- R3: a new instance builds the original's default project from the user's plug-in binary (init patch upgraded by
  a port of the original's format upgrader, MultiCtrl multi); non-part records byte-identical to the original's
  default state, parts differ in 17 legacy bytes (same sound). Plays without any loaded state (nativehost).
- Decompile findings: stamp rule + audio sync markers (protocol.md), patch format versions 1..12 (upgrade_single).

- R4 started: src/editor (EditorLayout from editor.xml, Conditions over remotedevice + editordevice incl. "outside"
  tests), tools/vtirender (cairo offscreen render with knob filmstrips / radio states / conditions). Easy page vs a
  screenshot of the original (Xvfb + PIL): 92-93 % identical pixels; missing: text (bitmap fonts FNT_*.png, patch
  names, value displays), filter curve displays (LP4Response etc.), part strip details; the big Filter 1 cutoff ring
  (MaskedKnob KNB_Cutoff_Rot_Uni) is not drawn by the original at rest - open why (other MaskedKnobs are).
- Device -> host knob feedback, realtime-safe event ring (R3).

- R4: the plug-in has its EDITOR on Linux (VSTGUI): the original layout from the user's files via the shared renderer,
  page switching by the header tabs, knob drags (vertical, full range over 200 px) -> performEdit for automatable
  parameters / direct SysEx otherwise, part dumps updated, repaint on device changes. tools/nativehost --editor
  (X11 window + IRunLoop + XEmbed mapping, --click/--drag via XTest, --shot): click OSC -> OSC page; drag Oscillator
  volume +60 px -> 64 -> 102, "+38" shown, part dump byte 45 + checksums updated.

- Editor fidelity vs screenshots of the original (vtihost under Xvfb, tabs clicked with tools/xclick.c): identical
  pixels per page: Easy 97.0, Browser 89.3, OSC 90.3, Filter 93.7, LFO 97.5, Matrix 86.6, Arp 97.0, FX 93.3, Common
  95.9, FX2 (page 11) 93.6 % (mean 93.4 %); with nine-part tiled bitmaps (Bitmaps Tile/TileInsetRect, menu text inside the insets):
  Easy 97.0, Browser 91.7, OSC 94.7, Filter 93.7, LFO 98.3, Matrix 92.9, Arp 97.2, FX 94.4, Common 98.9, FX2 94.2 (mean 95.3 %). Missing: custom displays (envelopes 0x180039df0.., filter curves,
  osc shapes, patch browser), some menus/popups state. Menus pop up with the valuelist and switch layouts.

- R4: patch browser works in the plug-in: bank tables (97.3 % identical to the original), click selects (inverse
  font), double click loads the patch into the current part (dump to the Virus edit buffer + state + parameters).
  Part selection, menus, page tabs, knob/slider drags, buttons work; the editor follows device/automation changes.

- Plug-in memory sync: after activation a background thread checks RAM A-D (bank info) through the plug-in's own
  broker connection (MemorySync embedded: send function + feed of replies) and fetches only changed programs into
  ~/.local/share/virus-ti-native/cache (initially imported from the Access cache). Verified: corrupted RAM C program
  -> 1 fetched -> file identical to the original's cache again.

- Timing: the plug-in keeps a Virus timeline (newest ring frame - S), sends stamped MIDI clock at the host tempo, a
  stamped FE + song position at each anchor and stamps every event on the timeline; the Virus then honours the stamps
  with a constant per-session offset (measured with broker trace + raw recording, scratch measure.py). Open: runtime
  offset, clean start, host latency.

- Timing start (2026-10-13 05:20): the timeline anchors only after the state upload has drained (DeviceOut::busy) and
  0.5 s of steady audio, then follows drift by 1 frame/block -> sync reports 13 -> 1. Without stale start-up messages
  the stamps count as "late" (immediate, ~340 frames) - with a stale start they were honoured (K 3982, ~2000 frames
  after sending). Hypothesis: the Virus anchors on the song position's ARRIVAL; the F2/FE are now stamped M frames
  early (VTI_NATIVE_M, default 1024) so later events lie M frames in its future. UNTESTED: the Virus dropped off USB
  at 05:22 (port 1-8: resets, "device not accepting address, error -71") right after a broker give-back -> needs a
  power cycle by the user. Watch whether this repeats (broker reset at exit, or our stream hanging the firmware).

- Editor displays: ADSTR envelopes, filter responses (LP/HP/BP/BS), classic oscillator shape (all measured on the
  original), big rotating rings only while held, inactive controls use their DisabledBack strip. Pixel match per page:
  Easy 98.3, Browser 97.3, OSC 97.4, Filter 97.4, LFO 99.0, Matrix 96.2, Arp 97.4, FX 95.9, Common 98.9, FX2 94.1
  (mean 97.2 %). Open: FX2 menus that the original shows active although our state says inactive (Reverb Send 0 -
  the original probably uses the Virus' upgraded edit buffer values), Hypersaw/Wavetable displays, step sequencer.

- Editor: patch rename (double click on a part's name, VSTGUI text edit -> dump + Virus), LCD shows the edited
  parameter (display name, value text, part). process() is now free of blocking locks (automation -> lock-free ring ->
  device worker updates the part dumps). Virus still off USB (E136 in TI Revive) - device tests wait for the user.

- R6 start: device layer compiles for Windows (mingw, scripts/win-cross-tools.sh): broker client over the named
  pipe (overlapped I/O, reader + writers concurrently), audio ring via the named mapping, named event for the
  Windows broker's audio request. vtimodel.exe under Wine finds the Access installation (C:\Program Files\...).
  Untested against the Windows broker. The plug-in for Windows needs MSVC (CI).

- MILESTONE 2026-10-13: first DAW test by the user - REAPER 7.82 on Linux (pw-jack), "Virus TI (native)" from
  ~/.vst3: "everything is working", latency sounds good, only minor UI issues (to be listed by the user). The Virus
  was power cycled after the E136 hang and works again.

- User feedback round (2026-10-13): first start without a cache = full RAM sync like the original (512 patches ~8 min,
  identical to the original's cache; Virus display message + LCD progress); ROM A-Z from firmware.bin by model;
  browser / LOAD / SAVE / bank menu rebuilt after screenshots of the original (work/orig-ui: device > RAM/ROM banks,
  Personal Libraries, "000 Name", selection frame, category menu with check); big rings visible again; editor lag:
  partial repaints (~8 ms per drag step). Personal Libraries: the .mid patch files in Documents/Access Music/Virus TI/Patches (src/access/library; Linux
  ~/Documents/..., else the Wine prefix's) in the bank menu, LOAD menu and browser. Virus C (267-byte) dumps are upgraded
  with the default page B from the DLL; SAVE writes into library files (overwrite / append). Open: search display in the original
  (typing into the original under Wine did not reach the field), whether the original loads on double click.

- CI (.github/workflows/build.yml): Windows (MSVC, VirusTINative.vst3 artifact) + Linux builds on every push - both
  green (run 38367374934). The Windows build loads under Wine (vtihost: initialize, 3 out / 2 in buses, Access files
  found, editor attached; drawing not visible under Wine = Direct2D); it cannot reach the Linux broker from Wine (pipe;
  the Wine route is TCP + token like TI Revive's vticore_sock) - real test: the win11 VM with Virus passthrough.
- Questions for the user (could not be captured from the original under Wine): how the original's search shows
  results; whether a double click in the browser loads the patch (ours: double click loads, click selects).

## 2026-10-13 (midday) - user test feedback, browser fixes

User test in REAPER: CATEGORY text garbled, search field floating/not working, Piano filter dimmed everything,
part strip bar always full. Fixed (2ca76d0): built-in CATEGORY conditions + "No Label", filter index -> stored
category value, live search field at the layout position, VU = MIDI activity meter (decompile: ShowMIDIActivityMeters,
per-channel max velocity, -1 per idle tick). Verified in nativehost/Xvfb (search "pi", meters part 1 only). Category
popup itself not testable headless - waiting for the user's retest.

## 2026-10-13 (afternoon) - browser like the original, performance

- Search/category: one "Search Results" bank in the UPPER browser (Browser 0), sorted by name, pages of 128, the
  original's titles; sources RAM + ROM + all Personal Libraries incl. subfolders, de-duplicated by page A[2:]
  (reproduces the original's Piano list exactly: 70 entries, same 9 doubles). Libraries recursive, folders as
  submenus. Category menu tick fixed (generic menu uses the menu value). LCD shows the category + [original value].
- MIDI activity meters repainted 16x per tick -> REAPER sluggish while the editor was open; now one merged repaint
  of the changed meters (31bf493). Waiting for the user's REAPER retest.
- Open: generic Linux menus feel slower/bigger than the original's native menus (VSTGUI theme not configurable
  on X11 without an SDK patch - own menu view possible); first category search: categories now decoded in the background after the sync (~0.5-0.7 s).

## 2026-10-13 (afternoon) - Windows port started

- win11 VM: TI Revive 0.7.4 installed (MSI, silent), CI build of the plug-in + tools (nativehost.exe now in CI).
- Fixed: the plug-in starts vtibroker.exe on demand (was: connect only); bank sync cancellable on terminate.
- Verified with the Virus passed through: Access files + libraries found, TI2 detected, audio streaming with notes
  (peak ~0.07 like Linux), first sync running. Next: user test in REAPER/Ableton in the VM (editor on Windows
  untested so far - nativehost has no editor on Windows).
- Linux broker stopped for the VM test: afterwards `scripts/vm.sh detach`, `systemctl --user start tirevive-broker.socket`.

- Windows user test (Ableton 11.3 in the VM): plug-in found after a rescan (first scan had crashed with an older
  build; WER LocalDumps for "Ableton Plugin Scanner.exe" -> C:\dumps enabled, CI now uploads PDB + map as
  symbols-windows), sound OK, UI draws correctly, menus/browser OK. Fixed after the test: sliders track the mouse,
  inactive controls locked. Open: retest of those two, timing check.

## 2026-10-13 (evening) - macOS port

- CI macOS job: universal VST3 (arm64 + x86_64) + tools. Core Audio input backend (Virus = class-compliant
  "Virus TI", 6 in @ 44.1 kHz), broker started on demand (posix_spawn like libVirusUSB), double-connect bug fixed.
- MacBook (macOS 26.6, arm64): plug-in installed in ~/Library/Audio/Plug-Ins/VST3; Access files, firmware ROM
  (26 banks) and libraries found; notes reach the Virus, Core Audio frames advance but are all zero when run over
  SSH (no microphone permission). User test in Ableton 12 on the Mac: everything works, UI responsive.
- Afterwards: the Virus goes back to Linux (open Linux items need the original next to ours: Hypersaw display,
  ArpEdit, FX2 menus, timing R5).

## 2026-10-13 (night) - remaining displays

- HyperSaw display (3 lines, spacing from DetuneSpread) and the arpeggiator user pattern (ArpEdit: bars, span over
  off steps, mouse editing of velocity/length) - both pixel-identical to the original (captures via vtihost +
  VTI_LOADSTATE with states built by `vtimodel setdump`; scripts in the session scratchpad, procedure: base state
  from VTI_SAVESTATE, change part 0's dump, load, click the tab with xclick, ImageGrab).
- Stored values outside a parameter's range are clamped on display (Vocoder/Bands 127 -> 32 like the original).
- Page diff against the original (all tabs, same state): LFO, MATRIX, COMMON, FX1 identical; ARP/FX2 < 0.1 %
  (inactive menu text brightness differs in some places - the original is inconsistent there); EASY/OSC/FILTER
  ~2-3 % = mostly the big knob rings (shown permanently since the user's feedback; the original under Wine shows
  them only on hover), plus the curves below.
- OSC classic display: default saw pixel-identical (1 px per column at floor(y), 2:1 steps, extra column);
  mixed shapes (sine..saw, pulse) still differ: the original interpolates 64-point wave tables from the DLL
  (FUN_180048470, DAT_1801ac340) - to do: read those tables at runtime.
- FX2 "Reverb/Type" active with Reverb/Send 0 in the original while Reverb/Mode (same condition) is greyed: an
  inconsistency of the original (no special case in the decompile); ours follows the XML condition for both.

## UI to-do (paused 2026-10-13 evening, continue here)

Procedure: capture the original with `vtihost` + VTI_LOADSTATE (state built from VTI_SAVESTATE base + `vtimodel
setdump` changes on part 0's dump), click the tab with work/bin/xclick (editor at screen offset 4,30), ImageGrab
:77; render ours with nativehost --state <component state> in `bwrap --tmpfs /tmp` (no Virus access); diff pixels
(threshold 24) per page. Scripts were in the session scratchpad (capa.py / allpages.py / fcmp.sh) - re-create.
Current page diffs (base state): LFO, MATRIX, COMMON, FX1 0 %; ARP 0.04 %; FX2 0.08 %; EASY 1.65 %; OSC 2.29 %;
FILTER 2.70 % (mostly the big knob rings, which we show permanently).
1. OSC classic display: mixed shapes (sine..saw, pulse) - the original interpolates 64-point wave tables from the
   DLL (FUN_180048470, DAT_1801ac340, second table at object+0x1c0): read the tables at runtime.
2. Filter displays: highpass resonance height (original ~2 px higher at resonance 100, non-linear), HP within 2 px.
3. Envelope displays: handles up to 1 px off for the base patch (measured geometry was from the init patch).
4. Inactive menu text brightness differs in some places (ARP/FX2 vs MATRIX - the original is inconsistent).
5. Editor speed in REAPER (field diagnostics, diag.on): a full repaint costs ~77 ms in REAPER (20 ms in nativehost
   on Xvfb), a partial ~5 ms; knob drags used up to 25 % of REAPER's main thread. LCD-only changes now repaint
   only the LCD. Done (2026-10-13): static layer cache + X pixmap copies of all bitmaps on Linux -> full repaint
   1-7 ms, knob step < 1 ms on XWayland (was ~70 / ~5 ms); page switch 30-45 ms (cache rebuild, CPU render).
6. Big knob rings: shown permanently (user wanted them); the original under Wine shows them on hover only.
7. Meta engine: tested reverb switch; LFO clock menus and filter/vocoder links untested with the Virus.

## 2026-10-13 (night) - stress tests

- Open/close 20x: every second re-open had no audio (broker refuses the start right after a session) -> retry
  every 500 ms (eea2b7e). Endurance 10 min with notes: 0 drops, distance steady ~1000-1150 frames (no drift).
- MIDI load (notes on 16 channels + 32 automated parameters per block = ~5500 msgs/s): the send queue starved ->
  8 real-time messages then one queued (dedd4e0); bank sync completes under load.
- Virus hang: the Virus' audio goes digitally silent (MIDI still answers, needs a power cycle) under a parameter
  flood: unthrottled automation 32 lanes/block (~2750/s) hangs it within 4 min; notes alone at ~2750/s are fine;
  automation throttled to 1000/s alone fine, but together with the notes it still hung (full-scale burst first).
  Automation is now coalesced per parameter and sent within a budget (AUTOMATION_RATE, now 250/s) - retest of the
  full load passed at 250/s (4 min notes on 16 channels + 32 lanes, Virus fine afterwards).
- Block sizes: 64 had a ring re-anchor (click) every ~30 s -> audio distance at least 768 frames (0 in 60 s);
  128..2048 fine. Open (R5): the plug-in reports no latency to the host yet (getLatencySamples) - needs the real
  note-to-audio latency (variable K), else delay compensation would be wrong.
- Tooling: nativehost --stress / --stress-notes / --stress-params / --block N; VTI_NATIVE_STATS.
- User tests in REAPER: (a) pages showed part 1's values for every part (containers' Part="0" was inherited) ->
  fixed (bbfb44b); (b) USB unplug during playback not detected, no sound after reconnect -> the worker follows the
  broker's device state: "Virus disconnected" on the LCD, on return start-up messages + state again, audio retry,
  timeline re-anchor (a64743b) - user: works. (c) selected part jumped back to 1 after a patch load: non-dump values
  (selected part, part volume/pan, globals) are now kept in the state (9adf045) - user: works. (d) linked knob
  (LFO link) followed only on release -> repaint of changed params during drags (8bb3a5f). (e) LFO link lost after
  project reload -> editor-only link buttons saved as F0 7D 'VTN' records (ea08936). (f) notes hung while the
  start-up bank sync ran -> sync pauses while the host plays (8119d36) - user: works. (g) editor drawing slow
  (REAPER lag/dropouts) -> static layer cache + X pixmaps on Linux (7c045ea, c5c8645) - user: "snappy". (h) HyperSaw
  display ignored Density -> lines = saws, re-captured and pixel-identical (5617ddf); waiting: does the Density knob
  audibly change the sound (CC 17)? Note: after power cycles the original under Wine resyncs its memory (~10 min,
  let vtihost exit normally so its cache is written) before captures work again.

## Next
- R5 timing: (1) anchor the timeline once the audio stream runs steadily (no re-anchors -> no Sync Information
  Events), (2) the runtime offset between stamps and audio: the original anchors on out-of-range audio samples
  (0x1800a8900) - no such samples on any of the 6 ring channels in our session; check whether the original writes the
  host->Virus audio stream (broker OUT ring) and whether the markers appear only then; else calibrate with a probe note
  on a part routed to an unused USB output, (3) read the audio at timeline + offset and report the latency to the host.
- R4: custom displays (KEnvelope 0x180039df0 + stage setup in the control factory, filter responses = KXYControl?,
  KOscClassicShape/KOscHypersawShape, KStepSequencer), patch name editing, info panel / LCD texts, remaining 2-6 %
  pixel differences (scripted page captures: tools/xclick.c + vtihost under Xvfb, see the orig-page*.png procedure).
- REAPER test with the user (Linux VST3 "Virus TI (native)"; drop-in build for projects saved with the original).
- R6: Windows (broker pipe client, WinUSB shm ring) and macOS (Core Audio path) ports of src/device.

## Plan: stability and performance (analysis 2026-10-13)

Done: automation path CPU (worker 12.4 % -> ~3 % under --stress-params --notes), commit 19ffe1c.

P1 - stability
1. `ui_state_` (std::map) is read without a lock in `ui_value()` (worker: meta conditions; UI thread) while other
   threads insert -> possible crash with modulated meta-linked parameters. Guard with `values_mu_` or keep it to one
   thread.
2. `process()`: `evs[128]` is filled with clock, CCs and up to 96 automation messages before the notes -> in a dense
   block note-offs can be dropped (stuck notes). Reserve slots for notes, never drop note-offs.
3. Sanitizer runs (TSan/ASan nativehost --stress-params --notes) + a 1 h Ableton test with 3+ M4L LFOs on each platform.

P2 - CPU
4. Worker polls every 1 ms -> wake on condition/event (idle wakeups 1000/s -> ~0).
5. `getenv` in hot paths (worker loop, `process()` lines ~524/562) -> read once at start.
6. Meta engine: `meta_source` walks all editor-device parameters per change -> precomputed path -> metas index.
7. Automation changes set `ui_dirty_` -> full repaint at 20 Hz while LFOs run; repaint only the affected controls.
8. Broker: one write per message -> batch per worker cycle.

P2b - tests
9. Unit tests for everything that works without the Virus (ctest, run in CI on all platforms): RemoteDevice
   encode/decode round trips, update_dump fast path = full path, dump upgrader, valuelists, conditions, meta engine,
   state container save/load, automation throttle/coalescing, event ordering in process(), SMF libraries, ROM banks
   (skipped when no Access files are present).

Progress (night 2026-10-14): all of P1 (except the Ableton M4L test) and P2 done, unit tests in place - see
RESUME HERE. Earlier progress note: P1.1, P1.2 done (b9df2d6); P1.3 sanitizers done on Linux (TSan found a broker close race, fixed 040ea29;
ASan/UBSan clean) - the Ableton M4L test is still open. P2.4 + P2.5 done: idle 5 % -> 1.4 % CPU, worker wake-ups
1000/s -> ~220/s; --stress-params --notes 19 % -> 2.9 %.

P3 - open features: UI to-do above, timing R5 + getLatencySamples, Snow issues #3/#4.
