# HANDOFF

## START HERE (end of day 2026-10-27): round 0.8.35, branch `round-0.8.35` (pushed, c402c5a+), v0.8.35-rc7 published

**State.** All code of the round is committed and pushed; nothing uncommitted except the vst3sdk submodule marker.
Today (details below, E372-E376): segment drag in the filter/envelope displays (E372); REAPER sync report at every
Play fixed (heldnotes.h, E373; automated REAPER A/B runs); Control Center nerd mode Options menu + `tirevive-cc option`
(outdelay-changes, timing-classic; default = like the original); REAPER part volume 127 at Stop = REAPER's 'Reset MIDI
CC/Pitch on stop' (note in README/site, E373); greyed controls match the original (E375, no change); Snow plug-in as
Audio Unit (E374, CI: builds, auval passes for both units); voice count idea dropped (E376: the Virus never reports
it). REAPER updated to 7.83 (backup ~/.local/opt/dl/reaper/backup-7.82-20261027). Dev plug-in ba517ed5.. in ~/.vst3
(= the REAPER fix build; rc6-era plug-in in ~/.vst3-backup/before-segments-1717). win10ref off, Virus on Linux,
broker socket active. Current Control Center build: build-native/ccapp/TIReviveControlCenter.

**2026-10-28 (user: 'final release today', gets the Mac):** CI TEST BUILD 0.8.35 run 40745427793 (2963d42) started.
risk.py --round 0.8.35: class C on linux/windows/mac (hil cold/reopen/export, nothing by hand from core). Round
checklist scripts/checklists/round-0.8.35.json PUBLISHED https://claude.ai/artifact/ULCMWzVCGh111Sgg5qeTGh (db):
L1 REAPER Play/Stop, L2 drag and drop; W1 hil-win, W2 sound/timing/mod wheel Live 11, W3 Init/drop/browser, W4 graph+ARP;
M1 hil-mac, M2 Snow AU in Logic, M3 exports 44.1/128 + 48/128, M4 mod wheel Live 12. NEXT: CI green -> install
Linux + hil.sh, win11 VM, Mac push; then site-check, VERSION 0.8.35, release notes tested-on line, merge, tag.
CI: Linux/Windows/MSI/hil kits GREEN; mac job NOT RUN 3x (18:28, 18:44, 19:01 UTC): no hosted macos-15 runner
within 15 min, GitHub annotation 'capacity constraints ... macOS arm64 runners', no incident on githubstatus.
Artifacts ~/Downloads/TI-Revive-0.8.35-test (old run 40568526535 moved to ...-test-40568526535). Linux = CI build
(install.sh; plug-in 4d0d0635.. = the tarball's). hil.sh ALL PASS (work/hil/20261028-194527; export 1.00x, max 0.78 ms).
L1 + L2 PASS (user, REAPER, CI build). diag.log of that session: 9 sync reports in ~93 s with ~16 Play/Stop cycles
while the bank sync ran (39 s); 7 of 9 'since Play' 0 = while stopped (the original's output-delay set at Stop, E373
default, 3-4/10 yesterday); none per Play like before the fix. No sync page (user).
WINDOWS (win11 VM, Live 11): setup.exe /quiet exit 0, TI Revive 0.8.35, plug-in 34cecfeb.. = the MSI's. W1-W4 PASS
(user). hil-win ALL PASS (work/hil/win-0.8.35/results-20261028-204116; export 1.00x, max 0.99 ms, drift +22..+70 ppm).
NOTE: the Live session (W2-W4, 162 s) had 32 sync reports, rising 3-5 per 10 s while the user dragged graphs/ARP
(W4), drops 0, no sync page; the 0.8.34 Live session in the same VM had ~5 (hover only). Cause not checked (VM USB?
edit bursts?); E315: the sync page came at 70-101. Check on the Mac: count reports while dragging graphs.
klog.on is still set in the VM (R5 diagnostic, harmless). Virus back on Linux, VM off. mac job attempt 4 (19:49 UTC).
#20 (new, 2026-10-28, test report): TI Polar, Mac mini M4, macOS 26, Logic 12.2, USB hub, 'no issues so far', no
version/stages. Reply posted (user OK, issuecomment-6479629791): asks version + AU/project recall.
**2026-10-29:** mac job GREEN on attempt 8 (user restarted it; runner from 05:31 UTC) -> run 40745427793 all green.
Mac PUSHED: ~/Downloads/TI-Revive-0.8.35-mac (pkg b6ce1ddf.. = CI, INSTALL-COMMAND.txt, Install test build.command,
hil/); the 2026-10-26 folder moved to ~/Downloads/TI-Revive-0.8.35-mac-20261026. NEXT: user installs, M1-M4, plus
a graph drag in the editor so the sync report count can be compared with the VM.
**Mac sync errors (2026-10-29, E377):** Logic (Snow AU, 48k) clean; then Live 12: endless sync reports (>3000), the
Virus delivering the OTHER rate than Core Audio's nominal (48 nominal / 44.1 delivered; after a Live restart at 44.1:
44.1 / 48; after a power cycle again 44.1 / 48); a rate change in Live (48, back to 44.1) fixed it. Cause: stale Core
Audio nominal rate, open() sets only on difference. User chose 'A, fix it now': ratemeter.h (TDD, 5 tests, 445 total,
ctest 5/5 Linux) + AudioIn::check_rate (vti_relock away/back on the Core Audio device) + worker log lines; release
note bullet. Mac code NOT compiled locally (no Mac toolchain here) -> CI build, then Mac retest: power cycle, Live 44.1,
expect the 'other rate ... then back' line and clean stats. Old bug (since the rate following), not a regression.
CI TEST BUILD with the fix: run 40827916741 (f3f7149) ALL GREEN (first Mac compile of check_rate). Artifacts
~/Downloads/TI-Revive-0.8.35-test-f3f7149. Mac PUSHED (pkg 722fc4af..); previous folder ->
~/Downloads/TI-Revive-0.8.35-mac-40745427793. Linux/win11 still on the 40745427793 build (Mac-only change).
MAC CHECKLIST (user): M1-M4 PASS, all on the FIRST build (marks 06:43-06:57, fixed build installed 07:20):
hil-mac ALL PASS (work/hil/mac-0.8.35/results-20261029-063830; max 0.78 ms, export 1.00x); M3 only ONE export
(Timing 16ths.wav 06:48, 48 kHz): 256/256, jitter 0.00 ms, drift 0 ppm (work/timing/mac-0.8.35); 44.1/128 not exported.
Fixed build: one Live session 07:20:49 at 44.1: 441216 / 441344, 0 anchors, 1 sync report - rates matched at the
start, so the relock path did NOT run (not proven on hardware yet). All 10 checklist items pass.
Fixed build again (user): hil-mac ALL PASS (work/hil/mac-0.8.35/results-20261029-072801); power cycle + Live 44.1
07:34: matched from the start, 0 relock lines -> the stuck state did not come back; relock unproven on hardware
(E377 updated). NEXT: site-check, VERSION 0.8.35, tested-on line, merge + tag after the user's OK.

**NEXT (user: no test session on 2026-10-27):**
1. Full CI test build of the branch (build.yml -f version=0.8.35; the last full run 40568526535 failed only in mac,
   fixed since, dev-mac 40569442737 green) -> install on Linux/win11/Mac (test machines latest).
2. Mac session (Virus to the Mac): Snow AU in Logic (both units listed, the Snow class + 4-part editor, sound; autest
   on the Snow component), exports 44.1/128 + 48/128 -> timing_analysis.py (E316/E317 residual note, lead 3072 now).
3. Windows: nothing of this round tested on Windows yet (drop, Init, timing, Snow class, ARP/graph, held notes).
4. risk.py --round 0.8.35, hil.sh, checklist artifact; tirevive-site-check; release 0.8.35; close #19 at the release;
   tell #14 (Snow AU, rc/release) and #16 with the user.

**Ideas backlog (user, 2026-10-27: 'keep it in our ideas backlog'; nothing started):**
- Gap to the original: REMOTE page templates (define what the Virus' knobs send as a controller; writes RAM -
  analysis + hardware test first). Help '?' menu (original: Setup Guide, docs, videos; ours could link README, issues,
  'Save logs').
- Own additions: Undo/Redo for editor edits; Compare (edit vs stored patch, the stored values exist for the LCD
  hover); Panic button (all notes/sustain off, held-notes logic exists); automatic backup of a RAM slot before the
  browser overwrites it; patch randomizer with amount/sections (bounded send rate, rule 2; asked in #19).
- Suggested order (mine): Panic + Help menu (quick), then Undo/Redo; REMOTE templates as its own round.
- Dropped: voice count / limit warning (E376).

## (previous START HERE) (2026-10-25, afternoon): round 0.8.35 on branch `round-0.8.35` (from main 6f8b79d)
**2026-10-27 evening (user: '1. Snow plug-in as AU, 2. the cosmetic fix, 3. find out what causes the REAPER
behaviour, 4. the Mac export; before you start make RC7'):** v0.8.35-rc7 tagged on 211ddb5 + pushed, release CI run
40564530449 (watching). (1) Snow AU BUILT on Linux side, NOT COMMITTED, Mac build untested: factoryorder.h +
test_factoryorder.cpp (440 tests), entry.cpp hand-written factory (Snow first when plugin.vst3/Contents/Resources/
snow-first exists), CMake vti_add_au() -> VirusTINative_AU + VirusTINative_AU_Snow ('TI Revive Virus Control
(Snow).component', subtype VTIs, own Cocoa view class), Info.plist.in placeholders, dev-mac.yml (auval both),
mac-build-release.sh, postinstall, uninstaller. The Mac has no cmake/checkout -> one CI test build after 2-4, then
Mac tests together (autest on the Snow component, Logic). Docs (README Snow bullet, architecture.md, notes) pending.
(3) REAPER volume DONE (E373 updated): 'Reset MIDI CC/Pitch to neutral on playback stop' (neutral CC7 = 127);
automated run with ccresettab=cc7=-1 in reaper.ini: 0/10 CC7 at Stop (10/10 without); reaper.ini restored from
scratchpad auto/reaper.ini.bak. README 'Good to know' + site details note (same text), NOT COMMITTED yet.
(2) Cosmetic: the old note (HANDOFF ~l.2036) says Delay Clock and Reverb Mode look ACTIVE in the original with Init,
inactive in ours -> not an opacity question. Delay/Clock condition = 'Parameter Visbility/Pages/Classic Delay Clock'
(Mode 1..5, Type 0, Send >= 1), Reverb/Mode = 'Parameter Active Condition/Reverb/Send is active'; both Menus (Reverb
Type too). REA subagent running: what the original's vtable+0xC0 'is active' checks, groups, menus.
(2) Cosmetic DONE = E375: nothing to fix - the original (win10ref, user screenshot, Init, FX 2) greys the same
controls as our renderer, grey levels within measuring error; the old 0.8.21 note was wrong. rc7 PUBLISHED (run
40564530449 all green, pre-release, 6 assets). win10ref running with the Virus attached (detach after the user quits
Live). Snow AU COMMITTED 0549a4d (E374; README/notes/architecture, 440 tests), pushed; win10ref off, Virus back on Linux,
broker socket active. CI TEST BUILD 0.8.35 run 40568526535 (0549a4d) started = first Mac compile of the Snow AU.
NEXT (Mac session, Virus to the Mac): install the CI pkg (INSTALL-COMMAND.txt + 'Install test build.command'),
autest on 'TI Revive Virus Control (Snow).component' (Snow class? editor PNG), Logic: both units listed, Snow
editor, sound; exports 44.1/128 + 48/128 -> timing_analysis.py. CI test build 40568526535: Linux/Windows/MSI green, MAC FAILED (Snow AU post-build script: '(' in the path ->
no shell quoting) -> fixed 36354e6 (VERBATIM); dev-mac run 40569442737 GREEN: both AUs registered, auval SUCCEEDED for VTIr and VTIs, marker present, package built (E374 updated).
After it is green: a full build.yml test build for the Mac session (user: NO test session today, 2026-10-27).
NEW user question (2026-10-27 evening): show how many voices play and warn at the limit. Spec (virus.info/virusti/
specs): 20-90 voices depending on the patch (Snow 10-50), dynamic allocation over two DSPs, no voice/load display
mentioned; remotedevice.xml has no voice/load report (only Multi Part Bits/Voice Steal Priority); E341 said 'the
Virus does not report its voice use' without a check. Offered: static firmware check (DSP voice counter + any read
path) or a plain "notes held" counter without a limit warning. User chose the static firmware check: DONE = E376 - the DSP counts voices per part but nothing sends it out (no parameter, no reply, no panel display; dropped notes leave no trace); only new firmware / the private F4 path would reach it (rule 2: no). DECIDED (user, 2026-10-27): voice count idea DROPPED (no 'notes held' counter either). (4) Mac export: fixed in 0.8.31 (E316/E317); left: one 2.5 ms note at 44.1/128, jitter 0.26-0.40 ms. 0.8.35's lead
3072 likely covers it -> measure on the Mac (exports 44.1/128 + 48/128, timing_analysis.py) together with the Snow AU
test on one CI test build.
**2026-10-27 (user: 'do the small fixes first and also the fade one. after the we talk about about the "e" prefix'):**
#19 confirmed by the reporter on rc6 (close at the 0.8.35 release, user). Reddit mod-wheel question = #18, fixed since
rc4 (reply draft given to the user). (1) Segment drag BUILT, NOT COMMITTED (E372): graph_segments/graph_grabs/
graph_grab in editorrender, editor drags a line like the original's KEnvelope; test editor_graph_segment_grabs, 433
tests, ctest 5/5; README/site 433, release note 'handles and lines'. cpp-review: no CRITICAL/HIGH; fixed: a segment that drags nothing no longer takes a click from the cutoff knob, test proves the segment path. Dev build 68ef72a9.. in ~/.vst3 (previous in
~/.vst3-backup/before-segments-1717), user check pending. (2) Fade: E372 (B) - the original has fade code but nothing
starts it (MEDIUM) -> nothing to build. (3) REAPER restart sync report: NOT reproduced with nativehost on the TI2
(2 x setActive 0/1 via --rate-at 44100@T, with and without --ahead 200: 0 sync reports, stamps ~3100 frames ahead;
scratchpad restart1/restart2) -> needs a REAPER session with VTI_NATIVE_DEBUG=1 VTI_NATIVE_STATS=1 (offered to the user).
NEXT: user checks the segment drag, commit; REAPER restart log; then the 'e' prefix talk; then the round's test steps.
**REAPER restart sync reports (2026-10-27 evening, 3 REAPER sessions, logs scratchpad reaper1-3):** ~1 report per
Play (14-16 per session). NOT late stamps: every send >= ~2100 frames ahead of the newest ring frame; reports arrive
90-100 ms after the first note-ons after Play. (a) Output delays (16 x 6F pp 7F at Play/Stop, E344): 15/16 reports
followed a set in session 2, so (user: 'try the recommended first' + an options menu) BUILT, NOT COMMITTED: default =
only changes + after a reconnect (OutputDelay::resend_all), Control Center nerd mode 'Options...' menu + `tirevive-cc
option [NAME on|off]` (diag.h plugin_options: outdelay-original.on, timing-classic.on), tests diag_plugin_options,
outputdelay_play_stop_only_like_the_original (435). Session 3 with it: still 10 reports -> the sets were NOT the cause
(coincided with Play). (b) nativehost on the TI2: loop restarts with F2 (--transport 5 --loop 2): 0 reports; REAPER's
Play CC block (CC10 64, CC7 100, bend centre, RPN 0 = 12 on ch 1-3) via --param-at 20 ms before notes: 0 reports.
Open: the combination REAPER does (setActive 0/1 + F2 + 73 00 23 01 + CC block + processing ahead). Debug logs added
(VTI_NATIVE_DEBUG): 'sync report N from the Virus, newest F' and data bytes on every 'send' line.
**Part volume 127 at Stop (user):** REAPER itself sends CC6 0, CC7 127, CC10 64 on ch 1-3 with the note-offs at Stop,
and CC7 100 (+ pan, bend, RPN) at Play (session 3 log); since rc4 every CC is passed on (#18, E368), so the part
volume follows. Which REAPER setting sends it: not checked. Dev build 44192299.. in ~/.vst3.
**SOLVED (user: '1. original as default, new one as option, 2. yes (nativehost Stop/Play), 3. update REAPER'), E373:**
REAPER updated 7.82 -> 7.83 (portable folder; full backup ~/.local/opt/dl/reaper/backup-7.82-20261027). Automated
REAPER runs: scratchpad auto/ (with-cc.RPP / no-cc.RPP = copies of multi-syth-test, playstop.lua passed on REAPER's
command line: 10 x Play 4 s / Stop 2 s then quit; ab.sh NAME runs one and counts reports; note: Steam's process is
also called 'reaper'). Cause: setActive(0) at every REAPER Play sent CC64+CC123 on all 16 channels (32 msgs) at the
stamp of the Play block -> report at every Play (20/20; 0/20 without). Fix BUILT, NOT COMMITTED: heldnotes.h (only
held keys / pedals released at setActive(0)), test_heldnotes.cpp (3 tests, CMake list). With it: 0/20 (only on
change), 3-4/10 with the original's set at Stop (default, user's choice). Output delays: default = like the original
again; option 'outdelay-changes' (outdelay-changes.on). Restart set now after the block's events (like the original,
E280; no measurable effect). Volume 127: REAPER resets chased CCs at Stop (CC7 127) - proven by the no-cc copy; the
REAPER preference not identified. README + site (Options sentence), release notes (2 bullets), 438 tests, script
tests 88 OK. Dev build ba517ed5.. in ~/.vst3. NEXT: user checks (segment drag, Options menu in the Control Center,
REAPER Play by hand), then commit the round's code; the 'e' prefix talk; the round's test steps.
**User checks OK (2026-10-27):** segment drag, REAPER Play/Stop by hand, the 'e' (appears when a parameter is
modified -> stays not copied, E370 updated). Options menu: the user found none because I named the STALE
build-native/bin/Release/TIReviveControlCenter/ (2026-10-15); the current one is build-native/ccapp/TIReviveControlCenter
- rendered on Xvfb :97 (scratchpad shot/xshot.c, xclick): Options... next to Back, both entries. COMMITTED 7e756bb
(segment drag) + 250d19f (REAPER fix, options, docs). NEXT: the round's test steps (rc7 for testers?, test machines,
Windows VM, hil-mac, risk.py checklist, final 0.8.35).
**#18 (2026-10-26 evening, user 'yes, go ahead', E367):** mod wheel / every CC 0..119 now mapped (VST3
IMidiMapping) and sent; name field removed on focus loss (Enter without a change left it white). Linux REAPER test
with the dev build (b85dcb1f.. in ~/.vst3, previous plug-in in ~/.vst3-backup/before-issue18-1749): CC 1 on the wire
(virusmon), cutoff audible, name field 'works'. vtitests 419/419, ctest 5/5 (gui_duplicate_editors flaky once, again).
**#18 follow-up (user: 'make the follow up and a new RC and write a response', 8690cf8):** host CCs update the
channel's 'Internal MIDI Standard Parameters' (state + preset re-send); not statically checked whether the original
updates them from host MIDI (its state keeps them, it re-sends them after a preset change, E152). Dev build 31f4a896..
in ~/.vst3; recall test with the user pending (CC1 end value -> save -> reopen -> virusmon shows it in 'state sent').
Release notes for rc2 written (build.yml). Reply draft work/issue18-reply.md. NEXT: recall test, then tag v0.8.35-rc2
(separately), then post #18 after the user's OK.
**E368 (REA, 2026-10-26):** the original runs every host MIDI event through its device-description decoder
(processEvents 0x18002ffa0 -> 0x18007aa50 -> CIntermediateMIDIQueue -> 0x180080740 -> 0x180062070), so ALL decoded
parameters follow host CCs, not only the Standard Parameters -> follow-up widened (drain_param_updates decodes and sets
every change; host_controller_kept + its test removed, 419 tests). NEXT: practical check in win10ref with the original
(user prepares the VM: Live 11, CC 1 envelope ending at a known value, save; I decode the .als state), then the REAPER
recall test with our build, then rc2.
**E368 practical check DONE (win10ref, Live 11, user):** original VST3 state CC 1 = 0 (its VST3 maps only AT/PB,
so Live never hands it CCs - #18 also happens with the original VST3); original VST2 state ch1 CC 1 = 107 (kept and
saved). The widened follow-up (99cd092) matches. Reply draft mentions the VST3 limitation. NEXT: user quits Live in
win10ref -> vm.sh detach, Virus back on Linux, install 99cd092 build in ~/.vst3, REAPER recall test (virusmon at
project open: CC 1 with the last value in 'state sent'), then tag v0.8.35-rc2 separately + release CI, then post #18.
**REAPER recall check DONE (2026-10-26 ~18:51):** saved during playback -> state ch1 CC 1 = 80 + part 1 dump
byte 10 = 80 (= the original VST2's pattern); saved after a stop -> 0, because REAPER itself sends CC 1 = 0 at the stop
(virusmon). Dev build a22695c8.. in ~/.vst3. NEXT: tag v0.8.35-rc2 (separately), release CI, then post #18 + Reddit
after the user's OK.
**rc3 (user: 'works. include it in RC3', 2026-10-26 ~19:00):** name-field selection light blue instead of red on Linux
(frame focus colour, E367 (3)); user checked in REAPER. rc2 tagged on 52190d0 (run 40372646035), rc3 on the colour fix.
The #18 reply now points to rc4: the rc2 run failed in native-windows (MSVC: test_devicestatus.cpp included
unistd.h for getpid, from the AURA test 193273c) - rc2 and rc3 runs cancelled (tags stay, no releases), test made
portable (steady_clock), rc4 tagged on the fix.
**v0.8.35-rc4 PUBLISHED (2026-10-26 ~19:10, run 40373523502 all green, pre-release, 6 assets). #18 reply posted
(issuecomment-6423689426, asks for an Ableton/macOS check). Reddit reply: the user posts it (rc4 link).** NEXT: install
rc4 on Linux/win11/Mac (test machines latest), then #14 Snow points (level meter, empty name field, preset location),
Windows VM test, hil-mac, round checklist, final 0.8.35.
**#14 Snow points (E369, 2026-10-26 evening, user: 'go straight to 2'):** (1) level meter: the original Snow editor
has none -> same as the original, answer the user; (2) blank name after SAVE/LOAD/Init: most likely the #18 white field
(rc4), ask for a retest; (3) preset location: the original sets dump bytes 10..12 (0 / bank RAM 1-4 ROM 5.. / index;
libraries 0x20) before a patch load, TI Revive does not. vtirender can draw the Snow editor (VTIRENDER_SNOW=1).
NEXT: observe the original in win10ref (load a ROM patch from its browser, what the Virus display shows), then
implement the same bytes in load_patch (TDD) and compare on the TI2.
**#14 point 3 DONE (2026-10-26 ~20:40):** usbmon capture of the original (captures/orig-load-origin-1951.pcapng):
Virus-bank patch = Bank Select + Program Change, then the v12 dump with origin bytes ~0.3 s later; library = dump with
0x20. TI Revive does the same now (E369); the user saw 'RAM-B 4 BadBrain M', 'ROM-A 0', 'LIB Fizzbomb' (the original adds
an 'e' through an extra parameter pair after the dump - not copied). 422 tests, ctest 5/5, dev build 6af0981a.. in
~/.vst3. Also noted (user): the original's browser marks the selected entry with a full blue background, ours only with
a frame - to check. NEXT: browser selection look, then reply #14 (meter = original, name field = #18/rc4, location
fixed in the next build).
Committed 203aa5a + 6c5120f (E369 confidence HIGH for point 3), pushed. Not in rc4 - an rc5 after the browser look
is offered to the user. NEXT: (1) browser selection look (full blue background in the original), (2) #14 reply draft
shown to the user, (3) rc5 on the user's OK.
**Browser selection DONE (2785ce0):** the list with the focus fills its selection (E318), user: 'works'; dev build
e617df91.. in ~/.vst3. **'e' prefix (eRAM-B / eLIB) analysed:** not in the original's load code; 0x1800876b0 writes
per-part listener parameter values into an edit-buffer dump before it goes out (likely host/automation values) - the
parameter pair after the dump is most likely a host echo, not a deliberate signal (not proven). Proposed to the user:
do not copy it (TI Revive's edit buffer = the stored patch, so no 'e' is accurate). NEXT: user's decision, then E370
(the 'e' analysis), #14 reply draft, rc5.
**User OK (2026-10-26): 'e' not copied -> E370 (a9b80d7).** rc5 release notes (location on the Virus display,
filled browser selection), 423 tests in README/site (57ba335). #14 reply draft work/issue14-reply2.md (ES + EN, points
1-4, rc5 link) shown to the user. NEXT: on the user's OK tag v0.8.35-rc5 separately, release CI, then post #14.
**v0.8.35-rc5 PUBLISHED (run 40390678963 green, pre-release); #14 reply posted (issuecomment-6426010189). #18 and #17
closed with comments (user OK). New #19 (#18 reporter, rc4, macOS/Live 12): (1) browser bank arrows jumped from personal
libraries to the factory banks - FIXED f30ff2d (browser_bank_step over the bank menu's order; test), dev build
4a245822.. in ~/.vst3, user check pending; (2) filter display: drag changes cutoff but not resonance; envelope handles
not draggable; (3) ARP user pattern: step bar drags buggy, buttons below the pattern dead; (4) randomize missing.
A REA subagent analyses the original's handlers for 2-4 (factory 0x180055028/0x180057e0f, KEnvelope/KEnvelopeStage,
ExecuteARPRandom* 0x180168980..). NEXT: implement 2-4 from that report (TDD), user tests, then rc6 + #19 reply.
**#19 progress (2026-10-26 late):** point 1 user OK ('the arrows work'); arrow hit zone +4 px above/below for small
Inc/Dec buttons (051c91b, user: 'hit zone really narrow'). E371 = the subagent's REA report (filter/envelope drags,
KStepSequencer, ARP actions). Point 2 BUILT, NOT COMMITTED: EditorRenderer::graph_handles/graph_value (geometry pulled
out of envelope()/filter_response(), inverse; test editor_graph_handles_round_trip, 426 tests) + editor drag
(relative, Shift 4x finer with re-anchor, Ctrl+click = 64 / resonance 0, handles +-5 px; segment hits not done).
Dev build eaae5e70.. in ~/.vst3, user test pending. NEXT: commit point 2 after the user's check; point 3 ArpEdit like
KStepSequencer (span incl. following off steps, length/velocity grab zones, relative, Alt+click toggles a step);
point 4 ARP actions (Shift L/R, Random note/velo/length, Reset, Fill Up; buttons + context menu); then rc6 + #19 reply.
**Point 2 COMMITTED 8d5067c** (user: 'works. nice'; fix: the cutoff MaskedKnob covers the filter display ->
graph_hit_test lets a handle win). **Points 3 + 4 BUILT, NOT COMMITTED:** EditorRenderer::arp_grab/arp_cell (grab
zones like KStepSequencer, test), editor relative drag on one step + Alt+click toggles a step; model/arpactions.h
(shift, random pattern/velocity/length, reset, fill up; tests/test_arpactions.cpp, CMake list), VirusPlugin::arp_action
(reads 32 steps, sends only changes), ExecuteARP* KickButtons routed in ui_set, right-click menu on 'Arpeggiator User
Pattern/...' controls. 432 tests, ctest 5/5. Dev build c8284207.. in ~/.vst3, user test pending. NEXT: commit 3+4
after the check, rc6, #19 reply (no patch-wide randomize in the original).
**#19 points 3+4 COMMITTED 9e11408** (user: Fill Up 'works' once the pattern length was below 32; log showed
length 32 first). Release notes for #19, 432 tests in README/site. #19 reply draft work/issue19-reply.md (rc6 link).
NEXT: on the user's OK tag v0.8.35-rc6 separately, release CI, post #19.
**v0.8.35-rc6 PUBLISHED** (run 40397692349: the mac job got no runner and was cancelled after 15 min, rerun
--failed -> all green, pre-release, 6 assets). #19 reply posted. NEXT for the round: test machines on rc6 (Linux,
win11, Mac), Windows VM test, hil-mac, round checklist (risk.py --round 0.8.35), final 0.8.35; #16 waits for the
reporter; open: segment hits in the graph displays, the original's 'e' (E370) not copied.
NEXT for #14 (Snow reporter, 2026-10-26 comment): Snow editor - part 1 level meter empty, the name field empty after
SAVE/LOAD/Init Patch, preset location (bank/number) not shown; then replies on #18 and #14 shown to the user first.
**Decided (user, 2026-10-25 evening): WEEKLY releases.** Testing took most of the time for small rounds, so 0.8.35
becomes a bigger round: as many open tasks and features as possible, then one test run and the release.
**Release notes no longer pile up (2026-10-25):** build.yml's notes carried every 'New in' since 0.8.2 (v0.8.34 body
26 KB, all repeated in every release). Now: header, this version's 'New in', the tested-on line, a pointer to the
earlier releases + the site's changelog, the notice. At release prep REPLACE the 'New in' part (do not add on top).
Site: summary() stops at 'Earlier versions: ' / 'Not affiliated with ' (scripts/tests/test_site_changelog.py, 2
tests; script tests 80/80). Old releases keep their texts (the site already cut them, 0cc5b58).
**Round 0.8.35 plan (user 'sounds good. Lets go', 2026-10-25):** 1) #16 AU crash (E336/E359), 2) item 2 inactive text
(REA), 3) #14 d: Snow parts (REA). (1) STARTED: E359 - a thread of ours (waitpid for the broker the plug-in started)
sits in unloaded code in the reporter's macOS report; fix: entry.cpp KeepLoaded (macOS: dlopen RTLD_NODELETE on
itself). autest --delete-open N added (FL's order: view alive, unit disposed, N s run loop, prints whether
plugin.vst3 is still loaded). Mac unreachable 2026-10-25 evening -> NEXT on the Mac: build autest, run
`autest "/Library/Audio/Plug-Ins/Components/TI Revive Virus Control.component" --delete-open 40` with 0.8.34
(reproduce) and later with the CI build (fix). DONE 2026-10-25 ~18:15 (E359): reproduced 4/4 with 0.8.34
(SIGSEGV at the pool pop, object = VSTGUI_NSView whose class VSTGUI disposed in the static destructors run by the
wrapper's dlclose); with AUTEST_PIN=1 (the fix's technique from outside) 0.8.34 runs clean 3/3. autest on the Mac in
~/tirevive-autest (build: clang++ -isysroot .../MacOSX26.5.sdk ...; the default 27.0 SDK fails to link there).
NEXT: the CI build of 0.8.35 with autest --delete-open 40 (no AUTEST_PIN) on the Mac.
(2) DONE in code (E360): the original's inactive rule found with REA - opacity 95/255 (not our 0.4), inactive text
by DisabledFont only (a DisabledBack picture no longer makes the text opaque). Test editor_render_inactive_text;
vtitests 414. Not built: the original's fade between active and inactive. Visible with the Access skin: only
0.4 -> 0.373 (no element has text AND a DisabledBack); the Delay Clock 105/149 difference stays unexplained. No user
check needed beyond the round checklist. (3) DONE in code (E361, user chose 'separate Snow plug-in' like the
original): VST3 class 'TI Revive Virus Control (Snow)' (kSnowClassId 8A5A1125..) with the Snow's files (editorPR.xml:
4 parts, no REMOTE page; remotedevicePR.xml, automationPR.xml). Not as AU (SDK wrapper takes the first class).
vtitests 415, ctest 5/5 (factory_classes checks the Snow class). README Snow bullet (site has no such paragraph).
Rule 2: the Snow class was NOT activated against our TI2 (it would send the Snow description's start-up). A look at
the Snow editor: only with the Virus switched off. NEXT: #14 reply (with the user) asking the reporter to test it.
**Timing fix BUILT (2026-10-26, user: 'implement both, the 3072 lead and the safety net' + an A/B switch):**
lead A = 3072 everywhere (stamprule.h timing_lead), safety net StampFloor (stamps >= newest + 1024, never backwards
under 1 s; DeviceOut pack), log 'timing: lead N frames, safety net on/off' at initialize and 'timing safety net
moved N stamp(s) forward (M in total)' (at most every 2 s). A/B: timing-classic.on next to diag.on = before 0.8.35
(A 1024 on Linux/macOS, no net), read when a plug-in instance starts (re-insert the plug-in / reopen the project).
VTI_NATIVE_A still overrides A. vtitests 417, ctest 5/5 (Linux). Dev build c798e6b4 in ~/.vst3 (Linux).
hil.sh on it (work/hil/20261026-071442, TI2): ALL PASS (max 0.45 ms, jitter <= 0.20 ms, export 1.00x); log per
item 'timing: lead 3072 frames, safety net on'; the net moved 1 stamp per run, at the end of the run (the stop's
messages from the worker); Virus sync reports per item 1/1/1/1/5 vs 1/2/1/1/3 with the old timing (24.10.) - the
start quirk, H6 = forced reconnect. Reported latency now A + block + 640 (e.g. 4224 frames at 512).
User REAPER test (07:21, 48 kHz block 256, new timing): no red message; ~1 Virus sync report per Play = REAPER's
setActive(0/1) at every Play/Stop + re-anchor (anchors 1, err to -4221) - the old start quirk (also in hil with the
old timing); net moved 1 stamp at the end. Separate item: which message at the restart arrives late.
Branch round-0.8.35 PUSHED, CI TEST BUILD 0.8.35 run 40279333536 (5c8cbdd): ALL GREEN (first macOS/Windows build
of KeepLoaded, Snow class, timing). Artifacts ~/Downloads/TI-Revive-0.8.35-test. Mac PUSHED:
~/Downloads/TI-Revive-0.8.35-mac (pkg 6393864f.. = CI, INSTALL-COMMAND.txt, Install test build.command, hil/).
**RC plan (user, 2026-10-26):** new timing = default (already); pre-release v0.8.35-rc1 for the #14/#16 replies;
no Logic project attached (rule 1: the plug-in state holds Access patch data; Apple Loops; paths) - the setup is
described instead (multi-timbral instrument, 4 MIDI parts, part 1 USB 1, part 2 Out 2, a drum kit as 5th).
Prepared: native VERSION 0.8.35, build.yml 'New in 0.8.35' (tested-on: Linux REAPER + hil, Mac Logic + autest;
NOT Windows, NOT a Snow), README/site 417 tests. Drafts: work/issue14-reply.md (es + en), work/issue16-reply.md
(LINK placeholder). User approved ('sounds good', 2026-10-26 ~08:03): tag v0.8.35-rc1 (annotated, on bdb49fa) pushed separately,
release CI run 40283501817 ALL GREEN; pre-release https://github.com/derpsn/access-virus-ti-revive/releases/tag/v0.8.35-rc1
(pre-release flag, 6 assets, tester note + New in 0.8.35). Replies POSTED: #14 issuecomment-6410323810 (es + en),
#16 issuecomment-6410324094. WAITING for the two reporters (Snow plug-in, Logic diag.log / Apollo path; FL Studio).
NEXT for the round: Windows VM test of 0.8.35 (drop, Init, timing), hil-mac, the round checklist, then the final
0.8.35 (main merge + tag) - weekly cadence.
DONE on the Mac (E365, 2026-10-26 ~07:40-07:55): installed 0.8.35 CI; #16 fixed (autest 2/2 clean, no pin);
Logic A/B: new timing clean in every run; old timing ALSO clean today (E363 late notes not reproduced: buffers
2048..128, 150 BPM, part 2 on Out 2) - improvement unproven, by design like the original. Mac left on the new timing
(timing-classic.on removed). NEXT: #14 reply (pre-release, Snow plug-in, drop, Init, Logic: diag.log with the mode
line, how the analog outputs come back into Logic - Apollo input monitoring?), Windows VM test of the round.
OLD NEXT: user installs; Logic A/B (timing-classic.on in ~/Library/Application Support/virus-ti-native);
autest --delete-open 40 (no AUTEST_PIN) on the installed 0.8.35 AU for #16.
**Original's timing (E364, REA 2026-10-26):** stamps = device position NOW + offset + fixed lead 3072 (forced at
every start, = reported latency) -> never late, whatever the host's block timing; a host table only shifts the song
position/clock. Ours: timeline-based stamps with A = 1024 on Linux/macOS. Test running: VTI_NATIVE_A=3072 via
launchctl setenv on the Mac (Logic restart) - if the sync errors go, raise A (and/or clamp stamps to now + A).
**Logic on the Mac (E363, 2026-10-25 ~19:15-21:00):** analog vs USB in sync in Logic (user, all buffers) - the
reporter's 'analog out of sync' is not reproduced; BUT Logic gives many late notes (sync reports 18-42 per Play vs
1-2 in Ableton) and the red sync error shows - likely the reporter's Logic sync problem. NEXT: analyse the Logic
diag logs (scratchpad logic/diag4.log: block sizes, read margin, timeline steps, stamps) against an Ableton session,
find why notes reach the Virus late under Logic; rewrite the #14 draft point 4 accordingly (reproduced, sync errors).
Logic Pro (trial) installed on the Mac; diag.on + klog.on set there.
#14 new comment (2026-10-25 17:14): Logic - analog-output parts out of time (USB fine; Live fine) -> new, needs
details + diag.log; volume (SHIFT held) confirmed; dragging a library file did nothing (= our 0.8.35 drop); missed
copy/paste and Init. Drafts (not posted): scratchpad issue14-reply.md (Spanish + English) and issue16-reply.md;
user decides post now with 'link follows' or with the pre-release. User: check whether the Mac has Logic for the
analog sync (Mac unreachable ~19:00; else Logic Pro's 90-day trial). Init (user: 'implement this feature to make
the plugin more modern'): E362 'Init Patch' in the LOAD menu, built; user test in REAPER DONE (dev build 588552af,
'works': Init sound, Revert to back).

**User's goal:** open tasks that need no full all-OS run. Picked: (1) #14 b file drop from Finder/Explorer, (2) inactive
text strength, (5) HANDOFF item 6 library scan (odd dump sizes, version 127). REA on the x64 DLL (snapshot
work/rea/virus-control-5.1.7-x64.ghidra12.snapshot, session closed):
- (1) **E355: the original does NOT accept file drops** (KPatchTable drag-move 0x180049b50 / drop 0x180049e60 take only
  text items = its own entries). Not built (would be our own addition); README + site library bullet: add a library by
  copying the .mid into Documents/Access Music/Virus TI/Patches, a file drop does nothing as in the original. Mac
  build not checked.
- (5) **E356: the original's decoder (CPatchLibrarySMFDecoder 0x18006b360) keeps only 267/524-byte dumps**, skips the
  rest silently, no version check. The odd sizes are damaged files (Incubation V3 .mid.mid: 158/202-byte SMF events),
  so our 31 of 128 = the original. Version 127 (Howard Scarr, 1 patch) stays out on purpose (rule 2). BUILT (TDD):
  PatchLibrary::skipped, load_banks writes 'library <menu>/<name>: N patch(es) skipped (damaged or unknown format)'
  to stderr + diag.log (name only, no path). Test library_counts_skipped_dumps (in test_library.cpp, so no CMake
  change). vtitests 410/410, ctest 5/5 (Linux). nativehost (dev build, Linux, TI2 on, 1 s) on the user's libraries:
  Cosmosis 5, Incubation V3 97, Howard Scarr 1, Toxic 1, Interactive 1 = the scan.
- (2) **NOT built:** DisabledFont/DisabledFore exist in the skin, but DisabledFont sits only on always-disabled LCD
  /version text with the same font; the Delay Clock difference (orig 105 vs ours 149, Reverb 186-205 vs 161) goes
  both ways, so it is not a plain alpha. Our disabled_alpha 0.4 (editorrender.h) has no source. Needs the original's
  inactive draw rule found with REA (or a win10ref pixel comparison) first - stays open.
**Library file drop (user: 'Lets do this', 2026-10-25, E357) - our own addition:** a .mid/.midi/.syx dragged from
the desktop onto a browser pane is imported into Personal Libraries (import_library in access/library: .mid copied,
.syx <= 128 -> .mid, free name 'X 2', same patches in the same slots = only shown, no patches = not copied, no
folder = refused), listed at once, the pane switches to it, LCD hint while dragging (lcd_hint saves/restores the
LCD) and the result after (lcd_message releases the hover). No drop onto RAM (rule 2). cpp-reviewer: fixed UTF-8
paths on Windows (u8path, not compiled with MSVC yet), slot-blind same_patches, empty library_dir, LCD vs hover,
disk I/O under banks_mu_, uninitialised type; accepted: copied-but-unreadable (re-read fails) not listed;
coordinates unchanged (same as mouse events). Tests library_import, library_import_slots_and_no_folder,
lcdhover_release_keeps_a_message: vtitests 413/413, ctest 5/5 (Linux). Dev .so ce77ca88 in ~/.vst3 (0.8.34
release backed up in ~/.vst3-backup/0.8.34-release-24bf7c3c). Test files in the session scratchpad droptest/
('Drop Test.mid' = Nice Bytes Toxic copy, 'Drop Test Syx.syx' = Electro (Toxic).syx, notes.txt). REAPER/Linux
first: nothing happened -> E358: two VSTGUI X11 bugs (REAPER/SWELL forwards drags as XDND version 3, VSTGUI wanted
5; VSTGUI kept the drag's entry position, so drops went to the lower pane), fixed in
native/patches/vstgui-x11-xdnd.patch, applied by native/CMakeLists.txt on Linux only (git status then shows the
vst3sdk submodule as modified - expected). User test 2026-10-25: 'works!' (pane under the mouse, already-there,
.syx -> .mid). LCD texts were cut at 20 characters on line 2 ('to Personal Librarie', 'My Sounds: 128 patch')
-> shortened (hint 'Drop to add a library' / '(Personal Libraries)', result 'Library added' + name, 'Added (N
damaged)', short import errors <= 20, failed import logged with the file name); vtitests 413/413. Website + README
(user, 2026-10-25): new feature 'Libraries by drag and drop' (same words in both) with screenshot
docs/images/plugin-library-drop.png (captured from REAPER's plug-in window with XGetImage, frame after a drop of 'My
Sounds.syx' = Electro (Toxic) copy; site built locally, checked in headless Firefox, NOT deployed; tirevive-site-check
still due before the release). Test libraries removed from the Patches folder after each test. OPEN: Windows (Live)
and Mac (Live/Logic) by hand.
**#17 SOLVED (2026-10-26, E366):** reporter removed AURA's USB MIDI driver plugin -> works with the 3 m cable; user
replied 10:37. Issue still open (close with the user). AURA hint BUILT (user: 'implement the AURA hint now'):
model/otherdrivers.h + test, plug-in checks the two macOS folders at start-up (log line), editor draws 2 red lines
on the no-device page; README 'One driver at a time' + AURA's file name; vtitests 418, ctest 5/5 (gui_duplicate_
editors failed once, passed on the rerun - flaky). Not in 0.8.35-rc1; NOT tested on a Mac (would need a dummy
'AURA Plugins Virus TI USB Midi.plugin' folder in /Library/Audio/MIDI Drivers + the Virus unplugged).
**Issue #17 (2026-10-25, TI Desktop OS 5.1.7, MacBook Pro M2 Max, macOS 26.6.2, Cubase 15, TI Revive 0.8.33):** Virus
rarely detected; better with a 1 m cable than 2-3 m, port-dependent, CalDigit TS4 dock on another port; the user also
runs AURA Virus HE (works with the 3 m cable). Screenshots only: System Information shows 'Virus TI' at 12 Mb/s
(normal: full-speed device) on its own built-in bus. Suspect: AURA's USB MIDI driver racing TI Revive for the
interfaces (E134) - hypothesis, no log yet. Reply posted with the user (issuecomment-6399775473): asked for
/tmp/tirevive-vtibroker.log, ls of MIDI Drivers/HAL, ioreg Exclusive owner; test with AURA's driver plugin moved out
+ restart; update to 0.8.34. WAITING for their logs.
README/site: tests 409 -> 413, library bullet (same words in both). risk.py --round 0.8.35: now class C on all
three (native/CMakeLists.txt + native/patches, new rule in risk.json) -> core items all automated (hil cold/reopen/
export per OS), nothing from core by hand; the drop by hand on Windows and the Mac. NEXT: user decides whether
to add more to the round (e.g. #14 replies, item 2 after REA); then dev build smoke test by the user, CI test build,
hil on the three machines, round checklist (L: diag line with a damaged library).

## (previous START HERE) (2026-10-25): round 0.8.34 on branch `round-0.8.34` (fresh from main d7033d0)
## Site SEO (2026-10-26), straight to main outside round 0.8.35 (user: "bring this faster to the main and deploy it")

Branch `site-seo` from main, merged to main; site.yml deploys on the push. Live round work stays on `round-0.8.35`
(its HANDOFF is the current one; main gets merged into it). What changed (scripts/site/build.py, site/src):
titles with "Access Virus TI", "Windows 11", "macOS", "Linux"; a description per page; canonical URL; Open Graph +
Twitter card with our own preview image (site/src/social.png from scripts/site/social.py, no Access screenshot -
rule 1); favicon.svg; SoftwareApplication JSON-LD on the landing page; sitemap.xml; loading="lazy" for every
screenshot but the first; the "Important" box uses no headings so the h1 is the page's first heading.
Tests: scripts/tests/test_site_seo.py (8). Open, the user's part: Google Search Console - add
https://derpsn.github.io/access-virus-ti-revive/, verify (HTML file or meta tag), submit sitemap.xml.
Deployed 2026-10-26 (main 04bc44b, site run green, live tags checked with curl). Search Console: the user's
verification file site/src/googleec62795eb211ed59.html is on main (cdb7992) and live - KEEP it, Google re-checks it.
NEXT (user): Verify in Search Console, submit sitemap.xml, optionally request indexing. Main merged into
round-0.8.35 twice (71a3e9b, 6ea94e6); the round's NEXT is unchanged (Windows VM test, hil-mac, checklist, release).

## START HERE (2026-10-25): round 0.8.34 on branch `round-0.8.34` (fresh from main d7033d0)

**Decided (user, 2026-10-25):** the REAPER Play-start work (E351) is dropped - small symptom (a start right on a note, pre-roll avoids it) against much timeline complexity next to E337. It stays on the LOCAL branch `round-0.8.34-reaper-timeline` (not pushed: play_started/activated in timelinefollow.h, 19 model tests, review fixes). **Kept: E352** - the audio retry (Virus switched on after the DAW) never took the ring's rate: 48 kHz ring, plug-in counted 44.1, re-anchors twice a second, sync errors at every Play (v0.8.33 has it). DeviceOut::start_audio() now serves the first start and the retries. Built, vtitests + ctest 5/5 (Linux). The same fix was heard on the old branch (user ~07:05, Virus on late: host 482304 / virus 480000, no sync page). Dev .so 5ea919c2.. installed in ~/.vst3 (old-round build in ~/.vst3-backup/0.8.34-reaper-timeline-ff896dcb). REAPER check DONE (user ~07:16, Virus on late): no sync errors; diag.log idle 480000 / 480000, anchors 0 (Play windows err ~-2080 = E351 burst, expected). **Hover value in the LCD (item 5b, started 2026-10-25):** the original DOES show the control under the mouse (E353, user in win10ref: name / value / [stored patch value] / Part N, at once, empty off a control, text values for switches and menus) - a gap to close, not our own addition. Static (E354, REA, hover handler 0x18003cbd0): skips VirusControl/... and Internal/... (except Internal MIDI Standard Parameters), clears the four fields only if hover filled them, writes only changed fields. Our LCD already has the layout (set_lcd: lcd_param_/value_/part_/orig_), but lcd_orig_ = value before the first edit (resets when another parameter is edited) - the original keeps the patch's stored value. BUILT (2026-10-25, TDD): native/src/editor/lcdhover.h (LcdHover enter/leave, lcd_hover_shows; tests/test_lcdhover.cpp, 5 tests); VirusPlugin::hover() (name, value_text, [stored], "Part N" always), stored_values_ per part taken in apply_state_to_params(stored): STORED_ALL (project/default state), the part (load_dump = patch load), STORED_NONE (rename); ui_set's value text moved to value_text(); editor: lcd_hover() in the canvas view's mouse observer (every move without a button, the plug-in hears only of a new control; the timer repaints the LCD). vtitests 408/408, ctest 5/5 (Linux). Dev .so 936ac0ab.. installed in ~/.vst3 (bce25ca: hover as checked in the VM). REAPER check (user, 2026-10-25 ~08:35, Linux, TI2 Desktop): "looks good" (hover on controls, [stored] after edits and patch load, globals, tabs/empty panel blank). README + site DONE (2026-10-25): hover bullet (features) + E352 bullet (reliability fixes), same wording; tirevive-site-check scoped to the round (full check was at the 0.8.33 release): new bullets ok, unit tests 403 -> 409 (stale, fixed), coverage 94 % (16 Oct) unchanged, E351 not claimed; plug-in VERSION still 0.8.33 (bump at release prep); site built locally, not deployed. risk.py --round 0.8.34: class C on all three (native/CMakeLists.txt: the new test file) -> automated hil cold/reopen/export per OS, nothing by hand from core; round items by hand: hover (W, M), Virus switched on late (Linux done by the user, W). Branch pushed, CI TEST BUILD 0.8.34 started 2026-10-25 ~08:44: run 40173908853. CI GREEN (all jobs). Artifacts in ~/Downloads/TI-Revive-0.8.34-test (linux, msi, mac, hil-mac, hil-windows). Linux = CI build 0.8.34 (install.sh as the user, no sudo; plug-in bdc24ce4.. = the tarball). hil.sh first try refused: its DAW check matched Steam's game launcher (also named "reaper") -> fixed (daw_running skips processes under Steam). hil.sh on the installed CI build (after the game was closed): ALL PASS (work/hil/20261025-090406; H1, H2, H3 export 1.00x, H6; max 0.57 ms). win11 VM = CI build 0.8.34 (setup.exe /quiet exit 0, entries TI Revive 0.8.34, plug-in c862c260.. = the MSI's; C:\tirevive-test\0.8.34; VM off). Mac: NOT reachable at ~09:05 (asleep) - push pending (pkg zip + hil kit in ~/Downloads/TI-Revive-0.8.34-test/mac, hil-mac). CHECKLIST published 2026-10-25 ~09:12: https://claude.ai/artifact/KA8cifGDUCWTJmZayktaxF (spec scripts/checklists/round-0.8.34.json; db, marks rounds/0.8.34/marks - empty at publish): L1 hover + L2 Virus on late (Linux, CI build), W1 hil-win + W2 hover + W3 Virus attached after Live (VM), M1 hil-mac + M2 hover (Mac). LINUX DONE (user, ~10:31): L1 hover pass, L2 Virus on late pass (CI build; diag.log 10:29:53 session: device connected after REAPER, idle windows host 480000 / virus 480000, anchors only at Play (1, err -217..287); a second short session 10:31 again 'audio 1 drops 4' at the late start = the known low-priority item). WINDOWS DONE (user, ~10:39, win11 VM, Live 11): W3 pass (diag.log: device connected 21 s after the editor opened, 44.1 kHz, idle window host 440832 / virus 441000, anchors only at Play, drops 0), W2 hover pass, W1 hil-win (results-20261025-103602 in the VM): H1, H2-1, H3 (export 1.00x), H6 pass; H2-2 drift +112 ppm = the only FAIL, drift in the VM is not judged (E334), timing max 0.50 ms there - counted pass. Virus detached, VM off. MAC PUSHED (~10:41): ~/Downloads/TI-Revive-0.8.34-mac (TI Revive.pkg, INSTALL-COMMAND.txt, 'Install test build.command', hil/hil-mac.command). MAC DONE (user, ~10:46, MacBook Air M3, macOS 26.6.2, Live 12): installed (plug-in binary 08:49 = the CI package), M1 hil-mac ALL PASS (results-20261025-104158 on the Mac: H1, H2 x2, H3 export 1.00x, H6; max 0.96 ms), M2 hover pass. ROUND 0.8.34 TEST COMPLETE: all 7 checklist items pass (L1 L2 W1 W2 W3 M1 M2). RELEASE 0.8.34 (user: 'go ahead with the release', 2026-10-25 ~10:50): native VERSION 0.8.34 (62c65b1), release notes in build.yml (hover, late-Virus rate; tested-on line), vtitests + ctest 5/5 (Linux). README/site claims were checked for this round earlier (409 tests). MERGED into main b7f03da (-F file; parents d7033d0 + def85a5, tree = round tip), tag v0.8.34 pushed separately, release CI run 40182270329 started ~10:55. v0.8.34 RELEASED (2026-10-25 ~11:15): release CI 40182270329 all jobs green, GitHub release 'TI Revive 0.8.34' (not pre-release; msi, setup.exe, mac zip, linux tarball). Site rebuilt (site.yml run 40182663856 green; live page shows 0.8.34, 409 unit, hover bullet). Linux = release (install.sh; plug-in 24bf7c3c.. = the tarball's). win11 = release (setup.exe /quiet exit 0, TI Revive 0.8.34, plug-in a6bf7409.. = the MSI's; VM off). Mac = release (user installed 10:59, receipt io.github.derpsn.tirevive 0.8.34, plug-in binary 10:54 = the release build). NEXT: open low-priority items - 'audio 1 drops 4' at a late Virus start (seen again 10:31 on Linux, not audible), REAPER Play-start crackle (E351, pre-roll workaround, branch round-0.8.34-reaper-timeline local); issue #14 open points (above). Previously next: --round 0.8.34, hil.sh, round checklist (Windows/Mac: hover in Ableton). CHECKED in win10ref (~08:20, E353): patch load takes the stored value anew (= our build), Part 2 ok, page tabs show nothing (= VirusControl/SelectedPage, already skipped), global settings (PATCH UTILITY) and Clock Tempo (ARP) DO show - the short-lived "part none = blank" rule (0fc8845, from a misread step 5) is reverted; test lcdhover_shows_global_settings. VM: the first attach after a Linux plug-in session gave "no device" - power-cycling the Virus on the Linux side (broker stopped) before the attach fixed it. Edits keep the existing behaviour (the bracket = value when editing started, formats.md); after hover over the same control a drag keeps hover's [stored] (same param/part in set_lcd). **VM:** win10ref sessions done (2026-10-25 ~07:25 and ~08:15), VM off, Virus back on Linux (ACL ok, no udevadm needed), broker socket started. NEXT: risk.py --round 0.8.34, hil.sh, round checklist.

## (previous START HERE) (2026-10-24 ~21:10): **v0.8.33 RELEASED** - site live, release on Linux + win11, Mac package pushed

**Seen on the Windows PC (user):** LOAD > Personal Libraries > "Manuel Schleis/Manuel Schleis Incubation V2" is
listed, its bank is EMPTY (no names; "worked already" on other machines). The file is fine: 128 Virus C dumps (267
bytes, version 7, bank byte 1, programs 0..127), unchanged since 2009.
**Cause (static, Python copy of our logic against the user's files, no C++ build on that PC):** C:\Program Files\Access
Music\Virus TI\Common\Virus Control.dll there was the **x86** build (PE machine 0x14c, sha256 3e844c35..; inventory.md
"x86"). AccessFiles::default_page_b() (native/src/access/accessfiles.cpp:388) takes page B at `-Init-` dump - 0x400,
which is the x64 layout (a622f9a2..: Init at 0x1c46b0, table OK). In the x86 DLL that spot has bytes > 0x7F -> {} ->
load_library_file drops EVERY 267-byte dump -> all Virus A/B/C libraries load empty, TI-format ones (OS4/OS5 ...) are
fine. Nothing in diag.log: neither a missing page B nor skipped dumps are logged.
**How the x86 DLL got there:** an ADMINISTRATIVE extract of "Virus TI Software Suite 5.1.7.00 64-bit.msi" (msiexec /a)
flattens the x64 and x86 trees into one Access Music/Virus TI/Common, and the x86 file (File key viruscontrol_dll)
overwrites the x64 one (viruscontrol_dll64); the .vst3 comes out x86 too. The user's Downloads\VirusTI Driver folder
(copied to Program Files on Oct 17) is such an extract. The x64 file comes out right with 7-Zip: Installer.cab ->
viruscontrol_dll64.
**Workaround DONE (2026-10-24 ~18:55):** x64 DLL (a622f9a2..) from the MSI's cab copied into Program Files\...\Common
(elevated), the x86 one backed up in the session scratchpad; page B check on the installed file now OK. NOT yet
checked in the plug-in: DAW restart -> Incubation V2 shows 128 names and plays (user). The Downloads\VirusTI Driver
folder still holds the x86 DLL.
**DECIDED (user, 2026-10-24): only the 64-bit Virus Control.dll (E345). Steps 1-3 DONE ~20:00 (Linux, unit tests):**
(1) AccessFiles::open() skips a PE32 DLL and names it in the error -> Control Center status line at start, plug-in
diag.log line "no Access files: ..." (no editor, as without an installation); (2) install.sh picks the PE32+ copy
(pe64) and replaces an installed 32-bit one on a re-run; tirevive-access.ps1 treats a 32-bit DLL as missing and takes
viruscontrol_dll64 from the embedded #Installer.cab (Add-Type over msi.dll + expand.exe); (3) the knob template reader
reads 64-bit pointers only (E202's PE32 path removed). README: one note each for Windows and Linux. Checked on the
real DLLs (vtimodel files --access): x86 refused with the message, x64 opens. NOT RUN: the PowerShell path (no pwsh
here) -> win11 VM next, together with step 4.
The user's libraries from the PC are in ~/Dokumente/Access Music/Virus TI/Patches (no overwrite; the Linux
"My Patches.mid" kept) -> Incubation V2 can be checked on Linux.
**Also 2026-10-24 ~20:20:** MSI finish page text (WIXUI_EXITDIALOGOPTIONALTEXT): updating from 0.8.32 or older ->
keep the box checked and the Access .msi at hand (not built yet). **Mac finding E346:** default_page_b() on the Mac
binary takes init - 0x400 = wrong 7-bit bytes (137/256 differ), the real page B is at init + 0x2a0 -> Virus C
libraries on the Mac load with a wrong page B since 7121a5e. FIXED ~20:45 (user: 'really important'): REA confirmed
the Mac upgrader FUN_000e5c00 copies page B from init + 0x2a0; default_page_b() now picks by format (PE -0x400,
Mach-O +0x2a0), tests for both layouts, vtimodel pageb: Mac bundle == Windows x64 bytes. Not run on the Mac yet ->
round item: Mac, load an old Virus A/B/C library patch, compare a page B value (e.g. a mod matrix slot or FX
parameter of page B) with Linux/Windows. Release notes: old Virus C libraries on the Mac sounded with a wrong page B.
**NEXT:**
4. DONE ~21:20 (win11 VM, snapshot before-e345-test, reverted; both VMs off): new tirevive-access.ps1 replaced the
   VM's 32-bit DLL with a622f9a2.. (x64), rerun idempotent; a normal Access install puts x64 in Program Files\...\
   Common, x86 in Program Files (x86) (E345). The VM is back to the 32-bit DLL = a real case for the next CI build
   (setup.exe exit dialog / Set up Access files must fix it). win10 VM was started by mistake (vm.sh default) and
   shut down again, nothing ran in it.
5. Linux item (user, 2026-10-24 ~19:30): issue #14 - the reporter wants the Virus' volume knob to move the plug-in's
   volume. ORIGINAL checked (E347, win10ref, captures/ref-volknob-1): in Multi edit mode the knob sends CC 7 on the
   part's channel, the original's MIXER part slider ("07 Volume" = Part Volume) follows and it echoes CC 7 thinned;
   the amplifier section's Patch Volume (CC 91) does NOT move. Normal mode: nothing (E338). The Snow prints PART
   VOLUME under VALUE 1 (photo) -> likely CC 7 in normal use there. NEXT: the same on Linux with TI Revive (REAPER,
   Multi edit, knob on part 1): does our mixer part slider follow, do we echo CC 7 like the original (capture
   trv-volknob-1)? Then decide what to build.
   DONE ~20:00: E348 (original: every Shift function is an ordinary report; Shift+Attack = Patch Volume CC 91,
   Shift+Osc Balance = Panorama; the original echoes only automatable ones; its window follows Patch Volume) and
   E349 (TI Revive on Linux: amplifier Volume/Pan and the mixer part slider follow - nothing to build on the TI2; we
   do not echo, by design). Open for the Snow: which knob the reporter means (big VOLUME = analog master, never USB;
   Shift+Value 1 = Patch Volume) -> reply on #14 drafted for the user, not posted. Virus back on Linux, win10ref off.
** v0.8.33 RELEASED 2026-10-24 18:51 UTC: release CI 40122288225 all green, release public + Latest, 6 assets
   (setup.exe, msi, mac.zip, linux.tar.gz, tirevive-usbinfo.ps1, libusb source). Site rebuilt by hand (run 40122650350,
   main 9773607): live page shows Download 0.8.33, the Important notice box (64-bit), 403 unit tests, part following.
   Release on the test machines: Linux install.sh (plug-in 868214e3.. = release tarball, broker e3787d73..; the CI
   test build backed up in ~/.vst3-backup/0.8.33-ci2), win11 setup.exe /quiet exit 0, entries 0.8.33, DLL a622f9a2..
   (C:\tirevive-test\0.8.33-release), VM off; Mac: release unpacked in ~/Downloads/TI-Revive-0.8.33-release-mac (pkg
   e548f83f.. = release, INSTALL-COMMAND.txt + Install test build.command) - the user installs. win10ref: user check
   done, VM shut down by the user; Virus on Linux (ACL access ok, broker socket active).
   Site 2026-10-24 ~21:20 (user): home link "TI Revive" on the left of the top bar on every page (base.html,
   style.css .bar .home); small phones (<= 400 px): links one line, the GitHub icon hidden (footer links GitHub);
   checked in headless Firefox at 360 / 390 / 600 / 1400 px.
   #14 status (read with the user ~21:30, reporter's comments 18:19 / 18:59; the user replied 18:36 / 19:04): Snow +
   Mac Studio M1 + Live stable, no crashes, start-up sound solved; the reporter tests Shift+Value 1 tomorrow.
   Open from #14 (next round, nothing posted by Claude):
   a) the red "data loss" message at the first load (16:05, unanswered; later "no errors with the update") - if it
      returns: exact text / screenshot + diag.log (our sync message or 'event(s) lost'?).
   b) bank file dragged from Finder into the plug-in does nothing (reporter + Gemini tried): our drag works only
      inside the browser (library pane -> RAM slot). Explain the path (.mid into Documents/Access Music/Virus TI/
      Patches -> Personal Libraries -> drag onto RAM); check whether the ORIGINAL accepts file drops from Finder
      (gap vs own addition); maybe a README help line.
   c) saving an edited sound: the reporter expects right-click > overwrite and did not find SAVE (asked 15:20 and
      18:59) -> short answer with where SAVE is / drag the part's patch name onto a RAM slot, ideally a screenshot.
   d) "the Snow has only 4 parts, not 16": the Snow's description lists the same parts as the TI (E151), we always
      show 16; what the ORIGINAL shows with a Snow is unknown -> REA on the model handling (no Snow here).
   e) Logic Pro sync problems from the first report: not mentioned since -> ask whether 0.8.33 still has them.
   f) analog/USB delay: fixed on our TI2 (E344, by ear) -> the reporter's confirmation on the Snow.
   Reply tomorrow after their test: volume result, bank-to-RAM in 3 steps, how to save, screenshot request, Logic.
   NEXT (next round): hover value in the LCD (5b, check the original first), #14 answer from the Snow reporter
   (Shift+Value 1), the library scan items (6), Reddit report (7).
** RELEASE 0.8.33 (user: "yes, release it", ~20:50): merge f4f05de on main (parents 03e281c main + c3f2c4e round,
   tree = round 9f1bcc58, merge message from a file), pushed; tag v0.8.33 (annotated) on f4f05de pushed separately ->
   release CI run 40122288225 (event push, v0.8.33). NEXT: when green -> check the release (6 assets, notes),
   site rebuild by hand (gh workflow run site.yml; notice box goes live), install the release on Linux / win11 /
   Mac. Meanwhile (user's request): win10ref running, Virus attached (Access driver 2.9.66) for a check by the user;
   detach only after Live is closed (E327).
** ROUND 0.8.33-2 DONE 2026-10-24 ~20:45: 6/6 pass (E350): L1, L2, W1, W2, M1 (hil-mac ALL PASS graded on Linux),
   M2 (20/70/70). Tested-on line in the release notes. win11 + win10ref off, Virus on the Mac (moved for M1/M2).
   NEXT: release decision with the user: push, merge round-0.8.33 -> main with -F <file>, verify the merge commit,
   tag v0.8.33 (separate command), release CI, then site rebuild by hand (gh workflow run site.yml) - the new notice
   box goes live with it; install the release on Linux / win11 / Mac.
5b. Wish (user, 2026-10-24, M2): show a knob's value in the bottom LCD on hover (today the LCD changes only when a
   value changes: ui_set -> set_lcd). FIRST check what the original Virus Control does on hover (win10ref, no
   guessing); like the original -> a gap to close, else our own addition, say so. Next round, not 0.8.33.
6. Open from the scan (not fixed): dumps of other sizes (202/158 bytes in "Yoz & Andy/Incubation V3 ... .mid.mid",
   31 of 128 kept; Cosmosis 54/59; Nice Bytes Toxic/Interactive 127/128) and upgrade_single false for version byte
   127 (Howard Scarr Sounds, 1 patch) - check with REA whether the original keeps them; a diag.log line per library
   with skipped dumps.
7. Reddit report "personal libraries aren't producing any sound": likely E345 -> ask whether their Common\Virus
   Control.dll is 32-bit (14,040,312 bytes) and whether only Virus A/B/C libraries were empty.
8. Release prep 2026-10-24 ~20:15 (user: targeted round, not a full one): native VERSION 0.8.33, pushed, CI run
   40119192061 (3217a3a). Site check (tirevive-site-check): TESTS 379 -> 403 (site + README); part following and
   analog outputs in time (E342, E344) added to the features (site + README, same words); Windows card: update note
   (64-bit only, E345); timing table unchanged (no new 16th-note DAW measurement; B1 hil-win is another measure),
   coverage 94 % (16 Oct) kept with its date. Site built locally only (not deployed). Release notes 0.8.33 in
   build.yml (Tested-on line after the targeted round). Site: new notice box above the hero (build.py NOTICES +
   notice_section, style.css .notice; empty list = no box), first entry = the 64-bit Windows update note (E345); the
   README starts with the same text. Checked in headless Firefox at 1400 and 390 px (scratchpad screenshots).
   Targeted round spec: scripts/checklists/round-0.8.33-2.json (L1-L2, W1-W2, M1-M2), checklist artifact
   https://claude.ai/artifact/HQvBECEREpToNdDWRyYZRz (db, marks rounds/0.8.33-2/marks). CI run 40119192061 ALL GREEN.
   LINUX: install.sh of the CI build over the existing setup = "already set up", DLL a622f9a2.. kept, plug-in
   7a544108.. = tarball, broker e3787d73.. unchanged (old plug-in backed up in ~/.vst3-backup/0.8.33-ci1); hil.sh
   ALL PASS (max 0.66 ms); L1, L2 marked pass by the user. M2 marker (vtimodel libdump, new): Incubation V2 patch 0
   "Exciter MS", correct page B -> Envelope 3/4 Attack 20, Decay 70, Release 70; the old Mac bytes gave 0/0/0, Delay
   Type 105 and ASCII in the arp user pattern. Mac: package pushed + unpacked (~/Downloads/TI-Revive-0.8.33-mac, pkg
   eef65c1d.. = CI, hil kit, INSTALL-COMMAND.txt, Install test build.command), Incubation V2 copied to the Mac's
   Personal Libraries. win11 VM running, setup.exe (d9865ba6.. = CI) in Downloads next to the Access .msi, VM DLL
   still 32-bit (3e844c35..) = the update case. Targeted round on the new CI build: VM setup.exe upgrade over
   the 32-bit DLL (finish page text, DLL replaced, plug-in opens), Linux install.sh again + hil.sh + Incubation V2
   names, Mac install + one Virus A/B/C patch page B + hil-mac.

## START HERE - state 2026-10-24 ~18:05: round 0.8.33 checklist - Linux, Mac, Windows PC DONE (B1 graded: all pass)

**Windows PC session 2026-10-24 17:15-18:05 (CI run 40104018425):** artifacts fetched via nightly.link (no gh/login on
that PC; zip sizes = the API's), MSI plug-in 1548EEA1 = the VM's; setup.exe /quiet exit 0, installed binaries = MSI
(replaced the earlier CCE39B57 of run 40093015139). All four marked pass by the user, notes in the checklist db.
- **B1 hil-win:** results-20261024-172158, H1/H2.1/H2.2/H3/H6 ran, nothing failed. GRADED on Linux 2026-10-24 ~19:10
  (hil_check.py --folder, zip copied to ~/windows_tests): ALL PASS (H2.1 max 0.14 ms, H2.2 0.43 ms, H3 export 1.00x,
  H6 0.75 ms / +4 ppm).
- **B2:** Live 17:27-17:59 (31 min), 44.1 kHz, user: no crackle. diag: drops 0, over block 0, anchors 1 at the start
  plus 1 each at 17:56:17 / 17:57:57, both next to a new Play ("N since Play" reset) - read as transport restarts, not
  the E337 jumps. Uneven stretches (err up to +-800, once 2257 at the Play) handled by drift steps only (bursts
  ~130-146); this morning's old build had 47-198 anchors per 10 s in the same kind of stretch -> E337 fix exercised.
- **B3:** 'part 3 output 1 -> output delay 90 ms' = (3072+256+640)/44100; Live passed block 256 the whole session
  (largest 256) even when the user switched 512 / 2048 - like the Mac. User: in sync at both.
- **B4:** no 'more than 256 in one block', no 'event(s) lost', no full-queue line; some notes still missing in the
  dense passage, same on Linux -> the Virus' voice limit (E341), not our side.
Logs: Downloads\TI-Revive-0.8.33-run40104018425\windows-logs\diag-B2-173950.log, diag-B4-175934.log (not in git).
NEXT: grade B1 on Linux; then README/site (analog delay, part following; tirevive-site-check), release notes in
build.yml, version.

## (state before, ~17:20)

**For the Windows session (the user boots this PC into Windows and runs Claude Code there):** checklist
https://claude.ai/artifact/RJnAcjRBphmuowwFqgeNCL (v2; marks L1, L2, M1, M2, M3 = pass). Items B1-B4:
- install the CI build: TI-Revive-0.8.33-setup.exe (CI run 40104018425, artifact TI-Revive-msi; also on Linux in
  ~/Downloads/TI-Revive-0.8.33-test/msi), plug-in log on (%LOCALAPPDATA%\virus-ti-native\diag.on);
- B1 hil-win (artifact TI-Revive-hil-windows; no Python on that PC -> grade the folder on Linux with
  hil_check.py --folder, or copy it to Linux like /home/datoml/windows_tests); covers cold/reopen/export;
- B2 Ableton >= 15 min at the Creative card 44.1 kHz / 2048 (E337 episodes): no crackle; diag.log 'anchors' 0 after
  the start, err ranges (steps only in uneven stretches);
- B3 analog part in time at Ableton buffers 512 and 2048; the Windows latency is A 3072 + block + 640 frames: at plugin
  block 2048 ~131 ms > the Virus' max 127 ms -> a few ms early possible; NOTE: Live passes small plug-in blocks (Mac:
  128-512 at any Live buffer, diag 'host ... block'), so the delay may stay well below 127 ms - check the diag line
  'part N output X -> output delay N ms';
- B4 dense MIDI passage: no 'more than 256 in one block' line (notes may still be missing: voice limit, E341).
Mac block (17:10-17:16): M1 hil-mac ALL PASS (results-20261024-170929, max 0.96 ms, export 1.00x), M2 analog in
sync at Live 512 and 2048 (diag: part 2 Out1 -> 44 ms; Live plug-in blocks 128-512), M3 part following - all pass.
After Windows: README/site (analog delay, part following; tirevive-site-check), release notes in build.yml, version.

## (state before)

**Linux = CI build 0.8.33** (install.sh as the user, no sudo; plug-in b4b032e5.., broker e3787d73.. = the tarball;
unpacked in ~/Downloads/TI-Revive-0.8.33-test). hil.sh on it: ALL PASS (work/hil/20261024-165448; max 0.57 ms).
**win11 VM = CI build 0.8.33** (setup.exe /quiet exit 0, entries "TI Revive 0.8.33", plug-in 1548eea1.. = the MSI's
nativevst3; C:\tirevive-test\0.8.33; VM off). **Mac: package PUSHED** (~17:10) to ~/Downloads/TI-Revive-0.8.33-mac
(pkg 1eebd732.. = CI) with hil/ kit, INSTALL-COMMAND.txt and "Install test build.command"; INSTALLED by the user (~17:15):
receipt io.github.derpsn.tirevive 0.8.33, VST3 binary 039b19e7.. = the pkg payload. All three test machines + the
Windows PC download ready; CHECKLIST published 2026-10-24 ~17:25:
https://claude.ai/artifact/RJnAcjRBphmuowwFqgeNCL (spec scripts/checklists/round-0.8.33.json; db, marks in
rounds/0.8.33/marks - empty at publish). risk.py: class C (CI files); Linux core by hil.sh (done, all pass); Windows
core on the Windows PC (BC1-3, not the VM: E337 + analog delay need real timing), Mac MC1-3. New items: L1 analog
delay 2048/256, L2 part following + part volume; M1 hil-mac, M2 analog delay, M3 part following; B1 hil-win, B2 long
Ableton session (E337), B3 analog delay at Windows latency (at 2048 the needed ~131 ms is above the Virus' 127 ms max
-> a few ms early expected), B4 dense MIDI counter. Read marks with ArtifactData list rounds/0.8.33/marks. LINUX DONE (17:07): L1 pass, L2 pass; diag.log on the CI build: only the analog part
delayed (part 3 on Out1 / Out3 R), 56 ms at REAPER 48 kHz block 1024, 40 ms at block 256 (= (A+block+640)/48000);
USB parts 0. CHANGED (user's question, 17:12): hil-mac + hil-win have both
passed once (2026-10-23 / 2026-10-24), so risk.json now lists cold/reopen/export as automated on Windows and macOS too
(testing.md updated); checklist republished as version 2 (same link, marks kept): MC1-3 and BC1-3 dropped, M1/B1
cover them. Mac M1 DONE: hil-mac on the installed CI build, results-20261024-170929
(pulled to ~/Downloads/TI-Revive-0.8.33-test/hil-mac-results): ALL PASS (H1, H2 x2, H3, H6), graded on Linux too.
Note: the hil zip unpacks into hil/hil/ on the Mac (zip has its own folder). NEXT: M2, M3, then the Windows PC. Windows PC: the user installs ~/Downloads/TI-Revive-0.8.33-test/msi/setup.exe.

Round 0.8.33 so far: E337 crackle fix (Windows: crackle gone), per-block event counter (E341: missing notes = the
Virus' voice limit), part following (E342), analog parts in sync with USB parts (E344, "perfectly in sync" on the
TI2 in REAPER), output/mute both ways + analog outs confirmed (E343). #14 reply posted. NEXT: run green -> install
on Linux (install.sh), win11 VM / Windows PC, Mac; risk.py --round 0.8.33, hil.sh, hil-win, hil-mac, checklist with the
new items (analog delay at different buffers incl. Windows A=3072, part following, E337 episodes on the Windows PC);
README/site: analog delay + part following (tirevive-site-check).

## (previous START HERE)

**Windows test 2026-10-24 afternoon (CI build 40093015139, logs in /home/datoml/windows_tests, not in git):** hil-win
ALL PASS (graded on Linux). Ableton 14:25-14:32: user "crackle gone"; timeline: 0 jumps after the anchor, one burst of
134 drift steps at 14:30:29; drops 0, USB errors 0. But no strong uneven stretch came up (err max 509 vs +-800 in the
morning) -> the jump suppression was not really exercised on Windows yet (details E337). **New report:** at the end,
3 presets with very dense MIDI, some notes seemed missing. Log: no "event(s) lost", no sync error, no rule-2 drop.
Candidates: (a) the Virus' own voice limit / voice stealing (heavy patches, 3 parts) - most likely, unproven;
(b) process() collects at most EV_MAX = 256 events per block and drops the rest SILENTLY (virusplugin.cpp ~1320,
no counter) - a counter + log line would settle it. Questions to the user: one instance with 3 parts or 3 instances,
MIDI channels, missing = never started or cut off, chords or fast runs, automation/MPE on those tracks.
User answers (2026-10-24 ~15:10): ONE instance with 3 presets (3 parts); the missing notes NEVER STARTED (not cut off);
in dense chords and fast runs. User leans to the Virus' voice limit. Not settled: whether the Virus drops new notes or
steals old voices when it runs out is not in the evidence; the EV_MAX counter would rule our side in or out.
**Counter ADDED (2026-10-24 ~15:40, user OK):** native/src/device/blockevents.h (BlockEvents: MAX 256, counts what
does not fit, flags a lost note-off; 4 tests), used by process(); DeviceOut::collect_lost -> the worker logs
"N event(s) lost, more than 256 in one block" and sends all notes off when a note-off was among them (like the
full-queue path). 393 tests, ctest 5/5, hil.sh ALL PASS. nativehost --stress 20 s: the new line 0 times; the old
"queue from process() was full" 1645 lines / 20377 events (expected at ~2750 notes/s + 1000 params/s, way past the
4 kB/s budget; that line logs every worker tick - spammy, not changed). The user's Ableton log had NEITHER line ->
both queues were fine there; only the per-block limit was invisible before. NEXT: CI build RUNNING (run 40095512298), the user plays the same
dense passage (one instance, 3 presets) and also the passage with only one preset; diag.log: if no "more than 256"
line and notes come back with one preset -> the Virus' voice limit.

Branch round-0.8.33. **Linux session 2026-10-24 (after the Windows PC session that wrote steps 1+2 without a compiler):**
- Build broke on a name clash: `follow_` already is the PanelFollow -> the TimelineFollow member is `tl_follow_`.
- 389 unit tests pass, ctest 5/5. Mutation check: the old per-block jump rule put back into timelinefollow.h fails 3 of
  the 6 timeline tests -> the tests guard the fix.
- New nativehost `--rough T1:T2` (tools/hostclock.h RoughCadence, 4 tests in test_hostclock.cpp): bursts of 8 blocks,
  +-20 ms jitter, one burst 40 ms late every ~5 s (harsher than Ableton: err -2100..2780 vs E337's -767..852).
- Device runs (Virus TI2 Desktop, dev build, scratch logs not kept): `--block 256 --rough 15:75`, 80 s: OLD rule 315-627
  jumps / 10 s, notes skipping +-480 ms; NEW 0 jumps. `--ahead 100@20 --ahead 25@40` (E279 case): new 1+1 jumps vs old
  6+2, only the note at each moment off, grid exact after. hil.sh on the dev build ALL PASS (work/hil/20261024-092029).
  Details in E337.
- CI run 40074101252 FAILED in native-windows only: `FAR` is a windows.h macro (C2059 timelinefollow.h:18) -> renamed
  AT_ONCE, `(std::max)` in hostclock.h (6f7c93f; checked under g++ with windows.h-style macros, not built with MSVC).
- **NEXT (Windows PC):** CI test build 0.8.33 RUNNING = run 40093015139 on 6f7c93f (started ~12:50). Check it:
  `gh run view 40093015139`; if native-windows fails again read `gh run view 40093015139 --log-failed`. When green:
  download the MSI/setup from the run's artifacts, install, then Ableton at 2048 through two ~6-minute episodes (diag.log
  via Control Center "collect logs": anchors ~0, no crackle), hil-win.ps1, then a REAPER render check by hand (E279) and
  E337 final.

**Issue #14 (Snow, Mac Studio M1, Sequoia, Logic/Ableton) - reporter update 2026-10-24 10:39 UTC (Spanish), read with
the user:** (1) plug-in opened with the Snow already on -> broken/crackling sound until a power cycle; (2) audio only
over USB, choosing another output in the plug-in does not move it to the analog outs; (3) the Snow's volume knob does
nothing over USB; (4) earlier, when analog out worked, delay in Logic (USB fine); (5) wish: copy/paste/move presets,
import whole libraries to RAM. No version number, no diag.log. Reply posted (issuecomment-6384039769, Spanish, AI note):
asked for diag.log of a broken start, a screenshot of the part's output setting, the exact version. (2) and (3) not in
the evidence yet -> NEXT: check output routing to the analog outs and the volume knob on our TI Desktop.

**Volume knob over USB (issue #14 point 3) = E338 + E339 (2026-10-24 ~17:15):** default mode: the knob does nothing on
USB, with the original too (Access Quickstart says so). BUT with the Global switch "Remote Main Volume" = "Volume>MIDI"
(the user set it on the Virus panel - it STAYS set on our TI2 Desktop; set it back when done) the knob sends CC 7 ch 1,
and the ORIGINAL plug-in sends it back to the Virus itself (also with Live's input on "No Input") -> Patch Volume of
part 1 -> USB level follows (user heard it). NEXT: same knob test with TI Revive on Linux (nativehost + PANELLOG,
wav level): does our plug-in take the incoming CC 7 and send it back? If not -> feature = copy the original's echo
(test-first). Also from the captures: panel Multi changes report as 72 00 29 vv (Output Select) / 72 00 48 (Part Mute)
- useful for #14 point 2.
The original follows Output Select + Part Mute BOTH ways (user, 2026-10-24 ~17:25; no echo on the wire). User wants
TI Revive to do everything the original does: (1) knob CC 7 echo, (2) output/mute plug-in -> Virus (#14 point 2),
(3) output/mute Virus -> plug-in (TI Revive's generic panel follow probably covers it - verify). STATE NOW: win10ref
RUNNING with the Virus attached and Live 11 open; the Virus is in "Volume>MIDI" mode. NEXT: user quits Live -> check
no Ableton process -> vm.sh detach + shutdown -> user runs udevadm trigger -> the three tests with TI Revive on Linux
(nativehost + VTI_NATIVE_PANELLOG, wav level) -> build what is missing, test-first.
DONE 15:16-15:18 (E340): VM off, Virus back on Linux (ACL ok, no udevadm needed). TI Revive: knob works WITHOUT echo
(the Virus applies CC 7 to part 1 itself), panel mute/output applied by the Virus. Plug-in -> Virus output/mute: NO
72 00 29 / 72 00 48 sent in the run - ask the user whether they changed it in the plug-in; if yes = #14 point 2 bug
(look at how the editor's "Part output" / "Part on/off" controls are sent). Plug-in view following the panel: user said
"works" (to confirm per item).
Linux plug-in since 2026-10-24 ~15:30 = DEV build of 1e83740 (sha 40d322e9.., ~/.vst3); the one before (b65b0744..,
installed 2026-10-23 21:12) in ~/.vst3-backup/0.8.32-installed-b65b0744. User tests output/mute in REAPER. RESULT: output/mute follow both ways in REAPER (user) -> #14
point 2 not reproduced on the TI2 (ask the Snow reporter for the screenshot/log). Dense-MIDI "missing notes" on Linux
too (E341): nothing lost or late on our side while playing -> the Virus' voice limit by elimination. CONFIRMED: one part
muted -> nothing missing = the Virus' voice limit (E341 final, closed; the per-block counter stays as a diagnostic). (The "Volume>MIDI" remark was wrong: Multi edit mode, see the correction.)

**2026-10-24 ~15:45 - CORRECTION from the user:** there is NO "Volume>MIDI" mode the user switched on the panel; the
Global volume only affects the outputs. What the user did in the knob tests: Shift+Edit = Multi edit mode, where the
knobs control the selected part -> that is where the CC 7 ch 1 came from. E339/E340 wording ("Volume>MIDI mode") must be
corrected (asked which knob was turned). The remotedevice "Remote Main Volume Enable" switch exists, but was not used.
**#14 update 13:21-13:28 UTC (Spanish, by email):** "screenshot attached, updated to the latest" - the screenshots did
NOT arrive (email attachments; no image links); "closing and reopening Virus Revive makes the sound work; sound comes
out via MIDI [USB?], not via audio [analog?]" (ambiguous). Reply (draft with the user): ask to drag the screenshots into
the GitHub page; still need version + diag.log.
**E342 (15:38-15:40):** the original's window follows the part selected on the Virus (Panel Part 72 00 1d vv), it
sends nothing back. TI Revive (15:43, trv-partsel-1): volume/mute/output fine, window did NOT follow the part
selection -> FIXED (Panel Part report stored under the old part instead of as a global; PanelFollow::globals_without_part,
394 tests, ctest 5/5); dev build sha 2b50d878.. installed in ~/.vst3 (VM off, Virus on Linux). USER CHECKED in REAPER
(~16:00): the window follows the selected part. DONE. Older plan line: does its window follow the Virus'
part selection, part volume knob (CC 7 on the part's channel), mute, output - for parts other than 1 too.
**#14 update 13:49 UTC (reporter):** the start-up crackle (point 1) is SOLVED by the update (likely 0.8.32); analog
outs still give no signal (they prefer USB, not urgent); still wish: hardware volume knob (= original behaviour, E338;
Multi edit mode works on the TI - Snow unknown) and save/load presets/banks (mostly exists: LOAD/SAVE, Personal
Libraries, browser drag to RAM, Control Center backup/restore - tested on the TI2 only). Reply drafted (Spanish,
scratchpad reply14b.md) -> REPLACED and POSTED 2026-10-24 ~17:15 (issuecomment-6385574844, Spanish, AI note): part
volume in Multi edit, library features, analog delay fixed for the next version (E344), no-signal open; asked for the
next version's diag.log (output change lines: line + no sound = Snow/cabling/Apollo side; no line = our side, e.g.
Logic/AU), version number, files dragged into GitHub (email attachments do not arrive).
**E343 (16:00):** analog Out1/2/3 + headphones work with TI Revive on the TI2 (REAPER, user); Output Select values
decoded (1/4/7 = Out1/2/3 L+R, 10 = USB1 L+R). #14 reply draft updated (point 1 = Multi edit part volume, not master);
USER HEARD IT (~16:10): part 1 on Out1 (Virus headphones, one ear) vs parts 2/3 on USB (PC, other ear) are audibly
out of sync - expected from the USB return path (Virus -> USB -> plug-in -> host buffer -> sound card), so analog is
probably EARLY. Not measured (no line-in cable). Open question: does the ORIGINAL align analog parts (per-part
timing)? REA analysis started (background agent, x64 DLL a622f9a2.., snapshot work/rea/...ghidra12.snapshot; known:
send 0x1800a86b0, stamp 0x1800a8420). If the original does nothing special -> same as original, tell the reporter;
if it does -> copy it. RESULT = E344: the original sends F0 00 20 33 01 10 6F pp 7F ms F7 per part
(ms = delay for analog parts, 0 for USB; likely its reported latency in ms); seen on the wire with 0 only; TI Revive
never sends it. NEXT: capture of the ORIGINAL with part 1 on Out1 (VM) -> the real non-zero value and when it is
sent; then implement in TI Revive test-first (send for each part whose output changes, at connect, on latency change). DONE: ref-outdelay-1 = 6F 00 7F 1A (26 ms) for the analog
part 1, 0 for the rest, all 16 at Play and Stop. IMPLEMENTED (outputdelay.h + VirusPlugin::send_output_delays in the
idle hook, ms = reported latency in ms, max 127; 399 tests, ctest 5/5). NOT tested on hardware yet: VM still running
with the Virus attached (Live open?) -> detach, VM off, install the dev build, user checks analog vs USB parts by ear
in REAPER; diag.log shows "output delay N ms" per change. VM off, Virus on Linux (ACL ok); dev build of 5ce094c (sha 11e9e883..)
installed in ~/.vst3 (16:35); user tests by ear in REAPER. RESULT: no effect - BUG: Snow parameter name on a TI (every part
read 0 = analog, all 16 delayed alike). FIXED (part_output via dev_.decode(72 pp 29); 400 tests, ctest 5/5); dev build
74c6079d.. installed (~16:50). USER: "Perfectly sync now" (~17:00). DONE on the TI2.
**VM test running:** win10ref ON, Virus attached (Access driver OK) - test: switch presets on the Virus (Multi edit
mode) and see whether the ORIGINAL plug-in follows; capture ref-presetswitch-1. Then detach, VM off, the same with
TI Revive (load_reported_presets, E152/E157).

**Problem (E337, follow-up of E187):** Ableton on bare metal, Creative card 44.1 kHz / 2048 (no ASIO), crackles in
stretches of about a minute, roughly every 6 minutes. diag.log: drops 0, process time fine, read margin >= 0; in those
stretches `timeline steps` 195-587 and `anchors` (jumps) 2-198 per 10 s, err -767..852 (calm: err -184..256, 0 jumps).
Cause: process() in native/plugin/source/virusplugin.cpp (timeline block after wait_offline, ~line 1250-1310) reacts
to the INSTANTANEOUS err per block: abs(err) > 512 with a marker -> `tl_ += err` (jump back to real time, E279),
abs(err) > 256 -> one-frame step. With uneven host block timing err swings +-800 within a second -> jumps back and
forth, each a 12-19 ms skip/repeat = click.

**Plan (user agreed 2026-10-24), test-first (ecc:tdd-workflow):**
1. Failing test: feed the timeline uneven host cadence (e.g. bursts of 8 x 256 frames, irregular gaps, a steady ring)
   and assert no jumps / few steps; plus keep a test that a REAL lasting offset (REAPER render, E279; a DAW stall)
   still jumps at once-ish. If the timeline logic is not testable in isolation, factor it into a small pure struct
   first (no behaviour change), like OfflinePacer.
2. Fix: jump only when the error stays beyond the threshold for a while (~0.5 s, same sign; e.g. the min of abs(err)
   over the window), and take the one-frame drift steps from a filtered error (window mean or midpoint of min/max),
   not the last block. No extra work worth mentioning on the audio thread, no allocation.
3. Headless check if available (nativehost with uneven blocks), then the user in Ableton at 2048: play through at
   least two of the ~6-minute episodes; diag.log (Control Center "collect logs") should show anchors 0 or near 0 and
   no crackle. Also re-run hil-win.ps1 (all pass before: H1, H2-1, H2-2, H3, H6 on 2026-10-24 07:29).
4. Then REAPER render check (E279 behaviour must stay: no slow catch-up after a render), Linux hil.sh, evidence entry
   with the result, README only if users see a difference.

## State 2026-10-23 afternoon (the user home, Virus on USB again)

**Site changelog fix DEPLOYED (2026-10-23 ~21:17):** changelog.html repeated every older version's notes under each newer one (summary() in scripts/site/build.py did not stop at the next "**New in"). Fix 3c8c2ac fast-forwarded onto main (only build.py), site run 40010175419 green; live changelog 114 KB -> 55 KB, 0.8.29 text once.

**CI caching + pinned actions (branch round-0.8.33, 2026-10-23):** apt lists in .github/apt/*.txt, installed by .github/actions/apt-install (own action on actions/cache: .deb files cached per list + runner image, apt-get update and 3 retries only when something is missing; awalsh128/cache-apt-pkgs-action NOT used: on a miss it pipes an unpinned apt-fast install script from master into bash as root). cache.yml fills the apt cache on main (a tag run reads only its tag + main; push to main on list changes, Mon/Thu 03:23 UTC, by hand) - it only exists once on main. ccache for native-linux (actions/cache, key per sha; tag runs start empty). Mac/Windows: no compiler cache (Xcode / VS generators, ~4 min builds). All actions latest + SHA-pinned (upload-pages-artifact v3->v5, deploy-pages v4->v5 major bumps; no dotfiles in site/dist). risk.json: .github/actions + .github/apt = class C like build.yml, cache.yml = A. Run 1 (40010912452, cold): all green; caches saved (apt-binaries 110 MB, apt-native-linux 8.5 MB, ccache 7 MB); install without apt-get update worked (image lists present). ccache: 173 compiles cached but 159 'uncacheable' - reason unknown, so --show-stats --verbose added. Run 2 (40011696420): all green; apt 'Need to get 0 B' for binaries (115 MB) and native-linux (cache hit); ccache 173/173 hits, the 159 others 'Could not use precompiled header' (VSTGUI target_precompile_headers). Fix: CCACHE_SLOPPINESS=pch_defines,time_macros (__DATE__ only in two SDK sample tools). Run 3 (40012537722): green; this run 173 hits, 150 new misses (the PCH compiles now stored), 9 uncacheable (stats accumulate in the cache dir -> ccache --zero-stats added). Run 4 (40013258368): green; ccache 323/328 hits (98.5 %), 4 uncacheable (the PCH builds); native-linux Build step 120 s (run 1, cold) -> 21 s; apt 'Need to get 0 B' (the install step stays ~15 s: dpkg unpack, but no mirror needed). CI caching DONE on round-0.8.33 (head after this commit). ON MAIN (user OK): 03e281c (only .github + risk.json from round-0.8.33) pushed 2026-10-23 ~21:48; site run 40014265329 green with upload-pages-artifact v5 / deploy-pages v5 (live page 200, changelog 55 KB); cache run 40014265136: native-linux + linux lists done, binaries list still downloading the 115 MB mingw from the slow mirror (first fill on main, >10 min, job limit 20 min) - if it times out: gh run rerun 40014265136 --failed.

**Released:** v0.8.31 (2026-10-22, tag v0.8.31, release run 39824346270; site rebuilt by hand, run 39827849547).
Release-triggered site deploys do NOT happen: always `gh workflow run site.yml` by hand.

**Test machines (unchanged since 2026-10-22):** Linux = 0.8.31 release (broker ~/.local/lib/tirevive/vtibroker =
release; CI build backed up in ~/.vst3-backup/0.8.31-ci). Mac = 0.8.31 release pushed to
~/Downloads/TI-Revive-0.8.31-release-mac - install not confirmed (check the receipt). win11 = 0.8.31 CI build (0.8.31c),
release NOT installed, VM off. win10ref off. **Linux broker since 2026-10-23 14:27 = DEV broker (protocol 5, sha256
b2ae0256..., work/build/linux/vtibroker); the 0.8.31 release one is ~/.local/lib/tirevive/vtibroker.0.8.31-release.**
Virus TI2 Desktop on USB again (2026-10-23 afternoon).

**NEXT item 1 DONE (2026-10-23 14:27-14:33, Linux, Virus TI2 Desktop, 44.1 kHz only, no rate switch):** dev broker +
build-native plug-in (HEAD 4e9a65b, rebuilt; ctest 5/5). Capture captures/ours-startup-rate5-20261023: startup_diff vs
orig-startup-lcd-20261022 = SAME START-UP; audio start = SET_CUR 44100 to EP 0x82, 0x01, 0x82, 0x01 (all Success), IN
iso submitted 0.14 ms before OUT - as designed (step 3). Broker log "audio on (44.1 kHz asked) -> 1, 44.1 kHz".
scripts/hil.sh: ALL PASS (H2-1, H2-2, H3 export 1.00x, H6 broker kill; max 0.91 ms, jitter <= 0.18 ms, drift <= 12 ppm).
usbaudio "errors 1/2" at stop seen as before (older brokers 2026-10-20..23: 216x 0, 90x 1, 145x 2) - not new.
Dev broker kept installed for items 2/3.

**Branch round-0.8.32 (pushed, last 79e11c6).** Done 2026-10-22: start-up like the original (b3bde33, E321),
scripts/startup_diff.py (SAME START-UP), E322, E323 + plan findings/design-rate.md, rate following steps 1-2 (eba76ab,
213e6a0), hil-win.ps1 / hil-mac.command. Done 2026-10-23 - **rate following steps 3-6, all written, none on hardware:**
- Step 3 (e07b52c): src/broker/ratesw.h (switch in the driver's order: stop + wait, alt 0, 250 ms, alt 1, SET_CUR
  0x82/0x01 x2, restart; refused -> back once; 1 s debounce), protocol 5 (VTIMSG_AUDIO + u32 rate, 0 = no wish,
  missing = 44100), pktstat/lever relative to the wanted rate, Linux libusb stream_start/stop + vla_switch_rate.
  **Behaviour change at 44.1 kHz too:** SET_CUR now 0x82, 0x01, 0x82, 0x01 (was 0x01, 0x82) and IN queued before the
  OUT lead-in.
- Step 4 (28364f5): Windows stream switches in its own thread; who sets the rate = owner.h vti_rate_setter (the
  owning plug-in, or the ASIO driver while no plug-in owns the Virus - user OK 2026-10-23, narrower than "the last
  plug-in that starts" because of E112); ASIO driver 44.1/48 (init = no wish, setSampleRate asks the broker, switch
  under it -> sampleRateDidChange + kAsioResetRequest); macOS AudioIn::open(rate) / request_rate / rate listener.
- Step 5 (ed4f5b8): plug-in maps the host rate (virus_rate_for), asks for it at the first audio start and in
  setActive(true) (waits up to 3 s), kLatencyChanged; converters for both Virus rates built in setupProcessing.
- Step 6 (0eb60a4): developer docs (protocol.md "Sample rate", architecture.md, design-linux.md). README/site NOT yet
  (only after the hardware tests: new timing rows at 48 k, tirevive-site-check).
- Two cpp-reviewer rounds, findings fixed (design-rate.md steps 3/4). 372 unit tests, ctest 5/5 (Linux).
- **CI test build 39904309933 (version 0.8.32): ALL GREEN** - first MSVC/Xcode build of steps 4/5; Windows unit tests
  4/4. Not installed anywhere yet.
- **Issue #15 fixed (E324, 2026-10-23 noon, with the user):** Linux install.sh took the first of two Common/ copies in
  the .msi; the Program Files one has a 32-bit DLL and no Control Center exe. Now access_src prefers the copy with
  the exe, a re-run adds a missing exe from the .msi (else a note), find_msi no longer ends the script under set -e.
  scripts/tests/test_install_linux.py (7) + sandbox runs with the real .msi; README + README-linux.txt one line.
  Round item: Linux install from the .msi -> Control Center in the Access look. Reply on #15 posted (pushed ff1ce70);
  issue stays open until 0.8.32 is released.
- Issue #16 (TI Desktop, macOS Tahoe, FL Studio 2026, 0.8.28): stages 1-5 pass, VST3 fine; **AU crashes FL Studio
  when unloaded** (new, no crash log yet) + cosmetic bank list number defect (screenshot only). Reply posted
  2026-10-23: asked for the .ips crash report, diag.log, how it is unloaded, and a re-check on 0.8.31.
- User decision 2026-10-23: the Windows (WDM) sound card stays deferred - no kernel driver, nothing of
  design-wdm-bridge.md in this round.

**SAFETY (rule 2):** with a protocol-5 broker (the dev broker work/build/linux/vtibroker, any CI build of this round) a
48 kHz project or `nativehost --rate 48000` REALLY switches the Virus. The first such run is the controlled one on
Linux with the user watching the display ("Audio clock changed" expected) and the user's OK at that moment. Until then:
hardware runs at 44.1 kHz only; no Mac/Windows test of a 48 kHz project. The installed 0.8.31 broker ignores the rate.

**NEXT (in this order, when the user is home and the Virus is on):**
1. DONE (see above). 44.1 kHz check of the new start order: back up ~/.local/lib/tirevive/vtibroker (0.8.31 release), install
   work/build/linux/vtibroker (dev, protocol 5), `scripts/hil.sh --plugin "build-native/VST3/Release/TI Revive Virus
   Control.vst3"` at 44.1 kHz; compare the start in a capture with startup_diff.py; then restore or keep the dev broker.
2. DONE (E325: System Clock page "Source Auto", "Freq 44.1"; which parameter Freq is stays LOW). The user reads "System Clock / SA Freq" in the CONFIG menu (E323 (6), E175/E241 hypothesis). Never sent as a request.
3. PARTLY DONE (E326: 44.1 -> 48 -> 44.1 as fresh starts pass, 'Audio clock changed' seen, display Freq stays 44.1; OPEN: in-run switch + debounce need a nativehost option; the original plug-in checked 2026-10-23 ~14:55 in win10ref: display Freq stays 44.1 there too, it sends no 0x0F/0x10/0x11 either, the Virus reports 0x10 itself = E327; win10ref crashed at the detach: bug check 0xCE, the detach was a surprise removal while Live was still open - E327 corrected. DONE 15:02-15:19: second original session (clean quit + VM shutdown, no crash; Virus back on Linux, broker socket active, win10ref off): 32/96 kHz refused by the Access ASIO driver (Live's engine off, no SET_CUR, no display message); the Access driver sets 48 kHz (23x SET_CUR) when it releases the Virus, 2/2 = E327 (corrected). Open: an in-run switch + debounce on our side; 96 k with another sound card dropped (user). nativehost --rate-at R@T written 2026-10-23 ~15:25 (tools/hostclock.h, tests/test_hostclock.cpp, 376 tests; WAV per rate segment WAV.2.wav..; regression 44.1 k without a switch pass) - Run A (in-run 44.1 -> 48 at 15 s) DONE = E328: switch on the wire as designed, 14/14 notes, but the first two notes after the switch are off (+4 / +52 ms vs +45 steady) -> REA (E329): the original also sends notes in that window against its old reference (never holds/drops) - holding would be our own improvement, decide with the user; old REA snapshot no longer loads, new one work/rea/virus-control-5.1.7-x64.ghidra12.snapshot. Run B (debounce) DONE = E330: one switch on the wire for three requests, the plug-in converts when the broker keeps its rate, 19/19 notes after, pass. E175/E241 investigated 2026-10-23 evening (E331): a SET_CUR with the chip's stored rate is ignored; host CPU FUN_5953 can put the DSP to 48 k without the chip -> the broker's recovery (SET_CUR x3 + 73 00 10 00) is a no-op there; candidate: a real 44.1->48->44.1 switch; reproduction: 2 tries (replug after/during 48 k) NOT reproduced = E332. Recovery DONE 2026-10-23 ~18:30: ratesw.h vti_relock (switch away, 1 s, back; 3 tries, then the power-cycle message) in the Linux device thread and the Windows stream thread; SET_CUR-again and the 73 00 10 send removed (no-ops, E331); report logging kept. 379 tests, ctest 5/5, Linux + Windows brokers build; cpp-reviewer: 2 Windows findings fixed (refused switch away left a stale target; BACK lost in the read path), 1 LOW accepted (a wish for the away rate during the 1 s is dropped). hil.sh ALL PASS + a short run with the final broker (dev broker installed, sha c88f4295..). NOT tested: the recovery itself (E175 not reproducible, E332) - watch for 'switching to 48.0 kHz and back' in logs. E333 (18:37): the 23x SET_CUR 48000 burst is Linux' snd-usb-audio at every connect (not the Access driver, E327 corrected) - a likely E175 trigger on Linux; panel System Clock page locked while USB is up; the user set Freq = 48 with the cable out (to be set back to 44.1). Replug with a waiting client tried 18:41 (E333 (4)): clean, E175 not reproduced (3 tries in total); Freq back at 44.1 (user). Open decision: keep snd-usb-audio off the Virus' audio function on Linux (mitigation idea, not built). CI TEST BUILD 0.8.32 STARTED 2026-10-23 18:45: run 39989412010 on 65aed06 (rate following, E175 recovery, #15 fix). It FAILED only in native-windows scripts_py: test_install_linux.py ran the Linux install.sh on Windows (path forms) -> skipped on win32 like test_handoff_hook (4775265); NEW run 39990990671 on 4775265. 39990990671: everything green except binaries (cancelled: apt install mingw-w64/libusb hung past the 10-min job limit, both runs; runner side) -> gh run rerun --failed. RERUN GREEN (all jobs incl. binaries + msi) = CI test build 0.8.32 (run 39990990671, 4775265). Installed: Linux via install.sh (no sudo; broker bc06b909.., plug-in .so ad40058e.. = tarball; 0.8.31 release plug-in + the dev broker backed up in ~/.vst3-backup/0.8.31-release-b; unpacked ~/Downloads/TI-Revive-0.8.32-test); win11 VM setup.exe /quiet exit 0, entries 0.8.32, plug-in 56413c52.. = CI (C:\tirevive-test\0.8.32), VM running without the Virus; Mac: not reachable over ssh at ~19:20 (asleep?) - push pending. risk.py --round 0.8.32: class C everywhere (Linux: power by hand, hil cold/reopen/export/sleep; Windows + Mac all by hand). hil.sh on the installed CI build: ALL PASS (H1, H2 x2, H3 export 1.00x, H6; max 0.85 ms). Mac: package pushed + unzipped in ~/Downloads/TI-Revive-0.8.32-mac (pkg 7181a551.. = CI) with INSTALL-COMMAND.txt + 'Install test build.command'; user installs. CHECKLIST published 2026-10-23 ~19:35: https://claude.ai/artifact/4Wys5kAm3yy5AU8Hcg8z5h (spec scripts/checklists/round-0.8.32.json; db, marks in rounds/0.8.32/marks - empty at publish): Linux LC4, LC5, L1 48 kHz via pw-metadata, L2 install from the .msi (#15); VM WC1-WC4, W1 48 kHz in Live; Mac MC1-MC5, M1 48 kHz, M2 AU load/unload x5 (#16). LINUX BLOCK 2026-10-23 ~19:15-19:26: LC4, LC5, L1, L2 marked pass; broker log: all REAPER streams at 48 kHz (this PC's PipeWire default clock.rate 48000, allowed [48000]) - power cycle 19:19:20 and sleep 19:20:29 reconnected at 48 k; the step 'force-rate 0 = back to 44.1' was wrong for this PC, so L1 republished (v2) as a redo: force-rate 44100, REAPER from a 48 k Virus. L1 redo pass (19:27:07 REAPER asked 44.1, switched from 48). VM BLOCK ~19:28-19:35: WC1-WC4, W1 marked pass; a power cycle in the VM drops the Virus to Linux as a new device and leaves a stale hostdev (vm.sh detach + attach reattaches it; no VM crash this time). hil-win.ps1 first real run (E334): all pass except H2-2 drift +115 ppm (VM clock; not judged) - check on the real PC. MAC: receipt 0.8.32 (installed ~20:03), hil kit in ~/Downloads/TI-Revive-0.8.32-mac/hil; hil-mac.command by the user (results-20261023-200900): ALL PASS (H1, H2 x2, H3 export 1.00x, H6; max 0.90 ms, jitter <= 0.27 ms, drift -6..+1 ppm). Mac items: MC1-MC5, M1 pass; M2 marked skip first ('Ableton doesn't support AU') - Live needs Settings > Plug-Ins > Use Audio Units v2; user: GarageBand load/unload x5 no crash, AU in Live works -> M2 set to pass with that note. ROUND 0.8.32: 16/16 PASS = E335. RELEASE PREP 2026-10-23 ~20:40 (user: 'Prepare for full release and ship it'): tirevive-site-check - stale: every 'the Virus runs at 44.1 kHz' claim (README timing bullet, Mac audio device line, 'Any sample rate' bullet; site Timing text) -> 44.1 or 48 kHz like the original (E321-E330, E335); Linux replug known issue + sleep sentence -> the 0.8.32 recovery, marked untested against the fault (E331-E333); tests 345 -> 379; new status row and 'same as the original' item (rate following + 'Audio clock changed'); timing table unchanged (no new 16th-note DAW measurement this round). Note: the 0.8.31 release notes say Windows was tested 'with the new automated run' - E334 is the first recorded hil-win run (that 0.8.31 line has no source). native VERSION 0.8.32, release notes in build.yml. Site built locally (not deployed). MERGED: main f05194f (merge of f801634, tree identical, -F file), tag v0.8.32 on it, pushed 2026-10-23 ~20:45; release CI run 40005096326 (binaries cancelled again by the apt install hang - 3rd time today; rerun --failed started ~21:00; consider a retry/timeout for that apt step in build.yml). Rerun 1 cancelled the same way: Azure mirror (azure.archive.ubuntu.com) delivered the 115 MB mingw .debs at a few KB/s (not a hang). Rerun 2 GREEN: **v0.8.32 RELEASED** 2026-10-23 (run 40005096326, 6 assets, Latest). Site rebuilt by hand (run 40008457144): live page shows 0.8.32, 379 unit tests, '44.1 or 48 kHz'. Release on test machines: win11 setup.exe /quiet exit 0, entries 0.8.32 (plug-in 58c2f588.., C:\tirevive-test\0.8.32-release), VM off; Linux unpacked in ~/Downloads/TI-Revive-0.8.32-release (broker = CI build bc06b909..), CI plug-in backed up in ~/.vst3-backup/0.8.32-ci, install.sh NOT run yet (Virus at the Mac -> install.sh wants sudo for the udev rule; the user runs it); Mac asleep - push of the release zip (pkg f38a3101..) to ~/Downloads/TI-Revive-0.8.32-release-mac pending. #15 closed with a note (user OK, 2026-10-23). User asked about cache actions: plan for the next round - awalsh128/cache-apt-pkgs-action (pinned SHA) for native-linux + binaries, retry + step timeout for apt, ccache (hendrikmuhs/ccache-action) for Linux/mac; not on main under the release run. NEXT: green -> site rebuild by hand (gh workflow run site.yml), install the release on Linux/win11/Mac, close #15 with the user. #16 update read with the user (~20:50): AU crash in FL Studio when the channel is deleted with the window open = E336 (inner plugin.vst3 unloaded by the SDK wrapper; hypothesis: editor async work outlives terminate) - candidate for 0.8.33; reply posted on #16 (issuecomment-6372317265: cause guess, workaround close the window first / use VST3, asked for FL's own crash log). NEXT: item 5 (README/site texts: rate following, E175 recovery note?, timing rows at 48 k; tirevive-site-check), native version 0.8.32 + release notes in build.yml, then the release decision with the user. NEXT: when green -> round test (item 4: install Linux/win11/Mac, risk.py, hil, checklist). USER DECISION 2026-10-23: notes after an in-run switch stay as they are (like the original, E329; a switch while playing is not expected in practice). Step 3 DONE. NEXT: round test (item 4) - CI build 39904309933 (0eb60a4) lacks the #15 install.sh fix (ff1ce70), so a new CI run on the round head is needed first. After every VM session: the Virus' /dev/bus/usb node stays root:root -> the user runs `sudo udevadm trigger --action=add --subsystem-match=usb --attr-match=idVendor=133e`.) Controlled first switch with the user: nativehost --rate 48000 (44.1 -> 48), then 48 -> 44.1, then quick changes
   (debounce must hold them); log + display; new evidence entry (E324+).
4. Round test: install the CI build (Linux install.sh, win11 VM=win11 silent, Mac INSTALL-COMMAND), risk.py --round
   0.8.32 (class C everywhere), hil at 44.1/48/96, exports at 48 k/128, startup_diff at 48 k, the ASIO driver at 48 k
   (VM: function only), checklist artifact (scripts/checklist.py).
5. After the tests: README/site texts (rate following, timing rows), release notes in build.yml.
6. Open small items: install the 0.8.31 release on win11; confirm the Mac release install. Issue #14 (Snow on macOS,
   user report, waiting for the reporter's version/diag.log).

**Notes:** captures in captures/ (git-ignored): orig-startup-lcd-20261022 (original start-up + 5 rate switches),
ours-startup-*. Audit of every 44.1 kHz assumption: work/rate-follow/audit-44k1.md (local). The GateGuard hook asks for
facts before the first edit of each file - state importers/affected functions/the user's instruction, then retry.

---
_Older summary (2026-10-20, kept for history):_

_Last updated: 2026-10-20. **v0.8.28 RELEASED 2026-10-20** (merge 2d81a7b of round-0.8.28 into main, tag v0.8.28 on it, release CI run 39395027961 all green, 6 assets, Latest; site rebuilt by hand, run 39395731478, live shows Download 0.8.28 / 304 tests / the new timing rows / the hub hint). Round 0.8.28 checklist 18/18 pass (Linux 6, VM 5, Mac 7; E287-E289) plus the Mac re-test of the patch name font on CI run 39392580073. Test machines: Linux = release (install.sh; plug-in sha 7a40d595.., broker 6c99ab7a..; the dev build in ~/.vst3 replaced; CI test build backup in ~/.vst3-backup/0.8.28-ci); win11 VM = release (setup.exe /quiet, exit 0, plug-in = the release MSI, C:\tirevive-test\0.8.28-release), VM shut down; Mac = release (installed by the user 2026-10-20 11:46, receipt 0.8.28; broker, VST3, AU, AU's inner VST3 and Control Center binaries = the release pkg payload by sha256; no DAW session with it in diag.log yet, the last one (11:45, editor closed, setActive 0) was the test build before the install). Open: REAPER crash after a GUI render with the editor open (E287: intermittent, REAPER's SWELL/GDK, editor open needed; editor open/close now logged); real PC timing. **Round 0.8.29 (branch round-0.8.29, pushed, collecting; state 2026-10-20 ~12:30):** (1) accessfiles NaN check survives fast-math (+ test_resampler bit check), (2) E289 fixed: 'bank sync done / failed / stopped', (3) hil.sh H5 sleeps through systemd and checks that the PC slept - rerun ALL PASS, slept 48 s (E290; the first --sleep run had not slept). All Linux only, 305 unit tests, ctest 5/5; no CI build of the round yet. Reddit library report: reply sent, waiting for the reporter; the four test libraries removed from the Mac. Still not run: hil.sh --power (needs a switchable plug), a vX.Y.Z-rcN pre-release tag. E287 SOLVED 2026-10-20 ~14:31: REAPER/SWELL bug, reproduced without TI Revive (E291, round item 4). User note in README/site FAQ/README-linux.txt (site live 2026-10-20 via 1efad04 on main); Cockos report drafted (https://claude.ai/artifact/Vm7LSR65pJUDGLNBXgMRu1), the user posts it. Release prep 2026-10-20 ~16:40 (user: 'prepare the next release so we have everything on main'): local main reset to origin/main (its three HANDOFF commits were on the round already); native version 0.8.29, release notes in build.yml, 305 tests in README/site; risk.py: class C (version bump) -> Linux hil.sh cold/reopen/export, Windows + Mac cold and export by hand, reopen rotated out; spec scripts/checklists/round-0.8.29.json (L1 bank sync, W1/M1 classic oscillator display, M2 bank sync). CI test build run 39441322803 (version 0.8.29): linux, native-linux, binaries, msi, native-windows (unit tests) green; mac CANCELLED by GitHub ('job was not acquired by Runner of type hosted', no build started), reruns at 16:55 and 17:11 local cancelled the same way (no macos-15 runner within 15 min each; GitHub status: Actions operational; the same label built the 0.8.28 release at 10:39 local) - no Mac package yet, retry later, do not change the runner label without the user. Checklist published 2026-10-20 ~17:35: https://claude.ai/artifact/FPLV3yBfZW6E25dP2Mdx5P (db, marks in rounds/0.8.29/marks; Mac block says 'not ready yet' until the package exists, then republish the same file). Linux = CI build (install.sh as the user, no sudo; plug-in c77cea04.., broker cdf3d914.. = the tarball; 0.8.28 release plug-in backed up in ~/.vst3-backup/0.8.28-release; unpacked in ~/Downloads/TI-Revive-0.8.29-test). hil.sh on it (work/hil/20261020-165642): H1, H2 x2, H3, H6 ALL PASS (H3 export 1.00x, 45/45 notes, max 0.92 ms; H6 26/26); diag.log shows the new 'bank sync done in 33.4 s / 35.2 s' lines. MISTAKE: that first VM install went to win10 (scripts/vm.sh defaults to VM=win10; VM=win11 was not set): win10 got 0.8.29 via setup.exe /quiet (exit 0, C:\tirevive-test\0.8.29) and the Virus was attached to it briefly; the user noticed; Virus detached, win10 shut down - decide with the user whether to revert win10 to a snapshot. Then win11 (VM=win11): setup.exe /quiet over 0.8.28, exit 0, entries 0.8.29, plug-in 6ae8cade.. = the CI MSI (C:\tirevive-test\0.8.29); Virus attached ~17:45, MI_00/03/04 OK. Linux block: L1 marked pass, log 'bank sync done in 3.5 s / 6.5 s'. VM block 3/3 marked pass (WC1, WC3, W1); win11 diag.log: ready 1.6 s, 'bank sync done in 5.4 s', offline render paced, 'sync error shown (4 late events since Play)' during the render as in every VM round (E288, VM USB timing). Mac CI job: rerun 4 (17:30) cancelled the same way, no runner - 4 of 4 tries, try 5 (18:08-18:23) the same, GitHub status: no unresolved incidents; reruns stopped; user chose to try another label: macos-latest (6cf964f, run 39457214833) got no runner either (cancelled after 15 min, same message; all other jobs green), so not the label - reverted to macos-15 (fa7889d). No other run in progress in any derpsn repo. The release job needs mac (needs: [msi, mac, linux]): no release until GitHub gives a macOS runner. Billing: the user checked the usage page - macOS minutes Oct 20 gross $1.49, billed $0.00 (fully discounted), so billing is NOT the cause. The user reran run 39457214833 (commit 6cf964f, macos-latest; only the label differs from the branch): its log said 'about to start running on the hosted runner', then queued again at ~19:00. google/sbsim #126 (macos-13 retired) does not apply: macos-15 is supported and built 0.8.28 this morning. POSTPONED by the user to 2026-10-21. Run 39457214833 ended: mac cancelled again (7th try). GitHub support's answer (generic checklist) matches only queue/capacity; user: no free runners right now, no label change. Tomorrow: a NEW run on macos-15 (must include ee36986, the review fix after the test build, and the cleanup up to d275c1f: round items 5 and 6 - two stale path comments fixed after the C++ review (comment-only, the extra risk.py items skipped by decision), dead code removed, unused Python imports, vtiusbaudio.c/.h moved from src/wrapper to src/broker with a byte-identical vtibroker.exe, AGENTS.md's Mac path corrected (docs only); decided: Windows reopen/power/sleep that risk.py now adds are skipped this round, checklist unchanged). 2026-10-20 ~20:35 (user: try again): NEW run 39475965421 on 8c48c03 (macos-15, version 0.8.29; includes ee36986 and items 5+6), mac queued at the start; native-windows FAILED in scripts_py: my new test_handoff_hook.py (5 failures, bash exits 1 without output on the Windows runner), so msi skipped -> test skipped on win32 (Linux 51 tests OK); needs a new run for the MSI. 39475965421 cancelled (mac still queued); NEW run 39477408077 on b8eb7e3 (version 0.8.29) started ~20:50. NEXT: check run 39477408077 (before: 39475965421, 39457214833) (else rerun run 39441322803 or a new run on macos-15); when a Mac package exists: mac.sh push, unzip, add the install command to the checklist (republish the same file, marks stay), user tests M1 + M2 on the Mac; then merge round-0.8.29 -> main with -F <file>, verify, tag v0.8.29 separately, release CI, site check + rebuild, install the release on Linux/win11 (VM=win11!)/Mac. Local fallback if CI stays stuck: scripts/mac-build-release.sh on the Mac for the test; Mac block M1/M2 open, no Mac package. win10 reverted by the user's choice: snapshot-revert failed (the running snapshot pins the Virus at bus 1 device 5), so disk only: the user ran sudo qemu-img snapshot -a before-original-driver on win10.clean; booted once without the Virus: 0.8.29 test folder gone, entries TI Revive 0.8.17 (its pre-snapshot state), shut down cleanly. win11 stopped at 18:07:37 (not by me, no OOM in the kernel log; maybe the user); the Virus is back on Linux, broker socket started again. NEXT (old line): run green -> install on Linux, win11 VM, Mac; hil.sh on Linux; publish the checklist (capabilities db); after all pass: merge with -F, verify, tag v0.8.29. Before (round notes): **round 0.8.28 (branch round-0.8.28): new testing strategy (findings/testing.md, user: "start with step 1 and all others"), plus the review fixes for the dump pacing (two review rounds, clean), see below.** Testing strategy done 2026-10-19: risk classes A/B/C per path (scripts/checklists/risk.json, scripts/risk.py --round X: which core items by hand, which automated, which rotated; core.json items got "key", checklist.py "core_items" filter; unit test: every tracked file has a rule); automated Linux run scripts/hil.sh + hil_check.py (H1 cold, H2 reopen x2, H3 export + playback after it, H4 with --power CMD, H5 with --sleep): first run on the TI Desktop ALL PASS (jitter 0.03-0.15 ms, export 1.00x, 42/42 notes in H3); --sleep and --power not run yet at that time (sleep: first real run 2026-10-20, E290; power needs a switchable plug); rotation of reopen/power (no findings 0.8.23-0.8.27, yield table in testing.md); pre-release tags vX.Y.Z-rcN in build.yml (untested until a tag is pushed); mutation.py: first run (221 mutants, 19 device headers + ctrlguard/resetguard, copy of b7b9ed0^) started 2026-10-19 ~20:00, ~50 s per mutant; results in /tmp/claude-1000/-home-datoml-virus-ti-re/00756ea2-c6e6-4e70-a79e-7eb20f7d1883/scratchpad/mut/run.log and results.json (summary lines at the end) -> NEXT: copy the score and the survived mutants into testing.md "Mutation runs", add tests for the survivors; scripts_py in ctest (5/5 pass). C++ review 2026-10-19 (two cpp-reviewer agents, native/src/device + native/plugin/source; findings verified by hand for the HIGH ones): fixed H2 (send_state: project records through blocked_to_virus + one_message, 1433af6) and H1 (audio ring unmapped under process(): audiogate.h, 6846f2f); then (all 2026-10-19, TDD, hardware-checked on Linux) sendbudget tests + last_ns monotonic (mutation: every non-equivalent mutant killed), hil.sh H3 with a 4 s stop (nativehost --notes-off, hil_check --gap), H3 reconnect after the broker dies + control() clamp + dump/held reset (c8eb139), USB-MIDI parser restart at F0 + 64 KiB cap (5021d6b), MemorySync one request at a time (41672fe), hil.sh H6 (E284: 3/3 + full run H1-H3+H6 pass); editor drags one message per value (dragsend.h; a real drag on the next checklist), host values clamped + clock tempo (hostmidi.h); ALL review findings fixed 2026-10-19 (each verified in the code first; commits 1f2d32b M2/M4/M5/M7/M8, 9dbd210 M6, lows: rt overflow note-offs, scan_audio +66 frames, late control replies, pending_ coalesced, empty messages (ed8b751), editor: run-loop order, removed(), ~VirusEditor, open() result, ref, text edit removal; popups: X11 double destroy, desktop bound, Win32 class unregister (9f62cc4, Win32 only mingw syntax-checked); broker socket peer uid + shm fstat owner/size (46e4bcb, the test reproduced the SIGBUS); unchanged on purpose: wait_offline 20 s (intended); not confirmed by the reviewer, for the REAPER test: the pinned run loop of the first plugFrame after it closes). Final hil.sh run on the dev build: H1, H2 x2, H3, H6 ALL PASS; 292 tests, ctest 5/5. CI test build 0.8.28 STARTED 2026-10-19 21:57 (branch pushed, run 39315263357, version 0.8.28). **2026-10-20: run 39315263357 FAILED on native-windows only** (MSVC: broker.cpp:349 `std::min({..})` hit by windows.h's min macro, added in c8eb139; mac, linux, native-linux, binaries green); fixed as `std::min<size_t>({..})` like line 312 (reproduced with a min macro under g++: old form fails, new compiles; Linux build + ctest 5/5); needs a new CI run. Mutation run 2 (2026-10-20, findings/testing.md): 174 mutants of the send path and the device
headers, 30 survivors with missing tests now killed by 11 new tests (303 unit tests), 17 equivalent, 2 sanitizer-only;
user's order for today: mutation first, then the issue #9 changes (reply posted, close left to the user; DONE: E285, the Linux broker's
alternate-setting log names the interface, libusb's error and the hub hint; README, README-linux.txt and the site's FAQ
say the same - site not rebuilt), then the hardware runs I can do alone (hil.sh, the H3 series), then push + CI. Hardware 2026-10-20 (TI Desktop, dev plug-in 15779ed via --plugin, installed 0.8.27 broker = round broker but the hub log line): hil.sh H1, H2 x2, H3, H6 ALL PASS (max 0.44 ms, work/hil/20261020-082132); H3 series (E286): straight from the render into notes 5/5 first note +12-14 ms late, then exact; with the 4 s stop 5/5 clean. CAUSE: nativehost called the first real-time block at once after the render (one block early); it now waits one block period at the switch -> straight into notes 3/3 clean. The plug-in was never at fault; the 'post-export late note' item below is closed. CI run 39381740640 (91ff5a6): broker.cpp fix OK, but native-windows FAILED in vtitests: test_broker.cpp's three review tests (c8eb139/ed8b751) sat after the #ifndef _WIN32 block (FakeBroker is POSIX-only) - moved inside; resampler.h got <cstdint> (mingw syntax check of all round test files against Windows headers: clean except Linux-only test_resetguard); mac, linux, native-linux, binaries green. Needs a new CI run. CI run 39382517252 (ee8b5ce): everything builds; native-windows unit test hostmidi_clock_tempo_is_sane FAILED (clock_tempo(NaN) != 120): the VST3 SDK's SMTG_PlatformToolset.cmake sets /fp:fast (MSVC) and -ffast-math (Xcode = our Mac release build), so `!(v > 0)` no longer catches NaN (reproduced with g++ -ffast-math: NaN -> 999 BPM, velocity 127, bend 16383). Fix: hostmidi.h usable() checks the exponent bits; +-inf now also counts as no value (tests added). OPEN (not this round): accessfiles.cpp:192 std::isfinite on values from the Access files may be folded away under fast-math as well - check whether it matters. **CI test build 0.8.28 = run 39383301539 (0e35394) ALL GREEN** (unit tests on Windows too, MSI). Installed: Linux via install.sh as the user (no sudo needed; ~/.vst3 plug-in sha 995b0612.., broker 6c99ab7a.. = the CI tarball; unpacked in ~/Downloads/TI-Revive-0.8.28-test); win11 VM: setup.exe /quiet over 0.8.27 (upgrade, exit 0, entries 0.8.28, plug-in sha c63434d8.. = the CI MSI; C:\tirevive-test\0.8.28), VM running, Virus still on Linux; Mac: not reachable over ssh at 09:05 (asleep?), zip in the scratchpad ci-0.8.28/TI-Revive-mac. NEXT: the test round with the user, when they have time: checklist artifact https://claude.ai/artifact/ULjYFb8yaDSqbfiic6Qsum (spec scripts/checklists/round-0.8.28.json; core LC4, LC5 (sleep by hand: hil.sh --sleep not run), WC1, WC3, MC1-MC5; round items L1 drag, L2 popups, L3 REAPER close-first, L4 crash test E265/E277 (a) editor closed x3 (b) ReaSynth x3, W1 text edit rename, W2 Win32 popups + reload, W3 drag, M1 rename, M2 drag); marks in its db rounds/0.8.28/marks. Before the VM block: attach the Virus (vm.sh attach); Mac: push + unzip the zip when it is awake. L4 result (E287): (a) editor closed 3/3 no crash; a 4th render with the editor open crashed REAPER (same SWELL/GDK chain, gdb stopped it = the 'hang'); 5/5 crashes with the editor open. (b) ReaSynth only: 3 offline + 2 online renders, no crash. Pointer off vs over the editor at the dialog close: 3 + 3 renders, no crash (intermittent; the pointer does not decide). Editor open/close now logged (dev build; ~/.vst3 = dev build with it, the CI build backed up in ~/.vst3-backup/0.8.28-ci; rollback: copy it back). **Linux block 6/6 marked pass** (LC4, LC5, L1-L3; L4 marked pass by the user 'because we still do not know', see E287). LC5 log: suspend 09:30:22, wake 09:30:40 with an xHC reinit, stall seen at once, device disconnected/reconnected, ready 2.8 s, marker; a few Sync Information Events per Play before and after the sleep (1-4 since Play). Virus attached to the win11 VM 2026-10-20 ~09:50 (all its interfaces OK in Windows); **Windows VM block 5/5 pass (E288)**: WC1, WC3, W1-W3; WC3 export as in every VM round (183 sync events during the render, page cleared by Play; 201/256 on time, 55 late 3-48 ms = VM USB timing). **macOS block 7/7 pass (E289)**: MC3 export exact (256/256, jitter 0.19 ms, 0 sync events), reconnect and sleep ready 3.8 s. **ROUND 0.8.28: 18/18 marked pass** (Linux 6, VM 5, Mac 7; L4 = the REAPER crash, cause open, E287). Not in the tested CI build: 8e856cf (editor opened/closed log lines, Linux dev build only) and the nativehost fix (not shipped). Open LOW: 'bank sync done' logged after an aborted sync (E289). User finding (Mac, M1): the patch name text overflowed its field. Cause: the NameEdit fields are 60 x 10 px (read from the layout at runtime: 32 of them) and the name edit kept VSTGUI's default 12 pt; only the REMOTE fields got a fitted font. Fix: textfield.h text_field_font(h) = max(7, h - 1) for every name edit (9 pt for the name; test first); not checked on Linux (the Virus was on the Mac: the editor shows the not-found page without it); needs the Mac (M1 again) on a new CI build together with 8e856cf. CI run 39392580073 (2f3b5ab) all green; Mac: installed 10:29:34, 'editor opened/closed' in its log (= the new build), user: the name fits, rename works. Release prep 2026-10-20 (site check per the skill): TESTS 262 -> 304, README 304; timing rows Linux -> 0.8.27 REAPER render E278 (63/63, 0.16 ms, -72 ppm), macOS -> 0.8.28 MC3 E289 (256/256, 0.19 ms, +1 ppm) - the 0.01/0.02 ms rows were the first stage-3 measurements of 2026-10-14 (E140/E141); Windows rows unchanged (no newer real-PC run); jitter range 0.04-0.33 ms; plug-in version 0.8.28; release notes in build.yml. Site not rebuilt yet (by hand after the release). NEXT: release decision with the user (merge round-0.8.28 -> main with -F, verify, tag v0.8.28, release CI, site check + rebuild by hand, install the release on Linux/VM/Mac). NEXT (test round 2026-10-20, user): check the run is green, install on Linux + win11 VM + Mac, checklist via scripts/risk.py (Windows/macOS: cold, export, reopen; a real editor drag; text edit rename on Windows/macOS; REAPER duplicate-track close-first). **Post-export late note (H3, CLOSED 2026-10-20: a nativehost artifact, E286):** the first real-time note right after an offline render that goes straight on into real-time blocks (nativehost --offline 12 --offline-end 36) is 12-14 ms late at 36.06 s, intermittent: local builds v0.8.27 1/2, 4d605c3 2/2, 1446ac8 1/2, 22b9364 2/2, round HEAD 5/5, release 0.8.27 CI build 0/2 (luck). First reported as a round regression (wrong, corrected after the bisect). Open: whether a DAW ever does this switch without a stop (else a nativehost artifact; E243 is the Mac case after an export); hil.sh H3 now stops 4 s between render and playback like a DAW (nativehost --notes-off, hil_check --gap): 2/2 clean with the round build. Before: **v0.8.27 released 2026-10-19** (tag v0.8.27 on merge 957c383, release CI run 39288097657 all green, 6 assets; a first tag push on the old main commit 0f297c4 (failed merge command) started run 39287940425, cancelled within a minute, all jobs cancelled, no release, tag deleted and set again on the merge; site rebuilt by hand, run 39288880734, live shows Download 0.8.27 / 262 tests / the start-up page). Round 0.8.27-2 checklist: Linux 10/10, Windows VM 7/7, Mac 8/8, real PC skipped (E278, E282, E283). New in 0.8.27: patch changes like the original (cable 3, 64-byte pieces, notes between on cable 5, automation held; E268-E271, E274), F2 + the +1 tick at Play/loop (E275; the 16 6F part messages left out, E280), start pages Starting up / Syncing only on a real sync (E273), first notes at 48 kHz (E279, E280), meters during automation and the sync note (E282), T1-T8 of the first pass (E261-E264). Test machines: Linux = release (install.sh, plug-in + broker = release); win11 VM = release (silent, test build uninstalled first, plug-in 16:48 UTC); Mac: release unpacked in ~/Downloads/TI-Revive-0.8.27-release-mac, the user runs the sudo installer (it has the third test build). Open: REAPER crash after a GUI render on Linux (E265/E277, REAPER's SWELL, deciding test not run); the Atomizer part messages (E276) if an Atomizer user reports; real PC timing._
39131364441 green, 6 assets; site rebuilt by hand, run 39131769375, live shows 0.8.25 / 220 tests / original VST3
projects). New: original VST3 projects open (second class with the original's id, E221-E223; VST2 not), Revert to /
Recently Used (E220), Create Empty Library (E217), Windows 10 menu look (E219), plug-in version 0.8.25. Test machines:
win11 VM = release (silent, 0.8.25c uninstalled first, plug-in 13:27 UTC); Linux: release unpacked in
~/Downloads/TI-Revive-0.8.25-release, user runs install.sh (~/.vst3 still dev build 9dcd74f until then); Mac: release
unzipped in ~/Downloads/TI-Revive-0.8.25-release-mac, user runs the sudo installer; real PC still 0.8.24 (0.8.25b setup
in C:\tirevive-test\0.8.25b, release not copied). **Next round: 0.8.26 started (branch round-0.8.26), see below.** Open: Snow original id 1B4C380B.. as a
third entry (only if Snow users ask), E175 (not reproduced in E226, watch), Mac double notes E209 (7) (not reproduced in E225, watch). Not relevant (user 2026-10-18): VST2 projects of the
original (no converter, no VST2 build). Chronological notes: [HANDOFF-history.md](HANDOFF-history.md). Enforced since 2026-10-20: the Stop hook .claude/hooks/handoff-check.sh (registered in the git-ignored .claude/settings.local.json) blocks the end of a turn while commits sit after the last HANDOFF commit (test: scripts/tests/test_handoff_hook.py).
Evidence: [evidence.md](evidence.md) (E001-E291), protocol: [spec.md](spec.md); the native plug-in:
[../native/docs/](../native/docs/architecture.md) (its development log: native/docs/status.md)._

## Open report: personal library patches silent (Reddit, 2026-10-20)

User from the Cubase issue (Reddit chat): "non RAM/ROM banks (personal libraries) aren't producing any sound".
Code: library patches go the same way as RAM/ROM (load_patch -> load_dump, one single dump to the part's edit
buffer); all TI library files here have device id 0x10, bank byte 1, versions 8-12. One known difference to the
original: it rewrites bytes 10..12 before sending a library patch (E156, 0x180083ae0), we send them as in the file.
**Not reproduced** (user test 2026-10-20, Linux, REAPER, installed release 0.8.28): 2-3 patches each from OS5, OS4,
OS2 New ... patches and Patches For Virus Tutorial (Virus C format) via LOAD > Personal Libraries, all played.
Mac (same day, Cubase 15, macOS 26.6.2, second 0.8.28 test build 2f3b5ab, the four libraries copied over): user
"all patches worked in cubase too". diag.log: one session 11:14:30-11:16:07, Virus ready 1.8 s, bank sync 3.5 s,
audio drops 0, peaks on the Virus' outputs while playing, no errors. Patch loads are not logged (no log line in
load_dump). Project recall then tested (Mac, Cubase 15, same build): "Envy" (OS5 New Features patches, program 21,
format 12) loaded on part 1, project saved, closed, reopened; user: "patch played after reopen". diag.log 11:26:28:
setState 245 records, part 1 "Envy", state sent (244 messages), Virus ready 2.0 s, peaks while playing, drops 0.
The four test libraries were removed from the Mac's ~/Documents/Access Music/Virus TI/Patches afterwards
(My Patches.mid, New Library.mid kept). Reply to the reporter sent by the user 2026-10-20 (Reddit).
NEXT: wait for version, model/OS, where the libraries come from, LOAD vs project recall, one affected .mid file. No evidence entry until there is data.
**Update 2026-10-20 evening:** reporter: "it seems to be fine today" (silent libraries not reproducible on their side
either; closed unless it returns). New report from them: a personal library patch loaded in the browser, then
program +/- on the Virus -> the next RAM/ROM preset is loaded, not the next library patch; "the original software
didn't work this way". Ours: load_reported_presets loads (Bank, Patch) as the Virus reports them from the bank cache.
Static (REA/Ghidra, Virus Control.dll 5.1.7 x64, sha256 a622f9a2..): the original does the same on a report -
0x180083ae0 looks up (Multi Part Bank + 1, Patch) via 0x18008daa0, which only walks the repository's numbered banks
(the Virus' RAM/ROM, CVirusBank); listener offsets in 0x180081980 are object offsets - 0x10 (ccd98/ccda0 = Multi Part
Bank/Patch at ccda8/ccdb0 in 0x18006ed70). So any difference must come from the Virus: before sending a library
patch the original sets byte 10 (Single Patchmanagement/Flags) = 0x20 and leaves bytes 11/12; a RAM/ROM patch gets
00/bank/index (E156). We send library bytes 10..12 as in the file. Hypothesis (LOW, untested): with flag 0x20 the
Virus' +/- does not step its RAM/ROM bank (or reports differently). Not checked: the firmware's handling of the
flag; what the Virus reports on +/- in either case. Next (needs the user + TI2): read-only observer while +/- after
a library load, then the same with a dev build that writes byte 10 = 0x20 like the original.
**Resolved 2026-10-20 ~21:20 (E292):** tested the original in the win10 VM (Access driver 2.9.66 reinstalled there
with pnputil from the extracted MSI, drivers only, no updater; WinUSB packages removed; snapshot pre-access-driver-2
before): the original also steps through the Virus' RAM slots on +/- after a library patch (user: "It never switched
the preset on the library"). Not a TI Revive bug; no code change. Reply to the reporter: user.
Web text the user found ("Allow hardware control over the librarian patches") checked: not in the 5.1.7 DLL or
manuals (E292). VM: win10 running with the Virus attached - close Live, detach, shut down, start the broker socket.
Workaround the user confirmed in the original (2026-10-20): Browser page, a RAM bank in the second (lower) browser,
drag library patches into its slots, then +/- on the Virus steps through them. TI Revive: one browser only
(Browser 0), browser drag goes onto part strips only; per patch: load + SAVE into a RAM slot (store_patch). Feature
gap (not scheduled): second browser + drop onto a RAM slot - look up the original's layout/behaviour first (REA, VM).
Recording attempt of that drag (2026-10-20 ~21:31-21:45): Virus Control showed "No Virus TI connected" after a
Live restart (stale WinUSB devnodes removed with pnputil /remove-device, did not help before a VM reboot); the user
power-cycled the Virus, reattach -> two bug checks 0x139 (param 3, list corruption) in the VM at 21:41/21:42 host time
(minidumps 102026-5062-01, 102026-4203-01); after the reboot Control Center showed a wrong model / old firmware / OS
unknown although the Virus' USB identity was normal (Access Music / Virus TI / bcd 0x0300). Captures orig-libdrag-1/-2
and screenshots in work/libdrag/ hold no drag (not connected). Next time: dual-boot PC or a clean VM boot with one
attach; never power-cycle the Virus while it is in the VM.
**New reference VM `win10ref` (user, 2026-10-20 ~22:00):** fresh Windows 10 Pro (de-DE, Windows_10.iso) only for the
original 5.1.7 with the Access drivers; scripts/win10ref/ (create-vm.sh: q35, legacy BIOS, 8 GB, 80 GB SATA, e1000e;
autounattend.xml from win11's, MBR; make-unattend-iso.sh; install-access-reference.ps1 = MSI copy with only
ConnectDevicesDialog/InstallRunOnceEntry/ApplyOptimization/UpdateFirmware off, drivers stay). vm.sh: VM=win10ref, user
vti. Ableton 11.3.43 zip + web installer copied from win11 to work/vm-transfer/ (sha256 65d3bc8f.. same as on win11).
Plan: install -> SSH -> Access suite (Virus NOT attached) -> user installs/authorises Live -> snapshot ref-clean ->
one attach. The old win10 VM stays until win10ref works (user decides). Virus checked on Linux after the VM crashes:
tirevive-cc info OS 5.1.7.00, RAM A-D answer (libvirt left /dev/bus/usb node root:root; user ran udevadm trigger).
win10ref state 2026-10-20 ~22:00: Windows 10 Pro 19045 de-DE installed unattended, SSH ok; Access suite 5.1.7 via
install-access-reference.ps1 (exit 0, UpdateFirmware + InstallRunOnceEntry skipped; no Access Run/RunOnce/task).
The MSI's DIFx driver step failed silently (e0000242, publisher not trusted - /qn cannot show the prompt): the
Kemper signer (catalogs Valid) put into LocalMachine\TrustedPublisher, then pnputil /add-driver for VirusUSB,
vtiaudio, vtimidi - now part of the script. Virus not attached yet. Next: Live 11 (user), snapshot ref-clean, attach.
~22:10: Live 11.3.43 installed by the user, snapshot ref-clean (no Virus), Virus attached once (VirusUSB/vtiaudio/vtimidi
OK); user doing the first sync, then clean quit -> detach -> snapshot ref-synced. CI run 39477408077 (b8eb7e3, 0.8.29)
ALL GREEN incl. mac (earlier mac jobs: GitHub runner queue). Mac zip pushed + unzipped in ~/Downloads/TI-Revive-0.8.29-mac
(sha e66eec0c.. = CI); user: M tests not now, round continues with a new +/- related change first.
**RULE (E293): never `virsh screenshot` a VM that has the Virus attached under the Access driver** - it stalls the
USB passthrough and the guest dies with 0x139 (the cause of tonight's crashes, not a corrupt VM). win10ref has a
task for screenshots inside the guest: `VM=win10ref scripts/vm.sh ps 'Start-ScheduledTask vti-shot'` -> C:\ref\shots\
<HHmmss-fff>.png (C:\ref\shot.ps1, task vti-shot, interactive user vti), then vm.sh pull. Snapshots win10ref: ref-clean
(no Virus), ref-synced (Live 11 + Virus Control synced, cache written, Virus detached; the vti-shot task came after).
Original's BROWSER page (screenshot 2026-10-20): two panes - upper with path dropdown, up/down, CATEGORY, search; lower
"Virus TI2 (Virus TI2 Desktop)/RAM A" with dropdown and up/down only; 8x16 entries each. RAM A slots 1-3 hold
library patches the user dragged in earlier with the original (survived a power cycle).
**Verified 2026-10-20 ~22:35 (E294):** the original's library -> RAM drag works as the web text said: drop = one
single dump to (bank, slot) per patch, file bytes 10..12 unchanged, no read-back, multi-drop fills consecutive slots;
+/- on the Virus then plays them from the original's RAM cache. NEXT (feature, option 1): second browser pane + drop
onto a RAM slot in TI Revive (TDD; store path like store_patch, cache update, rule 2: only user-dropped stores).
Step 6 done: Live restart in win10ref connects again (the old VM's 'not connected after restart' was not a 2nd-session
rule - probably the screenshot loop / driver state). Virus detached, win10ref off; RAM A checked on Linux after a power
cycle (13 changed programs, dropped names in place; work/libdrag/ramcheck). Broker socket started again.
**Round 0.8.29 item 7: browser drop onto RAM (E294), dev build 2026-10-20 ~23:00, NOT tested by the user yet.**
Correction: TI Revive already had both browser panes (Browser 0/1); missing were drop + undo + menu. New
native/src/model/bankdrop.h (BankDrop: consecutive slots, cut at 128, only bytes 7/8 + checksums new, one-level
undo, refused when an overwritten slot is not cached; BrowserSelection: click / Shift range / Ctrl toggle) + 7 tests
(312 total, ctest 5/5). Plug-in: drop_to_ram (sources upgraded like a load; refused while the bank sync runs),
undo_drop, send_ram_writes (dumps + cache + cache file); store_patch and a sync that fetched changes forget the undo.
Editor: multi-selection framed, drop onto a RAM slot of the OTHER pane, right-click browser menu (Undo works;
Download bank / Sort / Delete selection greyed). Not verified with the original: drop into the same pane (ignored),
ROM/library targets (no drop), Ctrl selections with gaps (ascending order), older library formats (upgraded first).
Installed: ~/.vst3 = this dev build (sha a3d67ecf..), previous in ~/.vst3-backup/0.8.29-before-drop. NEXT: user smoke
test in REAPER (Linux), then risk.py / checklist item, CI.
User smoke test 2026-10-21 ~00:20 (REAPER, dev a3d67ecf): drop 1 onto RAM A 18 and 5 onto 19-23 (diag.log), undo
restored the 5 (one level: the earlier drop on 18 cannot be undone - OPEN: check in win10ref whether the original
undoes more than one step), +/- on the Virus to RAM A 18: right preset. Bug (old, also the part-strip drag): cursor
stayed the copy hand after a drop until leaving the window - X11 VSTGUI delivers mouse-up inside its pointer grab;
fix 79e2286 resets the cursor via doAfterEventProcessing; installed dev 5d9ec5c9.., user re-test pending.
User re-test: cursor back to the arrow after a drop (also part strip), same-pane drag does nothing - pass.
win10ref 2026-10-21 07:11 (E295): the original's undo TOGGLES within the last operation (old <-> dropped, any number
of times), empty library entries drop nothing -> bankdrop.h take_undo now swaps undo/redo (test first, 312 green).
Reddit (Deranger604, 2026-10-21 2:30): 'it doesn't stick with the bank loaded in the browser ... maybe it was always
like that' -> E296: manuals + web say nothing of the kind, the Tutorial describes the RAM favourites drag; reply drafted
(user sends). Tutorial lists more browser functions (E296 (2)): same-bank drag = SWAP (ours ignores it - fix), copy
library->library, Alt+Click rename, delete selected (libraries), sort. NEXT: record those in win10ref, then implement.
Recorded 2026-10-21 07:27-07:42 (E297): swap single/block (source slots first), Create Empty Library only in SAVE,
copy into a library (file only), Alt+Click rename (RAM: one dump; library: file), delete leaves a gap, sort closes it
and renumbers, RAM -> library keeps the dump (program byte too). win10ref off, Virus on Linux, broker socket active.
NEXT (implementation, TDD, in this order): (1) same-pane drag = swap (single + block) instead of ignore; (2) drag into a
library = copy (file); (3) part strip -> RAM slot = store; (4) Alt+Click rename; (5) library menu: Delete selection
(gap) + Sort alphabetically; RAM Sort only with care (128 dumps); (6) check our Create Empty Library placement (E217
says the bank/patch menu; the original shows it in SAVE only). Download entire bank: = re-read that bank (read-only).
Done 2026-10-21 (dev, not user-tested yet): (1) swap c9608d7 (same RAM bank, either pane; refused when the sets
overlap / run past 128); (3) part strip name -> RAM slot = store_patch; (4) Alt+Click rename in RAM (renamed(): name
only + checksums, one dump; forgets the undo); (7) Create Empty Library only in SAVE (README + site texts updated, also
the new browser bullet). 316 tests, ctest 5/5. Left: (2)/(5) library copy/delete/sort/rename - need one more
recording: is a library position the program byte (delete left a gap with 2 dumps in the file; G kept byte 27)?
(6) Download entire bank (forced re-read past the fingerprints) - greyed for now.
User test 2026-10-21 (REAPER Linux, dev d2576e9c = 0012093): swap single + block, undo toggled 5x (6 slots each),
Alt+Click rename in RAM (the Virus shows it after a program change, E157), part name -> RAM 30 = stored, Create Empty
Library only in SAVE - ALL PASS. User wish: a drop indicator while dragging like the original's (needs a screenshot of
the original mid-drag). NEXT: one win10ref recording for libraries (position = program byte? where a drop lands in a
library, onto an occupied library slot, mid-drag indicator) -> library copy/delete/sort/rename; Download entire bank;
then CI build + round checklist (Windows, Mac).
Library recording done 2026-10-21 08:14-08:22 (E298): position = program byte (gaps), drop overwrites and sets the
byte, file sorted by byte, undo covers library ops, swap in a library, delete removes the dump; drag highlight =
filled 66,111,204 + white text on source and target. BUG to fix first: our library reader uses file order.
FIXED (test first, 317 green, ctest 5/5): library.cpp places each dump at its program byte (gaps empty; file order
when bytes repeat or > 128 dumps), save_library leaves gaps out; tirevive-cc list/export skip gaps. Effect: the
factory libraries OS4/OS45/OS5 New Features now show the original's gaps (OS5: 54-63 empty, *Templates at 64 - as in
the original's screenshot); before, TI Revive showed those patches shifted up. Library ops (copy/delete/sort/rename,
undo for libraries) and the drag highlight: next.
Done (dev, not user-tested): LibraryEdit (drop/overwrite, swap, remove = gap, sort, rename) + library undo in the
shared BankDrop undo (toggle), plug-in drop_to_library/swap_in_library/delete_from_library/sort_library/
rename_in_library (file only, edit_library + save_library_file; SAVE to a library forgets the undo); editor: drops
onto library slots, Alt+Click rename in libraries, browser menu Delete selection (library, with a selection) + Sort
(library; RAM sort still greyed), drag highlight (SelectedFont + fill 66,111,204 on source and target). 321 tests.
Decided without evidence: a RAM -> library drop takes the target position (E297 (G) kept 27 - target not known).
User saw doubled browser numbers (b7d188fd): E299 - the numbers are baked into BG_Page_Browser and show through
the translucent table; our own digits (since baa6282) doubled them in both tables. digits() removed (test first).
2026-10-21 08:39: the Virus hung on USB again (E300 = E136 repeated, after a give-back reset + snd-usb-audio
hand-over; first write of the next broker failed, then error -71) - user power-cycles. Own round item: look at the
give-back reset in vti_close (rate-limit / skip / no snd-usb-audio rebind) - evidence first, not mixed into the browser work.
User test 2026-10-21 ~09:05 (REAPER Linux, dev 30f53946 = 5bafadd): numbers single (E299; the tight 3-digit numbers
in the last columns are the original's look - user: leave it), OS5 gaps like the original, drag highlight, drops into
libraries (LinuxTest/LinuxTest2), undo toggles, swap in RAM, rename RAM + library, delete selection (gap), sort -
ALL PASS ('all tests done. Looks good!'). Only LinuxTest*.mid changed among the library files; RAM A index 0, 1, 7, 18
touched by the test. Open in round 0.8.29: Download entire bank (greyed), RAM sort (greyed), give-back reset (E300),
then CI build + round checklist (Windows VM, Mac) + Reddit reply.
Download entire bank recorded 2026-10-21 09:45 (E301): 128 x (74 01 7E 04 + 30 0b pp -> 10 0b pp), ~1 s each, always all
128, entries emptied then filled; RAM backup before (work/libdrag/ram-backup-20261021-0921, all 4 banks). Next: RAM sort
recording (user picks a bank), then implement both.
RAM sort recorded 09:51 (E302): only changed slots written (RAM B: 4 dumps), no question, no undo afterwards; the menu
HIDES entries that do not apply (no greyed Undo). Implement: Download bank (E301), RAM sort (E302), menu without greyed
entries; then VM off.
Done (dev, not user-tested; 324 tests, ctest 5/5): MemorySync::read_bank (E301: rise 74 01 7E 04 + 30 -> 10 per
program, all 128, stops on cancel/no reply) + VirusPlugin::download_bank (own thread, bank emptied then filled, old
cache kept for unread slots, joined in terminate); BankDrop::sort_ram (E302: changed slots only, forgets the undo) +
sort_ram_bank; browser menu leaves out entries that do not apply (no greyed ones). README + site updated. VM off.
User test 2026-10-21 ~10:13 (dev 54799088): menu ok, download RAM B 127 s (original 131 s) ok; SORT WRONG - byte order,
wrote 85 slots of RAM B (E303: the original ignores case). Fixed (test first, 325 green); re-sorting RAM B with the fix
puts the original's order back. User idea: show the download's progress / time left on our LCD (the original shows it
only on the Virus) - optional, ask.
User: yes -> download_progress() ('37 / 128, about 1:31 left', test) on our LCD while downloading, then 'Download
done/stopped'. README/site mention it.
User test 2026-10-21 ~10:15 (dev c20a6678 = af82f34): sort RAM B with the fix wrote the 85 slots back, our cache =
the original's order (0 differences); sort again: 0 slots; download 128 s with LCD progress ('LCD looked good'), the
bank read back from the Virus = the original's order, no empty slot - ALL PASS. The browser now has every function of
the original's browser we know of (E294-E303). Still open in round 0.8.29: E300 give-back reset; push + CI build;
round checklist (Windows VM, Mac); Reddit reply; RAM A/B contents back to the user's pre-test state (old caches).
NEW round item (user 2026-10-21): issue #10 (Reddit report, reporter not named): knobs turned on the TI2 are not added to
Ableton Live's automation list (Configure mode) - the plug-in window's changes are; once configured, Live records the
hardware knob. Code: UI -> beginEdit/performEdit/endEdit (virusplugin.cpp ~1851), panel -> only outputParameterChanges in
process() (~1388). NEXT: REA on the original (how it hands panel changes to the host), evidence, then fix with the rate
limit and no echo back to the Virus; test in Ableton (Mac + win11) Configure mode.
#10 done (dev, not user-tested; 330 tests, ctest 5/5): E304 (REA sub-agent): the original sends begin/perform/end with
a 250 ms end via its VST2 shim. New model/hostgestures.h (+4 tests): device_change -> tick (UI thread: beginEdit,
setParamNormalized+performEdit latest value, endEdit 250 ms after the last change), echo() filters the value the host
hands back (while open, 500 ms after), flush on editor close. Plug-in: while an editor is open, panel knob reports (not
Bank/Patch) go to gestures_ instead of pending_; process() skips echoes; editor timer calls tick_gestures(). LIMIT: with
the editor closed the old path (output parameter changes) - no UI-thread timer without an editor on Linux; a plug-in
timer for Windows/macOS could follow. Test: REAPER 'last touched parameter' (Linux), Ableton Configure (Mac, win11).
User test 2026-10-21 (REAPER Linux, dev 869f7690 = a81c568): knob on the Virus -> REAPER 'last touched' shows it; turning
smooth (no echo jitter); Touch automation recorded from the Virus knob and played back - PASS. Ableton Configure mode
(the reporter's case) still to test on the Mac / win11 in the round test.
2026-10-21 ~11:28: round-0.8.29 pushed (7a4b98e), CI run 39577249656 (version 0.8.29) started. NEXT: when green -
install on Linux (CI build), win11 VM (silent setup), Mac (push zip); round checklist incl. browser functions and
Ableton Configure mode (#10); meanwhile E300 evidence on Linux.
E300 analysis 2026-10-21 ~11:40 (logs only): today ~20 give-back cycles fine (gaps 35 s..1 h), one failed; the resetguard
(E136, RESET_GAP_S) was not involved (4 min since the last reset); no kernel/PipeWire/WirePlumber messages between the
hand-back 08:35:32 and the failed first write 08:39:54; the kernel's own resets failed too (the device's USB function
hung). Idle card now: PCM closed (PipeWire node suspended). Trigger unknown - options for the user: (1) passive flight
recorder in the broker (recommended), (2) longer idle exit (fewer hand-backs, Virus later back as Linux sound card),
(3) note only.
CI run 39577249656 (2940ef8.. = round tip 7a4b98e + HANDOFF, version 0.8.29) ALL GREEN. Installed: Linux = CI
(install.sh, plug-in dc13ca8b.., broker cdf3d914..; dev 869f7690 in ~/.vst3-backup/0.8.29-dev-869f7690); win11 VM =
setup.exe /quiet exit 0 (C:\tirevive-test\0.8.29-ci; bundle entry visible, MSI entry SystemComponent=1), VM running;
Mac: unreachable (ssh timeout) - push work/ci/39577249656/TI-Revive-mac zip when awake. Packages in work/ci/39577249656.
Checklist republished 2026-10-21 ~11:41 (same link, version 2): https://claude.ai/artifact/FPLV3yBfZW6E25dP2Mdx5P - spec
scripts/checklists/round-0.8.29.json: L1, L2 (browser + #10 short on CI); VM WC1, WC3, W1, W2 (browser), W3 (Ableton
Configure #10); Mac MC1, MC3, M1, M2, M3 (browser), M4 (Configure #10). hil.sh running on the CI build.
hil.sh on the CI build 2026-10-21 11:39: H1 cold, H2 reopen x2, H3 export, H6 broker kill ALL PASS (max 1.20 ms, jitter
0.41 ms; work/hil/20261021-113937). Linux core items done; L1/L2 by the user.
Mac awake 2026-10-21: CI zip pushed + unzipped in ~/Downloads/TI-Revive-0.8.29-ci2-mac (sha 8cc52f81.. = CI), diag.on set,
old diag.log -> diag.log.pre-0.8.29-ci2; the user runs the installer.
Mac install 11:48: receipt 0.8.29 but VST3/AU NOT replaced (old 0.8.29 test build of yesterday, E305: BundleIsVersionChecked
default) - workaround given (sudo rm -rf both bundles, install again); fix in mac-build-release.sh (version check off, needs
the next CI package to verify).
Fix committed in c3dda38 (1bde9ed had only E305/HANDOFF - the script edit was blocked by the gate hook). NEXT: user
removes both bundles + installs again; Claude checks VST3 = 44e2d989.., AU = a7d8a2fe..; then Mac items MC1, MC3, M1-M4
after Linux (L1, L2) and the VM (WC1, WC3, W1-W3). Branch not pushed since 2940ef8.
Mac reinstalled (bundles removed first): VST3 44e2d989.., AU a7d8a2fe.., broker e234fccc.. = the CI package (11:34 build).
VM 2026-10-21 ~12:06: W2 pass; W3 FAIL - Configure added Filter 1 Cutoff AND Voc CarrCntFrq (both on CC 40, E306). Fixed (test
first, 331 green): panel reports filtered by their condition. WC1/WC3/W1 marks are from 2026-10-20 (older build) - redo.
New CI build for the VM/Mac re-test (also carries the E305 package fix).
Pushed; CI run 39582553971 (version 0.8.29) started 2026-10-21 ~12:10. NEXT: install on win11 (setup.exe /quiet), Linux
(install.sh), Mac (push; the package must now replace the bundles by itself - verifies E305); redo W3, WC1, WC3, W1.
CI 39582553971 ALL GREEN. Installed: win11 setup.exe /quiet exit 0, plug-in c45840bf.. = the new MSI's (Windows replaces
same-version files; C:\tirevive-test\0.8.29-ci3); Linux install.sh, plug-in 90cf53f7..; Mac: pushed + unzipped in
~/Downloads/TI-Revive-0.8.29-ci3-mac, user installs WITHOUT removing bundles first (E305 check). Packages work/ci/39582553971.
VM block DONE 2026-10-21 ~12:20: W3 pass on the new build (10:18Z, installed 10:17Z: only Filter 1 Cutoff added); WC1, WC3,
W1 redone 10:12Z on CI 39577249656 (no start/audio change since); W2 pass. WC3.wav 256/256, -32 ppm, sd 9.14 ms, max 51 ms
(VM USB timing as before; work/hil/vm-0.8.29). NEXT: Virus to the Mac, install ci3 without removing bundles (E305 check),
MC1, MC3, M1-M4.
Mac 2026-10-21 ~12:16: installed ci3 over ci2 WITHOUT removing bundles: VST3 b3706d1a.., AU a3526065.., broker e234fccc.. =
the package (E305 fix VERIFIED). User testing MC1, MC3, M1-M4 now (diag.on set).
M3 (Mac): 'libraries not showing', Create Empty Library saved MacTest.mid to ~/Downloads. Found: our library_dir() on macOS =
~/Documents/... only (has My Patches + New Library); the factory libraries are in /Library/Application Support/Access
Music/Virus TI/Patches (Access installer; Tutorial's Mac path) - never read. The original has 'Personal' and 'Public
Libraries' (E217). Possibly also no Documents access for Live (TCC) -> asked the user. REA sub-agent running on the
original's library roots (Windows + Mac). Maybe the cause of an earlier user report of missing libraries.
E307 (REA): three roots - Personal = Documents, User = %APPDATA% / ~/Library/Application Support, Public = C:\ProgramData /
/Library/Application Support (+ '/Access Music/Virus TI/Patches'); each its own submenu; Create Empty Library always saves
to Documents. DONE (user ok, 4b819dd): library_roots() reads all three (Windows/macOS; Linux Personal only), each its
own menu in LOAD/SAVE/browser + bank title; Personal always shown (Create Empty Library), User/Public only with a library.
Test library_roots_three; 332 tests, ctest 5/5 on Linux. Not run on hardware yet. Next: CI build, Mac re-test of M3.
Still open: Create Empty Library's dialog on the Mac opened in ~/Downloads (Live's Documents permission? asked the user).
CI run 39619240510 (4b819dd) all green (mac job needed one rerun: no runner, 'capacity constraints'); Mac installed
17:13:34, receipt 0.8.29, VST3 c92d9622.., AU 1be78fe9.., broker e234fccc.. = pkg payload. Live has Full Disk Access (user),
so not TCC; VSTGUI's NSSavePanel sets directoryURL correctly -> M3 re-test: check the 'Where' menu before Save.
Mac pushes now also get INSTALL-COMMAND.txt + 'Install test build.command' in the extracted folder (user request).
MAC BLOCK PASSED (E308): MC1, MC3, M1-M4 all pass; M3 re-test on 39619240510 pass (Public Libraries, MacTest.mid in
~/Documents/...). Checklist: all 13 marks pass (L1, L2, WC1, WC3, W1-W3, MC1, MC3, M1-M4). Not tested: the E307 change on
Windows (menus from %APPDATA% / C:\ProgramData) -> checked by the user on win11, CI 39619240510 (setup.exe /quiet exit 0,
C:\tirevive-test\0.8.29-ci3): LOAD + browser library menus 'works'. NEXT: release
decision with the user (user: release now, then a new round); site check 2026-10-21 (agent + spot checks): TESTS 305->332, Mac timing
row 0.8.29 (E308, 0 ppm, max 1.35 ms), README status row 0.04-0.33 ms / -72 to 0 ppm, new lines for #10 and the library
folders (site + README identical), README got the 'Presets and soft knobs' line, release notes rewritten; site built locally,
not deployed. RELEASE: merge 98e12e6 (round-0.8.29 -> main, -F, 2 parents, tree = round), main pushed, tag v0.8.29
on 98e12e6 pushed separately, release CI run 39626672973 running. NEXT: release green -> site rebuild by hand, install the
release on Linux, win11 (VM=win11), Mac (+ INSTALL-COMMAND.txt); then a new round. Release CI: mac job retried by a loop (no runner, 'capacity constraints'; try 1 cancelled 17:48).
ISSUES (gone through with the user 2026-10-21): replies drafted in work/issue5-reply.md (close), issue10-reply.md (close;
user test win11 VM: Configure opens the plug-in window itself; configured parameters follow the Virus knob with the window
never opened - log: no 'editor opened' in that session), issue13-reply.md (video analysis, retest 0.8.29; the Clock RX question dropped: GlobalInit sets 'Sync to External') - ALL posted only
after the release is published (user). #7 stays open. Collab update posted 2026-10-21 (user ok, firmware parts removed by the user's choice) in
Give-me-a-signal/virus-ti-rework#5: E188-E308 highlights (traffic, host/UI behaviour, library roots, pkg version check,
sleep/wake); text work/collab-update-2026-10-21.md. ROUND 0.8.30 PLAN (user ok): (1) #13 arp drift: reporter's video (scope, audio track silent, read by eye): Virus arp notes ~7-8 ms/s early vs the beat
(~0.7 % fast, ~141 vs 140 BPM), Live's arp stable in the same take; test file native/tools/arp-hold-140bpm.mid (C3 held
24 bars after a 1-bar lead-in, 140 BPM; timing_analysis.py --bpm 140 --div 4). Measure first (arp
1/16 at 120 BPM, timing_analysis 125 ms grid; Linux, then Mac 48k/128), then fix or note; clock code virusplugin.cpp:1284
looks right (Virus frames). (2) #11 mod matrix destinations: the Access XML gives the 20 destination params maxValue=121 but
the mapped list has values 0..127; editorrender.cpp:33 drops entries > max -> 122 Slot 6 Amount 1, 123/124 Osc1/2 F-Shift,
125/126 Osc1/2 F-Spread, 127 Distortion Mix missing; REA: how the original builds the menu + host range, then TDD.
(3) #7: first ping after the broker idle exit on the Mac (read-only, diag.log). (4) E300 option, restore RAM A/B.
E300 option open; restore the user's RAM A/B (work/libdrag/ram-backup-20261021-0921).

## Round 0.8.30 (branch round-0.8.30 from round-0.8.29 a095c61+, started 2026-10-21, while the v0.8.29 release CI waits for a Mac runner)

**RELEASE PREP 0.8.30 (2026-10-22):** issues checked with the user: #10/#13 confirmed by the reporter and closed, #12 closed
(right-click loads wavetables), #11 still open until the release (comment drafted: fixed in 0.8.30, please confirm). Version
0.8.30 (native/CMakeLists.txt), 338 unit tests, ctest 5/5 on Linux. risk.py --round 0.8.30: class C; Windows + Mac by hand
cold/reopen/export (WC1-3, MC1-3), Linux by hil.sh. Checklist scripts/checklists/round-0.8.30.json (+ #11 and restore per
system). CI test build run 39730939693 (version 0.8.30). NEXT: CI green -> install Linux (install.sh), hil.sh, win11 VM
(setup.exe /quiet), Mac package + INSTALL-COMMAND; publish checklist; then release notes/README/site (338 tests), merge, tag.
CI 39730939693 ALL GREEN (release skipped). Installed: Linux = CI build (install.sh exit 0; plug-in 8670bb4f.., broker
a1cdc38c.. = tarball; dev plug-in 1bfb7fc6 + dev broker 8b068b73 backed up in ~/.vst3-backup/0.8.30-dev-1bfb7fc6); win11 VM =
CI (setup.exe /quiet exit 0, entries 0.8.30, plug-in 46f97c18.. = CI MSI, C:\tirevive-test\0.8.30-ci; VM running, Virus still
on Linux); Mac: zip pushed + unzipped in ~/Downloads/TI-Revive-0.8.30-ci-mac/TI-Revive-0.8.30-mac (pkg a69567e5.. = CI) with
INSTALL-COMMAND.txt + 'Install test build.command'; the user installs. hil.sh running on the CI build. Checklist published:
https://claude.ai/artifact/EXf4qynRdt9pWgVoUy5KvX (db, marks in rounds/0.8.30/marks, empty at publish).
hil.sh on the CI build (work/hil/20261022-080203, ended 08:06): H1, H2 x2, H3, H6 ALL PASS (H6: 26/26, max 0.86 ms, jitter 0.23 ms,
+6 ppm). The user started REAPER at 08:05 while hil.sh still owned the Virus -> 'plug-in waiting' (expected, one owner); REAPER
took over at 08:06:05 by itself. Lesson: tell the user when hil.sh is still running.
Linux block 2/2 marked pass (L1 #11 incl. reload, L2 Control Center backup + restore, 08:09). OPEN (user, after the
release, 0.8.31): same bank in BOTH browser panes -> the selection is framed in both, because TI Revive keys the selection
by bank (viruseditor.cpp browser_sel_[bank], since 0.8.29), not by pane. Not in E294/E297/E298 whether the original does
the same: check in win10ref (same bank both panes, click an entry: one pane or both framed?) or REA, then fix if it differs.
VM block 5/5 marked pass (WC1-WC3, W1 #11, W2 RESTORE in the Access look); win11 diag.log: 'bank sync done in 30.7 s',
'sync error shown (4 late events since Play)' during the export, cleared by Play (as every VM round, E288). Virus detached,
win11 shut down. Mac: receipt io.github.derpsn.tirevive 0.8.30 (installed by the user ~08:15, after the 08:03 push), old
diag.log -> diag.log.pre-0.8.30-ci, diag.on set. Mac block next (MC1-MC3, M1 #11, M2 RESTORE).
Mac block 5/5 marked pass -> ROUND 0.8.30: 12/12 marked pass (E314). Mac export measured: 256/256, -8 ppm, jitter 1.05 ms,
10 notes 3.4-6.3 ms late with a sync error 3 s into the render = known E243/E244 (nothing in the Mac timing path changed;
0.8.29's release session showed it too). README/site timing rows kept at the 0.8.29 measurements (E308). Open LOW: 'bank sync
failed' is logged when the host deactivates the plug-in mid-sync (should it say 'stopped'?). NEXT: release texts, merge, tag.
Release texts 2026-10-22: release notes 0.8.30 in build.yml; README/site 338 tests. Site check (round claims): README
'tirevive-cc only reads ... restoring comes later' WRONG -> fixed; CLI list + restore line; status row restore now 'Linux,
Windows 11 (VM), macOS (E313, E314)'; native/docs/controlcenter.md scope row, title and RESTORE line updated. Site built
locally (not deployed). OPEN for the user: Mac timing row (0.8.29, E308 clean) vs the newer 0.8.30 export (E314: 10 late
notes, E243). CORRECTED 2026-10-22 (user): the earlier note here ('restore worked with the plug-in open, so tirevive-cc's
help is wrong') was false and unsourced - the user had to close the plug-in before the restore worked, as tirevive-cc's
help and native/docs/controlcenter.md say (one broker client at a time). No open point.
USER DECISION: Mac timing row -> 0.8.30 numbers (E314: 256/256, 1.05 ms, max 6.30 ms, -8 ppm), release. Site check found
the sync error FAQ's 'since 0.8.27 ... the late notes did not come back' contradicted by E314 -> README + site: '10 of 256
came 3-6 ms late in the 0.8.30 test, we are looking into it'. Status row jitter 0.04-1.05 ms. OPEN 0.8.31: Mac export late
notes DURING the paced render (E314; E243 was after it).
MERGED 2026-10-22: 337390d (main 98e12e6 + round 224d69a, -F file, tree = round tip), pushed; tag v0.8.30 on 337390d,
release CI run 39736879794 running. NEXT: CI green -> release check (assets, Latest), install the release on Linux, win11
(VM=win11), Mac (+ INSTALL-COMMAND); #11 comment (scratchpad draft, TESTED_LINE = VM + Mac M1/W1); site rebuild only when the
user says so.
**v0.8.30 RELEASED 2026-10-22 08:59** (release CI 39736879794 all green, 6 assets, Latest). Installs: Linux = release
(install.sh; plug-in 8670bb4f.., broker a1cdc38c.. = byte-identical to the CI build; NOTE: REAPER was open during install.sh -
no file changed because of that, but stop before installing while a DAW runs); win11 = release (setup.exe /quiet exit 0,
plug-in e285b4f7.. = release MSI; MSVC builds differ from the CI test build), VM shut down; Mac: release unpacked in
~/Downloads/TI-Revive-0.8.30-release-mac/TI-Revive-0.8.30-mac (pkg 5936f178.. = release) + INSTALL-COMMAND.txt + 'Install
release.command', the user installs. #11 comment POSTED (user ok; issuecomment-6334883221), issue left open for the
reporter. Site rebuilt (user ok, run 39738056459 on 337390d), live: Download 0.8.30, 338 unit tests, Mac row 0.8.30, FAQ
'10 of 256'. Remaining: the user installs the release on the Mac. Next round 0.8.31: the three open points above (Mac export late notes E314, browser selection in both panes, 'bank sync failed' wording).

**0.8.31 item 1, Mac export late notes (E314), started 2026-10-22 ~12:00 (branch round-0.8.30 still checked out):**
Mac diag logs pulled to work/timing/mac-0.8.30 (diag.log = 0.8.30 session, diag-0.8.29.log, older ones; MC3.wav saved
there). Every Mac export in those logs: 9/9 at Live 44.1 kHz / block 512 clean (sync page 0, 0-3 reports), 2/2 at
48 kHz / block 128 with the sync page (0.8.29 release session 101 reports, 0.8.30 MC3 70) - the late notes follow the
Live setting, not the build (E308's clean export was 44.1/512). Linux A/B the same day (nativehost H3, installed 0.8.30
release, work/timing/rate-ab-1209): 44.1/512 and 48/128 both 42/42, max 0.85 / 1.02 ms, 3 sync reports each -> Mac only.
Hypothesis (LOW, untested): the Mac's Core Audio IOProc hands the ring 512-frame chunks; offline blocks of 117.6 frames
are released in bursts per chunk, which eats into A = 1024. Next: the same nativehost pair on the Mac (Virus on the Mac),
then 48/512 and 44.1/128 to split rate from block size. No code change yet.
RESULT (user's Live exports 13:07-13:10, E315): block size decides, not the rate - 48/512 clean (max 0.74 ms, 0 late),
44.1/128 10 late (max 4.2 ms), 48/128 6 late (max 2.5 ms); late notes every 3.75 s = one phase of the Mac's 512-frame
chunks. Files in work/timing/mac-0.8.30/exports. Fix idea (not written): pace offline blocks by the ring's chunk step so
small blocks are not released in a burst behind their position (OfflinePacer, test first), Linux behaviour unchanged.
FIX WRITTEN 2026-10-22 ~13:20 on branch round-0.8.31 (from round-0.8.30 6effb6f; user: 'start the fix in round-0.8.31'):
OfflinePacer::early() (ring step learned per render, block runs up to step - block early, cap 512), wait_offline gets
`avail` (ring's real newest) for the read guards, log 'offline render ended (blocks up to N frames early)'. Tests first
(3 new, the burst test failed with the old pacer), 341 tests, ctest 5/5. NOT on hardware: the Linux dev runs at 13:19
had no Virus (it was still on the Mac), deleted. NEXT: Virus back on Linux -> hil.sh --plugin <dev build> + the H3 pair
48/128 and 44.1/512 (expect 'blocks up to ~0 frames early' there), then CI test build (version 0.8.31) for the Mac:
user exports 48/128 and 44.1/128 again, expect 0 notes > 2 ms late, no sync page, log 'up to ~394 / ~384 frames early'.
LINUX ON HARDWARE 2026-10-22 ~13:25-13:40 (TI Desktop, dev plug-in build-native 1382fcc+): H3 trio (work/timing/rate-ab-dev-1324)
48/128 42/42 max 1.03 ms, 44.1/512 max 0.86 ms, 44.1/128 max 0.41 ms jitter 0.09 ms, 1-3 sync reports each. CORRECTION of my
prediction: Linux is NOT unchanged - the broker fills the ring in 10 ms transfers (vtiusbaudio_libusb.c NP 10 = 441 frames), so
blocks ran 313/323 frames early (441 - block) and 370 once at block 512 (an 882 double step at the render start; under the
512 cap); comments fixed in a follow-up commit. hil.sh --plugin <dev> ALL PASS (work/hil/20261022-132959/): H1, H2 x2, H3 45/45 max
0.96 ms, H6. NEXT: push round-0.8.31, CI test build version 0.8.31, Mac: install + exports E2/E3 again.
CI test build run 39771581839 (version 0.8.31) ALL GREEN (release skipped). Mac: zip pushed + unzipped in
~/Downloads/TI-Revive-0.8.31-ci-mac/TI-Revive-0.8.31-mac (pkg 2e58c2eb..) + INSTALL-COMMAND.txt + 'Install test
build.command'; Mac diag.log -> diag.log.pre-0.8.31-ci. NEXT: user moves the Virus to the Mac, installs, exports E3 48/128 and
E2 44.1/128 to ~/Downloads (E3b-48k-128.wav, E2b-44k-128.wav); I pull, timing_analysis + count notes > 2 ms, log line
'blocks up to N frames early'.
MAC RESULT 2026-10-22 ~14:00 (E316): FIXED - 48/128 256/256 max 0.01 ms, 0 late; 44.1/128 only the first note late (4.9 ms,
was also late before), 0-1 sync reports, no page; log 394/384 frames early. Files work/timing/mac-0.8.31 (the user saved
44.1/128 as E3b-44k-128.wav, copied as E2b-44k-128.wav). Open LOW: the first note of a render (step not learned yet:
keep the step across the real-time -> offline switch or learn it in real time too).
FIRST-NOTE FIX WRITTEN 2026-10-22 ~14:15 (user: 'fix the first note too'): OfflinePacer::observe() on every real-time
block, reset() keeps the step, a backwards ring forgets it; tests first, 343 tests, ctest 5/5. Not on hardware (Virus
not on Linux, not listed on the Mac either at 14:15). Pushed, CI test build started (version 0.8.31). NEXT: Mac install,
E2c 44.1/128 + E3c 48/128 exports (first note on time?), then Linux hil.sh in the round test.
CI 39775550348 ALL GREEN; Mac: ~/Downloads/TI-Revive-0.8.31b-ci-mac/TI-Revive-0.8.31-mac (pkg a007f9a0..) + install files;
Mac diag.log -> diag.log.ci1-0.8.31 (copy in work/timing/mac-0.8.31/diag.log). Virus on the Mac (user, ~14:25).
MAC RESULT ~14:30 (E317, work/timing/mac-0.8.31b): first note on time in both (-0.08 / -0.14 ms); E2c 44.1/128 1 note
2.5 ms late at 14.25 s, max 2.55 ms, jitter 0.26; E3c 48/128 0 late, max 1.82, jitter 0.40; 0-2 sync reports, no page.
Item 1 (E314) done on the Mac up to one single 2.5 ms note (cause unknown, watch in the round test). NEXT: Linux hil.sh
on the round build when the Virus is back; other 0.8.31 items (browser selection both panes, 'bank sync failed' wording);
at release: README/site FAQ ('10 of 256 ... looking into it') + Mac timing row.
LINUX ON HARDWARE ~14:33-14:45 with the first-note build (dev, = 2410c7c code): hil.sh ALL PASS (work/hil/20261022-143309:
H1, H2 x2, H3, H6); H3 48/128 42/42 max 1.02 ms jitter 0.17, 44.1/128 max 0.81 ms jitter 0.16, 1 sync report each, blocks
323 / 313 frames early (work/timing/rate-ab-dev2-1437). Linux done for item 1.
ITEM 'bank sync failed' wording DONE (dev, ~15:00; user away, asked me to do items 1+2 alone): cause from the E314 Mac log
+ code - Live terminated the instance mid-sync, terminate() -> MemorySync::cancel() -> the request in flight returned
empty -> -1 'failed'. Fix: sync_bank/read_bank return -2 when the empty reply follows cancel() (was_cancelled()); the
plug-in then logs 'stopped' and saves the partial bank like the other stops. Test first, 344 tests, ctest 5/5. Not on
hardware. Control Center never calls cancel() (unchanged). NEXT: browser selection in both panes (check the original
first, static via REA - the user is away, no VM/hardware writes).
BROWSER SELECTION DONE (dev, ~15:40, E318): REA sub-agent - the original keeps the selection per pane (KPatchTable,
flag per entry), no mirroring between panes; ours now PaneSelections (per pane, tied to the pane's bank, cleared on a
bank switch); renderer frames per pane; editor timer drops a switched pane's selection. Test first, 345 tests, ctest
5/5. NOT seen in a running editor yet -> round 0.8.31 checklist item (same bank in both panes: click upper, lower not
framed; switch the upper bank and back: selection gone; drag from a selection still works; Delete selection in a
library). Open LOW: the original fills the selection in the focused pane and outlines it in the other (vtable +0x310,
not adopted); drag highlight (renderer_.highlighted) is still per bank.
ROUND 0.8.31 STATE: items 1 (E315-E317 Mac export), 2 (bank sync stop wording) and the browser selection done in dev;
remaining: round test (risk.py --round 0.8.31, checklist incl. Mac 48/128 export, browser panes), release texts (README/
site FAQ 'looking into it' -> fixed, Mac timing row, test count 345), version 0.8.31 in native/CMakeLists.txt.
DEV CHECK PASSED (Linux, dev build 14:55 = a0350b2 installed to ~/.vst3, 0.8.30 release backed up in the session
scratchpad): user: "all works" - browser selection per pane, bank switch, drag, Delete selection, bank sync wording.
CI test build started: run 39797996086 (round-0.8.31 @ 3723273, version 0.8.31). Next: install the CI build on Linux,
then the round test.
RESULT run 39797996086: all green except mac (no runner again, the known capacity problem; not retried, superseded).
HARDWARE RUN ON WIN/MAC (user: "can we also make an automate script I can run on windows and mac?"; edad2b9):
scripts/hil-win.ps1 (+ hil-win.cmd) and scripts/hil-mac.command play H1, H2 x2, H3, H6 like hil.sh and write
results-<time>/; hil_check.py --folder grades a folder (ITEMS + 4 tests, one keeps the runners' item lengths in step);
nativehost: no M_PI (MSVC), timeBeginPeriod(1) on Windows; CI builds nativehost on Windows/Mac in continue-on-error
steps -> artifacts TI-Revive-hil-windows / TI-Revive-hil-mac (not in the release). Desktop session needed: Mac over
ssh = no Microphone access = silence (E103), Windows broker shm per session (E047: VM via schtasks /IT). Checked: script
tests 55 OK, ctest 5/5, --folder on work/hil/20261022-143309 = ALL PASS like hil.sh. NOT run on Windows or the Mac
yet; risk.json keeps those items by hand until they pass once. Next: new CI run, then the first runs.
CI run 39800731071 (78f7808, version 0.8.31): ALL GREEN incl. mac (got a runner); artifacts incl. TI-Revive-hil-windows
(286 KB) and TI-Revive-hil-mac (126 KB). Linux = CI build (install.sh, no sudo needed; plug-in 569d1d8a.., broker
56d2f4e0.. = the tarball; dev build backed up in ~/.vst3-backup/0.8.31-dev-3723273; unpacked in
~/Downloads/TI-Revive-0.8.31-test). hil.sh on it (work/hil/20261022-190007): H1, H2 x2, H3, H6 ALL PASS (H3 export
1.00x, 45/45, max 0.94 ms; H6 26/26); --folder on it: ALL PASS too. Next: Windows (hil-win.cmd, first real run),
Mac (push package + hil zip, hil-mac.command by the user + 48/128 Live export), checklist.
MAC DONE (E319, work/timing/mac-0.8.31c): package + hil zip pushed to ~/Downloads/TI-Revive-0.8.31c-ci-mac (with
INSTALL-COMMAND.txt + Install test build.command); nativehost --scan over ssh loaded the plug-in (exit 0); user installed
19:13 (receipt 0.8.31), ran hil-mac.command: FIRST RUN ALL PASS (H1, H2 x2, H3 1.00x 45/45, H6 26/26); Live export E4
48/128: 256/256, jitter 0.01 ms, max 0.01 ms, no sync page; 'bank sync stopped' wording seen on hardware. Mac round
items cold + export covered (reopen rotated out). Remaining: Windows (hil-win.cmd, first real run; VM or the real PC),
then risk.json windows/mac 'automated' once Windows passed too, checklist, release texts.
WINDOWS VM (E320): win11 started, 0.8.31c installed (setup.exe /quiet exit 0, plug-in 266318e2.. = CI nativebin), files
in C:\tirevive-test\0.8.31c (hil\ = the artifact), Virus attached (VM=win11 vm.sh attach), scheduled task
'tirevive-hil' (Interactive, user vti) runs hil-win.ps1 -> console.log. Runner works end to end. Run 1: H1 + H3 FAIL,
run 2: H2-2 + H6 FAIL - single notes 8-21 ms off, moving between items; VM timeline errors up to +-1023..1845 frames
(Linux/Mac <= ~400), run 2 H6 audio drops + no marker. Judged as VM USB timing (MED), not proven for real Windows.
Results pulled to work/hil/win11-*. VM still RUNNING with the Virus attached. Decision for the user: real-PC run
(this PC booted into Windows, hil-win.cmd by hand) or accept the VM as function-only for this round; risk.json
unchanged (Windows items stay by hand).
USER DECISION (~19:45): "2 and ship it. I think we are clean" -> VM counts as a function check this round, Windows
timing left for a later real-PC session; no checklist artifact this round (automated runs + the user's Linux browser
check + Mac E4 export instead). Virus detached from the VM (back on Linux, broker socket active; VM still running).
RELEASE PREP (site check per the skill, only the claims this round touches): FAQ Mac export line 'looking into it' ->
fixed in 0.8.31 (E319); Mac timing row 0.8.30 1.05 ms -> 0.8.31 48k/128 0.01 ms, max 0.01, 0 ppm; jitter range
0.04-0.33 -> 0.01-0.33 (site + README x3, status table E314 -> E319); TESTS 338 -> 345; native version 0.8.31; release
notes 'New in 0.8.31' in build.yml. Site built locally (values present), not deployed. ctest 5/5.
MERGED + TAGGED: round-0.8.31 merged into main with -F (9adfd89, parents 337390d + 83ed98a, tree = round), main pushed,
then tag v0.8.31 on 9adfd89 pushed separately -> release CI run 39824346270 (running). NEXT: run green (mac job: rerun
only the failed job if no runner) -> check release assets + Latest; site: NOT by hand this time - site.yml deployed
on the main push (run 39824323963, build.py changed: new texts live while Download still showed 0.8.30) and redeploys
itself on 'release published' -> only check that it ran and shows Download 0.8.31;
install the release on Linux (install.sh), win11 VM (setup.exe /quiet, VM=win11, VM still running without the Virus),
Mac (push + INSTALL-COMMAND.txt + Install test build.command); then a new round branch for the next fixes.
Release run 39824346270: all green except mac (cancelled, no runner) -> release skipped; rerun of the failed jobs only
(gh run rerun --failed) started ~19:50, mac queued. No release yet.
Side task (user, while waiting): three hardstyle singles (HS Supersw, HS Pulse, HS Lead) as a library in the user's
personal patches folder (~/Dokumente/Access Music/Virus TI/Patches/TI Revive Hardstyle.mid); built from the plug-in's
init single through RemoteDevice::update_dump one value at a time (aliases share bytes; the 524-byte dump has two
checksums, so hand offsets for page C/D were one byte off - caught before use), values read back; one-off tools in the
session scratchpad only, nothing in the repo. Not listened to yet.
**v0.8.31 RELEASED 2026-10-22 20:01** (release run 39824346270 after a rerun of the mac job only; Latest, 6 assets).
Site: release-triggered deploy does NOT happen (GITHUB_TOKEN releases trigger no workflows) -> run by hand 39827849547,
live: Download 0.8.31, 345 unit tests, 'fixed in 0.8.31'. Installed: Linux = release (install.sh; plug-in 7bcee367..,
broker 56d2f4e0.. = tarball; CI build backed up in ~/.vst3-backup/0.8.31-ci; ~/Downloads/TI-Revive-0.8.31-release);
Mac = release pushed to ~/Downloads/TI-Revive-0.8.31-release-mac (+ INSTALL-COMMAND.txt + Install test build.command),
the user installs; win11 NOT yet (VM off; setup.exe in ~/Downloads/TI-Revive-0.8.31-release).
OPEN (user report ~20:00, Linux, 0.8.31 CI build): "everything loaded has a huge release or kind of reverb" (e.g. an
EPiano). Checked offline: the ROM EPianos as the plug-in builds them (firmware ROM, TI2 by bcdDevice 0300, decoded
through the model) have normal values (e.g. ROM C 21 EP3DogNyt: release 29, reverb send 29, delay off) -> conversion
not the cause (MED). Asked the user: front-panel A/B, transport stop (sustain off + CC 123), DAW, patch, 0.8.30?
User started win10ref (original Access software) to compare the sound; win10 was started by mistake and shut down.
RESOLVED (not TI Revive): the original in win10ref had the same ('the note is playing forever' = a stuck hold in the
Virus); the user power-cycled the Virus (while passed through to win10ref - avoid; after it the VM held the stale USB
address 18, re-attached as 19), afterwards REAPER on Linux sounds normal. Cause of the stuck hold unknown.
NEW GAP (user, ~20:30, "a CLEAR huge gap, we need the same behaviour"): with the original, the Virus' display shows a
'USB configuring' screen when the plug-in starts the first time, and a 'clock changed' message; ours shows neither.
Known: the configuring/connected screens are host-sent 640-byte LCD bitmaps from firmware.bin (lcd_configuring_000/001,
lcd_connected_000; E228). How/when/by whom they are sent and where 'clock changed' comes from: REA analysis running
(sub-agent, static only; driver, VirusUSB_X64.dll, Virus Control, Control Center, firmware listings). Nothing is sent
to the Virus until the request is known and judged safe (rule 2). Virus on Linux; win10ref still running (no Virus);
win11 release install still open.
ROUND 0.8.32 (branch round-0.8.32 from main 22f1455) item 1 DONE in dev (b3bde33, E321): captures of the original's
start-up (win10ref) vs ours + REA: the original raises Rise Message 7 'Configuring Virus TI' (74 00 7E 07 + Messagetimer
74 00 29 64 after GlobalInit, 74 00 29 01 after ParamInit, 74 01 7E 07 before every bank info request, timer 1 after
the check) and sends the plug-in's multi table UNCHANGED before ParamInit (we sent the state's zeroed copy after
ParamInit). Now the same (init_messages(dev, multi), MemorySync rise, timer after the check, stored multi not sent).
User saw the message on the Virus (first dev build); capture of the second build = the original's order and bytes;
nativehost 22/22 notes, max 0.08 ms. 347 tests, ctest 5/5. 'Audio clock changed' = the Access driver follows the DAW
rate (re-configure + UAC SET_CUR 48000/44100 on EP 0x82/0x01); we stay at 44.1 by design - open decision for the user.
Captures in captures/ (orig-startup-lcd-20261022, ours-startup-20261022, ours-startup-e321(b)-20261022).
User saw 'Configuring Virus TI' again with the final build (20:51). Item 2 DONE (0517bfa): scripts/startup_diff.py
compares two start-up captures (testing.md 'Start-up compared with the original'); on the final build
(captures/ours-startup-e321c-20261022, no notes) only the original's second bank info requests for banks 2-4 remain
(cause unknown, left visible; known differences IGNORE: 73 00 09, our probe 30 00 00, our anchor F2). Asked the user
(honest answer): rise/multi pieces were known (E273, E301, protocol.md), the zeroed multi on the wire was an
oversight, the start-up message + timer and the text mapping were new. OPEN decisions for the user: (a) follow the DAW
rate like the original (48k: re-configure + SET_CUR; big design change: timing assumes 44.1, E241 risk; conversion
still needed for 88.2/96), (b) replicate the double bank info requests or not.
USER DECISIONS (~21:05): (a) YES - follow the DAW rate, "we want to be as close as possible to the original plugin";
(b) find out with REA why the original requests banks 2-4 twice. Running in the background: REA (double requests,
proposes E322), REA (the original's rate following: trigger, mapping, timing at 48k, firmware side, safe device
sequence, E241; proposes E323), Explore audit of every 44.1 kHz assumption in our code. NEXT: a written plan for
rate following from the three results, before any code (TDD, hardware tests on all three systems).
DONE: (b) E322 (c2c32b4) - the original's repeated bank requests are a timer side effect (any reply resets its tick
timer; the Virus answers both identically; the 2nd reply changes nothing) -> not copied; startup_diff counts them as
known: fixed build vs original = SAME START-UP. Audit of the 44.1 assumptions saved locally:
work/rate-follow/audit-44k1.md (git-ignored). Waiting for the REA pass on the original's rate following (E323).
E323 written (the original's rate following: mapping 0x1800a9dc0, driver sequence, USB chip, DSP PLL, stamps in
Virus frames at the current rate, E241 hypothesis SA Freq). PLAN: findings/design-rate.md (6 steps, rule-2 gates,
hardware tests, risks) - waiting for the user's go before any code; first gate: the user reads SA Freq on the Virus.
User: 'yes, start with steps 1 and 2'. STEP 1 DONE (eba76ab): virusrate.h (the original's rule), time values from
the Virus' rate (keepalive, gap fill, anchor windows, marker timeout, klog, stats, MIDI clock 919/1000, transport,
resampler/sidechain step, latency report); the rate stays 44100 until the broker switches. STEP 2 DONE: rate_gen in
the shm (same layout, version 2), AudioIn::rate()/rate_changed(), the plug-in re-anchors on a switch. 354 tests,
ctest 5/5, hil.sh ALL PASS after each step (work/hil/20261022-212352, -213127). Nothing sent to the Virus at 48 kHz.
NEXT: step 3 (broker switch sequence) - only after the user read SA Freq and agreed; the first real switch is a
controlled run with the user watching the display.

Plan (user ok, issues gone through 2026-10-21): (1) #13 arp drift: measure first (native/tools/arp-hold-140bpm.mid,
**v0.8.29 RELEASED 2026-10-21 20:03** (merge 98e12e6, tag v0.8.29, release CI run 39626672973: mac job got a runner on try 10,
all green, 6 assets, Latest). Site rebuilt by hand (run 39649468184), live: Download 0.8.29, 332 unit tests, #10 line.
Replies posted: #5 (closed), #10 (closed), #13 (questions; our measurements: Mac Live 12 48k/128 real-time 384/384 -2 ppm,
Linux REAPER 48k 140 BPM +7 ppm, 120 BPM -10 ppm - not reproducible, E310). Installs: win11 VM = release (setup.exe /quiet
exit 0, plug-in 35212dd8.. = MSI, C:\tirevive-test\0.8.29-release, VM shut down); Mac = release unpacked in
~/Downloads/TI-Revive-0.8.29-release-mac (+ INSTALL-COMMAND.txt), the user installs; Linux NOT yet: install.sh needs sudo for
the udev rule while the Virus is on the Mac -> run it when the Virus is back (unpacked in
~/Downloads/TI-Revive-0.8.29-release-2005; CI plug-in backed up in ~/.vst3-backup/0.8.29-ci).
Mac = release installed by the user (receipt 0.8.29). #7 item DONE (E311): first ping after the broker idle exit answered 3/3
on the Mac, reply posted in #7 (no CC 120 tested). Collab update posted in virus-ti-rework#5.
Linux = release 0.8.29 installed 2026-10-21 ~20:20 (install.sh, no sudo with the Virus attached; plug-in 6fa9e00d.., broker
cdf3d914.. = release; release plug-in backed up in ~/.vst3-backup/0.8.29-release), then the round dev build in ~/.vst3.
#11 hardware check 1 (dev b70e1cca, REAPER): menu lists the new destinations and the Virus takes them (LCD 'Osc 2 F-Shift'),
but the field showed 'Slot 5 Amount 1': ui_value clamped read-back values to maxValue -> fixed 23d4ebb (RemoteDevice::shown,
test first; 332 tests, ctest 5/5); dev build 34fa9cbd installed; user: still wrong -> second clamp in the renderer's draw, fixed (EditorRenderer::shown_value,
test first, 332 tests, ctest 5/5). dev 1bfb7fc6 installed, user: fields right -> #11 DONE on hardware (E309). E300 hit again during the checks (E312:
5 give-back resets in 5 min, then error -71 and 'unable to enumerate'; power cycle + re-plug; the old broker reset the Virus
again 1 s after it came back). E300/E312 FIX written (user ok): give-back resets before releasing the interfaces (src/broker/giveback.h, vticore.c;
tests giveback_*, 334 tests, ctest 5/5; broker + vtiprobe built in work/build/linux, not installed). Hardware loop 30/30 OK (20:46-20:52,
reset then one probe each, 0 errors). Fixed broker INSTALLED on Linux (~/.local/lib/tirevive/vtibroker
8b068b73.., release broker kept as vtibroker.0.8.29-release) for everyday use until the round's release (user: release tomorrow).
NEW round item (user): Control Center 'Restore RAM from backup' like the original - REA check of Virus Control Center.exe
running; then TDD; the user's RAM A/B are restored WITH that feature (pre-test state = cache.old of 2026-10-18: RAM A 21
slots, RAM B slots 0-3 differ; restore libraries also built in ~/Dokumente/Access Music/Virus TI/Patches/Restore RAM A|B
(2026-10-18).mid as a fallback via the browser).
REA (sub-agent, Virus Control Center.exe x64): the original's restore is whole-device only (.vtibackup: RAM A-D as raw SRAM
images, user ROM banks, multis, device settings), written through USB vendor requests incl. flash erase/burn of the ROM banks
and the settings sector - NOT copied (rule 2, E177). Ours (E313 to write after the hardware test): restore_plan / dump_pieces /
restore_ram in src/cc/controlcenter (sync the bank, write only programs that differ as single dumps into RAM like the browser,
64-byte pieces on cable 3 10 ms apart, read each back, update the cache), bank_from_name; CLI tirevive-cc restore A|B|C|D FILE
[--yes]; window button 'Restore...' (file, then a menu 'Overwrite RAM X with ...' = the confirmation). Tests: ccrestore_* +
cc_restore_ram (fake Virus with memory); 338 tests, ctest 5/5. Commits 7190aaf, ac7576f. Safety backup of the current RAM A/B:
~/Dokumente/Access Music/Virus TI/Backups/2026-10-21 2116. DONE on hardware (E313): the user's RAM A/B restored
with the dev Control Center, all 512 programs = the 2026-10-18 state, fingerprints from the Virus confirm. Access look's RESTORE
wired to the same restore (SkinPageActions::restore, status in both views); README/site: backup + restore, 'Restore'
paragraph, status table row done, site user-report line + #11 (site built locally, not deployed). ctest 5/5 after the user closed
their Control Center (gui_controlcenter_close had failed only because of the open single instance). NEXT
(tomorrow, user): release prep 0.8.30 (version, release notes incl. #11, E312 give-back, Control Center restore; CI test
build; short checklist Linux/VM/Mac incl. RESTORE in the Access look); then release. (and after a project
reload); then E300 option, restore RAM A/B (work/libdrag/ram-backup-20261021-0921).
timing_analysis --bpm 140 --div 4; Linux, then Mac 48k/128), then fix or note. (2) #11 mod matrix destinations above
maxValue 121 (REA: the original's menu + host range, then TDD). (3) #7 first ping after the broker idle exit on the Mac
(read-only). (4) E300 option, restore RAM A/B. Release 0.8.29 follow-ups run in between (replies #5/#10/#13, site, installs).
#11 FIXED (E309, test first): menu limited by list position like the original; 332 tests, ctest 5/5; on hardware: check the
MATRIX page destination menu (Distortion Mix etc. present, no '-reserved-'). #13 measurement 1 (user, REAPER Linux, 44.1 kHz,
render of arp-hold-140bpm.mid but the project stayed at 120 BPM, pattern 2, 1/16; ~/Musik/arp.wav): 385/385 on the 125 ms
grid, drift -10 ppm, sd 0.99 ms, one 19 ms outlier -> no drift at 120/44.1 on Linux. Next: 140 BPM at 44.1, then at 48 kHz.

## Round 0.8.29 (branch round-0.8.29, collecting; started 2026-10-20)

1. **accessfiles.cpp: NaN in the oscillator wave table under fast-math (fixed, test first).** find_osc_waves checked
   the rows with std::isfinite; the Mac (Xcode -ffast-math) and Windows (/fp:fast) builds may fold it to true, so a
   candidate with a NaN row passed the +-1 check (reproduced with g++ -O2 -ffast-math: NaN row accepted; without the
   flag rejected). Effect if it ever happened: NaN in the classic oscillator display, the real table has none (low
   risk, but the check was meant for it). Now the exponent bits decide, as in hostmidi.h. Test: NaN and -inf rows
   rejected (NaN check failed first under -ffast-math). Same pass: test_resampler's NaN marker test used
   std::isnan/isfinite and failed under g++ -ffast-math (18 checks; MSVC /fp:fast keeps them, so Windows CI was green);
   now a bit check (test only). Built and tested on Linux: normal build ctest 5/5, a -ffast-math build of vtitests
   304 tests 0 failed. Not on hardware (no device path touched). C++ review 2026-10-20 (evening): the sine row
   (row 0) is now bit-checked too (finite_at for all 64 rows, test: NaN in row 0 rejected), size_t widening
   fixed (clang-tidy clean on the function), test_resampler flag renamed to no_nan. Linux build + ctest 5/5; this
   came AFTER the CI test build 39441322803, so the release build differs from the tested one in find_osc_waves.

2. **E289: "bank sync done" after an aborted sync (fixed, test first).** The bank loop stops at the first bank whose
   sync_bank returns < 0 (-1 no answer, -2 closed while syncing), but the line after the loop always logged "bank
   sync done" and the LCD said "Synchronization done". Now sync_end() (startprogress.h) names the end: "bank sync
   done / failed / stopped in N s", and the LCD only says done after a full sync (else its line is cleared). Test
   startprogress_sync_end_names_an_abort (failed first: not declared). Linux: 305 unit tests, ctest 5/5. Not on
   hardware (an abort needs a lost reply or a close during the sync; the next round's reopen test may show it).

3. **hil.sh --sleep never slept (found 2026-10-20, fixed, rerun passed).** First --sleep run (release 0.8.28 in
   ~/.vst3, work/hil/20261020-120224): H1, H2 x2, H3, H6 real passes (jitter 0.02-0.19 ms, 0 lost, export 1.00x);
   H5 "pass" but the kernel log shows `rtcwake -m mem` -> nvidia pci_pm_suspend returns -5, "Some devices failed to
   suspend", suspend exit after 0.7 s: H5 tested a ~1 s USB reset (disconnect, reconnect, ready 3.7 s, 50/50 notes),
   not a sleep. rtcwake goes straight to the kernel and skips nvidia-suspend.service (the manual LC5 at 09:30 went
   through systemd-sleep and slept). Now: rtcwake -m no sets the alarm, systemctl suspend sleeps, and H5 fails unless
   CLOCK_BOOTTIME - CLOCK_MONOTONIC grew by >= 30 s. Rerun by the user (work/hil/20261020-121623): ALL PASS, the PC
   slept 48 s, recovery via the stood-still path and the reconnect (two 'ready' lines, no double send), notes 50/50,
   jitter 0.24 ms (E290). LC5 can now run automated on Linux (the user starts it: sudo).

4. **E287 REAPER crash after a GUI render: SOLVED, it is REAPER's bug, not ours (E291, 2026-10-20).** SWELL keeps a
   closed modal dialog's window as a "spare" and destroys it 100/500 ms later without clearing its focused-window
   global; when another application has the focus as the render window closes, nothing resets the global and the
   next cursor update crashes (SWELL_SetCursor -> gdk_window_set_cursor, the E265 stack). Caught 3x under gdb
   (work/timing/e287-watch.py, X focus logger e287-focuslog.c, ping watcher e287-pingwatch.c; all git-ignored) and
   **reproduced with ReaSynth only, TI Revive not loaded** (user clicked into the terminal during the render).
   Workaround for users: stay in REAPER until the render window has closed - now in the README ("Good to know"), the
   site's FAQ (build.py; site NOT rebuilt) and README-linux.txt. The two freezes of the same session (13:46, 13:59:50)
   have no stack; the user is sure they were the same situation (not measured). Cockos report: paste-ready draft at
   https://claude.ai/artifact/Vm7LSR65pJUDGLNBXgMRu1 (the user posts it on the Cockos forum; not posted yet). No
   plug-in change. Site check 2026-10-20 (skill): the only site/README change since main is this FAQ entry; claims
   match E291 except the freezes (LOW there), now worded 'probably the same bug (not measured)'. TESTS stays 304
   (= the release; the round has 305). Local build OK. The github-pages environment only deploys from main, so (user's
   choice) the docs diff went to main alone as 1efad04 (README + build.py only); its push ran site run 39434812956,
   green, live page checked (FAQ entry, Download 0.8.28). The 0.8.29 merge will see the same text on both sides.

5. **Dead code cleanup (2026-10-20, /ecc:refactor-clean).** cppcheck unusedFunction + style over native/ (src,
   plugin, ccapp, asio, au, tools) and src/broker, every hit grep-checked against production code and tests. Removed:
   SkinPage::set_status (native/ccapp/skinpage.cpp, no caller anywhere). Kept on purpose: ~80 false positives
   (members/statics/per-platform), the test-only API (RemoteTemplates knob_behaviour/set_knob_behaviour, help_count,
   broker on_state/pending_bytes/pending_replies/set_endpoint), the ASIO Dll* exports and ASIO struct members (ABI).
   Python (22 tracked files, pyflakes 4.0.3 + vulture 2.16 from a scratchpad --target install): removed the unused
   imports wave (scripts/timing_analysis.py) and sys (native/tools/uicompare/capa.py); vulture's other hits are
   unittest classes, documented struct fields (macho_syms.py) and an in-place bytearray (vtitrace.py). Fixed too:
   uicompare/allpages.py line 20 had an invalid "\ " escape (SyntaxWarning); now "\\ ", the same string (AST
   compare), compiles with -W error; the tool itself not run (needs a display and the uicompare setup). clang-tidy
   21.1.8 (misc-unused-*, bugprone-unused-*, -Wunused incl. member functions/templates/internal declarations) over
   the 90 native files in compile_commands.json (Linux build, tests included): one hit, AudioIn::mac_ "not used",
   a false positive (all uses under __APPLE__); no change. Windows/macOS-only code is not seen by this pass. cppcheck
   on src/wrapper: 58 hits, nothing to remove (46 VST3/COM/ASIO ABI struct members, 10 DLL/host entry points, and
   vua_set_out/vua_rate_lost, which the Windows broker calls - see item 6). cppcheck on src/mac/bridge (C++ mode,
   VST3 SDK headers; the .mm files grep-checked): nothing to remove (6 BridgeCore/Conn members used by its methods,
   45 hits in the SDK). Found there: AGENTS.md still described the 0.7.x Mac path (bridge = the Audio Unit,
   mac-build-bridge.sh = the development build) - fixed to match README and mac-build-release.sh (since 0.8.0 the
   native plug-in is the VST3 and the AU; src/mac/* is reference only, risk.json class A is right there). Linux
   build + ctest 5/5; Windows/macOS not built. Like item 1, this came after CI test build 39441322803 (Control Center binary differs).

6. **vtiusbaudio.c/.h moved from src/wrapper to src/broker (2026-10-20).** Found by the item 5 scan: the shipped
   Windows broker (src/broker/Makefile, vtibroker.exe) compiled ../wrapper/vtiusbaudio.c - the WinUSB audio streaming
   and the 48 kHz check (E241) - while AGENTS.md calls src/wrapper "not shipped since 0.8.0" and risk.json gave
   src/wrapper/* class A, so a change to it alone would have planned no Windows test. So far nothing slipped: its
   only recent changes (round 0.8.28 items 1 and 2, the review fixes) came with other Windows files and that round
   ran its Windows items (VM 5/5). Now: git mv to src/broker, includes and both Makefiles adjusted (the wrapper builds it from
   ../broker/ like its other broker files), risk.json: the Windows USB-layer rule (class C, windows: cold, reopen,
   power, sleep) names both files. Proof that nothing changed: mingw cross-builds before/after; vtibroker.exe and
   the other EXEs byte-identical with PE TimeDateStamp/CheckSum zeroed (two builds of the same tree differ only
   there); the wrapper DLLs differ between two builds of the SAME source (non-reproducible, not usable), so for all
   six affected sources the preprocessed output (mingw gcc -E -P) old tree vs new is identical. Linux broker builds;
   ctest scripts_py passes (risk rules). Not on hardware. risk.py --round 0.8.29 now asks Windows by hand: cold,
   reopen, export, power, sleep (before: cold, export). DECIDED with the user 2026-10-20: reopen, power and sleep on
   Windows are skipped this round - the broker binary is identical (proof above), so they would test the same code
   as the CI test build; round-0.8.29.json and the published checklist stay as they are (Windows: cold, export).
   From round 0.8.30 on, risk.py's selection applies to these files without exception. Left as is: the evidence
   entries (E076, E078, E081: paths as they were then). Comments fixed later the same day (C++ review of the round,
   user's choice): src/inf/vti_winusb.inf and src/broker/vtiusbaudio_libusb.c now name src/broker/vtiusbaudio.c.
   Both files are class C, so risk.py now also asks Linux power + sleep and Mac reopen, power + sleep. DECIDED with
   the user 2026-10-20: these are skipped this round, comment-only (vtiusbaudio_libusb.c: gcc -E -P output old vs
   new identical; the .inf differs only in a ';' comment line, its catalog is generated at install time).

## Round 0.8.28 (branch round-0.8.28, collecting; started 2026-10-19)

1. **C++ review of round 0.8.27 (2026-10-19), fixes - built and unit-tested on Linux only, NOT on hardware.**
   (a) The worker's "8 events, then one queued message" rule starved the DAW's events during a dump in pieces: the
   counter only reset when a queued message was popped, which never happens while a dump runs, so once 8 events had
   gone and more was queued, notes and clock waited for the dump's last piece (up to ~130 ms per dump; state uploads,
   fast patch changes). Now `EventRun` (dumppacer.h): each piece is the queue's turn. (b) `HeldParams::held()` held
   every SysEx from process() during a dump, also the sync ping (it lost its raw 3072 stamp, the marker would be
   wrong until the next anchor) and the transport tick; now only parameter pages 6E-72 (as stamprule.h). (c)
   vtiusbaudio.c: the SET_CUR retry always asks both endpoints (the `||` skipped IN when OUT failed); an unchecked
   malloc in the MI_00 open (cppcheck). Tests: 266 unit tests pass (2 new in test_dumppacer.cpp, the held test failed
   first); gui_duplicate_editors failed once and passed twice on rerun (flaky, unrelated). Windows broker
   cross-built. Not changed: the broker's rate-lever tries reset only when audio stops (both platforms, as
   intended); 10 older clang-tidy warnings in virusplugin.cpp outside the round. Static tools now on this PC:
   clang-tidy, cppcheck.

2. **Second C++ review round (2026-10-19), fixes - built and unit-tested on Linux only, NOT on hardware.** (a) The
   fix 1(a) only helped when a piece was due: between pieces the events still waited up to 10 ms, and (older, since
   the multi-dump hold) after a multi dump up to 250 ms once 8 events had gone - the rule asked "is something queued",
   not "can it go now". Now `queue_ready()` (dumppacer.h): a due piece while a dump runs, else a queued message that is
   not held; used in the first check and in the drain loop. (b) `access_param()` (stamprule.h) checks the Access header
   and is shared by needs_stamp and HeldParams. (c) vtiusbaudio.c: no malloc below the struct size (malloc(0) +
   cbSize write). (d) HeldParams::add with find_if (cppcheck). Result: cppcheck clean on the changed files,
   clang-tidy (bugprone/performance/analyzer) nothing on the changed lines; 266 unit tests pass (3 new, failed first).
   gui_duplicate_editors: failed only in the first ctest right after a build (4x) - the flake of round 0.8.22.
3. **gui_duplicate_editors flake fixed (test only).** Cause, reproduced with full CPU load (1/1 failed): the waiting
   instance tries to take over every 2 s, and the first one's teardown after the removal at 3 s takes longer on a
   busy machine; with --seconds 6 the next try fell after the end. Now --seconds 10 (7 s after the removal): 5/5
   pass under full load, also the first run after a build. The test talks to the real broker when one runs (here:
   the Virus connected, "device connected" in its log).

## Round 0.8.27 (branch round-0.8.27, collecting; started 2026-10-18 evening)

1. **Firmware loose ends closed (E245-E250, docs only).** `X:$48293` is the DSP identity strap (two DSPs, one image;
   master = HDR bit 15 set at reset), not a clock freeze. The audio-ring control words are the OTHER DSP's status and
   command words (same layout both ways; 0x123456+n composed by DSP code; 0x12345b forwards channel messages master
   -> slave); the master owns the stamp clock, the slave takes it +256 frames; unit = one sample frame (HIGH). The
   sync ping (page 0x73 param 0x22 value 0) is parsed by the DSP: both clocks reset, the slave renders the marker
   sample; 0x7E/0x7F = a test signal on/off (never send). Stamp compare is modular (+-8192 frames). F2 does not touch
   the stamp logic (the 2026-10-13 "ignored without F2" was session luck in the modular window). Rule-2 reassessment:
   ring A cannot be flooded (~88 kB/s needed), DEBUGcc traps HALT the DSP (then the host CPU blocks on the port =
   whole Virus dead, E241's symptom), the pool allocator is per patch set-up, so the stamped queue wrap (E238) that
   can feed 0xF4 host commands into the parser stays the only MIDI-driven path. Sync Information Event (0x54) origin
   still open (E250). protocol.md got the firmware paragraph.
2. **Mac export clock shift (E243/E244) - cause found and fixed, NOT on hardware yet (E249).** E246 shows nothing
   outside the DSPs moves the Virus' clock, so the plug-in's ring lost frames: the macOS IOProc (audioin.cpp) ignored
   Core Audio's input sample time; skipped IO cycles during the CPU-heavy export shortened the ring silently (eleven
   512-frame cycles = the ~5600 frames), every stamp then landed late. Linux is immune (the broker fills USB gaps).
   Fix: native/src/device/gapfill.h (silence for the missing frames, from the device's sample time; re-base on a
   restart or a jump > 5 s), used in io_proc when the time stamp is valid; "filled N" added to the stats line.
   tests/test_gapfill.cpp (5 tests), vtitests 242/242, full Linux build ok; the macOS code needs the CI build.
   **Mac test:** Live export (MC3-like), klog K equal before/after, "filled" > 0 in the stats during the export, no
   sync error page; then a cold start and a sleep (StallWatch unchanged). Not yet run: CI (ask the user).
3. **Remaining firmware points closed (E251-E254, docs only; user: firmware first, testing later).** The Sync
   Information Event is composed by the DSP (0x5411f -> SysEx sender 0x52484, cable 2, device id 0x10) on the first
   MIDI-clock tick whose newest stamp is behind its clock after on-time ticks; the host CPU relays the DSP's USB-MIDI
   frames unchanged (INT0 -> FUN_2644). So the alert means "our lead is used up", one event per transition (E251).
   The host CPU's parameter model is decoded (4-byte records, internal pages, translation table 0x8700 in flash page
   2, DSP words, 0x4F frames to the PC; E252) - with Ghidra asm scripts DisasFuncs.java / DisasAt.java in
   work/firmware/scripts. F034/F035 = .lod data images "wvd119.lod" (2012, likely wavetables) and "fvd102.lod" (2009),
   not code (E253). ESAI_1 is the DSP-to-DSP link, the master clocks it; ESAI is the codec/USB side (E254). The
   DSP ignores Multi "Original Patch" 72 00 03 (page-0x72 table Y:$47f53, params 0-6 -> ignore handler); the host's
   multi header path and issue #7's power-up effect stay unproven (E255). Still open (LOW value): the host CPU's
   page-0x72 parameter parsing (FUN_3ae4 state tables 0x3b38/0x3c76), the USB chip's handling of 0x4F frames, when
   the .lod data is streamed to the DSPs, the meaning of the DSP's cable-1 message 0B.
4. **Last open points closed (E256-E258, docs only).** Correction: F020-F034 are the 15 sectors of the main DSP
   stream, "wvd119.lod" is that image's own build stamp; F035 is a separate one-sector image at the same flash
   address (use open, LOW). The host's 0x4F frames are messages to the USB chip: keys = globals 0x0F SA Freq, **0x10
   Frequency (0 = 44.1 kHz, 1 = 48 kHz - the chip switches its audio clock)**, 0x11 Clock Mode, 0x1F input gain,
   0x2B SPDIF routing, commands 0xF5/0xF6; the host re-sends 0x0F/0x11/0x2B/0x1F at a USB connect but not 0x10.
   The host parses the PC's parameter SysEx into its record queue (internal page | 0x80 = not re-sent to the DSPs)
   and stores it; "Original Patch" lands in the multi edit buffer, the power-up effect (issue #7) stays unproven.
   **Device-side proposal for the 48 kHz state (E175/E241):** when 48 kHz packets appear, send the global
   `73 00 10 00` (System Clock/Frequency = 44.1 kHz, what the panel does) next to the SET_CUR retry - normal global
   parameter, rule-2 safe; code + tests + a hardware check in the next test round.
5. **Nothing left open that static analysis can answer (E259, E260).** REA on the Access updater
   (CAInstallerHelperDeferred): the flash bank comes from the chunk TAG, record addresses are ignored - F035 is
   flashed to page 0x23 (0x230000), F060/F061 are two copies of the USB firmware (0x3C/0x3D), ROM banks are
   skipped when their CRC is intact; no host code reads page 0x23 -> F035 ("fvd102.lod", 2009) is a legacy sector.
   USB chip 0xF6 = the serial number for the iSerialNumber string descriptor (never logged, rule 5). The DSP's
   cable-1 "0B" sender is unreachable code. Issue #7: at boot the host re-derives Original Bank/Patch from the
   multi arrays; only the current part is restored from the power-off snapshot at 0x4B00 - the exact trigger of
   "all 16 Init" is the one residual that only hardware could show, and rule 2 forbids that test. Firmware analysis
   is complete; next = testing (CI build, Mac export check, the 0x10 lever), not today.

6. **Firmware findings challenged against the code (2026-10-19 morning, assessment, no code change yet).** Checked
   E228-E260 against the send path (virusplugin.cpp pack/send thread), keepalive.h, sendbudget.h, the ping, the
   three gap fillers and the DSP listing. Holds: cable routing, write retry, modular window vs our lead, FE every
   300 ms, ping value 0 only, no F4 anywhere, sync-event meaning. Does not hold: E238's residency (the stamp leaves
   the plug-in A frames ahead of the newest ring frame, not the whole lead; the DSP parks only what is beyond the
   threshold: Linux/macOS A=1024 -> ~300-500 frames ahead, nothing parked; Windows A=3072 -> ~2400 ahead, ~1400
   parked = 30 ms); the 2026-10-13 stress hang (Linux, MIDI still answering) is therefore unexplained again. New from
   the listing: within the threshold the stamp is stored as the current stamp (0x55904) and the note-on handler
   0x53337 copies it into the voice's start slot X:$54145 - the threshold is the max per-voice delay, not "play
   now"; threshold = 1023 only while the counter X:$5412e is 0 (what it counts: open). Gaps found: unstamped
   messages (parameter SysEx, part dumps on patch change, CC resets, the resume state) inherit the previous stamp
   and are not counted by the budget (Windows: parked, 2 patch changes + a burst ~ the 2046 wrap); no explicit
   guard against 72 00 03; the Windows streamer lacks the E241 handling; E248's "host blocks on the port" vs
   E230/E233 "no ready check" contradict; E245's slave clock +256 raises a two-part timing question.
   **Tasks (order agreed 2026-10-19; TDD, local Linux build + user smoke test first, CI after). Done 2026-10-19
   afternoon (Claude alone, the Virus was NOT on this PC: nothing below is tested on hardware):**
   - [x] T1 docs: E261 (threshold semantics, residency per platform, E238 corrected, E248's host-blocks chain
         withdrawn, the two failure modes, the code gaps); protocol.md paragraph, sendbudget.h and automation.h.
   - [x] T2 code: SendBudget::take_queued - queued cable-2 bytes count as a debt capped at one burst (a 100 kB state
         send delays stamped traffic by 250 ms, not 25 s; the Virus paces dumps itself, E239); DeviceOut calls it
         after a queued cable-2 write (cable 3 records not). Test sendbudget_queued_bytes_are_a_bounded_debt.
   - [x] T3 code: native/src/device/stamprule.h needs_stamp(m, n, from_process): channel/F8/FE/F2/ping always, a
         parameter SysEx (pages 0x6E-0x72, 11 bytes) only from process(); pack() uses it (from_process = the slot
         had a stamp). tests/test_stamprule.cpp (3 tests). 2026-10-19 evening, Virus on this PC: one F5 per
         stamp value within a USB write (StampRun in stamprule.h, 4th test; the ping exempt) - with one F5 per
         message, automation at the 250/s cap cost 16 B/message = the whole 4 kB/s budget and the notes and the
         readiness probe starved behind it (E264: nativehost A/B on the device).
   - [x] T4 code: native/src/device/sendguard.h blocked_to_virus(): page 0x72 parameters **0-3** (Flags, Original
         Bank, Original Patch - NARROWED from the agreed 0-6: 4-13 are the multi name characters in the XML, the
         DSP's ignore range 0-6 cuts through the name; widen m[8] <= 3 if you disagree) and status F4; checked in
         DeviceOut::send and send_rt (counted on the audio thread, one log line from the worker). The start-up
         messages (72 pp 22, 72 00 1D) pass; nothing in the plug-in sends 0-13. tests/test_sendguard.cpp (3 tests).
   - [x] T5 broker (Windows): src/wrapper/vtiusbaudio.c - OUT packet via vti_pkt_out (same size, silent for 47-49
         frames), SET_CUR again once per second of 48 kHz packets, 3x, then the power-cycle line; vua_rate_lost()
         for the broker; mingw broker + classic wrapper build. NOT tested on the VM (needs the E175/E241 state).
   - [x] T6 broker (Linux + Windows): src/broker/ratelever.h (vti_pack_rate_44k1 = USB-MIDI packets of
         `F0 00 20 33 01 10 73 00 10 00 F7` on cable 2; vti_rate_report finds the Virus' `73 00 10 vv`); sent by
         both brokers' device threads next to the SET_CUR retry (3x per stream), the report logged when it arrives.
         **Open: the "global request" read-back** - no request format for a single global is in our evidence
         (spec.md has 30 for singles only), and rule 2 forbids guessing one; the Virus' own report is logged instead.
         Tests ratelever_* in test_pktstat.cpp. Hardware check in the next round (E175/E241 state needed).
   - [x] T7 static: E262 (1)(2) - X:$5412e = live part set-up records (allocated in a burst by the part/patch
         set-up 0x55fe5/0x547f8 via 0x54484, retired per tick by 0x5457d); the threshold is 1023 outside a patch
         change and shrinks during one; the input X:$5413b = 1536 while X:$54144 counts down, else max(2*y:$2a,
         192) from the setting X:$54a2c (<= 0x40). What one record drives stays LOW.
   - [x] T8 static: E262 (3) - FUN_605b/6113/6270 are plain stores to BOTH host ports (0x00xx and 0x01xx) with
         interrupts off, no ready check, no wait: the host CPU cannot block there; E248's chain withdrawn, E230/E233
         stand; E241's dead panel = the host CPU itself stopped (open).
   - [x] E263 (2026-10-19, static, no task number): the two DSPs' work split - both parse the whole stream, a note
         runs on the DSP whose identity matches bit 6 of its slot index, the master hands slave notes over through
         the ring command 0x123456 with the start stamp minus 256 frames. Prediction for T9: equal onsets if the
         slave's +256 clock and the ring path cancel; a ~256-frame offset would reveal the split.
   - [ ] T9 hardware (next test round): same note on two parts, onset offset (E245: slave clock +256; E263); Windows
         automation timing with and without T3; Mac export with gapfill ("filled" > 0, K equal); E241 fix on wake;
         the T5/T6 path if the 48 kHz state shows up. Plus the user's crash smoke test of this dev build (Linux).
         Done on the device 2026-10-19 (nativehost, no DAW; E264): start-up of the 0.8.27 build clean (no message
         dropped by sendguard, marker found, 0 drops, bank sync done, notes audible), 10 s of automation at the cap
         with notes: audio while the automation runs, 3 sync reports (0.8.26: probe unanswered, audio only after).
   - [ ] T10 optional: stamp the retry ping timeline-relative (today absolute 3072: up to 186 ms in the queue);
         an adaptive answer to the Sync Information Event (raise A or re-ping) - deferred until T9 says it matters.
   Build state after T1-T8: vtitests 251/251 (Linux, build-native Release), plug-in builds, Linux broker and
   mingw broker build (work/build), classic wrapper builds. Not run: CI, any hardware, the Mac.
7. **Issue #9 (2026-10-19, Reddit report, CachyOS/Arch, reporter not named):** plug-in loads but silent, Control
   Center closes at once. Assessment without logs: the Control Center binary needs gtkmm 3 (direct NEEDED: libgtkmm-3.0,
   glibmm-2.4, libsigc-2.0), not installed on Arch desktops -> loader abort = "spinning cursor, then gone";
   install.sh's hint names only apt/Fedora packages. Silence: usual first-install list (replug after the udev rule,
   E175, E135, Snow E115, PulseAudio). Reply posted on Reddit with the log commands. Done 2026-10-19: install.sh
   prints the Control Center's missing libraries and names the packages for Ubuntu/Debian, Fedora and
   Arch/CachyOS/Manjaro (gtkmm3; plug-in libs: xcb-util-cursor xcb-util libxkbcommon-x11 cairo pango fontconfig),
   README-linux.txt and the README's Linux requirements say the same (the website's Linux sentence does not mention
   gtkmm - check with tirevive-site-check at the next rebuild). Follow up when the logs arrive.

## Round 0.8.26 (branch round-0.8.26, collecting; started 2026-10-18)

**Round closed 2026-10-18 (user: "we close this round ... we prepare for the release").** Version 0.8.26 in
native/CMakeLists.txt. Round test build 0.8.26b = CI run 39148618705 (version input must be numeric for the MSI: 0.8.26; run 39148161706 with "0.8.26b" failed in the msi job) (0.8.26a run 39147901750 failed on macOS: writeretry.h was included only on Linux, fixed). Checklist
https://claude.ai/artifact/W8EcFqru6zvxirJGJY2c36 (spec scripts/checklists/round-0.8.26.json: core on Linux, VM,
Mac; L1/W1/M1 long notes (FE), L2/W2/M2 bank sync with panel changes (write retry), L3 per-monitor menus; real PC
optional, B1 + core). CI run 39148618705 all green; Linux build installed 19:18 (install.sh, version 0.8.26).
**OPEN, blocks the release: REAPER crashed after the LC3 render** (user, dev build of 18:51, editor open, host at
48 kHz block 256): plug-in log ends cleanly with "offline render ended" at 19:15:34, broker sees the client go at
19:15:35, no kernel segfault line, no core (ulimit -c 0, apport). Not reproduced by a command-line render
(REAPER -renderproject under gdb, editor closed, exits after the render: normal exit, WAV ok). Next: user runs REAPER
under gdb (command in the chat, output work/timing/LC3-crash-gdb.txt) and repeats LC3 with the editor open.
**Repeat 19:20 under gdb with the CI build (48 kHz, block 256, editor open): render OK, no crash** (user: "worked
now. maybe it was an issue with reaper?"). Status: WATCH, not reproduced (1 crash in 2 GUI renders, cause unknown -
not shown to be REAPER). If it happens again: the same gdb command catches it; keep running REAPER under gdb for
the rest of the Linux checklist if convenient.
**LC5 FAIL (E241, blocks the release):** after the wake the Virus stayed at 48 kHz (E175 state), the broker fed it
44/45-frame packets, loud signal on its headphones, no sound, then the Virus froze (power cycle). Fix in the broker
(silent same-size OUT packets for 48 kHz packets, SET_CUR again 3x) in test build 0.8.26c = CI run 39149779397 (all green), installed on Linux; needs a careful LC5 redo
(headphones off, monitors low).
**Linux block all PASS with 0.8.26c (E242)**; LC5 redo passed but the 48 kHz state did not recur (fix unexercised).
win11 VM: 0.8.26c installed silently (C:\tirevive-test\0.8.26c, entries 0.8.26), Virus attached (broker socket stopped on
Linux; re-enumerates on the broker stop, attach a few seconds later).
**VM block all PASS (WC1-WC4, W1, W2; checklist marks).** VM broker log: 0 write timeouts (retry unused on WinUSB too),
error 22 only where the Virus left Windows (WC4 power cycle 19:54:47, session end 20:06:58); W2 bank sync complete
(RAM D 128 fetched, 516 s). Virus detached from the VM, Linux broker socket started again. Mac: 0.8.26c zip unpacked in
~/Downloads/TI-Revive-0.8.26c-mac, the user runs the sudo installer.
**Mac block all PASS, real PC skipped (E243)** - but MC4 note: after the MC3 export the Virus' stamp clock had moved
~5600 frames against the marker, every note late (sync errors) until the Virus was power-cycled. Not the budget.
A/B done (E244): 0.8.25 has the same late notes after the export (K 512), only no error page -> old fault, not a
regression; 0.8.26's stamped FE makes it visible. Recommendation to the user: release with a known-issue line, fix
the clock shift next round. **User: "go ahead with the release".** Site check 2026-10-18 (round changes only;
the full check of 0.8.25 stands for the rest): TESTS 220 -> 237 (site + README), sleep claim gets the E241 case, three
firmware findings E237-E239 under "Problems found on the hardware" / "Reliability fixes found with the Virus", per-monitor
menus on Linux (E227), known issue Mac export (E243/E244) in the sync error FAQ. Release notes in build.yml.
After the checklist passes: full site check (README + site: firmware findings, Active
Sensing, budget, retry are user-visible stability changes - worth a line), release notes in build.yml, merge
round-0.8.26 -> main, tag v0.8.26, release CI, site rebuild by hand, install the release on Linux, VM (silent), Mac.

1. **Safety rule `72 00 03`** (issue #7 finding 1): never send the Multi parameter "Original Patch"; added to
   AGENTS.md rule 2 and the tirevive-test skill's safety list, pointing at issue #7 and E209 (their PR into
   claims.md is not in yet; point the rule at the claims entry once it is). Docs only, nothing to test.
2. Deleted the stray ignored file `--help` in the repo root (user 2026-10-18; it was a 907x513 PNG screenshot
   from 2026-10-17, written by a tool run by mistake).
3. **Mac double notes E209 (7): log analysis only** (work/timing/baseline-issue7/mac/M2-diag.log, session 3 =
   broker start 08:09:04, 16 s after the previous idle exit). klog K was 6024/6025 (= session 1, which also had
   5566 once), then 6382-6384 for the next 7 notes (+359 frames = 8.1 ms) from stamp ~1029117 on, with "timeline
   steps 0, anchors 0" in the stats: the plug-in did not move its timeline, so the Virus' stamp clock vs its USB 1
   output shifted by itself. Sync reports rose during playback and also with no sound for 20 s (6 at 08:10:06, 7 at
   08:10:18); sessions 1/2 had 0-1. Hypothesis (LOW, not tested): the Virus lost/re-took its sync in that session;
   the doubling itself is not visible in klog (it needs 2000 quiet frames; "no onset" lines are as frequent in the
   good sessions). Next: reproduce on the Mac with a recording the user makes in Live (resample track, no SSH
   permissions needed - user declined sudoers/TCC changes 2026-10-18), then compare onsets with timing_analysis.py.
4. **Issue #7 finding 2: code reading.** A missed ping is visible: after 5 s without a marker the log says "no sync
   marker - time stamps relative to the ring" (scan_audio), then up to 3 retries while quiet and not playing
   (process, marker_retries_). E209 (2) counted 2/2 Mac reopens after an idle exit with the marker on the first
   ping. TI Revive never sends CC120: the plug-in sends CC64 0 + CC123 per part at setActive(false), the broker
   CC64 0 + CC123 on 16 channels at a disconnect (vti_pack_notes_off), and host CCs are not passed on
   (getMidiControllerAssignment maps only aftertouch and pitch bend). So their exact trigger (CC120 + CC123) cannot
   come from TI Revive; remaining check: Mac, broker idle exit + reopen x3, look for the "no sync marker" line.
5. **Mac session checklist for 3 + 4:** https://claude.ai/artifact/Pg1eFZf4Fv7TCkJNmDA7a3 (spec
   scripts/checklists/mac-0.8.26-1.json: D0 recording track, D1 cold start, D2 three reopens after idle exit with
   a 30 s recording each, D3 if doubled: record + Stop/Play, D4 three more reopens). Mac on release 0.8.25 (pkgutil),
   diag.on + klog.on present. Before the user starts: rename the Mac's old diag.log; afterwards fetch diag.log,
   broker log and the recordings (project folder Samples/Recorded), run timing_analysis.py on each take.
   **Done 2026-10-18 (E225): all pass, D3 not needed.** 8 sessions, every first ping got its marker (Mac now 8/8
   reopens after an idle exit); 7 takes 480/480 onsets, 0 extra, jitter 0.16-0.29 ms; klog K constant, 1-2 sync
   reports per session. E209 (7) not reproduced (0/7), cause open: watch for it, no code change. Issue #7 finding 2:
   nothing to fix on our side (no CC120 sent, first ping answered). Files: work/timing/mac-0.8.26-1.
6. **Linux reconnects (E226, 2026-10-18, release 0.8.25 installed on Linux by install.sh):** new
   scripts/linux-usb-bounce.sh (sudo; sysfs authorized 0/1, optional --freeze PID). PresenceWatch bounced branch
   PASS on hardware (frozen host 3/3, 2/3 via the bounced branch; unfrozen 0.3 s drops 3/3, 2/3 bounced too);
   E175 cable 3/3 and power-off/on with a waiting host 3/3 recovered: E175 not reproduced (0/12). Likely why
   (LOW): since 0.8.21 the flash info read (~3 s) comes before the audio start, so the stream no longer starts
   while the Virus boots. E175 stays "watch", no code change.
7. **Per-monitor work area on Linux** (popup menus): _NET_WORKAREA is one box around all monitors (this PC under
   GNOME/XWayland: 67,32 4413x1408 over DP-3 2560x1440 at 0,0 and DP-2 1920x1080 at 2560,223), so a menu on the
   smaller monitor could open where it has no pixels. New native/src/editor/workarea.h (NativeRect moved there;
   work_area_at, parse_work_areas, clip_monitors; 5 tests, 225 in all) and popup_screen in popupwindow_x11.cpp:
   _GTK_WORKAREAS_D<desktop> (mutter: 67,32 2493x1408 and 2560,223 1920x1080) -> RandR 1.5 monitors cut to
   _NET_WORKAREA (KDE, Xfce, ...) -> _NET_WORKAREA -> the screen; the monitor at the anchor, else the nearest.
   Links xcb-randr (libxcb-randr0-dev installed here by the user, added to the CI apt list). Built + tests on Linux;
   dev build in ~/.vst3 (release 0.8.25 backed up in ~/.vst3-backup/0.8.25). **User test in REAPER PASS (E227):**
   menus stay on their monitor, submenu flips at the monitor border, no crash. RandR path untested (GNOME has the
   GTK property). Needs CI build (deps changed) before the round's test build. ctest installer_scripts "Not Run": stale PWSH_EXECUTABLE in the build
   cache (an old scratchpad), unrelated.

8. **Firmware analysis started** (E228-E230, 2026-10-18, user's idea: "decompile the firmware and check it against
   our implementation"). firmware.bin decoded (IFF FORM, 32 KB flash sectors with 3-byte addresses), chips named:
   host CPU = 8051 family with a 7-bit flash page register at XDATA 0x02E0, USB controller = TI TUSB3200A/TAS1020
   family (8052; its firmware F060 holds the USB descriptors and streams the audio), synth = DSP56300 (no
   disassembler, not analysed). USB-MIDI packets: USB chip 64-byte ring, holds the endpoint (NAK) below 32 free
   bytes, 9-bit UART with 4-byte frames to the host; host parks a packet when its ring has < 8 free bytes; cable
   5 (F5 stamps) goes straight to both DSPs, cable 2 to the parser ring and the DSPs, 3 to the parser ring, 0 to
   DIN out. Host rings 1 KB each; full ring = reset + "MIDI Buffer overflow". Tooling: REA cannot open 8051/56k,
   so Ghidra 12.1.4 headless with own scripts; everything in work/firmware/ (git-ignored, Access material).
   REA updated 6.3.0 -> 6.7.0 (setup --client claude_code, effective after a restart). Open: DSP side of a
   parameter flood (rule 2), handshake pins, sync event origin - see the memory note project-firmware-analysis.

9. **Send rates vs the firmware (E231) and a DSP disassembler (E232).** Our 250/s automation limit is far under the
   link's ~4 k packets/s (LOW); the hazard is bursts (one block's events go out in one write of up to 3072 bytes,
   each stamped message = 2 packets) and totals above the link rate, where the USB chip's NAK makes the broker's
   write block and the host CPU's parking rule drops frames. No code changed. DSP56300: the emulator core
   github.com/dsp56300/dsp56300 (GPL-3) has a standalone disassembler (built: work/firmware/tools/dsp56kDisassemble,
   listing work/firmware/out/dsp_main.asm, 126k lines) and the Ghidra module github.com/yatli/ghidra_dsp56k
   compiles with one fix and is installed as a user extension (DSP56K:LE:24:default, byte-swapped image). Next:
   the DSP's host-port receive handler (what happens to MIDI bytes when the host writes faster than the DSP
   drains) - the missing half of the rule-2 hang.

10. **DSP host-port receive handler found (E233, corrected the same day).** P:$5237d (HI08 receive vector): two
    unchecked X-memory rings - 128 words for the live MIDI byte stream (one byte per word; USB cables 2/5 and DIN
    input) and 512 words for the host CPU's own complete 3-byte messages; drained once per main-loop tick (0xd8f)
    into the MIDI byte parser 0x522a8 (tables Y:$482ca/Y:$482d1: program change ignored by the DSP, active-sensing
    watchdog of 750 ticks). The live ring is only 128 bytes deep: more than that between two ticks corrupts the
    stream silently. The DEBUGNE in the channel path checks a register invariant (m1 linear), not data.
11. **DSP image decoded, traps assessed, F5 stamp machinery read (E234, E235).** Sector headers + host-bootstrap
    stream + second-stage loader records (type/address/count); memory map; main code at P:$4f000-$6f8bc; tick
    order in 0x53840. 16 real DEBUGcc assertion sites, all bounds checks on internal pointers, counters and pools,
    none on MIDI data -> no proven DSP hang mechanism yet (open: pool allocator 0x53b0f, per-part counter 0x55fb4).
    F5: 14-bit stamp (low 7 bits first) vs clock X:$55865; late or within the threshold (<= 1023 units) = play
    now, else queued in a 2046-word ring and played per tick; a gate holds the live stream while the queue plays.
    E236: the stamp clock is a control word the USB chip delivers inside the audio-frame ring (16 words per
    frame, column 11 of every second frame after a sync marker), plus a fixed lead of 256 units; one tick = 64
    units (a 64-frame block if the unit is a sample frame - LOW). Ring B is the host CPU's own command channel:
    'F4 <cmd> <value>' sequences (private F4 handler 0x5256c) - never send F4 from the plug-in (rule 2).
    Still open: who composes the control words (not literal in the USB chip or host code), the unit, X:$48293
    (clock freeze, host-set?), the F2 dependence of stamps seen in protocol.md. Scripts:
    work/firmware/scripts/dsprecords.py; listings work/firmware/out/dsp_P_000000.asm, out/dsp_X_04f000.asm
    (git-ignored, rule 1).

12. **Active Sensing fixed from the firmware (E237, E238; code change, not yet on hardware).** The DSP arms a
    watchdog with the first FE and releases every voice ~1 s after the last one; the plug-in sent ONE FE per
    anchor (one-shot note cut a second after every anchor/re-anchor). Now: FE every 300 ms on the timeline
    (native/src/device/keepalive.h, test_keepalive.cpp; vtitests 227/227, Linux build ok). E238: the DSP's
    stamped-event queue is 2046 bytes without a fill check = the first firmware number that matches the old
    hang threshold (1000/s automation + 2750 notes/s = 3.6 kB per 160 ms lead); rule: < 10 kB/s stamped traffic,
    bursts < 1500 bytes - now enforced for ALL stamped traffic by native/src/device/sendbudget.h (4 kB/s, burst
    1000 bytes, VTI_NATIVE_MIDI_RATE=0 switches it off; test_sendbudget.cpp; vtitests 230/230). Hardware test
    needed: (a) held notes across the first seconds after start and after a host stall - no note cut, FE every
    ~300 ms in VTI_TRACE; (b) normal dense playing + automation - nothing delayed (VTI_NATIVE_STATS); (c) the old
    stress test (1000/s automation + a note flood) - the Virus must survive, events may be delayed/dropped.

13. **The USB-chip -> host-CPU link is lossless (E239), so the broker now retries timed-out writes.** The chip
    sends a byte only after the host toggled its handshake pin for the previous one; a parked frame (host ring
    nearly full) stops the chip, its ring fills, the PC gets NAK. E229's "frame lost" is corrected. Consequence:
    a 1 s transfer timeout means the Virus was busy for a second (patch load), not that data vanished - the old
    broker dropped the rest of the message (a broken SysEx dump), now vti_write retries the packet up to 5 times
    (src/broker/writeretry.h, both libusb and WinUSB; test_writeretry.cpp; vtitests 235/235; Linux + mingw
    brokers build). No pacing of SysEx dumps is needed: the Virus paces them itself through this chain.
    Hardware test: a bank upload while switching patches on the Virus' panel; look for "timeout(s), then
    accepted" in the broker log and no dump error.

14. **Dev build smoke test passed (E240, 2026-10-18).** Local Linux build of items 12-13 on the real TI2 in
    REAPER: "all sounds really good and stable"; broker log clean (no write timeouts or errors, so the retry
    was not exercised). Still open for the CI build / full checklist: held notes across start/stop and
    re-anchor (FE fix), a bank sync during panel patch changes (retry), Windows and macOS.

## Round 0.8.25 (released as v0.8.25 2026-10-18)

Same process as 0.8.24 (Linux checks only during the round; test build + one checklist later). In this round so far:
1. **Create Empty Library** (E217, commits a466a8b + 43e15de; user 2026-10-18: "replicate the original as close
   as possible"): entry last under "Personal Libraries" in LOAD, SAVE and the browser's bank menu; opens the
   original's save dialog (title "Save empty library as...", filter "Virus TI Patch Libraries" *.mid, library folder,
   "New Library.mid"), ".mid" added like the original, 26-byte empty SMF written, listed at once. Linux: VSTGUI's
   zenity/kdialog dialog (modal, blocks the host UI like every VSTGUI plug-in; without zenity/kdialog nothing happens).
   Xvfb check passed (dialog, OK, file byte-identical, listed). Windows/macOS: not built locally (CI later).
   Separator done with item 3.
   Checklist item later: LOAD > Personal Libraries > Create Empty Library (dialog, OK), SAVE into it, LOAD from it.
   Dev build NOT installed in ~/.vst3 (release 0.8.24 stays); user smoke test of the dev build still to do.
2. **Issue #7 finding 3 on Linux** (E218): 6 start-ups after broker idle exits with a full state restore, release
   0.8.24: ready 6.7-6.9 s, marker every time, normal sound, no hang (9/9 with E209). No code change.
3. **Menu look like Windows 10** (E219, commit b929bcb): measured in the user's logged-on win10 VM session with our own
   test menu (scratchpad sep/menumeasure.c): menus with columns = the older look we had (19 px rows, separator 9 px);
   single-column menus = the themed look (22 px rows, check band, chevron, separator 7 px). Ours now draws every
   one-column menu themed (LOAD/SAVE top level, Personal Libraries, value lists <= 32, categories) and has separator
   rows; Create Empty Library gets its separator. 217 tests; Xvfb check of LOAD > Personal Libraries (+ greyed
   "(empty)"). Not seen on screen: a checked entry in the themed look. Checklist items later (W especially: compare
   with the original's menus side by side if the user wants).
4. **Project compatibility test** (2026-10-18, with the user): two Ableton Live 11 projects saved with the ORIGINAL
   on the win10 VM (restore-test-vst3.als = original VST3, class id = VTI_CID_ORIGINAL; restore-test-vst2.als =
   original VST2 'VCP ' 0x56435020): parts 1-3 Fimplex HS / Acidia AV / Alead BC, MIDI clip, automation Cutoff part 1
   and Reso part 2 (Easy page). Copies: work/projcompat/ (not in git: contain the original's state), real PC
   C:\tirevive-test\projcompat, win11 VM same, Mac ~/Downloads/projcompat. Linux (nativehost, release 0.8.24): the
   state loads (244 records, part 1 Fimplex HS, getState identical). Checklist
   https://claude.ai/artifact/5qa9fHxYrLiPG1CnMBViVV (spec scripts/checklists/projcompat-1.json: PW1-3, PM1-3,
   PB1-3). Expectation for the VST2 project: "missing" (we declare no VST2 id) - untested.
5. **Revert to / Recently Used DONE** (E220, commit 9dcd74f) + themed menus' 3 px bottom strip. 220 tests.
6. **Live 11 does not open the original's projects** (E221): fix = second VST3 class under the original's id
   "TI Revive Virus Control (original projects)" (commit d58e175, test factory_classes; user: keep two entries for
   now, decide before the release whether to take over the id - after the Live 11 redo and the Live 12 data).
   CI run 39124266237 failed on Windows: my library_create_empty test kept a file open while deleting its folder
   (Windows throws -> abort 0xc0000409); fixed + test runner names each test first (9dcd74f). 0.8.25a (CI
   39124817970) still failed PW1: the "original's id" B49F90D9735F4305.. was the raw TUID bytes, never the FUID;
   the real one is D9909FB45F730543AC5397BE8958CD31 (E221 correction, 2a6d97c; also fixes the moduleinfo entry).
   **0.8.25b (CI 39125467481) on the win11 VM: PW1 + PW2 pass, PW3 (VST2) fails as expected (E222).** Plug-in
   version was still 0.8.23 (also in the 0.8.24 release) -> 0.8.25 (87eef8a) + site-check rule. **Mac (Live 12.4.6,
   0.8.25b installed): PM1 + PM2 pass, PM3 VST2 fails as expected (E223). Real PC skipped (user). Test complete.**
   Site check 2026-10-18 (round items only; full check before the release): "projects of the original open" was
   wrong for 0.8.24 in Live 11 -> now "VST3 projects open (since 0.8.25, second entry), VST2 do not" (site +
   README); TESTS 214 -> 220; menu look, Revert/Recently Used, Create Empty Library added (since 0.8.25). NOT live
   (site from main by hand). Still to do at the release: retake docs/images/plugin-menus.png (old look). User
   recommendation taken: keep two entries. Round checklist plan (user: UI on Linux is enough): Linux core + menus +
   Revert/Recently Used + Create Empty Library; VM and Mac core + Create Empty Library dialog only.
   **Round test build 0.8.25c = CI run 39130131007 (7b49ada). Checklist https://claude.ai/artifact/YV55z8yQ737u5JpLcNAx93
   (spec scripts/checklists/round-0.8.25.json: L1-L3, W1, M1; no core items and no real PC this round - user).
   Plan (user): ship 0.8.25 soon; live site stays until then.**
   **Round results (E224): L1-L3, W1, M1 all PASS. Ready for the release steps:** full site check, retake
   docs/images/plugin-menus.png (themed look), release notes in build.yml, merge round-0.8.25 -> main, tag v0.8.25,
   release CI, site rebuild by hand, install the release on Linux (user runs install.sh), VM (silent), Mac. Linux ~/.vst3 = dev build 9dcd74f (backup of 0.8.24 in
   ~/.vst3-backup/0.8.24). User smoke test of the earlier dev build (menus, Create Empty Library): ok.
win10 VM: 5 crash dumps since 2026-10-10 (bugcheck 0x139 at 13:06 today while Live + the Access driver ran with the
Virus passed through) - the VM is a reference machine only.

Not possible alone (need the user): PresenceWatch "bounced" branch on hardware (a sub-second USB drop: needs sudo
for /sys/bus/usb/devices/1-8/authorized, or a cable pull + replug within ~0.3 s); Linux replug known issue (E175,
needs a physical replug); per-monitor work area on Linux (xcb-randr: libxcb-randr0-dev not installed, needs sudo +
CI apt list); E180 pixel comparison (needs the 0.7.x wrapper rebuilt + installed in ~/.wine, see
native/tools/uicompare/README.md; "Virus TI (TI Revive).vst3" is no longer in the prefix); Mac double notes (E209 (7)).

## Round 0.8.24 (branch round-0.8.24, collecting; new test process 2026-10-18)

Process (agreed with the user 2026-10-18): fixes collect on this branch; I check on Linux only (tests, build, own
TI2 runs, user's short crash smoke test of a dev build); the round's test build + ONE checklist come when ~5-10
changes are in, something users wait for is ready, or the user asks. Checklist = core items from
scripts/checklists/core.json (checklist.py merges them with `"core": "core.json"`: cold start, reopen after broker
idle exit, export, Virus off/on, sleep; ~15-20 min per system) + one item per change on the systems it affects.
Failures go back into the batch; hotfixes only for crash / data loss / broken install.

In this round so far:
1. Sleep: a stall re-sends start-up messages + state like a reconnect (E210); Linux sleep works (E211, via the
   kernel's USB reset). Needs: Windows real PC + Mac sleep (core item C5).
2. StallWatch false alarm on every first connect fixed (E209 (5), test stallwatch_starting_stream_is_no_stall;
   Linux checked E211). Needs: core C1 on all systems (no "audio stood still" line).
3. Docs only (skill stocktake, 9cf52c3): tirevive-diagnose table refreshed (Snow works per E188, "latest release"
   instead of 0.8.0, Driver Extensions fix, replug and sleep rows, 0.7.x buffer row re-sourced to E111/E123);
   tirevive-pr (native first, real evidence.md columns, PR template, maintainer rules); tirevive-site-check
   (core.json / baseline-*.json, full style rules); Snow wording in AGENTS.md and CONTRIBUTING.md. No test needed.
**Fix for LC5 (E213, 2026-10-18): the Virus gone-and-back in 0.27 s at wake, unseen by the worker -> broker client
counts arrivals, PresenceWatch reconnects on a changed count. 214 tests. Dev build in ~/.vst3 (backup of 0.8.24a in
~/.vst3-backup/0.8.24a). L6 (LC5 redo, dev build) PASS (E214). CI test build 0.8.24b = run 39110354589 (version 0.8.24,
green): Linux installed (= CI), real PC setup in C:\tirevive-test\0.8.24b (old 0.8.24a setup removed), Mac zip pushed
+ unzipped in ~/Downloads/TI-Revive-0.8.24b-mac (user runs the sudo installer, then I check pkgutil), VM stays on
0.8.24a (WC1-WC4 passed, path untouched). E215: MC1-MC5 PASS on 0.8.24b (Mac sleep fix confirmed). Real PC ran
0.8.23 (setup never run) -> BC1-BC5 cleared, new item B0 (installed version check). ONLY OPEN before the release:
B0 + BC1-BC5 on the real PC with 0.8.24b (setup in C:\tirevive-test\0.8.24b). Then: site check, merge, tag v0.8.24.**
**Round complete (E216): all 21 items pass. Site check before the release (2026-10-18): tests 208 -> 214; sleep moved
from "Being tested now" (section removed) to the solved list (site + README); release notes "New in 0.8.24"; timing
summaries 0.01-0.04 -> 0.01-0.33 ms std. dev. (user: honest range; this round's exports 0.16-0.33 ms) + 0.8.24 Windows
PC row in both tables. Unchanged: Linux replug known issue (not retested), coverage 94 % (16 Oct).**
**Baseline results (E212, 2026-10-18): NOT releasable yet.** Linux LC1-LC4 pass (false alarm gone), LC5 sleep FAIL:
the stall path (no USB reset this time) sends set-up + state, Virus ready, but no sync marker afterwards -> timeline
never re-anchored, silent. Next: trace in code why no ping/marker follows the stall path (reconnect path E211 gets
one), test first. VM WC1-WC4 pass. Mac ran 0.8.23 (pkg not installed): redo MC1-MC5 with 0.8.24a (or the fix build).
Real PC BC1-BC5 not run: user does a bigger Windows run later.
**Plan (user 2026-10-18): test, merge to main, release 0.8.24, then the next round.**
Test build 0.8.24a = CI run 39105250660 (version 0.8.24, from sleep-reconnect fe7b196; the branch has no code
change since, only docs/checklist tooling, so no new CI run). Installed: Linux (install.sh, plug-in hash = CI), win11
VM (setup.exe /quiet, entry 0.8.24, VM left running, Virus not attached yet), Mac (zip pushed + unzipped in
~/Downloads/TI-Revive-0.8.24a-mac, user runs the sudo installer), real PC (setup.exe in C:\tirevive-test).
Checklist https://claude.ai/artifact/CPe7iSh2gaTduJHBxaXeeG (spec scripts/checklists/round-0.8.24.json = core only:
LC1-5, WC1-4, MC1-5, BC1-5). After all pass: merge round-0.8.24 into main, tag v0.8.24, release CI, site check +
site rebuild by hand, install the release on the machines.
Candidates for the round: Mac double notes after a reopen (E209 (7), watch); issue #7 PR review + `72 00 03` safety
rule when it comes; E180 pixel comparison, E175 (old list).

## Baseline round results (2026-10-18): E209, E210

All systems on release 0.8.23 (Linux installed by the user, Mac pkg installed by the user, Windows real PC).
Logs and renders: work/timing/baseline-issue7/{win,linux,mac}. Results: issue #7 findings 1 and 3 not seen, finding 2
not reproduced (6/6 reopens after the broker's idle exit got the marker on the first ping; E209). Windows catch-up
B4-B7 pass (export 0 ppm / sd 0.04 ms on the real PC). Open, in this order:
1. **B8 Windows sleep FAIL (E210):** after wake the plug-in re-anchors and gets markers, but the Virus stays silent
   until the plug-in closes. The stall path (E208) only raises the generation; a reconnect also sends the start-up
   messages + state. Fix candidate (test first): a stall = a reconnect (start-up messages, state, readiness probe).
   Then sleep on all three systems (Mac M3 of 0.8.23 and Linux L4 never ran with the fix).
2. **StallWatch false alarm on every first connect (E209 (5)):** the starting stream counts as a stall; reset the
   watch when a stream (re)starts. Same branch as 1.
3. macOS double notes after the last reopen (E209 (7)), 9 Sync Information Events; cause open, watch for it.
4. Not done as planned: three reopens per system (1-3 were done); B6 has no file. Repeat with the next round if needed.
Reply on issue #7 with the results once their PR is in (or before, if the user wants).

## Issue #7 (2026-10-18): findings from a separate macOS TI2 project (wirrareka), waiting for their PR

Four hardware reports from their own code (TI2, macOS 26, Apple Silicon), not reproduced by us. Replied twice (personal
+ technical, with E031/E112/E136/E139/E145/E149); asked for a PR into `findings/claims.md`. When it comes: review
(no Access material, claims format), then move what we reproduce to evidence.md. Our side, checked in code only:
1. Multi "Original Patch" write `72 00 03` -> all 16 edit buffers Init at the next power-up (2/2). TI Revive never sends
   it (ParamInit = part MIDI channels + Panel Part only; not in automation; bulk multi dump ignores the byte). To do
   after the PR: a safety rule "never send `72 00 03`" - DONE in round 0.8.26 (AGENTS.md rule 2, tirevive-test);
   still to do after the PR: point it at the claims entry.
2. Panic (CC120 + CC123) before close -> no sync marker in the next session (5/5). We send CC64 0 + CC123 on
   setActive(false), no CC120; Linux broker resets the port on close, macOS/Windows do not; marker retry (3x) would
   hide a missed first ping. Open check (Mac, read-only): first ping after a broker idle exit + reopen answered? If
   not, try without CC123.
3. Heavy restore after a reopen hangs the Virus (full-scale on all six inputs, until a power cycle). Our pacing (E145
   250 ms pause, automation rate limit, one readback E149) should avoid their trigger. Open check (Linux): several
   start-ups after broker reopens in one power-on, watch for a hang. They test pacing and report back.
4. Panel knob drift (Osc1 PW 0 -> 21/22 by itself). Nothing to do; note for unexplained readback differences.

Baseline checklist for checks 1-3, same items on all systems with release 0.8.23:
https://claude.ai/artifact/TsNG9uUTrf9Do1VMR441oz (spec scripts/checklists/baseline-issue7.json; B1-B3 bare-metal
Windows first + catch-up B4-B8 since 0.8.20: menus/REMOTE, export+freeze, aux track, Virus off during export, sleep; then L1-L3, M1-M3). Windows logs: C:\tirevive-test\B*-diag.log on the Windows partition
(nvme2n1p2, mount read-only from Linux after the reboot).

## Previous state (2026-10-17 late: branch round-0.8.23, offline render)

**Released v0.8.23** (tag on main bcec1ce, CI run 39060420044 all green incl. release; assets setup.exe, msi, mac zip, linux tarball, tirevive-usbinfo.ps1, libusb source). Website rebuilt by hand (site run 39060784148), live: Version 0.8.23, 208 tests, offline/sync error help, original-problems section. Test machines: win11 VM release installed silently (0.8.23, plug-in has the sleep fix); Linux: install.sh now asks sudo for the udev rule - user runs it (still the 0.8.23a test build without the sleep fix until then); Mac asleep, release zip in ~/Downloads/TI-Revive-0.8.23-release on this PC, push when awake. Next round: Mac sleep test (E208), L4. Before: **Round 0.8.23 done, release prepared (state 2026-10-17 21:xx).** Results E207 (all pass except M3 Mac sleep FAIL -> fix E208 StallWatch, untested, user: release with it, test with the next round; L4 Linux sleep not tested). Site check before the release: release notes say the Windows test was in the VM; sleep sentence fixed (the failing build was 0.8.23a, not 0.8.22); USB-port advice sourced in E205. Version 0.8.23, 208 tests. Earlier state: **Branch `round-0.8.23` (pushed; commit 8574a72 ...).** CI test build 0.8.23a = run 39057457787 (version 0.8.23; run 39057093527 was 0.0.1 by mistake, discarded); installed: Linux (install.sh), win11 VM (bundle uninstall 0.8.22d + setup.exe /quiet, one entry 0.8.23); Mac asleep, package in ~/Downloads/TI-Revive-0.8.23a-mac on this PC, push when awake. Checklist https://claude.ai/artifact/LC2ixtX9PCrDWfHP5e7cJs (spec scripts/checklists/round-0.8.23.json: L1-L4, W1-W2, M1-M3). Also done: sync error page like the original (E205, SyncAlert, 203 tests; user wanted it before the release); offline render + sync error as features on README/site; REAPER offline render 0 ppm / 0.01 ms (E204). Started from the user's web research on what owners
reported about the original plug-in (community claims C11-C18 in claims.md, with links). Done: (1) offline render
(E203): the original refuses it (warning, canDo offline = no); the native plug-in now paces offline blocks to the
Virus (native/src/device/offlinepace.h, VirusPlugin::wait_offline / report_offline; start wait once per render, anchor
in that block, 1 s timeout per block), 6 new unit tests (196), nativehost --offline T; measured on Linux with the TI2
(1.00x, onsets identical to real time). (2) README + site: section "Problems in the original Access plug-in" with
the community reports and their links (identical texts), "Export and freeze at real-time speed", the stale Windows
"sync error can appear once" line removed (the native plug-in never shows that page: it does not read the Virus'
Sync Information Event 0x54 - open question for the user whether to show/log it), test count 196.
Next: user dev smoke test on Linux (REAPER: render with "Full-speed offline" and "Online render"), then CI test build
and a checklist with: offline render per OS/DAW (Ableton export, REAPER, Logic offline bounce via AU), (1) sleep/wake
with the plug-in loaded (C17), (2) the plug-in on a return/aux track, delay compensation (C18), the 1 s timeout
(Virus off during an export). The site must not go live with the offline line before the release.

## Previous state (2026-10-17 evening: v0.8.22 released)

**Released v0.8.22** (tag on main 5116102, CI run 39051260777 all green incl. release; assets: setup.exe, msi, mac zip,
linux tarball, tirevive-usbinfo.ps1, libusb source). After the tag (user): menus framed as "reworked, modernized, the
same on Windows, macOS and Linux" in README, site and the GitHub release text (edited with gh release edit, only the
menu paragraph), screenshot docs/images/plugin-menus.png (LOAD > device > RAM A, Xvfb with a grey root, the installed
0.8.22d plug-in with the TI2) in README and on the site. Website rebuilt by hand (site run 39052054849), live: 0.8.22,
190 unit tests, "Menus, reworked" with the image. Test machines: Linux and win11 VM have 0.8.22d (same code as the
release), the Mac 0.8.22d (user installed). Virus on Linux. Next round candidates: from the older lists below (E180
pixel comparison, inactive text strength, bare-metal list, E175).

## Previous state (state 2026-10-17 night: round 0.8.22, popup windows done on Linux, CI for Windows/macOS)

**Results 0.8.22c (user, 2026-10-17):** L6 pass, L7 FAIL (Alt-Tab did not close the menu on Linux: no focus token
there -> fixed, _NET_ACTIVE_WINDOW, not tried yet), W4 pass, W5 pass, W3 pass, W2 FAIL ("Knob Name is working but the
dropdown not. Dropdown acts like a textbox." -> E202: the VM's Access DLL is the 32-bit one, our knob template finder
read only 64-bit pointers (0 templates, the arrow opened nothing); fixed (PE32, 35 of 35 identical), not re-tested).
macOS postponed by the user. Evidence now E001-E202. Dev smoke test of both fixes on Linux (Alt-Tab closes, no crash): user "working" -> CI test build 0.8.22d. CI run 39050163917 green: Linux installed (./install.sh as user, ~/Downloads/TI-Revive-0.8.22d-linux), win11 VM 0.8.22c uninstalled + 0.8.22d silent (plug-in 16:40 UTC build), Mac package downloaded to the scratchpad (macOS postponed). Checklist v8: L8, W6.
**Round 0.8.22 COMPLETE (user, 2026-10-17):** L8 pass (Alt-Tab), W6 pass (REMOTE knob templates with the 32-bit DLL), M5, M6, M2, M3, M4 pass on the Mac (0.8.22d). With L3, L5, L6, W3, W4, W5 every item passes. The Virus is on the Mac (VM entry detached, Linux broker socket started). Next: merge round-0.8.22 into main, release 0.8.22 (site check, release notes, website screenshot of a menu beyond the editor). Release 0.8.22 (user: "ship it"): VERSION 0.8.22, release notes in build.yml (menus, REMOTE page), site check 2026-10-17: TESTS 165 -> 190 (site + README), REMOTE page feature line added (E196-E201, L5/W6/M3/M4), Menus block (L6/L8/W4/W5/M5/M6), coverage 94 % (16 Oct) kept with its date, E175 still "being worked on"; menu screenshot for the site still open.

**Done on round-0.8.22 (after 838544a):** items 1 + 2 of the plan below. Popup menus are top-level windows of their
own on all platforms (native/plugin/source/popupwindow.h + _win.cpp / _mac.mm / _x11.cpp): one window per open
menu, never activated (Windows: WS_POPUP tool window, the VSTGUI child's button press bypasses its SetFocus;
macOS: non-activating NSPanel at NSPopUpMenuWindowLevel; X11: override-redirect, _NET_WM_WINDOW_TYPE_POPUP_MENU),
placed in the monitor's work area at the display scale (Windows: GetDpiForWindow, Linux: the host's content scale,
macOS: points). PopupView is now an invisible modal view over the editor (keys; a click into the editor closes) with
a 15 ms timer: submenu wait, a press outside menus + editor closes, a focus switch closes (Windows foreground
window, macOS app active; Linux: none). Submenu delay in PopupCascade (Windows' SPI_GETMENUSHOWDELAY, 400 ms on
macOS/Linux): highlight at once, submenus switch after the wait, reaching the open submenu keeps it. Patch/bank
menus back on our popup on Windows/macOS. Fallback inside the editor: VTI_NATIVE_INLINE_POPUP=1 or no window.
**REAPER crash fixed (dev build, user test 2026-10-17):** choosing a preset from LOAD crashed REAPER (SIGSEGV,
REAPER's main loop called a freed handler: rip 0, gdb on the real desktop). Cause: the menu closed from a timer
callback (Call::later / its 15 ms timer) and stopped timers / destroyed its X11 frames (their redraw timers) there;
REAPER keeps plain pointers to handlers (as in E183). Fix: PopupHost per editor = a pool of menu windows + one timer,
kept until VirusEditor::close; menus hide and reuse windows, a click is handled in the event (no Call::later).
nativehost now aborts when a handler is unregistered inside another handler's callback (VTI_HOST_LAX=1 turns it
off); the flow LOAD > choose (part 16) > reopen (pool reused, RAM B) > desktop click > editor click passes with it.
New rule (user): dev build on Linux first, CI only after the user's OK (memory). User OK on the fixed dev build in REAPER (2026-10-17: "It works") -> CI test build started. Order (user): dev phase = short crash smoke tests only; after CI the Linux CI build is installed first, and the checklist covers all platforms as before (CI run 39048281922 green, version 0.0.1: Linux unpacked ~/Downloads/TI-Revive-0.8.22c-linux/TI-Revive-0.0.1-linux, ./install.sh as normal user (never sudo: it refuses root); run 39048629581 with -f version=0.8.22 for the win11 VM MSI + Mac; checklist v6 same link: L6, L7, W4, W5, W2, W3, M5, M6, M2-M4). win11 VM: 0.8.22b uninstalled with its cached bundle (/uninstall /quiet), then TI-Revive-0.8.22c-setup.exe /quiet (same version number: no reinstall over it, E166); Virus attached to the VM (all interfaces OK); Mac push started (Mac may sleep). Installed: symbol dev build
(build-dbg, RelWithDebInfo, .so.dbg) in ~/.vst3; backup of 0.8.22b in the session scratchpad.
Earlier check on Linux (Xvfb + nativehost with the TI2): LOAD menu beyond the editor, RAM A ->
patches across RAM B keeps RAM A, desktop click and editor click close, Osc 2 list, 200 % editor (menus at 100 %),
editor closed with a menu open (exit 0). 189 unit tests. CI run 39046709950: Windows built, macOS failed (missing
include, fixed); next CI only after the user's dev-build OK.
Open: Linux work area is _NET_WORKAREA (spans all monitors, no RandR); ctest gui_duplicate_editors failed once in
4 runs right after a parallel build (flake?). Next: CI green -> test build, install on Linux/VM/Mac, checklist
items L4 redo + menu items for W and M (the Windows/macOS menus are new code: hover delay, outside click, Alt-Tab /
Cmd-Tab closes, HiDPI placement, a menu near the screen's right edge opens submenus on the left).

## Previous state (2026-10-17 evening: round 0.8.22 paused after the Linux tests)

**Results 0.8.22b (user, 2026-10-17):** L5 REMOTE page PASS (LCD on a double-click, text fields fit); L3 pass (earlier
build). L4 patch/bank menus: loading works but the menu is still not good: bound to the plug-in window; when moving
from RAM A to its patches the pointer crosses ROM W..Z and switches the patch list; moving to ROM A shifts the whole
cascade left over the editor. W1-W3 and M1-M4 not tested yet (paused by the user).
**User decision: our own popup on ALL platforms, but not bound to the plug-in window** (like the original's Windows
menus, which are windows of their own). Next round, in order:
1. Popup menus as their own top-level windows (Windows: a WS_POPUP window, macOS: a borderless NSWindow/NSPanel,
   Linux/X11: an override-redirect window), each hosting a VSTGUI frame with our PopupView/PopupCascade; positioned in
   screen coordinates, clamped to the monitor's work area instead of the editor; the patch/bank menus back to our
   popup on Windows/macOS too (this round they use the system menus, round-0.8.22 a7.. "system menus" commit).
2. Submenu behaviour like Windows menus: a short hover delay before a submenu switches and keeping the open submenu
   while the pointer moves towards it (no switching when crossing other entries on the way, e.g. ROM W..Z).
3. Then redo L4 and W1-W3, M1-M4 (checklist https://claude.ai/artifact/8Qi89czHHyzB79hQa5eSAq, spec
   scripts/checklists/round-0.8.22.json).
Build 0.8.22b is installed: Linux (user ran install.sh), win11 VM (silent), Mac pushed (not installed).

## Previous state (2026-10-17 evening: round 0.8.22 test build)

**Test round 0.8.22:** first results L1/L2 FAIL, L3 pass -> fixes (system menus on Win/macOS, Linux parent shift, LCD on template choice, edit field size); test build 0.8.22b = CI run 39044254597 (installed: win11 VM silent; Mac pushed ~/Downloads/TI-Revive-0.8.22b-mac; Linux unpacked ~/Downloads/TI-Revive-0.8.22b-linux, user runs install.sh); order this time Linux, VM, Mac; checklist
https://claude.ai/artifact/8Qi89czHHyzB79hQa5eSAq (spec scripts/checklists/round-0.8.22.json: macOS M1-M4 incl. the
Wavetable delay re-check, Linux L1-L3, win11 VM W1-W3). Next: install on Linux (unpack, user runs install.sh), win11 VM
(silent), Mac (push, user runs the installer), then the user's results; then merge + release.

## Previous state (2026-10-17 late afternoon: round 0.8.22 started)

**Branch `round-0.8.22` (not merged, no build yet, 171 tests):** (1) patch/bank menus in our own popup (E195):
PopupCascade (native/src/editor/popupmenu.*) with submenus - hover/click/Right/Return open, Left/Esc close the
deepest, left of the parent when no room on the right; LOAD/SAVE and the browser's bank menu use it, VSTGUI's
COptionMenu and the generic Linux menu theme are gone from the editor. Value lists now use the original's column rule
(entries / 4 above 32 entries, REA 0x180040f50) instead of 25 for all. Checked under Xvfb with the TI2 (LOAD > device >
RAM A 6 columns, Personal Libraries, browser bank menu, choice ROM A). Open: the original's "Create Empty Library"
entry; n of the original's patch submenus. (2) The Mac Wavetable delay re-check (655cf8c): user decision - with the
next major release test round, not now. Item 3 (REMOTE page) started 2026-10-17, user decision: editor first, NO sending. REA done (E196: record
layout, the original's cache file and when it sends). Done on round-0.8.22: full template model (records, edits,
cache file byte-identical to the original's), our RemoteSets file next to the bank cache (import of the original's
Windows cache once), Template Name / Knob Name editable, MIDI Channel Behaviour menu works, saved after each edit;
checked under Xvfb with the TI2 (rename, knob name, flag, reload after restart). MIDI String editable too (the original's
encoding, E196; Xvfb: "b0 7 x" -> "B0 07 *"). Nothing is sent to the Virus. Knob Template menu done (E197: read
from the user's binary at runtime by shape, Windows DLL and Mac binary, 35 entries; a choice fills the MIDI String).
MIDI String Info and Load/Save Template File
done (E198: *.vrt "RMOT"/"RSET" format, LOAD into the selected template, SAVE / Save as new; Xvfb-checked). The
REMOTE editor is complete except sending. Joint session 2026-10-17 (E199): capture of the original choosing/editing a
template (double-click chooses; Remote Patch 73 00 6C + 33 records on cable 3; no answer) and the Virus' own
Transmit MIDI Dump "Remote Patches" (16 factory-like knob records of template 0, no "TEST" from the original's
edit). User decision: send like the original. Done on round-0.8.22: choose (double-click) = Remote Patch + 33
records, edits = their record, LOAD = 33 records, cable 3; both template places in LOAD/SAVE (E200); hardware test
passed (E201: byte-identical to the original, Virus fine, its own dump unchanged). Still open: what the Virus does with
the records (same unknown as with the original) (a capture of
the original choosing/editing a template in the win10 VM with the user, then a test the user approves).

## Previous state (2026-10-17 afternoon: v0.8.21 released)

**Released v0.8.21** (tag on main 5aaef09, CI run 39031771705 all green incl. release; assets: setup.exe, msi, mac zip,
linux tarball, tirevive-usbinfo.ps1, libusb source; website rebuilt by hand, run 39032109744, live with 0.8.21 and 165
tests). main = round-0.8.21 (fast-forward). Installed: win11 VM release setup.exe silent (one entry 0.8.21); Linux
unpacked ~/Downloads/TI-Revive-0.8.21-release/TI-Revive-0.8.21-linux (user runs install.sh); Mac pushed
~/Downloads/TI-Revive-0.8.21-mac (user runs sudo installer). Virus on Linux. No site/README feature lines for the
popup menus / INFO box (user: parity fixes). Next round candidates: re-check the Mac Wavetable delay with the repaint
fix; patch/bank menus (submenus) in our own popup; REMOTE page sending/SAVE (E193); E180 pixel comparison; inactive
text strength; bare-metal list for one session.

## Previous state (2026-10-17 noon: round 0.8.21)

**Branch `round-0.8.21` (pushed, NOT merged; CI test build run 39018067599 all green, Pester tests ran on Linux + Windows runners; checklist https://claude.ai/artifact/AQAVVmwAfEjjdpu6FxXW8A; installed: win11 VM setup.exe 0.8.21 after Claude's W1 checks, VM shut down; Linux unpacked in ~/Downloads/TI-Revive-0.8.21-linux (user runs install.sh); Mac asleep: ~/Downloads/TI-Revive-0.8.21-mac.zip, push with scripts/mac.sh):** (1) E186 fixed: tirevive-bundles.ps1 (MSI
deferred CA, only without setup.exe = TIREVIVE_BUNDLE) removes stale TI Revive Burn bundle registrations; Pester tests
in src/installer/tests, run by ctest `installer_scripts` (skipped without pwsh + Pester 5; CI windows job installs
Pester; locally pwsh 7.4.6 from the scratchpad: cmake -DPWSH_EXECUTABLE=...). Cause read from the bare-metal
Windows registry/Burn logs (read-only mount). (2) E13: -Wall -Wextra on our sources, SDK/VSTGUI/pugixml headers as
system headers, -Werror in the native-linux CI job (VTI_WERROR); 2 warnings fixed. (3) osc-wavetables merged (E180);
Mac bundle path unit test; the table is found in the real 5.1.7 Mac binary (same as Windows). 143 vtitests.
Not done: E180 pixel comparison with the original (needs the original's editor at known OSC settings - test round,
with the user); E187 earlier crackle and E175: nothing to do until they show up again. Next: the user's checklist (Linux L1-L2, VM W1-W2, Mac M1-M3, bare metal B1-B2), then merge + release 0.8.21
(bump project VERSION in native/CMakeLists.txt). Issue #4 closed (E188).

**Round 0.8.21 work after the comparison (2026-10-17, branch round-0.8.21, pushed, 157 tests; user decisions: OS
version read from flash = yes, broker version in the driver rows, REMOTE page now (read-only first), keep our help
mode, plain menus first):** DONE: menus anchored + 25/column + right click through the ring (E189); out-of-range
values clamped (Bands "32"); INFO box: website text, "TI Revive <v>", broker version (VTIMSG_INFO, make VERSION=),
Virus OS read by the broker with the original's fixed read-only flash sequence (flashinfo.h, VTIMSG_FLASHINFO, owner
only, cached; tested on the TI2: "5.1.7.00", E191; skipped for brokers without INFO); HyperSaw display with the
original's rule, pixel-exact (E192); REMOTE page read-only, pixel-exact, nothing sent (E193; the original sends 0x13
records while browsing - not done, needs analysis + hardware test); FILTER LINK and page tabs: no difference
(E190; tabs identical). NOTE: commit 1d4ec04 did not build, fixed in 5492ec7. OPEN: REMOTE knob names/MIDI strings
and template files ("Personal Remote Templates"), sending/SAVE; inactive text strength (original Delay Clock text
105 vs ours 149, Reverb 186-205 vs 161), envelope graph ends, filter curve, saw edge anti-aliasing; then the
checklist. Tools: scratchpad probes (init.state with both checksums, diff.py/regions.py for orig vs vtirender),
captures/remote-page.pcapng (untracked). Second test build: CI run 39027398548 (all green); checklist (same link,
version 3) https://claude.ai/artifact/AQAVVmwAfEjjdpu6FxXW8A; installed: win11 VM setup.exe silent (one entry);
Linux unpacked ~/Downloads/TI-Revive-0.8.21b-linux (user runs install.sh); Mac unpacked ~/Downloads/
TI-Revive-0.8.21b-mac (user runs sudo installer). The Virus is on Linux; the win10 VM still has the original +
Access driver (snapshot before-original-driver = with TI Revive 0.8.17).

**Round 0.8.21 second build results (user, 2026-10-17):** Linux L4, L5 pass; VM W2, W3 pass; macOS M1, M3 pass.
L3/M2 FAIL = the Wavetable list is one long column on Linux and macOS: VSTGUI's columns (setNbItemsPerColumn) only
exist in its Windows menu (macOS NSMenu has none, the Linux menu ignores it) - needs our own popup view (the user's
"modern menu" idea), next step. DONE (2026-10-17, not in a build yet): our own popup (E194; native/src/editor/popupmenu.* +
PopupView in viruseditor.cpp, unit tests popup_*, 164 tests) for value lists, category filter and size menu on all
platforms: the original's Windows look measured from the screenshots (classic style with columns), 25 per column, at
the control's corner, inside the editor (moved up/left; more columns if taller), keys + Return + Esc. Checked under
Xvfb with nativehost (Wavetable 4 x 25, Osc 1 Mode, Return, size menu at 200 %; the editor needs a Virus to leave the
status page - a temporary local hack, not committed). The patch/bank menus (submenus) are still VSTGUI's native/generic
ones. Open: Esc and the Windows/macOS look in the next build; the "modern" look as an option (user idea) later. Third test build (0.8.21c): CI run 39030095040; checklist version 4 on the same link (redo items with new IDs: M4,
M5, L6, L7, W4; order macOS first, then Linux, VM; bare metal postponed). Installed: Mac pkg pushed (~/Downloads/TI-Revive-0.8.21c-mac,
build 39030095040 = version 0.0.1, same code); Linux and VM from run 39030413663 (version 0.8.21): Linux unpacked in
~/Downloads/TI-Revive-0.8.21c-linux, win11 VM setup.exe silent (one entry 0.8.21), Virus attached to win11. NOTE: after a
VM test always `VM=win11 scripts/vm.sh detach` - the Virus stayed in the VM after the last round and REAPER found nothing. RESULTS 0.8.21c (user, 2026-10-17): all pass - M4, M5 (macOS), L6, L7 (Linux), W4 (VM). Note M4: the Wavetable list
opens with a short delay on the Mac (likely: PopupView invalidates the whole frame-sized view on open and on every
hover, so the editor below repaints fully; plus 100 CoreText measurements) - idea for the next round: invalidate only
the menu / the two changed rows. Virus back on Linux (detached). Repaint fix done (655cf8c, popup_repaint_areas; Xvfb: a hover repaints 2 rows
instead of 907x513), not hardware-tested (Mac delay to re-check in the next round). Release 0.8.21 (user: "fix the
repaint first, then release"): VERSION 0.8.21, release notes in build.yml, 165 tests in README + site. Site check
2026-10-17: TESTS 165 updated; COVERAGE 94 % (16 Oct) kept with its date (not re-measured); E175 "being worked on" still
open (not seen since 0.8.18); Snow matrix ok (E188); no new feature lines on the site/README yet (popup columns, INFO
box are parity fixes - ask the user). L5 notes: the link must point to our website -> done (sysinfo website_url, click
opens the browser; not yet in a build); the REMOTE MIDI channel menu is locked on purpose (read-only page). Bare
metal (B1, B2): postponed by the user - collect a bigger bare-metal list for one session later.

**Round 0.8.21 POSTPONED (user, 2026-10-17 noon): W2 failed (Wavetable list), then a full comparison with the
original.** Reference: the original Virus Control 5.1.7 in the win10 VM with the Access driver (TI Revive removed
there, snapshot before-original-driver), 21 screenshots of every page taken with virsh screenshot (scratchpad only,
never in the repo; editor at screen offset 278,207, our vtirender output matches pixel-exact where equal). Our pages
rendered with the Init patch (scratch init.state: init_single upgraded, both checksums) differ only where listed:
- identical (apart from patch names/LCD): LFO, MATRIX, ARP, COMMON; FX 1 only LCD.
- popups: the original anchors them to the control - Menu at its top-left corner (Wavetable 296,146; Category
  722,54), HierarchicMenu below it (bank list at 212,73 under the 212,50 23 px bar); ours opens at the mouse.
  Wavetable list: opens on RIGHT click on the table name (user), 4 columns x 25; ours: the MaskedKnob ring
  (WaveTableIndex) covers the menu in hit_test, a click never reaches it (scratch probe). Rule for columns and
  right-click: REA open. User wants a more modern popup look as an option.
- HyperSaw display (OscHypersawShape) not drawn: lines = Density (ring), spacing = Local Detune (0 centre, 127 full
  width); exact rule: REA (KOscHypersawShape, strings 0x180160e20).
- PATCH UTILITY INFO: URLButton text (valuelist "Access Website List") not drawn; version fields empty. Original:
  Control version = its built-in string (FUN_180078060); TIOS = read from the Virus' flash (FUN_1800ac7c0: blocks at
  0x7fffa0/0x40ffa0 + 0x400009, "no TIOS" fallback) - our broker blocks vendor flash requests on purpose (E177,
  ctrlguard): user decision needed (narrow read-only exception or "-"); driver rows: user decision (broker version?).
- Vocoder Bands: Init stores 127 (range 0..31); original shows the clamped "32", ours "127".
- FILTER LINK: original on with Init, ours off (pixel-exact with Filter Link = 1); LFO1/2 Mod Link off in both. Where
  the original sets it: REA open (7 refs to 0x180163cd0; start-up FUN_18006ed70 touches all four links per part).
- FX 2: Delay Clock and Reverb Mode look inactive in ours, active in the original (not analysed yet).
- FILTER page: envelope graphs (release end / sustain) and the filter response curve differ slightly; Classic saw
  edge anti-aliased in the original (E180 pixel work).
- REMOTE page (remote templates): not implemented in ours (empty list, "0" for MIDI Channel Behaviour).
- Help "?": original opens a menu (Setup Guide, Documentation, Video Tutorials, Access homepage, Create timing
  profile, Contact support); ours opens the help mode.
Our renderer stays the measuring tool (vtirender + scratch init.state); a new checklist follows the fixes.

## Previous state (state 2026-10-17 09:05: v0.8.20 released)

**Released v0.8.20** (tag on main 31af3e9, CI run 39011780610 all green incl. release; assets: setup.exe, msi, mac zip,
linux tarball, tirevive-usbinfo.ps1, libusb-1.0.30.tar.bz2 (LGPL source); website rebuilt by hand, run 39012105808,
live with 0.8.20). main = round-0.8.20 (fast-forward). Round 0.8.20 passed on all four systems (macOS, Linux, win11
VM, bare-metal Windows 11; B6/B7 large buffers fine, E187). Posted the short findings update on
Give-me-a-signal/virus-ti-rework#5 (comment 6230987764). Installed: win11 VM (release setup.exe, silent; VM shut
down); Linux: release unpacked in ~/Downloads/TI-Revive-0.8.20-linux (user runs install.sh; test builds renamed
*-testbuild); Mac was asleep: ~/Downloads/TI-Revive-0.8.20-mac.zip on this PC, push with scripts/mac.sh when awake.
Checklists: scripts/checklist.py + scripts/checklists/round-X.json (user wants one before every release).
**Site/README check (2026-10-17, skill tirevive-site-check):** Windows timing row was E143 (0.8.0 test1, real PC,
304/304, <= 0.66 ms upper bound) -> now E186 (0.8.20 bare metal: 224/224, sd 0.24 ms, max 2.09 ms, 2 ppm); sidechain
"not yet on a real Windows PC" removed (B1 passed on bare metal); site limit "USB input not fed by the plug-in"
removed (sidechain since 0.8.14). Pushed (ac7fa87), site rebuilt (run 39014607805), live checked.
**Next round (0.8.21) candidates:** the stale "TI Revive 0.8.0" uninstall entry on bare-metal Windows (E186); the
earlier crackle at large buffers not reproduced (E187 open part); osc-wavetables branch (pixel comparison, macOS
bundle check); E175 not seen this round (capture lines in place); E13 warning flags.

## Previous state (state 2026-10-17 morning: round 0.8.20 built for the hardware tests)

**Round 0.8.19 macOS results (user, 2026-10-17):** M1 pass; M2 FAIL (Control Center refused while Ableton ran;
backup "permission denied"); M3 FAIL (duplicate looked usable). Causes: E181 (POSIX broker: first client of any kind
owned the Virus; waiting instance showed the normal page since 0.8.18) and E182 (~/Documents/Access Music made by
the Access installer as root). Round 0.8.19 is not tested further; its fixes all went into round 0.8.20.
**Branch `round-0.8.20` (from round-0.8.19, pushed, NOT merged):** src/broker/owner.h (one owner rule for both
brokers: first plug-in owns, plug-ins of other programs wait, every other client shares; POSIX decides at HELLO),
StatusChange (devicestatus.h) for the waiting instance, backup error with the way out, postinstall + setup .command
chown the root-owned Documents/Access Music entries, README/package readmes "Control Center works next to the DAW",
THIRD-PARTY-NOTICES.txt in every package + libusb 1.0.30 source attached to releases (user decision: keep MIT),
plug-in version 0.8.20 (CMake). TDD, 139 tests. CI test build: run 39003702169. Checklist (all 4 systems, the fixes
on each): https://claude.ai/artifact/RntLcBgj3aAifV54cNompD; installed: Mac unpacked in ~/Downloads (user runs sudo installer), Linux unpacked (user runs install.sh), win11 VM setup.exe silent (0.8.20, notices in the install folder). The user's Mac folder is left
root-owned on purpose (M1 checks the postinstall). After the tests: merge, site rebuild, release 0.8.20.

**Round 0.8.20 progress (2026-10-17 morning):** macOS M1-M4 pass; Linux L1, L2, L4 pass, L5 FAIL -> fixed (E183,
REAPER crash at a duplicated track: VSTGUI run loop adapter replaced by a second instance/editor; user: "reaper is
working now!"), no status page without a broker -> fixed; win11 VM W1-W4 pass, Control Center without the Access
look (E184, setup fixed, verified in the VM), Control Center window cut off/too big at start and Linux crash on
closing (E185, fixed; Linux verified in Xvfb). New: end-to-end GUI tests via ctest (gui_test.sh, xvfb; the duplicate
test skips without Access files, e.g. CI), CITATION.cff + "Using our findings" (attribution request). Second build
CI run 39007000903 (green) installed: Mac ~/Downloads/TI-Revive-0.8.20b-mac (user runs installer), Linux
~/Downloads/TI-Revive-0.8.20b-linux (user runs install.sh), VM setup.exe silent. Checklist (same link) has redo items
M5, L6, L7, W5, B5 - all PASS (user, 2026-10-17: macOS M1-M5, Linux L1, L2, L4-L7, VM W1-W5; L3 not needed,
E175 did not occur). Bare metal B1-B5 PASS (E186: render 224/224 on the grid, the VM's
gaps were the passthrough; packets clean; replug recovers). User question: crackling with a sound card at buffer
2048/4096 -> E187: 4096 re-anchored every block in the first seconds (fixed test-first, 140 tests), 2048 on Windows
not measured yet. User chose: investigate 2048 before the release. Third build CI run 39010110029 (green): the 4096
fix + diag.log line with host rate and block sizes (also with the editor closed). Checklist items B6/B7 (same link):
the user plays at 2048/4096 on bare metal; afterwards read C:\Users\<user>\AppData\Local\virus-ti-native\diag.log
from the Windows partition read-only (udisksctl mount -o ro). Then release 0.8.20. The Virus is in the VM. After merge: post the short findings update
on Give-me-a-signal/virus-ti-rework#5 (draft approved by the user: scratchpad rework-update.md - pointer to
findings/evidence.md, no credit request).

## Previous state (state 2026-10-16 late night: round 0.8.19 built, hardware tests 2026-10-17)

**Branch `round-0.8.19` (pushed, NOT merged; main has 3 local HANDOFF/findings commits that are also on this branch):**
CI test build 0.8.19 = run 38962757365, all 7 jobs green (packages say 0.8.19; the plug-in itself still reports
0.8.18 - project(VERSION) in native/CMakeLists.txt is bumped at release time). Contents, all TDD (134 tests): (G) both
brokers refuse every control request except standard reads (src/broker/ctrlguard.h, E177); (A) E175 capture: IN
packet-size statistics + "out of range since N ms" log lines on Linux and Windows (src/broker/pktstat.h; journal
analysis E176: stuck sessions get exactly 2815 good packets after each start, then only wrong sizes; the Access driver
has no recovery for the Virus either, E178); (B) scripts/vm.sh attach stops the Linux broker first (E179: the error 31
handover was the broker reopening the Virus while libvirt attached it); (C) greyed controls already ignore clicks
(code since 8e7077c, no change); (E) Linux library folder from the XDG documents dir (~/Dokumente on this PC - the
factory libraries there were never found before), install.sh same rule, README. E13 (compiler warning flags) skipped
on purpose. Installed: win11 VM (MSI 0.8.19 silent, VM shut down); Linux: unpacked in ~/Downloads/TI-Revive-0.8.19-linux
(user runs install.sh, needs sudo); Mac was unreachable - ~/Downloads/TI-Revive-0.8.19-mac.zip on this PC. The Virus is
on the Mac. Checklist (4 blocks: Linux, win11 VM, Windows 11 bare metal on this PC, macOS):
https://claude.ai/artifact/1gKwV9ZEiB8JiZ6GpzgFLj (2026-10-17: macOS first; Mac unpacked in ~/Downloads, win11 VM running with setup.exe 0.8.19 installed over the bundle). Bare-metal results go to a folder tirevive-0.8.19 on one of the
NTFS data disks (mount read-only from Linux afterwards). After the tests: read the broker journal for the new packet
lines (L2/L3), merge, README/site, release 0.8.19 (bump CMake version).
**Branch `osc-wavetables` (pushed, from round-0.8.19, NOT in the test build):** (D) classic OSC display with the
original's 64 waves (read at runtime by content) and mix (E180); vtirender images look right; open: pixel comparison
with the original, macOS bundle check. Next round.

## Previous state (2026-10-16 night, after the 0.8.18 release)

**Released v0.8.18** (Latest, CI run 38953060428, all jobs green; website rebuilt by hand, run 38953633854, live with
0.8.18 and the new screenshot docs/images/plugin-help-inactive.png): full project recall (E174), the "No Virus TI can
be found" page (E173), help notes for greyed-out controls. Round 0.8.18 passed on Linux (REAPER), win11 VM (Ableton 11)
and macOS (Ableton Live 12); known issue E175 (Linux only so far: silent after a cable replug / power-on with no DAW
running, a power cycle with the DAW running fixes it) in the README, site and release notes. 127 unit tests, vticore
coverage 93 %. Installed: win11 VM (release, /repair over the test build of the same version); Mac and Linux: release
unpacked in ~/Downloads (test builds renamed *-testbuild), the user installs (Linux needs sudo while the Virus is on
the Mac). The Virus is on the Mac.

**After the release (main, not in a release yet):** help mode framed an empty box next to the ? - the display-only
"Global Section Lock Indicator" got a frame since 0.8.18 includes disabled controls; its note removed (commit 0b12af5),
website screenshot corrected. Ships with the next version. Coverage pass (2026-10-16, commit 37011e9): test_decode got
decode_describe_with_model (hand-written model: Nerd mode's parameter names, global/part/action pages, CC) and the
truncated-message fallbacks; decode.cpp 90.2 -> 99.4 %, CI-like vticore 93.5 -> 94.1 % of 2696 lines (97.0 % with
header lines merged over the test objects); no file under 90 %. README/site: 128 tests, 94 % (site not rebuilt). Left
uncovered on purpose: controlcenter "did not answer"/"incomplete" (would wait out 3 x 3 s MemorySync retries) and
diag.cpp filesystem/platform branches. Dead-code pass (commit 9a2edd7): a separate build with -Wunused found only two
unused locals (editorrender.cpp osc_classic, nativehost.cpp), removed; no unused vti:: functions (checked against the
plug-in, CC app, tools, tests and the ASIO/AU sources). The native build itself has no warning flags (option for later).

**Next round:** (1) E175 on Linux: capture the IN stream in the stuck state (E112 pattern?), compare the broker's
open/stream timing with macOS and the VM (the VM got the Virus late and recovered), REA: the original's start-up;
(2) DONE: greyed controls already block clicks (see round 0.8.18 notes); (3) Linux -> VM handover left the
Virus answering error 31 until a power cycle (give-back reset?); (4) bare-metal Windows: the real PC is THIS PC
booted into Windows (user, 2026-10-16) - test everything possible in the win11 VM first, then one self-contained
bare-metal session (no Claude during it): sidechain, timing render (E141 gap pattern), reconnect without libvirt
re-attach (E175 on Windows?), ASIO latency; save logs/renders where Linux can read them. Bench against the original:
plan in [bench-original.md](bench-original.md) (stage B needs Windows 10 + the Access driver; that PC runs
Windows 11 - blocked; PARKED by the user 2026-10-16, curiosity only, not
part of "complete"; until then no performance comparison with the original anywhere).

## Round 0.8.18 notes (branch round-0.8.18, merged)

**Round 0.8.18 (branch round-0.8.18, TDD, not merged):** (1) disconnect page: the plug-in now sets SystemStatus/Error
and the three "Internal System Info/... Communication" values from the broker's presence (model/devicestatus.h, E173);
Linux TI2: Virus attached to the VM -> "No Virus TI can be found" page, back -> "Virus reconnected", ready after 3.9 s.
(2) recall bug found and fixed (E174): the state's part dumps were format 9, the Virus converts them and resets 20 newer
fields (Input Mode, unison, env 3/4, arp mode, chorus type ...); now upgraded to 12 on load and in the default state;
hardware per-field test all TAKEN. Projects saved before keep the Virus' converted values. (3) help notes for inactive
controls: Enabled="false" ("Inactive now: Chorus Type is Off.") and greyed by the parameter's own condition ("depends
on Input Mode, Input Atomizer."), Xvfb screenshots with the TI2. 127 unit tests, vticore coverage 93.2 % of 2768 lines
(changed files 94-98 %). Open for the user's test round: all three on Linux/macOS/win11, REAPER default pins 3/4 ->
Sidechain (E171), Windows sidechain on a real PC (no answer yet whether one exists). Test build 0.8.18 (CI run 38945354060, all green): installed on Linux and win11
(setup.exe on the Desktop), Mac unpacked in ~/Downloads (user runs sudo installer). Checklist: https://claude.ai/artifact/41uVXA5BRHVExjWpex23Ee. User test (Linux, in progress): disconnect page ok with power, but after a USB cable replug and after a
power-on with REAPER closed the Virus stayed silent on USB (MIDI fine) until a power cycle - E175, cause open, next round.
Recall confirmed: after that power cycle the reopened project played ARP Up.
Linux results (user, 2026-10-16): all pass except (a) tabs locked on the "No Virus" page = original behaviour (the tab
buttons exist twice, Enabled="false" under "Any Communication Error"; checklist expectation was wrong), (b) E175 twice,
(c) Chorus Type "Classic" in a new project = the Virus' conversion of the original's v9 Init patch (0.8.17 showed
"Off" from the wrong positions), (d) the user had pointed at the ENV FOLLOWER's Input menu (two menus labelled INPUT, checklist wording
fixed); on the INPUT box's Input select the "Inactive now: depends on Input Mode, Input Atomizer." note appears -
pass. Checked 2026-10-16 (code): greyed controls, menus included, already ignore clicks since 8e7077c (viruseditor.cpp, the param_active check runs before the menu branch), like the original under Wine (native/docs/status.md 2026-10-13); the "clickable" note came from the ENV FOLLOWER menu, which is not greyed. Hardware check in round 0.8.19. REAPER pins 3/4 -> Sidechain by default: pass (E171 closed).
Windows results (win11 VM, Ableton 11, user 2026-10-16): all pass, incl. USB cable replug (the VM gets the Virus only
on Claude's re-attach, seconds after it booted - supports the E175 hypothesis "stream starts while the Virus boots"),
recall (ARP Up, Unison 8 heard after power cycle), help notes, and the sidechain in Ableton (VM; real PC still not
available). Note: Chorus Type changed while a key is held applies from the next note - the Virus' own behaviour (user:
the hardware panel does the same), not a bug.
macOS results (Ableton Live 12, user 2026-10-16): all pass, incl. the USB cable replug and the power-on with the DAW
closed - so E175 (silent after replug / power-on) shows only on Linux so far. Round 0.8.18 is through on all three
OSes; open for release: README + site (disconnect page, full recall, help reasons; screenshot), then the release. Handover Linux -> VM: the Virus answered every WinUSB transfer with error 31 until a
power cycle (+ re-attach, the VM keeps the old USB address) - possibly the Linux give-back reset or E175. Note for the user: the original's
diagnostics page advises holding ARP EDIT while powering on - effect unknown (rule 2: we do not advise it).

## Previous state (2026-10-16 evening, after the 0.8.17 release)

**Released v0.8.17** (Latest, CI run 38939098874, all 7 jobs green; website rebuilt by hand, run 38940119942 - a CI-made
release does not start site.yml): sidechain input to the Virus (3 Outs / 1 Input; REAPER: Sidechain bus, Ableton: main
input, E167/E168/E171/E172), help mode behind ? (helptext.cpp), Linux reset guard as a tested header (E136), CI job time
limits. Tested in round 0.8.14-0.8.16 (checklist artifact https://claude.ai/artifact/8zqCEtreSPUbRGw86XQNc7): Linux all
pass, macOS all pass incl. Logic Pro AU, Windows (win11 VM) smoke + help mode pass, sidechain on Windows untested on a
real PC. 119 unit tests (111 run, 8 need Access files), vticore coverage 93.6 % of 2704 lines (sidechain 96.7 %,
helptext 96.1 %). Installed: win11 VM (0.8.17), win10 (0.8.17, no longer a test machine); Linux and Mac: packages
unpacked in ~/Downloads, the user installs (Linux needs sudo while the Virus is on the Mac; Mac: sudo installer ...).
The Virus is on the Mac.

**Next round (TDD, test rounds per [[release batching]]):** (1) disconnect page: the plug-in never sets "Internal
System Info/USB Communication" - look up in the original (REA) when it does; (2) recall bug: a reloaded project does not
send part 1's Input Mode (and maybe more parameters outside the part dumps) to the Virus (E172); (3) help notes also for
inactive controls (Chorus Type Off), with the reason; (4) sidechain on a real Windows PC with Ableton; (5) REAPER: check
that the stereo-only buses (0.8.15) map 3/4 -> Sidechain 1/2 by default (E171).

**0.8.12 RELEASED 2026-10-16** (Latest, CI run 38910873523, all 7 jobs green; setup.exe in Downloads on win11 + win10; Mac was unreachable - push the mac zip later): the sharper editor images + Linux reopen crash and zoomed menus fix (skins branch). Built + tests on Linux before tagging. 0.8.11 was only a CI test build (no tag).

**Released:** v0.8.10 (Latest, CI run 38890324901, all 7 jobs green, 2026-10-16): reconnect waits for the Virus and
finds a new marker (E163); the queue waits for a Virus that is not there yet (DAW started first, E166); Windows: one
plug-in owner across programs, the second DAW shows "Virus unavailable" and takes over (E166); Out 3 in "3 Outs /
1 Input" (E164). Tested: Linux (TI2: reconnect, absent start), Windows VM (Ableton + REAPER). v0.8.9: the third USB
output (E159, Linux/Windows/macOS). v0.8.8: soft knobs on the Virus change the sound. v0.8.7: presets chosen on the
Virus. All work branches merged into main (106 unit tests; CI-like vticore coverage 94.9 % of 2507 lines, measured 2026-10-16 after the encode_into/library/load-error tests, 98 run / 8 need Access files; CI runs them on Windows and Linux, README and site say so).
Two programs on macOS: tested 2026-10-16 (Ableton 12 + Cubase 15, E166). Open: the Virus' USB input in mode 2
(the plug-in sends no audio; the original sends its Sidechain bus there, E167; implemented on branch `sidechain` for Linux, works on the TI2 with nativehost, E168 - next: a REAPER test with a real sidechain route, then Windows (ASIO driver writes the same ring) and macOS (no output to the Virus yet)), the Snow reports on #4.

**Sharper images (merged into main 2026-10-16, CI test build 0.8.11 = run 38901544137; user tests: Linux REAPER, macOS and Windows 11 "works!"; ships with 0.8.12):** option A of the skin question:
sharper editor images on scaled displays, computed at runtime from the user's own Access bitmaps (Lanczos-3,
`native/src/editor/upscale.*`, 3 tests; editor integration in viruseditor.cpp, see native/docs/architecture.md).
Built + 102 tests on Linux; checked under Xvfb at VTI_NATIVE_ZOOM=2 and 1.5 (sync, Easy, Filter pages) and with
forced tiles (VTI_NATIVE_TILE=1000: identical pixels). NOT built on Windows/macOS (D2D/CG paths untested), not
tested in a DAW or on a real HiDPI screen. User decision 2026-10-16: we support it (goes into 0.8.12; CI test build 0.8.11 still to do - pushing the branch needs
the user's OK). Found while testing in REAPER on Linux, fixed on the branch: (1) reopening the editor crashed the
host (cairo-xcb-screen.c _get_screen_index assertion) - VSTGUI disconnects X without cairo_device_finish and the
next connection reuses the address, our static `ref` surface outlived its connection too; since 0.8.0, also in 0.8.10
(nativehost --reopen: 3 of 6 runs crashed, after the fix 48 reopens without one). (2) Linux menus at 150/200 % opened
at zoom x the click position (VSTGUI's generic menu adds the zoom twice) - menu_at() divides it out. User test in REAPER on Linux (2026-10-16): sharper images, reopening and the menus at 150/200 % work ("works"). Option C (recreated artwork): dropped by the user 2026-10-16 (rule 1). Website LIVE since 2026-10-16: https://derpsn.github.io/access-virus-ti-revive/ (one page + changelog, generator scripts/site/build.py, deploy .github/workflows/site.yml on each published release / site change on main / by hand; Pages source = GitHub Actions). Update TESTS/COVERAGE constants in build.py when they change. Since 2026-10-16 also: section + issue form "Problem in the original Access plug-in (Virus Control)" (label `original`) for problems of the original that we could fix, a Contributing section (AGENTS.md, skills), the virus.info download link (account needed) also in the README.

**Branch `sidechain` (pushed 2026-10-16, CI test build 0.8.13 = run 38919495024):** the Sidechain bus to the Virus' USB
input in "3 Outs / 1 Input" on all platforms (SidechainSender: rate-controlled windowed-sinc converter like the
original, E167/E168; Windows: the plug-in steps back while the ASIO driver writes the ring; macOS: the Core Audio
IOProc plays the ring's output part), Linux broker E136 guard (give-back resets at least 5 s apart). Tested: Linux
nativehost (first version, E168), unit tests (109). Next: one test session per OS (sidechain in a DAW, host rate
48 kHz, E136 guard on Linux with two quick restarts), then README + merge.

**Restore a backup (asked 2026-10-16):** the original Control Center restores by erasing and writing flash banks (E169) - not allowed (rule 2). User decision 2026-10-16: postponed (backups stay read-only; the single-dump alternative would need a one-slot hardware check first).

**Test round 0.8.14 (branch `round-0.8.14` = sidechain + help-mode, 2026-10-16):** one CI test build, one checklist
per OS (smoke test, sidechain in a DAW, 48 kHz, Linux reset guard, help mode). Release after it passed.
CI run 38922591289 (re-run after a stuck apt mirror; CI now has job time limits). Installed: this Linux PC
(install.sh), win11 VM (silent MSI + setup.exe, running), win10 VM (silent, off); Mac: unpacked in ~/Downloads, the user runs
`sudo installer -pkg ...`. Checklist page (private artifact): https://claude.ai/artifact/8zqCEtreSPUbRGw86XQNc7.
The Virus is on Linux; attach it to win11 with `VM=win11 scripts/vm.sh attach` (stops the Linux broker first, E179) when the user starts the Windows part.
Linux results (user, 2026-10-16): 22/22 pass. But the reset-guard steps could not trigger the guard: the broker stays
up while REAPER restarts (it exits only after 30 s without clients, IDLE_EXIT_S), so no reset happened at all. The guard
is now covered by a unit test instead (TDD: test_resetguard.cpp RED 08afb1f - header missing; GREEN - spacing, fail-open
on a missing stamp or a clock that went back, no symlink following; vticore.c uses src/broker/resetguard.h).
Windows (win11 VM): reconnect after power-cycling the Virus needed a re-attach (libvirt keeps the old USB address;
VM limit, not a plug-in bug) - then it played again (pass). NEW FINDING (user, Linux and Windows): when the Virus is
switched off the plug-in only writes "Virus disconnected" on the LCD; the layout's communication error page never
shows, because the plug-in never sets "Internal System Info/USB Communication" (conditions "... USB Communication
Error" / SystemStatus/Error). Next round, TDD: look up with REA when the original sets these, then implement.
Windows VM: the sidechain arrived silent at the Virus (E170, corrected: Input Mode Static silences part 1 when the input
is silent; all IO configurations play). Open: Ableton's sidechain buffers vs. the VM's iso OUT - check via the ASIO
driver (same OUT path). Next round (TDD): the stats line also reports the sidechain input level.
Windows results (user, win11 VM): smoke 4/4 (reconnect after a VM re-attach), help mode 8/8 (Esc, ?, 150 %),
sidechain open (E170). The ASIO device is no check in 3 Outs / 1 Input (the Virus is no sound card then; feedback).
Linux sidechain re-test (E171): WORKS in REAPER once the pins are Track 3 -> Sidechain 1, 4 -> Sidechain 2 (USB 1 peaks
~15000, rate +-30 ppm). REAPER mapped 4-pin buses wrongly by default. Next round (TDD): Sidechain bus strictly stereo;
stats line with the sidechain input level; disconnect page (above).
macOS (0.8.16, E172): the sidechain WORKS in Ableton Live 12 - Ableton puts it on the main input (active: 1 0), the plug-in
now reads that (USB 1 ~15000). Windows/Ableton had the same symptom (E170) - most likely fixed by 0.8.16 too, untested.
NEW BUG (next round, TDD): reloading a project does not send part 1's Input Mode to the Virus (editor Static, Virus off).
macOS results (user, 0.8.16): all pass - smoke 4/4 (no disconnect page, sound comes back), sidechain 8/8 incl. 48 kHz,
help mode 8/8, and the Audio Unit in LOGIC PRO loads and plays (first Logic test; open item 6 done). Note: with Chorus
Type Off the chorus controls are inactive and show no help note - next round: notes for inactive controls too.
Builds of this round: 0.8.14 (round), 0.8.15 (bus lookup + stereo + log), 0.8.16 (main input for Ableton); Mac and
win11 are on 0.8.16/0.8.15, Linux on 0.8.14 (its install needs sudo while the Virus is away).

**Branch `help-mode` (2026-10-16):** the "?" button (the original's Help Menu, which listed manuals; ours showed
numbers 0-127) toggles a help mode: frames around every explained control, a note for the control under the mouse
(click pins it, clicks change nothing; page tabs, part strip and envelope select still work), Esc or "?" leaves.
Texts: native/src/editor/helptext.cpp, our own words written from the user's Virus TI Reference PDF (read locally,
not in the repo - rule 1) and the editor model; 501 of the 520 control paths covered (the rest: welcome page, progress
bars). Hover needs a frame mouse observer (VSTGUI passes moves to views only while a button is down). Tested: unit
tests (help texts, visible_controls), Xvfb screenshots at 100 % and 150 % with the TI2 connected. Not tested: Esc,
Windows, macOS, a DAW.

**Test builds:** always a NEW version number (a forced reinstall of the same version removed the WinUSB driver in the VM, E166).

**Replies on #4 (2026-10-16):** 6208931585 (nasguile, 0.8.7), 6208931949 (Manu, 0.8.7, logs asked), 6210139464
(both: correction - use 0.8.8, soft knobs on the Virus now change the sound). Waiting for their test results/logs.

**This PC:** ~/.vst3 holds a main build (0.8.8 code + the E157 clean-up); the user's old 0.8.3 copy is in the session
scratchpad (vst3-backup) - the 0.8.8 Linux package can be installed over it. Coverage measurement: a scratch build
`cmake -S native -B <scratch>/cov -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="--coverage -O0"` (NEVER with
FETCHCONTENT_BASE_DIR=build-native/_deps - it stays in the cache and breaks build-native's pugixml build on every
later coverage build; the session's cov2 has its own _deps), run vtitests with HOME = an empty
folder and XDG_CONFIG_HOME/WINEPREFIX/VTI_ACCESS_DIR unset, gcov over CMakeFiles/vticore.dir, LC_ALL=C.

**Open, in order of value:**
1. DONE 2026-10-16: Windows check of 0.8.8 (E155).
2. DONE 2026-10-16: E154 (3) explained (E157) - in plug-in mode the Virus changes only name + origin of the edit
   buffer when a program is chosen on the panel, so a read-back cannot supply a patch missing from the cache; the
   read-back code is removed (branch `readback-experiment`, merged). ROM banks need firmware.bin (normal install).
3. #4 ManuAnilkumar23: RAM banks "Init" on the Snow. The Snow description = TI description (E151). Possibly
   related to E152 (if he switches presets on the Snow, the plug-in stayed on "-INIT-") - ask him after 0.8.7;
   else his diag.log ("RAM A sync" lines) and broker log.
4. #4 nasguile: FX 1 changes only after re-triggering - NOT reproduced on the TI2 (E160: CC, pp and SysEx Bank C
   FX parameters all reach a held note at once). Needs his answer: which knobs, Snow/macOS/Ableton, same on the Snow
   itself? The editor path (Chorus/Distortion with types, user test in REAPER) works live too.
5. DONE 2026-10-16: CI-like coverage 92.3 % (test_firmware, test_diag, test_protocol). Lowest now: library.cpp 85 %,
   remotedevice.cpp 90 %.
6. DONE 2026-10-16 (E161): the original drops host notes + clock while busy (start-up queue, bank sync, requests)
   and sends All Notes Off when it becomes busy; ours holds notes until ready and pauses the sync - kept.
7. Older items unchanged: (USB 3: released in 0.8.9, E159; mode 2 measured E164, Out 3 on main, not released; the plug-in sends no audio to the Virus input - the original sends the Sidechain bus (VST3 input bus 1) there, E167; next: implement it), (reconnect path: fixed on main, E163, not released), (macOS duplicate track: works, E165), (two DAWs on Windows: fixed on branch offline-queue, E166), Snow "officially" (record a test report).

**Tools:** `virusmon` (read-only broker observer, decoded traffic) is in the repo: native/tools/virusmon.cpp,
target `virusmon` (build-native/bin/Release/virusmon, `--hex` for SysEx bytes).

**Environment:** the Virus is on the Linux PC; win11 VM off. REA analysis of Virus Control.dll:
work/rea/virus-control-5.1.7-x64.ghidra-snapshot (git-ignored; open the copy in
~/.local/share/tirevive/access/... (sha256 a622f9a2...) with provider_id "ghidra" + snapshot_path). The user's
plug-in in ~/.vst3 is 0.8.3 (moduleinfo).

**Replies posted:** #4 on 2026-10-15 (fixes in the next release, logs asked). Memory: always REA for RE.

## 0.8.5 (2026-10-15): issue #6, duplicated track puts the Virus out of time (merged from branch single-instance)

- Cause (E148, MEDIUM): on Windows a second plug-in instance (Ableton: duplicate track) got the Virus too, sent its own
  sync ping and reset the Virus' only stamp clock; the original kept its old marker -> its stamps off by 13312 frames
  until a DAW restart. Fix: one instance per process (DeviceOut::claim_owner), others wait (LCD "Virus in use / by
  another instance"), takeover with state + new ping when the owner is removed; waiting instances drop their notes.
  Also: stats "virus frames" per instance. Built + ctest on Linux only.
- Test 1 (2026-10-15, Win11 VM, Ableton 11, TI Desktop, CI run 38791531643): original stays in time with a duplicate
  (user); the duplicate showed the LCD text only; takeover worked but notes were up to ~1.9 s late (klog) for ~25 s:
  the 529 queued start-up messages went out while notes played (E144) and the ping found no marker until the user
  stopped playing; once the marker came, K = 2559/2560 like the original. Changes after test 1: waiting instance shows
  the original's "Virus unavailable" page (SystemStatus/Error = 2, checked with vtirender), and after a takeover the
  sync thread runs the start-up again (start page, 4 s note hold, bank sync).
- Test 2 (CI run 38793336069): works (user); the first ping after a takeover still unanswered, marker ~13 s later
  (open, minor). Merged to main, released as 0.8.5. Open: macOS/Linux quick check of a duplicate.
- Not covered: two different DAWs on Windows still share the Virus (broker without owner on Windows).
- **0.8.6** (merged from branch `virus-ready`, E149): the fixed 4 s note hold is replaced by a readiness probe (single dump
  request `30 00 00` queued behind the start-up messages + state; its reply = the Virus is through); the timeline
  anchors and pings only after it, notes wait up to 3 s for the marker. Cause of the unanswered first ping: the
  Virus was still digesting 530 queued messages (two state uploads queued while waiting). nativehost `--duplicate T`
  = takeover test. Tested on Linux (TI Desktop): first start + takeover, marker on the first ping, K 6024 for all
  notes. Windows (VM, Ableton 11, CI run 38797985200, package 0.8.7): first start + takeover playing/stopped,
  ready after 3.9 / 6.9 / 6.8 s, marker on every first ping, works (user). Then: a state upload replaces the
  queued records of an earlier one (takeover queue 285 instead of ~530; Linux only, nativehost duplicate with
  setState). Open: the reconnect path (device back) still has no note hold; macOS not tested.

## 0.8.4 (2026-10-15): editor sizes (merged from branch ui-scaling)

- Plug-in: display scale from the host (IPlugViewContentScaleSupport) x user size (right-click menu 100-200 %, file
  ui-zoom), resizeView; tested on Linux with nativehost (--rclick added; its window follows resizeView): 150 %/200 %
  rendering, menu, runtime resize, saved choice. Control Center: View > Size + right-click in the Access look
  (preference "Zoom"), tested on Xvfb. User tests 2026-10-15: Linux (REAPER + Control Center) works; macOS with
  the CI package of run 38754600760 works in Ableton (VST3) and GarageBand (AU). Windows: Win11 VM at 150 %
  display scaling, Ableton Live 11 - works (user). Released as 0.8.4.

## 0.8.3 (2026-10-15, RELEASED: CI run 38750678863, all jobs green, Latest)

- The fixes from issue #5 (scan, state growth E147, first-sync estimate, dev tools). Open: the user's Windows check
  with a Virus (0.8.2 + 0.8.3 parts: Control Center, Nerd mode, plug-in).

## 0.8.2 (2026-10-15, RELEASED: CI run 38734447522, all jobs green, Latest)

- Open after the release: Windows check with a Virus (Control Center, Nerd mode while Ableton plays) - the user
  checks it later.
- After 0.8.2 (findings from issue #5, collaboration with virus-ti-rework - findings only, never code): plug-in scan
  no longer reaches the Virus, state growth fixed (E147), start page shows the time left for a long first sync,
  autest --no-notes / no crash after --png, nativehost --scan; their third-output finding recorded as C10 (UNTESTED).
  Replied in #5; our findings posted in their repo: Give-me-a-signal/virus-ti-rework#5 (allowed by the user).

- Clean-up after 0.8: the 0.7.x migration kit and test-package scripts in `native/` removed, broker headers only from
  `src/broker`; Linux broker without the TCP listener (opt-in `VTI_BROKER_TCP=1`, socket unit has no TCP port, install.sh
  removes `broker.token`; checked: no listener on 47615). `native/tools/autest.mm` (AU test host).
- Bug reports: template and `tirevive-usbinfo.ps1` collect the plug-in's `diag.log` (only "Virus TI native:" lines) and
  the broker log (checked in the win11 VM). Release workflow builds only what ships (broker `vtibroker.exe`, no mingw
  in the Linux job) - rehearse before the next tag.
- Issue #4 (Snow on macOS): status reply posted, both users asked to run the first test stages with 0.8.1.
  2026-10-15: nasguile (comment on #5) reports the Snow "recognized and fully works", total recall works, USB 1/2 ok,
  USB 3 silent (expected: Out 3 is silent by design, needs USB IO Configuration = 3 Outs, C10). Version not stated;
  asked on #4 for version, DAW, macOS and logs before recording evidence.
- Merged to main and pushed (1e88e73).
- **Control Center phase 1** (branch `control-center`): `native/src/cc/` + `native/tools/tirevive-cc` (info, backup,
  list, export), read-only, docs in native/docs/controlcenter.md. Tested on the M3 + TI2 (built there with clang++
  and `-isysroot .../MacOSX26.sdk`: the CLT linker rejects the default 27 SDK; no cmake on the Mac): info, backup
  from cache, full RAM D fetch byte-identical (2 min 9 s). Shipped in all packages (MSI: Program Files\TI Revive,
  pkg: /Library/Application Support/TI Revive, Linux: ~/.local/bin via install.sh); README "Backups" section.
  **App "TI Revive Control Center"** (native/ccapp, VSTGUI standalone, the user chose it 2026-10-15): device +
  banks, backup with progress/cancel, library conversion; packaged (MSI + Start menu, /Applications, Linux
  ~/.local/bin + desktop entry, needs gtkmm 3). Tested: Linux with the TI2 (Xvfb + XTest), Windows VM start (no
  Virus). Open: start on macOS (user), file dialogs on Windows/macOS. RAM restore only after a separate review.
- **Nerd mode** (user's idea 2026-10-15, agreed plan): (1) DONE logs + debug switches (page of the main window,
  `tirevive-cc logs` / `debug on|off`), tested on Linux (Xvfb). (2)+(3) DONE for the POSIX broker: protocol 4
  observer role (vtiproto.h), decoder src/cc/decode, Live tab, `tirevive-cc watch`; tested on Linux with the TI2
  (installed 0.8.1 broker stopped for the test and restored). (4) Windows broker observer DONE (VM test without Virus),
  macOS broker + CLI built on the M3 and tested without Virus (Virus is on the Linux PC), CI run 38718444950 green,
  merged. Open: the app's Nerd mode page on Windows/macOS only built, not looked at; Live with the Virus on
  Windows/macOS. Later ideas: timing view (klog as a switch), filter by part/kind, save the live view.
- **Access look of the Control Center** (branch `cc-skin`, user's wish 2026-10-15): layout + images of Access'
  Control Center exe at runtime (native/docs/controlcenter.md "The Access look"), tested on Linux under Xvfb.
  Setup scripts copy `Virus Control Center.exe` (Linux/Windows). Open: Windows look check, macOS source of the
  images, model choice (Desktop/Keyboard/Polar), real folder dialog check on a desktop. MERGED to main.
  Then: macOS images (setup command copies Contents/Resources PNG+XML; loading checked on the M3), model choice by
  clicking the picture (VSTGUI preferences), Nerd mode label for page-74 actions. macOS checked by the user
  (2026-10-15, test pkg 0.0.7): Control Center works, sound fine with the new broker. Version 0.8.2 prepared.
- README screenshots (docs/images/: plug-in filter page, Control Center Access look, Nerd mode): the maintainer
  allowed screenshots as the only exception to AGENTS.md rule 1 (2026-10-15); made with nativehost --editor --shot
  and the app on Xvfb (dbus-run-session, no paths / user names in the images). No "send SysEx" console (rule 2).
- Mac broker: the MIDI driver stand-in (`vtimidi_start`, VirusTICommMem, E100) only served the original plug-in of
  the classic path; it is now opt-in (`VTI_BROKER_MIDISHIM=1`). Without a reader its ring overflowed ("MDR ring full,
  dropping" in 0.8.1 broker logs). Tested on the M3 + TI2 with a broker built there: autest (native AU) connects,
  VTI_TRACE shows 434 writes to EP 0x06 and 129 reads from EP 0x86, tirevive-cc info works; no stand-in log lines.
  Audio could not be checked over SSH (no microphone permission: peak 0 also with the installed 0.8.1 broker) -
  a DAW check by the user is still open (the user will test sound later). The Virus is back on the Linux PC.
- `build.yml` can now run by hand: `gh workflow run build --ref <branch> -f version=0.0.1` builds all packages as
  artifacts (the release job still needs a tag). Use it instead of the rehearsal repo before a tag.
- Later: Linux `library_dir()` and install.sh use `~/Documents` even where the XDG documents folder is localized
  (e.g. `~/Dokumente`); consistent today, nicer with `xdg-user-dir DOCUMENTS`. USB3 output (Global USB controller 9); real readiness instead of the fixed 4 s
  note hold. **No flash writes, no OS update, ever** (AGENTS.md rule 2).

## 0.8.1 (2026-10-15): native Audio Unit, Mac without Rosetta

- `native/au/tirevive_au.mm`: the plug-in as AUv2 for Logic / GarageBand (Steinberg's wrapper, AudioUnitSDK 1.4 built
  by CMake), with the classic Audio Unit's codes; its old projects open (E146). `auval` passes, user-tested.
- Mac package: VST3 + AU + broker only; `Set up Access Virus TI files.command` unpacks the Access `.pkg`; the classic
  bridge, Rosetta helper and libVirusUSB are gone (src/mac/ kept for reference). Workflow `dev-mac` (manual) builds
  the Mac plug-in and package from any branch.

## 0.8.0 (2026-10-15): the native plug-in replaces the classic one

- **"TI Revive Virus Control"** (`native/`, moved in from the private workshop repo virus-ti-native with its full
  history, native/docs/migration.md): VST3 on Windows (MSVC), macOS (universal) and Linux (Ubuntu 22.04 build); the
  Windows ASIO driver **"TI Revive Virus Audio"** (6 in / 2 out, no Access driver). Timing sample accurate with
  reported latency (E139-E141, E143), any host sample rate (resampler), start-up/sync behaviour (E144, E145).
- It takes over the class id of the classic plug-in (Windows/Linux wrapper id, macOS bridge id; moduleinfo
  Compatibility for the others), so projects of "Virus TI (TI Revive)" open with it. Vendor stays "TI Revive".
- Installers: Windows MSI without wrapper / ASIO wrapper / Control Center launcher; `tirevive-access.ps1` only unpacks
  the Access `.msi` (msiexec /a) and offers to remove a full Access install of 0.7.x (files kept, dead Access ASIO
  entry removed). Linux `install.sh` without Wine/yabridge (msiextract or Wine unpack). Mac pkg without the bridge
  VST3 (the Audio Unit for Logic stays, the Access software is still installed for it).
- Tested by the user on real hardware: Windows 11 PC (Ableton 11, ASIO), MacBook Air M3 (Ableton 12), Linux REAPER
  (44.1/48 kHz). Release workflow rehearsed in a private repo (all jobs green).
- Not in 0.8.0: Virus TI Snow (owners stay on 0.7.4), Virus Control Center, Windows MIDI ports.
- Next: Snow in the native plug-in (needs a tester), OSC display for mixed shapes (wave tables), possibly the third USB return (cf. github.com/Give-me-a-signal/virus-ti-rework: Global USB controller 9)
  and RAM restore on exit; contact with that project pending.

## Status

| Platform | State | Released | Key docs |
|---|---|---|---|
| Windows 10/11 | native plug-in (VST3) + ASIO "TI Revive Virus Audio", WinUSB binding, broker with USB audio, Control Center app | `-setup.exe` (Burn bundle) + `.msi` | native/docs/architecture.md, design.md |
| macOS 14.4+ | native plug-in (VST3 + AUv2, universal, no Rosetta since 0.8.1), broker, Control Center app | `-mac.zip` (pkg + setup scripts) | native/docs/architecture.md, design-macos*.md |
| Linux | native plug-in (VST3), native broker owns the whole Virus (MIDI + iso audio), Control Center app | `-linux.tar.gz` (install.sh) | native/docs/architecture.md, design-linux.md |

Latest release: **v0.8.26** (2026-10-18; the rounds 0.8.19-0.8.26 are in the sections above; 0.8.18 of 2026-10-16: full project recall E174, "No Virus TI" page E173, help reasons; earlier: v0.8.10 reconnect + absent start + two DAWs on Windows, E163/E166; 0.8.9: the third USB output, E159; 0.8.8: soft knobs on the Virus change the sound, E154; 0.8.7: presets chosen on the Virus + soft knobs, E150/E152; 0.8.6: start-up waits for the Virus, E149; 0.8.5: one plug-in instance per process, issue #6). The 0.8.x
releases are summarised in the sections above; 0.7.x and older in [HANDOFF-history.md](HANDOFF-history.md).
Virus TI Snow: works with the native plug-in per user reports (macOS, 0.8.3+, USB 3 and soft knobs with 0.8.20, E188; issue #4 closed 2026-10-17); v0.7.4 stays available for Windows.

## Open items (order agreed with the user 2026-10-12: easy first; deferred stays deferred)

1. ~~Mark a release as Latest~~ - done (0.7.1).
2. ~~Tidy up HANDOFF~~ - done (this file).
3. ~~Windows setup.exe interactive dialogs~~ - done (E126).
4. CLOSED for the native plug-in (checked 2026-10-16): E127 was measured on the classic 0.7.x path; the native plug-in
   starts exactly on Linux/REAPER and macOS/Ableton (first onset 2000.00 ms, E140/E141). Windows only measured in the VM
   (E141's open gap pattern) - recheck on a real PC. Old note: first notes at transport start (E127): same on Windows; the user remembers the same with the original
   Access driver (start playback slightly before the first note). Optional later: confirm with the win10 snapshot pre-winusb.
5. OBSOLETE since 0.8.0 (checked 2026-10-16): only the 0.7.x Wine path; the native plug-in on Linux runs without Wine.
   Old note: occasional sync reports on Linux (E128): cause = wineserver's registry save ~30 s after the plug-in process starts
   (stalls the process ~25 ms -> plug-in re-anchors -> synth reports). Done: vtiaudio polls the ring under Wine (released
   in 0.7.2). Wine 11 tested (E131, paused by the user): WineHQ 11.0 stable / 11.19 devel (noble builds, unpacked in ~/.local/opt/wine11*, prefix copy ~/.local/opt/wine11/prefix) have NO ntsync support compiled in; same 30 s registry stall (61-64 ms). /dev/ntsync exists after `sudo modprobe ntsync` (until reboot). This PC is back to the plain 0.7.1 release (plug-in, broker, units).
6. DONE 2026-10-16: Logic Pro test of the native AU (0.8.16 test build: loads and plays, user).
6a. ~~macOS + Cubase blocklist~~ - resolved (E129): Cubase 15 loads TI Revive fine; the blocklisted plug-in is the
    original Intel "Virus TI.vst3". (a) DONE (released in 0.7.2): "Install Access Virus TI software.command" checks Rosetta and offers to install it,
    bridge log names a missing Rosetta (EBADARCH). (b) DONE (E130, released in 0.7.2): originals moved to /Library/Application Support/TI Revive/Access plug-ins;
    verified in Cubase 15 with a local 0.7.2 test package; uninstall moves them back (tested on the Mac).
6b. macOS universal bridge (E132, RELEASED in v0.7.3, release package verified universal): DAWs under Rosetta can load TI Revive; tested with an x86_64 test
    host under Rosetta. Real test done (E133: Cubase 15 under Rosetta plays). Reddit Cubase 14 user: 'not connected' = LIBUSB_ERROR_ACCESS (E134): old Access drivers from a full install hold the Virus; told him to remove them + restart; install script now detects/disables them (not released). CONFIRMED: Aura Plugins' Virus Editor USB MIDI driver held the Virus; parked -> connected and syncing. Install script now also detects Aura's driver and offers to park it (default No). RELEASED in v0.7.4.
6c. OBSOLETE since 0.8.1 (checked 2026-10-16): the native AU/VST3 has no Rosetta helper round trip. Old note: macOS CPU (Reddit user, Cubase 14 under Rosetta, M4 Max): Cubase's CPU meter jumps only while STOPPED, smooth during
    playback -> ASIO-Guard real-time path for the selected/armed instrument track (per-block round trip to the helper,
    translated). Harmless unless dropouts; advice: buffer 512, deselect the track. Measured on our M3 (Cubase 15 under Rosetta,
    30 s play/stop/play): Cubase 23-28 %, vtihelper ~2.5 %, vtibroker ~4 % of one core, NO change between playing and
    stopped -> the ASIO-Guard jump at stop is Cubase moving the selected track to the real-time path, not extra work.
    Optional later: editor mirror copies only changed frames.
7. DONE in 0.8.2: TI Revive Control Center (all platforms, also `tirevive-cc`).

8. STILL WATCHING: E136 (Virus hung after rapid give-back resets during tests on 2026-10-13); if it repeats,
   rate-limit the reset in vti_close (src/broker/vticore.c, unchanged). Kernel log of this PC checked 2026-10-16: no
   'error -71' / 'not accepting address' since (the original event is not in the log either, so weak evidence).

**Snow on Windows works** (issue #3, E138, 2026-10-14): after removing the old Access USB driver (oem8.inf) and
0.7.4 the Snow plug-in works in Ableton 12.4.6 / Windows 11 25H2 (user confirmed, WinUSB on MI_00/03/04).
Waiting: **Snow on macOS** (issue #4) - MIDIServer driver holds the Virus; instructions posted, waiting for the user.
Stage 3 (native rewrite of the plug-in) is developed in the PRIVATE repo derpsn/virus-ti-native (~/virus-ti-native,
plan docs/plan.md, status docs/status.md); TI Revive is its submodule. State 2026-10-13 (evening): the native VST3 works on Linux (REAPER),
Windows (Ableton 11, win11 VM) and macOS (Ableton 12 on the M3, universal, Core Audio input) - user tests: sound,
timing, editor from the user's Access files, browser/search like the original. Night 2026-10-14: stability and
performance pass (thread races fixed via TSan/ASan, no lost note-offs, idle CPU 5 % -> 1.4 %, partial repaints for
automation, unit tests in CI); the device tests of it are on the list in docs/status.md there. Open there: a few
displays, an Ableton test with several M4L LFOs. Timing R5 solved 2026-10-14 (E139: the sync ping's
marker on USB 2 = the Virus' stamp clock 0 -> same latency in every session, reported to the host). Verified
2026-10-14: Linux/REAPER and macOS/Ableton 12 renders sample-exact (E140, E141), Windows only in the VM (open gap
pattern); macOS M4L test with three LFOs ~40 min stable (0 drops). Contributor/agent guidelines added (CONTRIBUTING.md,
AGENTS.md, .claude/skills). Issue #3 (Snow, Windows) solved; #4 (Snow, macOS): not supported by the Mac helper yet
(answer posted, Snow VST2 hosting would be needed - or the native plug-in).
Broker side from it: Mac broker logs "MDR ring full" once per episode (079904d, next release).
Kemper (2026-10-13, reply to the user's request): the Virus TI software will not be open-sourced, no plans; the
wish is forwarded to management. -> stage 3 stays on its rule: no Access material in any repo, everything loaded
from the user's own installation.
Deferred by the user: Windows MIDI ports (design-midi-ports.md), WDM sound card (design-wdm-bridge.md), latency
measurements.

## Release process

- Releases ONLY via version tags: `git tag -a vX.Y.Z -m ... && git push origin vX.Y.Z`; CI (.github/workflows/build.yml)
  builds binaries (mingw) -> MSI + setup.exe (windows-2022, WiX 5.0.2, `build.ps1 -Bundle`) -> mac zip (macos-15,
  scripts/mac-build-release.sh, VST3 pluginterfaces pinned 4f547e8e) -> linux tarball (scripts/linux-build-release.sh,
  static libusb without udev) -> release with all assets + scripts/tirevive-usbinfo.ps1.
- After CI: `gh release edit vX.Y.Z --notes-file work/release-notes/vX.Y.Z.md` (notes written beforehand).
- Identity: commits/tags only as `derpsn <callmederpsn@proton.me>`; never the old account/name/e-mail; never push to
  the old repo. Commit trailer: `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Before releasing a new Windows wrapper: run src/wrapper/vtiloadtest.exe against it (E092).

## Environments

**This Linux PC** (Ubuntu 26.04, kernel 7.0, PipeWire; Virus TI Desktop (TI2) on USB; udev rule 99-virus-ti.rules = plugdev):
- TI Revive installed like a user: broker ~/.local/lib/tirevive/vtibroker (copied 2026-10-15 13:44) started on demand
  by the systemd user socket `tirevive-broker.socket` (Unix socket /tmp/vtibroker-<uid>.sock only, no TCP since 0.8.x);
  the native plug-in in ~/.vst3 ("TI Revive Virus Control.vst3", moduleinfo 0.8.3 - older than the release);
  Control Center ~/.local/bin/tirevive-control-center, CLI tirevive-cc. Broker log: `journalctl --user -u
  tirevive-broker`. Access files (the user's): ~/.local/share/tirevive/access/... (Virus Control.dll 5.1.7, sha256
  a622f9a2..., also used by REA); the classic Wine prefix ~/.wine + yabridge are only for the 0.7.x path.
- Development tests without a DAW: `build-native/bin/Release/nativehost "<plugin.vst3>" [--editor] [--seconds N]`
  (DISPLAY=:0 shows the editor on the user's desktop; Xvfb for headless shots), `VTI_NATIVE_DEBUG=1` for the log;
  `build-native/bin/Release/virusmon [s]` shows the traffic read-only next to it.
- REAPER 7.82 portable: ~/.local/opt/dl/reaper/reaper_linux_x86_64/REAPER/reaper; start `pw-jack .../reaper`
  after `pw-metadata -n settings 0 clock.force-rate 44100` + `clock.force-quantum 512` (PulseAudio backend = messy, E122/E123).
- User is in group pipewire (rtprio 95, memlock 4 GB). Ask the user to close REAPER before tests that need the Virus
  (one owner at a time; a second plug-in instance waits).
- Gotcha: never `pkill -f <pattern>` in a tool call whose own command line contains the pattern (kills the shell).
- Gotcha: tools built against build-native/lib must use the headers of the same branch (Broker's layout differs
  between branches -> crash in the reader thread); rebuild vticore after switching branches.

**Windows VM** `win11` (libvirt, `VM=win11 scripts/vm.sh ps|push|pull`, user in ~/.config/tirevive/vm.env): the
current disk is build machine AND test machine (WiX 5.0.2 + .NET in C:\build, extensions UI/Util/
BootstrapperApplications - NOT Bal.wixext, that shows "damaged"; TI Revive 0.6.4 test build, Access 5.1.7, Ableton
Live 11). `bash scripts/win11/build-msi.sh <version>` builds MSI + setup.exe there. Virus passthrough:
`virsh detach-device/attach-device ... scripts/virus.xml` (never virt-manager USB redirection, E061). Shut the VM
down after use.

**Mac** (MacBook Air M3, host/user in ~/.config/tirevive/mac.env, `scripts/mac.sh ssh|push|pull`): Ableton Live 12
native, GarageBand; SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk; release builds via CI.

## Things that bit us (keep in mind)

- win10 VM (original + Access driver): Windows crashes with bug check 0xCE (a driver unloaded with pending
  operations) when the Virus disappears or resets under the Access driver - a Virus power cycle or re-plug while it
  is attached, or a libvirt attach right after it re-enumerated (2026-10-17, twice). Close the DAW before a detach,
  never power-cycle the Virus while it is in the VM. Reinstalling the Access driver starts the firmware updater after
  the reboot, which switches the Virus into update mode; the re-enumerated device is not passed to the VM, so nothing
  is flashed - close the updater, power-cycle the Virus (E199).

- The Access plug-in needs constant host blocks of 64/128/256/512 at 44.1 kHz (Mac AU block splitting reverted, E111;
  Linux PulseAudio backend E122/E123).
- The plug-in writes its Virus memory cache (ProgramData/.../Cache/<id>) only on a clean shutdown; a killed DAW means
  a new full sync (~7 min on a fresh cache, ~1 patch/s like the Access driver, E031/E119/E123).
- One program at a time owns the Virus (POSIX brokers, E112); a second one shows "no Virus TI connected".
- Linux: snd-usb-audio cannot share the Virus; the broker takes all 5 interfaces and hands them back on exit by
  claim/release + port reset (E113/E116). Wine 10 (Ubuntu) has no AF_UNIX -> TCP + token, TCP_NODELAY (E117/E118).
- yabridge 5.1.1 + Wine >= 9.22: dead mouse input (E120); install.sh warns.
- After an editor was open, Virus Control.dll can hang in DLL_PROCESS_DETACH at process exit (E119); the FreeLibrary
  path (what DAWs do) is fine.
- WinUSB / Windows: MI_00 bound to WinUSB too (broker streams audio itself, E075-E078); never WASAPI clocks.
