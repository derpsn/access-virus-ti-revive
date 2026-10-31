---
name: tirevive-test
description: Help a user test TI Revive on their own setup (Virus TI Snow, Polar, Keyboard, other DAWs, Intel Macs, other Linux distributions, Windows 10) safely, and write a compatibility report for a GitHub issue. Use when someone wants to test TI Revive, report what works on their hardware, or help with Snow support.
---

# Test TI Revive on a user's setup

Goal: a careful, honest test report from a setup we have not tested yet. Partial results are valuable - report each
stage as **pass**, **fail** or **not tested**. Never claim something you did not observe.

## Safety first (tell the user before you start)

- Never update the Virus firmware/OS, never send SysEx or USB requests that TI Revive itself does not send. No
  "let's see what happens" commands (AGENTS.md rule 2).
- Do not overwrite the user's stored patches or banks as part of a test unless they have a backup first (a bank
  dump or the Access/TI Revive backup); losing their own sounds is the real risk of a store, not the hardware.
- Never send the Multi parameter "Original Patch" (`72 00 03`): reported to set all 16 edit buffers to Init at
  the next power-up (issue #7, AGENTS.md rule 2).
- Use a **disposable DAW project**. Loading the plug-in sends its project state to the Virus' **edit buffers**
  (the currently loaded sounds); stored patches are not touched, and a power cycle of the Virus restores the
  previous sounds. Mention this, especially for models we have not tested (Virus TI Snow).
- Do not disable operating-system security features. Remove user names and paths from logs before posting.

## 1. Setup facts

Ask for and note: Virus model (TI / TI2 / Snow / Polar / Keyboard; Desktop or Keyboard) and its OS version (shown on
the Virus), computer (Windows version / Mac model + chip + macOS / Linux distribution + version), DAW + version, TI
Revive version, sample rate and buffer size, USB connection (direct / hub).

## 2. Stages (stop at the first failure and report it)

1. **USB detection (read-only).** Windows: run `scripts/tirevive-usbinfo.ps1` (right-click > Run with PowerShell)
   with the Virus connected and keep `TI-Revive-usbinfo.txt`. macOS: `ioreg -p IOUSB -l -w0 | grep -i -A30 virus`.
   Linux: `lsusb -d 133e: -v`. This step changes nothing on the Virus.
2. **Install** as the README describes for the platform (Access files from the user's own installer).
3. **Turn on the plug-in log:** create an empty file `diag.on` in the plug-in folder (Windows
   `%LOCALAPPDATA%\virus-ti-native`, macOS `~/Library/Application Support/virus-ti-native`, Linux
   `~/.local/share/virus-ti-native`).
4. **Plug-in loads and connects:** the DAW lists "TI Revive Virus Control", the editor opens, the LCD shows no
   "Virus disconnected", the first bank sync finishes (progress page).
5. **Sound:** notes from a MIDI track play, the Virus' sound comes back through the plug-in track (part output USB).
6. **Editing and browser:** turning knobs changes the sound, loading a patch from the browser works.
7. **Project recall:** save, close and reopen the project - the same sounds are on the Virus.
8. **Timing (optional):** record a clip of short 16th notes into an audio track; do they sit on the grid?
9. **Windows only - sound card:** ASIO device "TI Revive Virus Audio": playback through the Virus outputs.

## 3. Report

Open a GitHub issue with the template "Test report" (or paste this structure): setup facts, each stage with
pass / fail / not tested and a one-line observation, then attach `diag.log` (plug-in folder), the broker log
(Windows `%TEMP%\vtibroker.log`, macOS `/tmp/tirevive-vtibroker.log`, Linux `journalctl --user -u tirevive-broker -b`)
and for stage 1 the usbinfo output. Let the user read the report before it is posted. If you found something new
about the device, say so - a maintainer turns it into a `findings/evidence.md` entry.
