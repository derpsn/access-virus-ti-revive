# Design: follow the DAW sample rate like the original (round 0.8.32, plan, nothing built yet)

Decision (user, 2026-10-22): "we want to be as close as possible to the original plugin" - the Virus runs at 48 kHz
for 48 kHz projects instead of a fixed 44.1 kHz plus resampling. Sources: E321 (the capture), E323 (the original's
plug-in, driver sequence, USB chip and DSP), the audit of our 44.1 kHz assumptions (work/rate-follow/audit-44k1.md,
local), the risky cases E112, E175, E241, E257.

## What the original does (E323)

- **Rule** (plug-in 0x1800a9dc0): double the host rate while below 44000, halve it while above 88000; then
  >= 46000 -> the Virus at 48000, else 44100. No rate refused (96/192 k -> 48, 88.2/176.4 k -> 44.1, 32 k -> 48);
  the rest is converted host <-> Virus in the plug-in.
- **When**: at a host rate change and at the first start when the device runs at another rate; applied at the next
  resume (hosts change the rate while the plug-in is inactive).
- **On the wire**: stream stopped, alt 0, ~250 ms, alt 1, SET_CUR to EP 0x82 and 0x01 (twice), IN, then OUT; never a
  SET_CUR while streaming; ~0.7 s until 48 k audio; IN keeps 44.1-size packets ~230 ms after SET_CUR.
- **The Virus**: its DSP clock follows - synthesis and the stamp clock run at 48 kHz (1000 frames per MIDI clock tick
  at 120 BPM instead of 919). It shows "Audio clock changed" by itself.
- **Timing**: stamps in Virus frames at the current rate; fixed frame constants are not scaled; a new sync ping
  0.5-1 s after every restart; the new latency reported to the host.

## Our design, in the order it gets built (each step test first, then hardware)

1. **Mapping and units (pure logic).** `virus_rate_for(host)` exactly as above. `ratio` = Virus rate / host rate
   everywhere (resampler step, sidechain, wait_offline, timeline, transport, MIDI clock, latency report). Constants
   that are TIME become rate-derived (keepalive 0.3 s, gap fill 5 s, anchor windows 0.5 / 1 / 6 s, marker timeout 5 s,
   klog windows, stats "last second"). Constants that are STAMP FRAMES stay frames like the original (S, A, M, the
   ping's 3072, read margins, ring 16384). `resampling()` = host rate != Virus rate. Tests: mapping table (22.05 ...
   192 k), each derived constant at 44.1 and 48 k, resampler at 48 k and the exact 2:1 step.
2. **Shared ring knows its rate.** vtiaudioshm: `rate` is read, plus a `rate generation` the broker bumps at every
   switch (VTI_AUDIO_VERSION bump). The plug-in drops its anchor on a new generation and re-pings like after a
   reconnect (that path exists). Tests with a fake ring (test_audioin).
3. **Broker: one rate switch, done like the driver.** Protocol: VTIMSG_AUDIO carries the wanted rate (protocol
   version bump; old clients = 44100). The broker switches only when the stream's owner asks, debounced (at most one
   switch per ~1 s, rule 2), in the driver's order: stop iso and wait for every cancel, alt 0, 250 ms, alt 1, SET_CUR
   0x82 then 0x01 (twice, results checked), IN, then OUT; never SET_CUR on a running stream. Deliberate deviation:
   no SET_CONFIGURATION (libusb/WinUSB cannot re-issue it with claimed interfaces). The 44/45 pattern, the range check,
   `pktstat` and the "rate lost" detector (E241) become relative to the wanted rate; the lever (73 00 10 vv) sends the
   wanted value - otherwise our E241 fix would fight a deliberate switch. Tests: pktstat at 48 k, the switch sequence
   against a fake libusb/WinUSB layer, debounce.
4. **Who owns the rate.** Linux: the broker (one stream). Windows: one stream for every plug-in plus our ASIO driver -
   like the original's single driver rate, the last plug-in that starts sets it; the ASIO driver accepts 44.1 and 48 k
   and gets kAsioResetRequest / sampleRateDidChange when the stream changes under it. macOS: the plug-in sets the
   Core Audio nominal rate of the Virus device itself (audioin.cpp), reads it back, listens for changes by others
   (E112: a flip from a second process hung the Virus - so only on our own resume, debounced).
5. **Plug-in flow.** setupProcessing stores the host rate; setActive(true) (resume) asks for the Virus rate when it
   differs (like 0x1800a9880), waits for the new generation, re-pings, re-anchors, then restartComponent
   (kLatencyChanged). Offline renders at a new rate go through the same path.
6. **Docs and texts**: protocol.md, architecture.md, design-linux.md, README/site timing rows (new measurements at
   48 k), the ASIO driver's "44.1 kHz only".

## Before any switch reaches the Virus (rule 2)

- The user reads the Virus' "System Clock / SA Freq" setting on the device (CONFIG menu) - E323 (6): a Virus set to
  48 k may switch itself after a USB reset, which would explain E175/E241. No new request is sent to read it.
- The first switch on hardware is done by hand in a controlled run (nativehost --rate 48000 on Linux, the user
  watching the display: "Audio clock changed" expected), then 48 -> 44.1, then rapid changes are refused by the
  debounce.

## Tests on hardware (round checklist)

- Linux: hil.sh at 44.1, 48 and 96 kHz (nativehost --rate), H3 export at 48 k/128, a switch 44.1 -> 48 -> 44.1 while
  the plug-in is loaded, LC5 sleep at 48 k, LC4 power at 48 k.
- macOS: hil-mac.command at 48 k, a Live export at 48 k/128 (E319 setting), Live's rate change.
- Windows: hil-win at 48 k in the VM (function only, E320) and the ASIO driver at 48 k; timing on the real PC.
- startup_diff.py against a capture of the original at 48 k (the switch sequence on the wire).

## Risks

- Every switch re-locks the Virus' DSP clock; a safe rate of switches is not known (debounce, no automatic retries
  that switch back and forth).
- E241/E175: our current "48 kHz = lost" logic becomes wrong the moment 48 k is wanted; it must change in the same
  step as the switch.
- Windows: two DAWs at different rates share one Virus - the last start wins (as the original), the other gets a
  reset request.
- macOS: the Virus device is shared with every other Core Audio app.

## Open (E323)

- What the original's `UseSampleRateConverter` = false changes.
- Whether the driver re-configures at start when the rate is already right (capture needed: original start at 48 k).
- The chip's 0x3a2 toggle path; the DSP variant flag X:$4806f (TI vs TI2).
