# Plan: TI Revive on Linux (user request 2026-10-12)

Target: this PC (Ubuntu 26.04, kernel 7.0, PipeWire), the Virus TI on USB, a native Linux DAW. Same principle as on
Windows and macOS: the user's own, unmodified Access software, no old drivers, no kernel code.

## Starting point (checked 2026-10-12, E113)

- The Access plug-ins exist only for Windows and macOS. On Linux, Windows VST3 plug-ins run in native DAWs through
  **yabridge** (Wine). Our **Windows** wrapper plug-in already emulates the old driver completely (vtiemu, E043-E058),
  so it is the natural thing to run under Wine.
- Linux's `snd-usb-audio` knows the Virus (quirk) and binds **all five** interfaces, including the vendor
  interfaces 3/4 that TI Revive needs. Taking interfaces 3/4 away with libusb (as the macOS broker does) makes
  `snd-usb-audio` shut the whole card down: the Virus disappears as a Linux sound card until it is replugged
  (tested). Sharing like on macOS is therefore not possible without kernel configuration.
- So Linux follows the **Windows** design: the broker owns the whole device while TI Revive is in use - MIDI on
  interfaces 3/4 (code exists: vticore.c on libusb, E037) and the audio function on interfaces 0-2, streamed
  isochronously by the broker itself (port of the Windows WinUSB streaming, vtiusbaudio.c: synth = clock master,
  frame counter never slips, 44.1 kHz; since round 0.8.32 also 48 kHz, design-rate.md) into the shared audio ring
  (vtiaudioshm.h). When the broker exits it hands
  the device back to `snd-usb-audio`, so the Virus is a normal Linux sound card whenever TI Revive is not in use.

```
 Linux DAW (REAPER / Bitwig / Ardour, native)
   └─ yabridge ── Wine: "Virus TI (TI Revive).vst3" (our Windows wrapper) ── original Virus TI.vst3 / Virus Control.dll
                       │ vtiemu: same driver emulation as on Windows, new backend vticore_sock.c:
                       │   control + MIDI over a Unix socket (Wine AF_UNIX), audio ring mapped from /dev/shm,
                       │   block notifications over the socket -> local event
                       ▼
                 vtibroker (native Linux, libusb): whole Virus TI - MIDI (if 3/4) + isochronous audio (if 0-2)
```

## Phases

| | What | Needs |
|---|---|---|
| L1 | Linux broker owns the whole device: detach `snd-usb-audio`, isochronous audio via libusb (port of vtiusbaudio.c) into `/dev/shm/tirevive-audio-<uid>` (vtiaudioshm.h layout), give the device back on exit | nothing (testable here now) |
| L2 | Wine-side backend `vticore_sock.c` for vtiemu: Unix socket to the broker (Wine supports AF_UNIX; TCP on localhost as fallback), audio ring via a file mapping of the /dev/shm file (`Z:\dev\shm\...`), notifications -> event | mingw build (exists) |
| L3 | Lab: Wine, yabridge, a DAW (REAPER has a native Linux version), Access Windows software in a Wine prefix (MSI unpacked, no drivers, no updater) | **user**: `sudo apt install wine` (and approval for yabridge/REAPER downloads) |
| L4 | Tests in the DAW: load, connect, sync, MIDI, audio, timing (16ths clip) | user at the PC |
| L5 | Packaging: `TI-Revive-<v>-linux.tar.gz` with install script (broker, udev rule, Wine prefix setup, yabridge sync), CI on tags, README | |

## Status (2026-10-12)

- L1 done (E114, E116): broker audio on libusb, hand-back to snd-usb-audio in all cases.
- L2 done: vticore_sock.c; Wine 10 (Ubuntu) has no AF_UNIX -> token-protected TCP on 127.0.0.1 (E117).
- L3 largely done WITHOUT root (E117/E118): lab Wine from the Ubuntu packages via bubblewrap, Access software from
  the patched MSI, virtual device enumeration; test host vtihost.exe under Wine: memory sync, notes and the Virus'
  audio through the plug-in. Missing: yabridge + a real DAW (user).
- L5 drafted: src/installer-linux (install.sh, uninstall.sh, systemd user socket, udev uaccess rule, vtimsipatch.exe),
  scripts/linux-build-release.sh -> TI-Revive-<v>-linux.tar.gz. Not yet in CI.

## Risks (to settle in this order)

1. Does the Access Windows plug-in (VSTGUI, GDI) run under Wine/yabridge? -> first thing in L3.
2. Wine's AF_UNIX support in the installed version (else TCP on 127.0.0.1 with a token).
3. Shared memory between Wine and a native process through a file mapping of a /dev/shm file (expected to work:
   Wine maps files with mmap(MAP_SHARED)).
4. Audio timing under yabridge (extra process hop) - measured with the 16ths clip as on Windows/Mac.
