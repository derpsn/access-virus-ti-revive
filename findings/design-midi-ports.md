# Design proposal: Windows MIDI ports for the Virus TI

Status: option A being implemented (2026-11-15, round 0.8.42), read-only, see "Implementation (0.8.42)" at the end.
First proposal 2026-10-11 (plan item 3 in the old HANDOFF, now `log/until-2026-10-12.md`).

## Goal

Use the Virus as a USB MIDI instrument without the Virus Control plug-in, as with the Access driver package:
any MIDI program (a DAW's MIDI tracks, sequencers, editors/librarians, MIDI monitors) sees MIDI ports and can
send notes, CCs and SysEx to the synth and receive its knobs/keys.

## What the original offered

The Access package's `vtimidi.sys` exposed two KS MIDI ports on the vendor interface MI_03 (E006, E007):

| Port | USB-MIDI cable on MI_03 (EP 0x05 out / 0x85 in) | Meaning |
|---|---|---|
| "Virus TI MIDI" | 0 | the Virus' MIDI In/Out DIN sockets (the Virus as a MIDI interface) |
| "Virus TI Synth" | 1 | the synth engine (notes, CCs, SysEx, the Virus' own keys/knobs) |

Linux' snd-usb-audio exposes the same two ports (E016). TI Revive already owns MI_03 in the broker
(handshake, keeps EP 0x85 drained, E042), so the data path to the synth exists; what is missing is a MIDI
port that Windows programs can open. The plug-in channel (MI_04) is separate, so ports and plug-in can be
used at the same time, as before.

## Constraint

A classic Windows MIDI port (WinMM `midiOut*`/`midiIn*`) is provided by a kernel MIDI driver - the reason the
old ports disappeared. TI Revive installs no kernel code.

## Options

| | How | Windows | For | Against |
|---|---|---|---|---|
| **A: Windows MIDI Services virtual devices** (proposed) | The broker creates two app-to-app MIDI devices with the Windows MIDI Services SDK and relays their traffic to MI_03 cables 0/1. Windows' MIDI service makes them visible to MIDI programs; WinMM/WinRT MIDI 1.0 APIs are "repointed to the new Windows Service" (Microsoft). | Windows 11 with Windows MIDI Services in-box ("supported Windows 11 releases (currently 25H2 and later)") | No driver at all; Microsoft's supported way; multi-client. | Not on Windows 10 / older Win11; ports exist only while the broker runs (virtual devices live as long as the app that offers them); new SDK, known early issues with the virtual device sample. |
| B: third-party virtual MIDI driver | loopMIDI / teVirtualMIDI (Tobias Erichsen): the user installs it, the broker opens two loopback ports. | Windows 10 and 11 | Works on older Windows. | Third-party kernel driver (HVCI status to check), licensing of the SDK for redistribution, two-sided ports (confusing names). |
| C: MIDI-forwarding VST3 | A small extra VST3 instrument "Virus TI MIDI (TI Revive)" that just forwards the track's MIDI to the synth through the broker (no Virus Control). | Windows 10 and 11 | Simple, no Windows ports needed, works in any VST3 host. | DAW only (no editors/librarians, no MIDI from the Virus to other apps). |

Recommendation: **A** as the main path (forward-looking, no drivers), **C** as an optional quick win for DAW
users on Windows 10 / older Windows 11. B only if users ask for it.

## Proposal A in detail

- **Broker**: on start, if Windows MIDI Services is available, create two virtual devices "Virus TI MIDI" and
  "Virus TI Synth" (MIDI 1.0 protocol, one group each). Port -> device: pack to USB-MIDI packets on cable 0/1
  (`vti_pack_sysex` and CIN mapping exist in vticore_midi.c), write to EP 0x05. Device -> port: split the
  EP 0x85 packets by cable (today they all go to the broker's clients) and send cable 0/1 messages to the
  matching virtual device. SysEx in both directions (reassembly per cable exists: `vti_unpack`).
- **Coexistence with the plug-in**: the plug-in uses MI_04 (cables 2/3) for its sync; MI_03 cable 1 also
  carries the plug-in-relevant handshake reply and hardware CCs (E023). Rule: everything on EP 0x85 keeps going
  to the plug-in clients as now, and is additionally offered on the ports; port output is written on cables 0/1
  of EP 0x05 only. Check: the Virus' global "MIDI Data cable" routing (E039: plug-in sets 3B = 1) - with the
  plug-in closed, the synth's defaults decide which cable carries its data.
- **Lifetime**: ports exist only while the broker runs. With the on-demand broker (0.3.1) nothing would start
  it for a MIDI program. So, like the WDM bridge: opt-in setting "Provide Windows MIDI ports" -> broker starts
  at logon (HKCU Run) and stays running while enabled; default off. Alternatively start the broker when the
  Virus is plugged in (needs a trigger: a scheduled task on the device-arrival event) - to evaluate.
- **Implementation language**: the SDK is WinRT (C++/WinRT or C#). The broker is plain C (mingw). Options: a
  small separate helper process in C++/WinRT (built with MSVC in CI) talking to the broker over the existing
  pipe protocol (it is just another client), or WinRT from C via the ABI headers. The helper process keeps the
  broker small and avoids WinRT in it - preferred.
- **Runtime**: whether end users need the Windows MIDI Services SDK runtime installed separately must be
  verified (the service is in-box on Win11 25H2+; the app SDK may be a separate install). If so, the TI Revive
  installer could detect it and link to it.

## Test plan

- Win11 VM must be on a build with Windows MIDI Services (25H2+); the current Win11 test VM is build 26300
  (check `midisrv` service presence). The Win10 VM cannot test option A.
- MIDI monitor (e.g. MIDI-OX via WinMM, the MIDI Services console `midi.exe`): notes into "Virus TI Synth" ->
  synth plays; Virus keys/knobs -> events on "Virus TI Synth" in; SysEx dump request/answer; "Virus TI MIDI" ->
  DIN out (needs a device on the DIN port, else only loopback checks).
- Plug-in open at the same time: no interference with sync (diag sync reports as in E081).

## Open points

- Exact virtual device API and SDK runtime requirement (Windows MIDI Services SDK reference and samples).
- Which Windows 11 version the user's PC runs (`winver`): option A needs Windows MIDI Services there.
- Cable mapping 0/1 with the plug-in closed (capture with a MIDI monitor).

## Sources

- Windows MIDI Services project page (in-box on "supported Windows 11 releases (currently 25H2 and later)",
  built-in virtual / app-to-app MIDI, WinMM/WinRT MIDI 1.0 APIs repointed to the service):
  https://microsoft.github.io/MIDI/
- SDK overview (C#/WinRT or C++/WinRT projections, NuGet `Windows.Devices.Midi2`): https://microsoft.github.io/MIDI/sdk-overview/
- Microsoft dev blog, Spring 2023 update (app-to-app MIDI without an additional driver): https://devblogs.microsoft.com/windows-music-dev/the-new-windows-midi-services-spring-2023-update/
- Release notes (virtual device sample issue): https://github.com/microsoft/MIDI/releases

## Implementation (0.8.42)

Decided with the user 2026-11-15: installed only where Windows supports it; only Windows' own copy of the API (no
SDK runtime or DLL shipped by us, so the ports come with the Windows update that brings the API); read-only, as on
macOS and Linux (DAW to Virus would be a separate decision under rule 2).

- `src/midiports-win/tirevive-midiports.exe` (MSVC, C++/WinRT, target `tirevive-midiports`): checks the build
  (>= 26200, 25H2), legacy MIDI mode (`HKLM\...\Drivers32\UseLegacyMidi` = 1), the API's activation and
  `IsSystemProvided()`, and `EnsureServiceAvailable()`; then one virtual device "Virus TI" (ProductInstanceId
  `TIRevive-VirusTI`) with two output function blocks, "Virus TI Synth" (group 1) and "Virus TI MIDI" (group 2),
  one group each, so Windows should name the MIDI 1.0 ports after the blocks (microsoft/MIDI docs, "how MIDI 1.0
  port names are generated": not checked on a PC yet). Log: `%TEMP%\tirevive-midiports.log`.
- Broker (`vtikeyport_win.c`): starts the helper when it lies next to `vtibroker.exe`, feeds it the frames of
  `midifeed.h` on a non-blocking stdin pipe; the helper ends when the pipe closes, so the ports live as long as the
  broker (as on macOS and Linux). `umpconv.h` turns the bytes into UMP (unit-tested).
- MSI (`native/packaging/windows/native.wxs`): the helper's component only when CurrentBuildNumber >= 26200,
  `System32\Windows.Devices.Midi2.dll` exists and UseLegacyMidi is not 1.
- CI: the .winmd of microsoft/MIDI's API-lock package (In-box Preview 11, `0.99.92-preview.11`) and cppwinrt.exe
  (Microsoft.Windows.CppWinRT 2.0.250303.1), both pinned by SHA-256, only to compile.
- Known from the microsoft/MIDI repo (2026-11-14): the API comes in-box with the end-of-November 2026 update for
  25H2 (up to 45 days to reach every PC); virtual devices could hang the service at teardown before that update
  (#1047, #1236); WinMM apps fail to open ports after a `midisrv` restart until they restart (#1037, open).
