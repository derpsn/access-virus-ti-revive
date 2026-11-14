TI Revive for macOS - Access Virus TI Total Integration without the old Access drivers
=====================================================================================

Requirements
- macOS 14.4 or newer, Apple Silicon or Intel. No Rosetta needed.
- Your own Access "Virus TI Software Suite" installer (.pkg, e.g. 5.1.7.00). TI Revive contains no Access software;
  the .pkg is only unpacked, not installed.
- A DAW that loads VST3 plug-ins (tested: Ableton Live 12) or Audio Units (tested: GarageBand).

Install
1. Double-click "TI Revive.pkg" (not signed: if macOS refuses, right-click > Open, or System Settings > Privacy &
   Security > "Open Anyway").
2. Put your Access "Virus TI Software Suite ... .pkg" into this folder and double-click
   "Set up Access Virus TI files.command". It unpacks the .pkg and copies only what the plug-in reads (skin and
   parameter descriptions, ROM banks), the manuals and the factory patch libraries. If drivers of an earlier Access
   installation would block the Virus, it offers to park them, and it offers to remove the original Access plug-ins
   of earlier TI Revive versions (Intel-only, no longer used).
3. Connect the Virus TI by USB. Start your DAW and allow it to use the microphone when macOS asks
   (System Settings > Privacy & Security > Microphone): macOS only gives the Virus' audio to apps with that
   permission; without it the plug-in stays silent.
4. Rescan plug-ins and use "TI Revive Virus Control" (vendor "TI Revive"): VST3 in Ableton, Cubase, Bitwig, REAPER,
   ...; the Audio Unit in Logic Pro and GarageBand. It looks and works like the original Virus Control and runs
   natively. Projects saved with "Virus TI (TI Revive)" of earlier versions (VST3 and Audio Unit) open with it.

How it works
"TI Revive Virus Control" reads the skin, the parameter descriptions and the ROM banks from the files set up in step
2 and talks to the Virus through TI Revive's broker (vtibroker, started on demand by the plug-in, ends 30 s after the
last use). Audio comes from macOS' built-in USB audio driver (the "Virus TI" audio device, 44.1 kHz).

Backups
"TI Revive Control Center" in Applications (also while your DAW runs; it takes the look of Access' Control Center
from your Access .pkg, set up in step 2) saves the Virus' RAM banks as patch libraries
in Documents/Access Music/Virus TI/Backups and converts libraries (.mid <-> .syx). In Terminal the same:
"/Library/Application Support/TI Revive/tirevive-cc" backup (also "info", "list", "export"). It only reads from the
Virus. Firmware/OS updates are not supported.

Uninstall
Double-click "Uninstall TI Revive.command" (also in /Library/Application Support/TI Revive). The Virus TI files and
your patch libraries stay.

Logs (for bug reports): the Control Center's Nerd mode > "Save logs...", or diag.log in ~/Library/Application
Support/virus-ti-native (on by default since 0.8.41) and /tmp/tirevive-vtibroker.log.

Not affiliated with Access Music or Kemper. https://github.com/derpsn/access-virus-ti-revive
