TI Revive for macOS, libusb package - for Macs where Apple's USB audio driver crackles (macOS 10.15 Catalina)
==========================================================================================================

What is different from the normal package
In this package TI Revive's service takes the Virus' audio itself over USB (libusb), as on Linux, instead of Apple's
USB audio driver. On Catalina, Apple's driver delivers the Virus' audio with slips and crackle; this way it does not
(first tested by the reporter of issue #37). The service runs with administrator rights (macOS lets only those take
a USB device from its own driver) and keeps the Virus while the Mac runs. The Virus is then no audio device in macOS:
choose another audio output in your DAW (e.g. the Mac's built-in output or your audio interface).
Not tested by us on Catalina: your report on GitHub (with the logs below) is what tells us it works.

Requirements
- An Intel Mac with macOS 10.15 or newer. On macOS 14.4 and newer use the normal package unless Apple's driver
  crackles there too.
- Your own Access "Virus TI Software Suite" installer (.pkg, e.g. 5.1.7.00). TI Revive contains no Access software;
  the .pkg is only unpacked, not installed.
- A DAW that loads VST3 plug-ins or Audio Units (Logic Pro, GarageBand, Ableton Live, ...).

Install
1. Quit your DAW and the TI Revive Control Center.
2. Double-click "TI Revive.pkg" (not signed: if macOS refuses, right-click > Open, or System Settings > Security &
   Privacy > "Open Anyway"). It installs TI Revive and its service; no Terminal commands are needed.
3. Put your Access "Virus TI Software Suite ... .pkg" into this folder and double-click
   "Set up Access Virus TI files.command" (as in the normal package).
4. Connect the Virus TI by USB, start your DAW with an audio output that is NOT the Virus, and use
   "TI Revive" (VST3, or the Audio Unit in Logic Pro and GarageBand).

Back to the normal package
Install the normal TI Revive package over this one: it removes the service. Then unplug the Virus' USB cable and plug
it in again, so macOS' own driver takes it.

Uninstall
Double-click "Uninstall TI Revive.command" (also in /Library/Application Support/TI Revive), then unplug the Virus'
USB cable and plug it in again.

Logs (for bug reports): the Control Center's Nerd mode > "Save logs...", or ~/Library/Application Support/
virus-ti-native/diag.log and /Library/Logs/TI Revive/vtibroker.log.

Not affiliated with Access Music or Kemper. https://github.com/derpsn/ti-revive
