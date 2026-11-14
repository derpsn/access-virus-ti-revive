# Design proposal: the Virus TI as a Windows sound device (virtual cable bridge)

Status: proposal (2026-10-11), not implemented. Plan item 2 in the old HANDOFF (now `log/until-2026-10-12.md`).

## Goal

Any Windows program (browser, media player, system sounds, a DAW without ASIO) can play through the Virus
outputs, as it could with the Access driver package's WDM driver (`vtiaudio.sys`). Optional later: the Virus
analog inputs as a Windows recording device.

## Constraint

A Windows audio endpoint ("Speakers", "Line In") only exists if a kernel-mode audio driver provides it.
TI Revive deliberately installs no kernel code (Windows 11, memory integrity / HVCI, no driver signing).
Since 0.3.0 the Virus audio function runs on WinUSB and is streamed by `vtibroker.exe` (E075, E081), so
Windows' own USB audio driver cannot be used at the same time.

## Options

| | How | For | Against |
|---|---|---|---|
| **A: cable bridge** (proposed) | The user installs a virtual audio cable (e.g. VB-CABLE). Windows plays to its playback endpoint; the broker captures the cable's recording endpoint with WASAPI and sends the audio to the Virus with the ASIO output. | No kernel code of ours; works next to the plug-in and ASIO; synth stays the clock master. | Third-party driver; endpoint is named after the cable (renameable in Windows); some latency (system audio, not for playing live); clock adaptation needed. |
| B: mode switch | A tool rebinds the audio function (MI_00) between WinUSB and Windows' USB audio driver. | Real Windows device, no third party. | Either/or: in Windows mode the plug-in falls back to WASAPI with the old drift/sync problems (E074); needs admin and a device re-plug each switch; no ASIO meanwhile. |
| C: own kernel audio driver | Like `vtiaudio.sys`. | Native. | Kernel code, attestation signing (cost, EV certificate), HVCI risk - what the project exists to avoid. **Out of scope.** |

## Proposal A in detail

### Data path

```
Windows apps -> "CABLE Input" (cable driver, PC clock)
                    |
   vtibroker: WASAPI capture of "CABLE Output" (shared mode, event driven)
                    |  adaptive resampler (cable clock / rate -> synth clock, 44.1 kHz)
                    v
   WDM output ring --+--> mix (sum + clip) --> OUT packets EP 0x01 --> Virus outputs
   ASIO output ring -+   (vtiaudioshm.h, E083)
```

### Clock adaptation (the core problem)

The cable runs on the PC clock (and on its own sample rate, usually 48 kHz), the Virus on its own clock
(measured 23 ppm apart in the VM, E074). Without adaptation the ring between them drifts empty or full.

- Resampler: a proven library, embedded (e.g. the speexdsp resampler, BSD licence: arbitrary ratios, quality
  settings). Converts the cable rate to 44.1 kHz and absorbs the drift.
- Control: a slow PI controller on the ring fill level steers the resampling ratio by a few ppm around the
  nominal value (target fill ~20 ms). No audible pitch change (ppm range), no drops.
- Hint for users: set the cable to 44.1 kHz in Windows sound settings, so the resampler only corrects drift.

### Latency

System audio, not live playing: target 30-60 ms (WASAPI shared period 10 ms + ring 20 ms + OUT lead). ASIO
remains the path for the DAW.

### Mixing with ASIO

Both rings are summed per OUT packet (with clipping). Each source is optional: ASIO alone, cable alone, or
both. The ASIO path is unchanged.

### Lifetime of the broker

The broker currently starts on demand and exits when idle (0.3.1, user wish: nothing running at logon).
Windows playback has no "client" to start it. Therefore:

- The bridge is **opt-in** (default off). When the user enables it, TI Revive registers the broker for the
  user's logon (HKCU Run, per user, removable) and the broker does not exit while the bridge is enabled.
- When disabled, behaviour is exactly as now (on demand, idle exit).
- The broker only captures from the cable while the Virus is connected; it releases the cable endpoint when
  the Virus is unplugged.

### Settings / UI

Phase 1: a small "TI Revive Settings" program (Start menu): checkbox "Use the Virus TI as Windows sound
device", choice of the cable recording endpoint (list from the MMDevice API, filtered by "CABLE" by default),
status (cable found, Virus connected, ring fill, drift ppm). Settings stored under HKCU\Software\TI Revive.

### Installer

The cable is **not bundled** (third-party licence, redistribution terms not verified). The settings program
detects whether a cable is installed and links to the vendor's download page. Uninstalling TI Revive removes
the Run entry and settings, not the cable.

### Recording direction (phase 3, optional)

Virus analog inputs (device channels 0/1, E082) -> broker -> render to a second cable's playback endpoint ->
Windows apps record from its recording endpoint. Needs a second cable (the free VB-CABLE has one; VB-Audio
sells A+B / C+D). Same clock adaptation in the other direction.

## Phases

1. **Playback bridge in the broker**: WASAPI capture of a configured endpoint, resampler + fill-level control,
   mixing into the OUT packets; settings from the registry; logging of fill level and drift. Testable in the
   VM for clock control and ring data (OUT audio itself only on the real PC, E083).
2. **Settings program + autostart switch**, installer integration (detection, link, cleanup).
3. **Recording direction** (second cable), if wanted.

## Open points / risks

- **VB-CABLE and memory integrity (HVCI)**: compatibility is not documented by the vendor; must be tested
  on the real Win11 PC with HVCI on before recommending it. If it fails, find an HVCI-compatible cable
  driver (the design is cable-agnostic: any WDM loopback pair works).
- Redistribution/licensing of the cable: users install it themselves; TI Revive only uses its endpoints
  through the public WASAPI API.
- Endpoint naming: Windows shows the cable's name; users can rename the endpoint ("Virus TI") in the Sound
  control panel. TI Revive could offer to do this (property store write, needs admin).
- Drift controller tuning: must stay stable over hours; logged values (ppm, fill) as in the plug-in diag.

## Sources

- VB-CABLE product page and shop (donationware, Windows XP-11, licensing terms for distributors in section
  3.4 of their terms, not verified): https://vb-audio.com/Cable/ , https://shop.vb-audio.com/en/win-apps/24-vb-cable-cd.html
- Alternatives overview (Virtual Audio Cable - paid; Synchronous Audio Router - routing driver):
  https://alternativeto.net/software/vb-audio-cable
