# Plug-in <-> Virus protocol as the original uses it (from broker traces, tools/msgsummary.py)

Transport: USB-MIDI packets on EP 0x06 (host -> Virus) / 0x86 (Virus -> host); cable 2 = parameters, notes,
requests; cable 3 = bulk dumps; cable 5 = `F5 lo hi` time stamps (14-bit frame) before timed events; MIDI clock F8
on cable 2. SysEx: `F0 00 20 33 01 <dev=0x10> <cmd> <a> <b> ... F7`. USB control requests in parallel (spec.md):
wIndex 0x01 status poll (~1 per 50 ms), 0x12 address writes, 0x30 16-byte reads, 0x22 (stalls, as with Access).

## Start-up with an empty cache (original-fullsync.trace, Virus TI2 Desktop, 2026-10-13)

| t | dir | command | meaning (hypothesis where marked ?) |
|---|---|---|---|
| 7.1 s | out | 25x `73 00 <id> <v>` | global parameters (remotedevice.xml GlobalInit) |
| 7.2 s | out | `11 00` 267 B | multi dump of the plug-in into the edit buffer |
| 7.2 s | out | `72 <part> <id> <v>` part 0..15 | multi part parameters (ParamInit?) |
| 7.2 s | out | `F2` song position | transport |
| 7.3 s | out | `6F <part> ...` part 0..15 | a page-D single parameter per part (?) |
| 7.4-9.4 s | out c3 | 16x `10 00 <part>` 524 B | the plug-in's 16 single patches into the part edit buffers (project restore) |
| 9.4 s | out | 224 CC | 14 controllers x 16 channels |
| 9.6-14 s | out/in | `39 <bank>` -> `14 <bank>` 267 B | per RAM bank 1-4: bank info (?) |
| 13.5-546 s | out/in | `30 <bank> <prog>` -> c3 `10 <bank> <prog>` 524 B | 4 RAM banks x 128 single dumps, one request per ~1.01 s, answer ~1 s later |
| 9.6-546 s | out | `74 01 7E` ~1/s | poll during the sync (?) |
| 546 s | | | sync done, notes play (vtihost) |
| 607 s | in c1 | `6F <part>` x16 | after the plug-in closed (reset echo?) |

ROM banks are not transferred over USB: the plug-in builds them from the firmware package (cache files "ROM A".."Z").
Answered: ~1 s per request is the Virus' pace (measured with virusctl: 0.85-1 s per reply; overlapping requests ->
truncated 281-byte reply, as at the start of the original's sync). `74 00 29 64` = Intern Display/Messagetimer 100,
`74 01 7E 07` = Actions/Rise Message 7 (repeated ~1/s while syncing: keeps a message on the Virus display, presumably
"synchronising"). `6F <part> 7F 00` (start) / device `6F <part> 7E 00` (close; 7E = Input/Atomizer): page D id 127 is not
in remotedevice.xml - open. Cached start: start-up part + 39 per bank, no 30/10.

## Parameter changes (automation, original-automation.trace)
CC / poly pressure / parameter SysEx exactly as remotedevice.xml describes (docs/formats.md), value = round(norm*127).
On close the original resets globals (73 00 3B/3A/3C/7A).

## Dump formats (verified on 528 recorded single dumps, tools: `vtimodel message <id>`, `vtimodel dump <file.syx>`)
- remotedevice.xml dump templates: the byte `index` is the offset inside the SysEx message (incl. F0).
- Single dump `10 <bank> <prog>`: 524 bytes; header 9 bytes, data 9..520 (two 256-byte pages A/B), byte 265 = add7 of
  bytes 5..264 (page A checksum), byte 522 = add7 checksum of bytes 5..521, F7. VirusTI_Singledump describes 435 parameter bytes (+ bit fields), the patch name is
  `Single Patch Name/Character 01..10` (bytes 249-258). Bank 0 = edit buffer (part = program byte). decode + re-encode
  of all 528 dumps is byte-identical (unknown bytes are kept: update_dump patches an existing dump).
- Multi dump `11 00 00` (267 B) at start: a FIXED table in Virus Control (start-up function 0x180070520 sends
  0x1801c5cc0): multi "MultiCtrl!", parts 1-16 = edit buffers on channels 1-16 (part bytes 0x01..0x0F ramp), volume
  0x7F, pan/etc 0x40, 0x0A and 0x4F columns per part. Its checksum byte (0x26) is stale (add7 gives 0x24) and the
  Virus accepts it. The rewrite builds this multi itself (own code, correct checksum), decoded via VirusTI_MultiDump.
- Bank info `39 <bank>` -> `14 <bank> 00` + 256 data bytes (267 B, add7 checksum OK): 128 x (lo, hi) = one 14-bit
  value per program = sum of the single dump's bytes 13..521 without byte 265, mod 16384 (Virus Control checks a
  cached dump this way; verified on all 512 recorded programs; RemoteDevice::single_fingerprint). Byte 521 (after the
  two 256-byte pages, before the checksum) is usually 0 but 100 in two patches. The cache files (RAM A-D: "MIDI"
  00 01 08 00 + 128 x [u32 BE 524 + single dump]) do not store the table; a cached start requests 39 for every RAM
  bank and fetches only programs whose value differs (none in our traces).
- Replies arrive on EP 0x86 (plug-in interface, cable 3) during a plug-in session and on EP 0x85 (MIDI port, cable 1)
  otherwise (virusctl sysex). The Virus needs ~1 s per request in both cases.

## Decompile pointers (Virus Control.dll 5.1.7 x64, sha256 a622f9a2..., Ghidra; output only in work/decomp)
- 0x1800a1540: cached single dump vs bank-info value (fingerprint, see above).
- 0x1800a8420: F5 time stamps to the device (USB-MIDI `53 F5 lo hi`, 14-bit frame, -0x400 when late).
- 0x1800303b0: transport -> F2 song position (+ FC stop) to the device.
- 0x180070520: start-up sequence (globals, multi table, part init).
- 0x18009b640: CVirusBank constructor (cache path {common_appdata}/Access Music/Virus TI/Cache/<device>/<bank>).

## Playing the Virus (verified with the native plug-in, 2026-10-13)
- The Virus only plays plug-in notes after the start-up GlobalInit (Global Master/Mode = MIDI REMOTE etc.;
  RemoteDevice::init_messages reproduces the original's 24 globals + 17 part settings byte-identically).
- Every channel event on cable 2 needs a preceding time stamp `F5 lo hi` on cable 5 (USB-MIDI `53 F5 lo hi`); events
  without one are ignored. A stamp = the newest frame of the broker's audio ring plays the event ~140 ms later
  (first test). The original's stamps run 400-1000 frames behind the ring's newest frame in a 512-frame sawtooth (its
  block grid) - R5: derive the exact rule from 0x1800a8420 for sample-accurate timing.
- SysEx dumps are accepted at about MIDI speed (16 part dumps ~2.7 s), events queue behind them.
- Stamp rule (decompile, R5 to-do): stamp = device time (virtual +0x60 of the audio object) - anchor (+0xa40) +
  sampleOffset * (device rate / host rate) [0x1800a8420]; the anchor is set in 0x1800a8900 when an INCOMING AUDIO
  SAMPLE is out of range (|s| > 0x300000 in 24 bit): the Virus marks sync points inside its audio stream and the
  plug-in anchors its time stamps to the sample position of the marker (+ device time from virtual +0x68). Check
  whether the broker's 16-bit ring keeps these markers (it converts 24 -> 16 bit) before implementing.
- Stamp calibration (virusctl timing, 2026-10-13): notes stamped at newest ring frame + D for D = -2000..+8000 all
  sound at a FIXED ~6920 frames (157 ms) after sending - with or without a running stamped MIDI clock (24 ppqn).
  So in our session the Virus does not honour the stamp value; the original reaches sub-ms timing (E121), so its
  session differs: candidates (R5) the audio sync markers / anchor (0x1800a8900), the time base being the Virus'
  counter at an anchor rather than the ring frame, the USB control-request handshake. Note: the Virus needs the
  GlobalInit AND the multi dump (after every broker restart) before it plays plug-in notes - virusctl timing sends only
  the globals, so run it right after the plug-in (nativehost) or extend it.
- Timing measurements 2026-10-13 (broker VTI_TRACE + VTI_RECORD while the ORIGINAL plays a note per second under
  Wine): the original's stamps run ~5000 frames behind the ring's newest frame (sawtooth 4790..5231 = its block
  grid) and every note starts EXACTLY 8315 frames after its stamp (18/18 notes; onset 3265..3528 frames after
  sending) -> the Virus honours stamps on a timeline it derives from the plug-in's stream of F5 stamps.
  Our experiments (virusctl timing, VTI_CLOCK/VTI_S/VTI_F5ONLY): with a continuous stamped stream (F8 or bare F5
  every 10 ms at newest-S) stamps are sometimes honoured (onset follows D, offset depends on S and the session),
  otherwise executed "immediately" (12-17 ms after sending, vs 66-157 ms without a stream); results change between
  sessions -> the Virus keeps timeline state (PLL-like). The exact rule (0x1800a8420 + device time + the anchor
  from audio markers 0x1800a8900, and how the original measures K = 8315) is the R5 work item.
- BREAKTHROUGH (native plug-in, 2026-10-13): the original's stream = MIDI clock F8 at the host tempo (24 ppqn, 918.75
  frames at 120 BPM), each tick stamped with its frame, events stamped on the same timeline, and at the start
  `F5 t FE` + `F5 t F2 <song position>`. With the stamped song position + stamped clock the Virus HONOURS the stamps
  with a constant per-session offset: S=5000 (stamp = newest-5000) -> onset = stamp + 10445 (onset ~5500 frames after
  sending), S=2000 -> stamp + 5683 (~3700 after sending); the original: stamp + 8315 (~3300 after sending). Without the
  F2 the stamps are ignored (immediate, 7-17 ms, or 66-157 ms with no stream at all). The Virus sends Sync
  Information Events (73 00 54 01) when our timeline re-anchors at the start. Open (R5): stable start (anchor once the
  audio runs), the offset at runtime (needed to read the audio aligned = sample accuracy; the original anchors on
  audio markers, 0x1800a8900), latency reporting to the host.
- Margin sweep 2026-10-13 (song position stamped M frames before the timeline, S=2000, clean start after the upload,
  0 sync reports in all runs): M=1024 honoured (K 3097/3498, notes 23-32 ms after sending; an earlier run: K 6832,
  ~105 ms), M=2000/3000 immediate (~340 frames; an earlier M=3000 run honoured with K 4133), M=4000 honoured (K ~8300,
  ~140 ms). The Virus' acceptance window is not a fixed function of M -> default M=1024 (lowest latency observed);
  sample accuracy still needs K at runtime (R5).
- Sync marker (decompile 0x1800a8880/8aa0/8900/8f60): the original waits for 1 s of silence on input channels 2-5,
  sends the sync ping `F0 00 20 33 01 10 73 00 22 00 F7` (global id 0x22, not in remotedevice.xml; in the traces with
  the fixed stamp 3072, next to FE + F2) and then scans channels 0/1 for a sample beyond +-0x300000 (int16 << 16 under
  TI Revive = +-48 in the ring) to set its stamp anchor. Measured: neither the original (vtihost under Wine) nor the
  native plug-in (VTI_NATIVE_PING[3072]) gets such a sample on ring channels 0/1 - the original's accurate timing
  (E121) does not depend on the marker under TI Revive. Next idea for R5: read the original's latency/K constant
  (its reported plug-in latency, getLatencySamples) and the stamp computation at 0x1800a8420 with the anchor unset.
- **SOLVED 2026-10-14 - the stamp clock and its marker:** after the sync ping (`73 00 22 00`, stamped 3072 like the
  original, sent with FE + F2) the Virus puts ONE non-zero sample (value 511) on **USB 2** (ring channels 4/5: right
  channel at frame f, left at f+1; earlier searches only looked at channels 0/1), ~800 frames after the ping is sent.
  At that frame its 14-bit stamp clock is 0: a stamp `s` (F5 lo hi) plays at the ring frame `f` with
  `f = marker + s (mod 16384)`, exact to the frame in every honoured session (6/6, 3 more with a different ping
  stamp). Without the marker our stamps (ring frames) land at a random point of that clock - in its future (honoured,
  K anywhere 6000..11400) or its past (played on arrival, +-10 ms jitter): the per-session latency 70..240 ms.
  Now (plug-in): the timeline is anchored after 1 s of silence on USB 1/2 (as the original, at most 6 s), the ping
  goes with FE/F2, the worker finds the marker (DeviceOut::scan_audio), and every stamp becomes
  `t + lead - marker` (lead = S + A, A = 2048): an event at timeline position t sounds at ring frame t + lead in every
  session (K = 7048 for 12/12 notes in 6/6 sessions; A >= 768 still exact, 512 partly late). process() reads the ring
  at block start + lead - latency, latency = A + max(2 * block, 768) + 256 (3328 frames = 75 ms at 512), reported via
  getLatencySamples. No marker within 0.5 s: stamps stay ring-relative (old behaviour) and the ping is repeated up to
  3 times once USB 1/2 are quiet and the host is not playing. VTI_NATIVE_NO_PING = old behaviour, VTI_NATIVE_KLOG =
  log K per note.
- The stamp clock in the firmware (2026-10-18, E245-E247): the TI2's two DSPs run one image; the master (a strap pin
  read at reset) owns the clock, the slave takes it from the master's status word in the audio ring with a fixed
  lead of 256 frames, and the master forwards channel messages to the slave the same way. The sync ping is parsed
  by the DSP itself (page 0x73 parameter 0x22, value 0): both clocks go to 0 and the slave renders the one-sample
  marker. Value 0x7E switches a continuous test signal on, 0x7F off (never sent by TI Revive). Stamps are compared
  modulo 16384 with a signed window: up to 8191 frames ahead = future (honoured), up to 8192 behind = late (played
  at once). Nothing outside the DSPs moves the clock - no host command, no USB condition. The F2 song position only
  feeds the Virus' own tempo sync (arpeggiator, LFO, delay); the "without F2 the stamps are ignored" observation above
  (2026-10-13) was the modular window with ring-relative stamps: about 44 % of sessions landed inside it (E247).
  The Sync Information Event (`73 00 54 01`) is the DSP's own SysEx (E251): at a MIDI-clock tick whose newest
  stamp is behind its clock, after ticks where stamps were ahead, it sends one event - "the lead is used up", one
  per transition, not one per late note. Late stamps still play; the event asks for more lead, not for a resync.
- Active Sensing (2026-10-18, firmware E237): the DSP arms a watchdog with the first FE it sees and releases every
  voice about a second after the last one. The original sends FE every 0.31 s (E025); the plug-in now sends one
  every 300 ms on the timeline (keepalive.h), the first with the anchor's FE/F2 as before.
- The stamp threshold and what the DSP parks (2026-10-19, E261/E262, corrects the first reading E238): a stamp at
  most 1023 frames ahead of the DSP's clock is parsed at once and the voice starts at the stamp (the threshold is
  the maximum per-voice scheduling delay); during a patch change the window shrinks for a moment. Only what is
  further ahead is parked, up to the next F5, in the DSP's 2046-byte queue without a fill check. A stamp leaves the
  plug-in A frames ahead of the newest ring frame and needs 512-768 frames to reach the DSP: on Linux/macOS
  (A = 1024) nothing is parked, on Windows (A = 3072) about 30 ms of traffic are. The byte budget (sendbudget.h,
  4 kB/s, bursts of 1000 bytes, far above musical rates) bounds what the DSP can park; unstamped SysEx on cable 2
  (editor parameters, dumps, the state) is parked with a parked stamp but does not count: the USB link (~4.2 k packets/s, E231) parks at most ~400 bytes of it in 30 ms, and as a debt it made notes late after patch dumps (E266). Automation from
  process() gets its own stamp at its block time (stamprule.h) so it lands where the host put it on every platform;
  messages from other threads inherit the previous stamp. One F5 per stamp value per USB write: the DSP keeps the
  current stamp for the bytes that follow, and an F5 per message would spend the whole budget on automation at the
  250/s cap (E264). The multi header (page 0x72 parameters 0-3) and status
  F4 are dropped before the wire (sendguard.h, rule 2). The 2026-10-13 stress hang (Linux, audio silent, MIDI
  alive) is not explained by the queue; its mechanism is open.
- The original reports a plug-in latency of 3840 + block size (4352 at 512, 4096 at 256; vtihost prints it).
  Experiments with the sync ping (S=5000, M=6441 = the original's F2 offset): ping stamped 3072 like the original ->
  honoured in 3/3 runs (K 10752-11322, ~6000 frames after sending); ping stamped on our timeline -> honoured 3/3, K
  10368-12568; without ping -> 1/3 honoured. The ping makes the Virus honour the stamps but K still varies per
  session by up to ~2000 frames -> the original must learn K differently (its device-time source, virtual +0x60/+0x68
  of the audio object). Default stays M=1024 without ping (lowest latency; the user rates it as good).

## Sample rate (rate following, round 0.8.32; design in findings/design-rate.md, E323; not yet tested on hardware)
- The Virus' USB audio runs at 44.1 or 48 kHz. The plug-in picks the rate from the host rate like the original
  (0x1800a9dc0): double below 44 kHz, halve above 88 kHz, then 46 kHz and up = 48 kHz (`virus_rate_for`). Other host
  rates are resampled by the plug-in.
- The broker protocol 5 carries the wish: `VTIMSG_AUDIO` = u32 on, the event name with its NUL, u32 rate (44100,
  48000, or 0 = no wish: keep the set rate). Older clients send no rate and get 44100 (`src/broker/ratesw.h`).
- A switch on the wire, as the Access driver does it: the stream stopped and every transfer back, alternate setting 0
  on interfaces 1/2, 250 ms, alternate setting 1, SET_CUR to EP 0x82, 0x01, 0x82, 0x01, then IN and OUT again. Never
  a SET_CUR on a running stream (except the E241 retry). No SET_CONFIGURATION (deviation: not possible on claimed
  interfaces). The start at 44.1 kHz uses the same SET_CUR order.
- At most one switch per second (rule 2: every switch re-locks the Virus' DSP clock). Who may switch: Linux the
  owning plug-in (or any client while none owns it); Windows the owning plug-in, or the ASIO driver while no plug-in
  owns the Virus (`owner.h`); a fresh start (no stream running) takes any explicit wish. macOS: the plug-in sets the
  Core Audio nominal rate itself and follows changes by other programs.
- The shared ring (`vtiaudioshm.h`) carries `rate` and `rate_gen` (+1 per switch, written before the first frame at
  the new rate); readers re-anchor and send a new sync ping on a new generation. Stamps are in Virus frames at the
  current rate; the frame constants (lead, ring margins) are not scaled, time constants are (`frames_for_ms`).
- Packet sizes are judged against the wanted rate (`pktstat.h`): 43-46 frames at 44.1 kHz, 47-49 at 48 kHz; a long
  run of the other rate's sizes is the lost rate (E241: SET_CUR again, plus the global 73 00 10 with the wanted value).
