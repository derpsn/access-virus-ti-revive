TI Revive @VERSION@ for Linux
=========================

Brings back the Access Virus TI's "Total Integration" on Linux, without the old Access drivers: the plug-in
"TI Revive Virus Control" (a native Linux VST3, no Wine, no yabridge) looks and works like the original Virus
Control, and the TI Revive broker (a small native Linux program) talks to the Virus over USB - MIDI, patch sync and
the Virus' USB audio.

You need
- an Access Virus TI on USB (tested: TI2 Desktop; the Virus TI Snow is not supported yet)
- a Linux DAW that hosts VST3 plug-ins (REAPER, Bitwig Studio, Ardour, ...), x86_64, Ubuntu 22.04 / Debian 12 or newer
- your own copy of the Access installer "Virus TI Software Suite 5.1.7.00 64-bit.msi" (not included): the plug-in
  reads the Virus skin, the parameter descriptions and the ROM banks from it. To unpack it, install msitools
  ("sudo apt install msitools") or Wine. The Access software itself is NOT installed.
- For stable real-time audio, let your user use real-time priority (Ubuntu: sudo usermod -aG pipewire $USER, then
  log out and in again).

Install
1. Unpack this folder and open a terminal in it.
2. ./install.sh "/path/to/Virus TI Software Suite 5.1.7.00 64-bit.msi"
   (it asks for your password once to allow USB access to the Virus; the Access files go to
   ~/.local/share/tirevive/access, the factory patch libraries to ~/Documents/Access Music/Virus TI/Patches, or the localized documents folder, e.g. ~/Dokumente)
3. In your DAW, rescan plug-ins and load the VST3 "TI Revive Virus Control" (vendor TI Revive).
   The first start reads the Virus' RAM banks, which takes a few minutes; you can play meanwhile (the sync pauses
   while notes arrive). Later starts use the cache and are ready after a few seconds.

Coming from TI Revive 0.7
- install.sh removes the classic plug-in "Virus TI (TI Revive)" (Wine + yabridge). Projects saved with it open with
  "TI Revive Virus Control" (same plug-in ID); the Access files are taken from your Wine prefix.

Good to know
- Any sample rate works (the Virus runs at 44.1 kHz; the plug-in converts). JACK through PipeWire works well
  ("pw-jack reaper"); avoid PulseAudio as the DAW's audio system.
- The plug-in reports its latency to the DAW, so recorded and played notes line up with the grid.
- Notes and knobs work but no audio over USB (log: "usbaudio: alternate setting 1 failed", kernel log:
  "Not enough bandwidth"): connect the Virus directly to the computer, not through a USB hub.
- REAPER: stay in REAPER while a render runs. If another program has the focus when the render window closes,
  REAPER 7.82 can crash or freeze. This is a bug in REAPER itself (it happens with ReaSynth too, without TI Revive).
- While TI Revive uses the Virus, Linux cannot use it as a normal sound card; it is handed back automatically when
  the plug-in is closed (about 30 seconds later).
- One DAW at a time uses the Virus; the plug-in in a second one shows "Virus unavailable". The Control Center works
  next to the DAW.
- Backups of the Virus' RAM banks (read-only): "TI Revive Control Center" in the
  application menu (needs gtkmm 3: Ubuntu 24.04+ libgtkmm-3.0-1t64, older libgtkmm-3.0-1v5, Fedora gtkmm3.0,
  Arch/CachyOS gtkmm3; without it the window closes at once), or "tirevive-cc backup" in a terminal (also
  "info", "list", "export"; both in ~/.local/bin). Firmware/OS updates are not supported.
- The Control Center shows its plain look instead of the Access look: install.sh of 0.8.31 and older could miss
  "Virus Control Center.exe" in the Access .msi (issue #15). Run ./install.sh "/path/to/...64-bit.msi" again; it adds
  the missing file and changes nothing else.
- Uninstall: ./uninstall.sh (your patch libraries in ~/Documents stay)

Logs: broker "journalctl --user -u tirevive-broker"; plug-in: create ~/.local/share/virus-ti-native/diag.on, then
see diag.log in the same folder.
TI Revive is not affiliated with Access Music or Kemper. MIT license, https://github.com/derpsn/access-virus-ti-revive
