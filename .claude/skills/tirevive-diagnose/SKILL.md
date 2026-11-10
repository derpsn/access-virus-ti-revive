---
name: tirevive-diagnose
description: Diagnose a TI Revive user report (Virus TI not found, not connected, silent, crackling, wrong model, sync errors) from its logs. Use when someone attaches TI Revive logs, a usbinfo file or describes a Virus TI problem on Windows, macOS or Linux.
---

# Diagnose a TI Revive report

Goal: find the cause from evidence, name it, and give the user concrete steps. Do not guess beyond the logs; ask for
the missing log instead.

## 1. Collect

Note platform, OS version, Virus model (TI / TI2 / Snow / Polar / Keyboard), DAW (native or Rosetta on macOS),
TI Revive version. Then make sure you have the logs (CONTRIBUTING.md lists where they are):

- **All platforms (0.8.0+, plug-in "TI Revive Virus Control"):** the plug-in's `diag.log` (on by default since 0.8.41;
  with 0.8.40 or older only after creating `diag.on` in its folder, see CONTRIBUTING.md; a report header "Plug-in log
  OFF" means it was switched off) - lifecycle, state sent, device connect, sync, timing stats.
- **Windows:** `TI-Revive-usbinfo.txt` from `scripts/tirevive-usbinfo.ps1`, (0.7.x: the newest `%TEMP%\vtiemu-*.log`),
  `%TEMP%\vtibroker.log`.
- **macOS:** `/tmp/tirevive-bridge.log`, `/tmp/tirevive-helper.log`, `/tmp/tirevive-libvirususb.log`,
  `/tmp/tirevive-vtibroker.log`; for connection problems also `ioreg -p IOUSB -l -w0` (Virus section) and
  `ls "/Library/Audio/MIDI Drivers" "/Library/Audio/Plug-Ins/HAL"`.
- **Linux:** `journalctl --user -u tirevive-broker -b` (0.7.x: `vtiemu-*.log` in the Wine prefix's Temp folder).

## 2. Match against known causes

Search `findings/evidence.md` and `findings/HANDOFF.md` for the exact error text first. The most common ones:

| Symptom / log line | Cause | Evidence | What the user does |
|---|---|---|---|
| macOS broker `claim interface 3: LIBUSB_ERROR_ACCESS`, ioreg `UsbExclusiveOwner = MIDIServer` | another MIDI driver (old Access driver from a full install, or AURA Plugins' Virus Editor USB MIDI driver) holds the Virus | E134, E137, README "One driver at a time" | disable the other driver (System Settings → General → Login Items & Extensions → Driver Extensions, or its uninstaller) and restart; for plug-in style drivers move `Virus TI MIDI Driver.plugin` / `AURA Plugins Virus TI USB Midi.plugin` out of `/Library/Audio/MIDI Drivers` (and `de.access-music.virus_ti.*` from `/Library/Audio/Plug-Ins/HAL`), or run "Install Access Virus TI software.command" of 0.7.4+ which parks them |
| Windows usbinfo: composite device on service `VIRUSUSB` ("Virus TI USB 2.9.66"), MI_00/MI_04 missing | the old Access kernel driver took the Virus back (Access software installed or repaired after TI Revive) | E137, E138 | `pnputil /delete-driver <oemNN.inf> /uninstall /force` (the INF named in the usbinfo file), replug, reinstall TI Revive |
| Windows: Device Manager **Code 39**, CodeIntegrity 3076/3077 | the old cross-signed Access driver cannot load on Windows 11 | README "Why" | expected; TI Revive's own WinUSB binding replaces it |
| connected, plug-in OK, but **silent** | part output not set to USB on the Virus (another editor changed it); macOS: DAW lacks microphone permission | E135, E103 | switch the part output to "Out 1" and back to "USB 1"; macOS: allow the microphone for the DAW |
| a **Snow** is not found, or found as a TI and syncs past its bank list | model detection by the USB strings | E115, E188 | update to the latest release: the native plug-in works with the Snow per user reports (0.8.3+, USB 3 and soft knobs with 0.8.20); on Windows the 0.7.4 "Virus TI Snow (TI Revive)" plug-in stays as a fallback; if still not found, ask for the usbinfo file with the Snow connected |
| macOS DAW blocklists the plug-in / "Unsupported Architecture" | the Intel-only original plug-in was scanned, or the DAW runs under Rosetta with an old bridge | E129, E130, E132 | TI Revive 0.7.3+ (universal bridge); the original is moved out of the DAW folders by the install script |
| "A sync error occurred. Press Stop and Play to clear." once after Play | the Virus' timing report at the start | E072, E125 | Stop and Play; start playback slightly before the first note |
| 0.7.x (original plug-in): "communication problems" / "buffer size too high" | the original Access plug-in accepts only 64/128/256/512-sample blocks | E111, E123 | set the buffer to 512 or less; better: update to the native plug-in (0.8.0+) |
| new project shows -Init- but the Virus plays the previous patch | (fixed in 0.8.0) the Virus dropped the part dump after the multi dump | E145 | update to the latest release |
| plug-in sounds pitched up / garbled at 48 kHz | (fixed in 0.8.0) no sample-rate conversion in the test builds | native/docs/architecture.md | update to the latest release |
| notes late or missing right after loading / during the first sync | the Virus delays notes while it processes SysEx; since 0.8.0 the plug-in holds notes ~4 s and pauses the sync for notes | E144 | wait a few seconds after loading; update to the latest release |
| no sound after a USB cable replug, or after switching the Virus on while no DAW runs | known issue, being worked on | README "Known issue" (Linux section) | switch the Virus off and on once while the DAW runs |
| silent or out of sync after the computer slept (broker "stalled ... ms") | the USB stream stalls during sleep; round 0.8.24 re-sends the start-up messages and state after a stall | E210, E211 | ask for a diag.log that covers the sleep; fixed in 0.8.24 |
| Virus audio suddenly silent, MIDI still answers | parameter flood hung the Virus' audio | HANDOFF / automation limit | power-cycle the Virus; reduce dense automation |

## 3. If nothing matches

- Quote the first unusual log line exactly and the lines just before it.
- Compare model strings: `bcdDevice` 0x0100 = TI or Snow (string decides), 0x0300 = TI2.
- Check whether TI Revive's own components are the ones loaded (paths in the helper/bridge log).
- Ask for exactly the missing information (one log, one command), not for everything.

## 4. Answer

Short and friendly: the cause (with the log line that shows it), the steps, and what to send back if it still
fails. Reference the evidence entry for maintainers. End with a short note that the answer was written with AI
assistance. If you found something new, propose an `findings/evidence.md` entry (see the `tirevive-pr` skill).
