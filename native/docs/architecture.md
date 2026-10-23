# How the native Virus TI plug-in works

This is the technical overview for anyone who wants to understand the plug-in. Byte layouts and protocol details are in
[protocol.md](protocol.md) and [formats.md](formats.md); progress and measurements are in [status.md](status.md).

## 1. The big picture

```
 DAW (REAPER, Ableton, ...)
  │  VST3: notes, automation, audio buffers, project state
  ▼
 TI Revive Virus Control.vst3 ──────────────────────────────────────────────────────────────┐
  │ plugin/  VirusPlugin (VST3 component + controller)   VirusEditor (VSTGUI)       │
  │ src/model    RemoteDevice: parameters, MIDI/SysEx encoding, dumps  ◄── remotedevice.xml, editordevice.xml,
  │ src/editor   EditorLayout + EditorRenderer: the original UI         ◄── editor.xml + PNG skin   automation.xml
  │ src/access   Access files, firmware ROM banks, Personal Libraries   ◄── user's Access installation
  │ src/device   Broker client, audio ring, memory sync                                 │
  └────────────────────────────┬────────────────────────────────────────────────────┘
                               │ broker protocol 3 (Unix socket / named pipe) + shared-memory audio ring
                               ▼
 TI Revive broker (src/broker) ── USB (libusb / WinUSB / IOKit) ──► Virus TI / TI2 / Snow
```

The plug-in never talks to USB directly. The **TI Revive broker** (`src/broker/` of this repository, the same one the
classic TI Revive plug-in used) owns the Virus. It frames USB-MIDI, carries the 6-channel USB audio, and lets one plug-in session at a time use the device.

### No Access material in this repository

Access/Kemper have not open-sourced the Virus TI software. So the plug-in ships **none** of it: no graphics, no XML,
no firmware, no decompiled code. At runtime it loads everything from the files of the user's own Access installer:
TI Revive's installers only **unpack** the Access installer (`.msi` on Windows and Linux, `.pkg` on macOS) and copy
`Virus Control.dll` / `Virus Control.bundle`, `firmware.bin`, the manuals and the factory patch libraries - nothing of
Access is installed or registered. These are the files it loads:

| loaded at runtime | from | used for |
|---|---|---|
| `remotedevice.xml`, `editordevice.xml` | Virus Control DLL / bundle | parameters, value lists, conditions, SysEx layouts |
| `editor.xml` + ~465 PNGs | same | the complete UI: positions, bitmaps, fonts, conditions |
| `automation.xml` | same | VST parameter IDs, so projects made with the original open unchanged |
| `firmware.bin` | Access `Common` folder | ROM banks A–Z (TI, TI2 or Snow image) |
| init patch / init multi / default page B | inside the DLL | new instances, Virus C patch upgrade |
| `*.mid` patch files | `Documents/Access Music/Virus TI/Patches` (with subfolders) | Personal Libraries |

## 2. Components

### Device model: `src/model/remotedevice.*`
- Parses `remotedevice.xml`. There are 587 parameters, each with range, default, value list, part scope and MIDI
  message (CC, poly pressure, parameter SysEx, bank dumps).
- **encode / decode:** parameter value ↔ MIDI bytes. Decoding goes the other way: incoming CC/SysEx from the Virus
  becomes parameter changes.
- **Single dumps:** 524 bytes, decoded to and updated from parameter values. Checksums sit at bytes 265 and 522.
- **Upgrader:** brings older dumps (format versions 1–12, Virus C 267-byte dumps) to the current layout, ported
  from the original.
- **`init_messages()`:** the original's start-up sequence (global init, parameter init), byte-identical to it.
- **Automation:** `automation.xml` maps (parameter, part) to a VST parameter ID.

### UI: `src/editor/*` and `plugin/source/viruseditor.*`
- `EditorLayout` reads `editor.xml` into a tree of containers and controls. `Conditions` evaluates show/hide
  conditions; a few of them the original computes in code (for example "Category Filter Selected").
- `EditorRenderer` draws the tree on an abstract `Canvas`:
  - filmstrip knobs, radio buttons and nine-part tiles;
  - bitmap LCD fonts;
  - envelope, filter and oscillator displays;
  - the patch browser and the MIDI activity meters.
- The same renderer runs in the plug-in (VSTGUI canvas) and in `tools/vtirender` (cairo, for pixel comparisons
  against screenshots of the original).
- Repaints are partial: a knob drag repaints only its controls and the LCD, and the meters repaint only the
  channels that changed. Every full repaint records which parameters each element reads (`EditorRenderer::Deps`);
  automation and knob moves on the Virus then repaint only those elements. A parameter that decides what is shown
  (a show/hide condition, the panel part) still repaints everything. `VTI_NATIVE_NO_PARTIAL=1` switches this off.
- The displays (envelope, filter, oscillators, arpeggiator) are clipped to their element, like VSTGUI views.
- The static layer (backgrounds, panels, labels) of the last 8 page states is cached; the skin bitmaps are decoded
  (and on Linux uploaded as X pixmaps) on idle ticks after the editor opens, so a page switch costs ~10 ms.
- Scaled displays and editor sizes (branch `skins`, not released): the skin bitmaps are 1x artwork. When the editor
  is drawn at more than one device pixel per editor pixel, each image gets a copy enlarged to that scale
  (`vti::upscale`, Lanczos-3; filmstrips frame by frame through `Canvas::frames`) the first time it is drawn, in
  memory only; VSTGUI picks the copy by scale factor. The static layer is rendered at device resolution. Cost: a
  page's first visit at zoom 2 ~150-380 ms instead of ~15-40 ms (the 129x16512 knob strips take ~185 ms each), later
  repaints as before. `VTI_NATIVE_NO_HIDPI=1` draws the 1x images.

### Plug-in: `plugin/source/virusplugin.*`
- **VST3 `SingleComponentEffect`:** stereo out, MIDI in, 224 automatable parameters with the original's IDs and an
  identical project-state format. The state is a "MIDI" container holding the multi, 16 part dumps, globals and CCs.
- **Real-time path:** notes and automation are stamped on a sample timeline. A MIDI clock and song-position markers
  are sent alongside, and a worker thread hands everything to the broker.
  - `process()` never allocates or locks: events go into a fixed buffer (256, notes always keep 100+ slots so no
    note-off is lost), are ordered by an insertion sort (stable: automation before notes at the same stamp) and
    pass a lock-free ring to the worker, which `process()` wakes once per block.
  - Automation is rate limited (`src/device/automation.h`): coalesced per parameter, at most 250 messages/s,
    oldest parameter first. A flood of parameter messages hangs the Virus' audio until a power cycle; with the
    limit, three LFOs on every block still give each parameter ~80 updates/s.
  - All stamped traffic (notes, controllers, clock, with their F5 stamps) is bounded by a byte budget
    (`src/device/sendbudget.h`: 4 kB/s sustained, 1000 bytes at once; `VTI_NATIVE_MIDI_RATE` overrides, 0 = off):
    the Virus' DSP parks stamped bytes in a 2046-byte queue without a fill check until they are due (E235/E238),
    so a flood larger than that within the lead window corrupts its MIDI stream. Musical rates never come near
    the budget; a runaway host is delayed a few blocks, and stamps are absolute, so timing is unchanged.
  - Active Sensing (`src/device/keepalive.h`): one FE every 300 ms on the timeline, like the original. The DSP arms
    a watchdog with the first FE and releases every voice about a second after the last one (E237), so FE goes out
    regularly or not at all - never once.
  - Timing (sample accurate, the same latency in every session): the Virus plays a time-stamped event when its own
    14-bit stamp clock reaches the stamp. After the start-up sync ping it marks where that clock is 0 with a single
    sample on USB 2; the plug-in finds it and converts every stamp, so an event sounds a fixed `lead` after its
    timeline position. The audio is read at the same fixed offset and reported to the host as the plug-in latency
    (75 ms at 512 samples), so the DAW's delay compensation lines it up. Details: protocol.md, "the stamp clock".
  - The worker writes a block's events in one broker message, and keeps the project state current off the audio
    thread (latest value per parameter, every 5 ms; a field index updates one value of a 524-byte part dump in
    ~0.4 us).
- **Audio:** read from the broker's shared-memory ring at the stamp-aligned position, with latency compensation. The
  Virus runs at 44.1 or 48 kHz, chosen from the host rate like the original (round 0.8.32, `src/device/virusrate.h`,
  `findings/design-rate.md`; not yet tested on hardware): the first audio start and every `setActive(true)` ask for
  it (the broker on Linux/Windows, the Core Audio device on macOS), a switch re-pings and re-anchors, and the host
  gets `kLatencyChanged`. At other host rates a 32-tap Kaiser windowed-sinc resampler (`src/device/resampler.h`,
  -90 dB) reads from a smoothly steered fractional position; one resampler and one sidechain converter per Virus rate
  are built in `setupProcessing`, so `process()` only picks one when the rate changes. The timeline carries the
  fractional rest per block, so nothing drifts.
- **Start-up:** the start-up messages (GlobalInit/ParamInit from `remotedevice.xml`) and the project state go out
  first; note-ons are held back for these ~4 s (the Virus plays notes up to 1.6 s late while it works through
  SysEx, TI Revive E144), and queued messages pause 250 ms after a multi dump (the Virus drops the next message
  otherwise, E145).
- **Bank sync pauses for notes:** while notes arrive no dump request is sent (a request delays the Virus' notes by
  ~130 ms); after 30 s of pauses in total a 300 ms gap is enough, so constant playing cannot block the sync. The sync
  page shows "SYNC PAUSED WHILE YOU PLAY".
- **Banks:**
  - RAM A–D: synced from the Virus on first start, then cached.
  - ROM A–Z: from `firmware.bin`; the device model comes from the USB descriptor.
  - Personal Libraries: the patch files.
- **Browser:**
  - Search by name and/or category builds a "Search Results" bank like the original: all banks, one entry per
    sound, sorted, 128 per page.
  - Load and save into RAM or libraries, rename, drag and drop between parts.

### Device layer: `src/device/*`
- `Broker`: connects to the broker (Unix socket or Windows named pipe), handles HELLO/WRITE/CONTROL/AUDIO, and
  receives RX/STATE.
- `AudioIn`: the shared-memory ring of 16384 frames × 6 channels: analog in, USB 1, USB 2.
- `MemorySync`:
  - checks bank fingerprints against the cache, and requests single dumps only for changed programs;
  - one request at a time, at the Virus' pace of about 1 s per request.

### Control Center: `src/cc/*`, `tools/tirevive-cc.cpp`
- The standalone replacement of Access' Virus Control Center, phase 1 read-only (device info, backup of the RAM
  banks as patch libraries, library export). No flash writes and no OS update, ever. See
  [controlcenter.md](controlcenter.md).

### Editor size
- The editor's size = the original layout (907 x 513) x the display scale the host reports
  (`IPlugViewContentScaleSupport`: Windows/Linux hosts pass e.g. 1.5 at 150 % display scaling; macOS scales itself
  and passes 1) x the user's choice. A right-click where there is no control opens "Size 100 % .. 200 %"; the choice
  is kept per user in `<plug-in folder>/ui-zoom` (`VTI_NATIVE_ZOOM` overrides it for tests).
- `CFrame::setZoom` scales drawing and mouse events; the new size goes to the host with `IPlugFrame::resizeView`
  (from `beforeSizeChange`); a host that refuses keeps the old size. No free resizing by dragging (`canResize` false).
- The original images exist at one size only: enlarged they are interpolated (soft at 200 %); the displays (filter,
  envelopes) and values are drawn as lines and text and stay sharp.

### Device ownership
- One broker owns the Virus' USB interfaces; it serves **one program at a time** (the owner). Others wait and
  take over when the owner disconnects. A second program restarting the stream in a running session left the Virus
  without USB audio until a power cycle (E112), hence the rule.
- Read-only observers (the Control Center's Nerd mode, protocol 4) never become the owner and cannot write.
- The plug-in connects only from its first activation (`setActive(true)`): a DAW's plug-in scan (initialize +
  terminate) never reaches the Virus (E147). The Control Center and `tirevive-cc` connect only while an action runs.
- Other software that drives the Virus over USB (Access' old drivers, another editor's driver, another project's
  plug-in) must not hold the interfaces at the same time: on macOS TI Revive's setup offers to park known conflicting
  drivers (E134); the broker log shows `LIBUSB_ERROR_ACCESS` when another driver owns them.

### Identity and old projects
- Name **"TI Revive Virus Control"**, vendor **TI Revive**, bundle `TI Revive Virus Control.vst3`, version = the TI
  Revive release (CMake project version).
- Class id = the one of the TI Revive plug-in it replaced in 0.8.0, so DAW projects saved with "Virus TI (TI
  Revive)" open with it: Windows/Linux the wrapper's id (the original's with byte 15 XOR 0x5A), macOS the bridge's id.
  `moduleinfo.json` declares the other ids compatible (the original Access plug-in, the other platform's, the test
  builds'; VST 3.7.5+ hosts). The project state format is the original's: a state saved by the original (= what the
  wrapper and the bridge store) loads unchanged, and `getState` returns it byte-identical.

## 3. Platforms

**Popup menus (all platforms, E194, E195):** the value lists (oscillator modes, wavetables, ...), the category filter,
the size menu and the patch/bank menus with their submenus open our own popup (`src/editor/popupmenu.*` = layout,
mouse, keys and the submenu wait, unit-tested; `PopupView` in `plugin/source/viruseditor.cpp` = drawing and control;
`plugin/source/popupwindow*` = the windows). It looks like the original's Windows menu: columns of 25 with a
separator line (the Wavetable list: 4 x 25), at the control's corner (E189), a tick at the current value, greyed
entries ("-G"), Up/Down/Left/Right/Home/End, Return, Esc. VSTGUI's native menus could not do this: columns exist only
in its Windows menu. Like the original's menus each open menu is a window of its own (since round 0.8.22), so it
may leave the editor and use the monitor's work area; submenus open on the right of their entry, on the left when
there is no room, and a list taller than the screen gets more columns. The windows never take the focus: the keys
stay with the editor, where an invisible modal view receives them; a click outside the menus, a click into the
editor or a switch to another window closes the menu. Submenus open, switch and close after Windows' menu show delay
(400 ms on macOS/Linux), so the pointer can cross other entries on its way into an open submenu.


### Linux (tested in REAPER 7.82 at 44.1 and 48 kHz)
| topic | how |
|---|---|
| plug-in format | VST3 bundle `~/.vst3/TI Revive Virus Control.vst3` (`x86_64-linux`) |
| UI | VSTGUI on X11; uses the host's `IRunLoop` (from the plug frame or host context) and `X11::FrameConfig`; the host maps the XEmbed window |
| menus | our own popup (above) in override-redirect X11 windows (`popupwindow_x11.cpp`); the work area is that of the monitor at the menu (`src/editor/workarea.h`): mutter's `_GTK_WORKAREAS_D<n>`, else the RandR monitors cut to `_NET_WORKAREA`, else `_NET_WORKAREA` |
| drawing speed | VSTGUI keeps bitmaps as CPU (cairo image) surfaces and uploads their pixels on every draw into the X back buffer - ~70 ms per full repaint on a real X server/XWayland. The editor keeps X pixmap copies of all bitmaps (`cairo_surface_create_similar` on the frame window) and caches the static layer (backgrounds, panels, labels) as a pixmap; repaints become server-side composites (GPU with glamor): full repaint ~1-7 ms, knob step < 1 ms. `VTI_NATIVE_NO_PIXMAPS=1` disables the copies |
| broker | systemd user socket `/tmp/vtibroker-<uid>.sock` (starts on demand, exits after 30 s idle) |
| audio ring | `/dev/shm/tirevive-audio-<uid>` |
| cache | `~/.local/share/virus-ti-native/cache` (`RAM A`..`RAM D`) |
| Access files | `~/.local/share/tirevive/access/Access Music/Virus TI/Common`, unpacked from the user's `.msi` by `install.sh` (msiextract or Wine's administrative extraction); the folder is written to `~/.config/tirevive/access-dir`, else `$WINEPREFIX`/`~/.wine` is searched (installations of TI Revive 0.7) |
| Personal Libraries | `~/Documents/Access Music/Virus TI/Patches` (factory libraries copied there by `install.sh`), also the Wine prefix's Documents |
| audio setup | any host rate (resampled); PipeWire with JACK (`pw-jack reaper`) works well, PulseAudio is not recommended |
| runtime | built on Ubuntu 22.04 (glibc 2.35) by the release workflow; needs the usual X11/xcb/cairo/pango libraries (`install.sh` checks with `ldd`) |

### Windows (tested in Ableton Live 11 on a Windows 11 PC and in a VM)
| topic | how |
|---|---|
| plug-in format | VST3 bundle in `C:\Program Files\Common Files\VST3\TI Revive Virus Control.vst3` (`x86_64-win`), built with MSVC in CI (`.github/workflows/build.yml`, artifacts `VirusTINative-windows` + `tools-windows`) |
| UI | VSTGUI with Direct2D; our own popup (above) in WS_POPUP windows that never activate (`popupwindow_win.cpp`); images taller than 16384 px (Direct2D's limit; the cutoff knob filmstrips are 16512 px) are split into tiles when loaded |
| broker | named pipe `\\.\pipe\vtibroker`; like TI Revive's own clients the plug-in starts `vtibroker.exe` on demand (path from `HKLM\SOFTWARE\TI Revive\InstallDir`, else `%ProgramW6432%\TI Revive`), detached from the DAW's job object; the broker exits by itself when idle |
| USB | TI Revive's WinUSB binding (MSI); no Access driver |
| audio ring | file mapping `Local\vtibroker_audio`, plus a named event per plug-in process |
| cache | `%LOCALAPPDATA%\virus-ti-native\cache` |
| Access files | `C:\Program Files\Access Music\Virus TI\Common\Virus Control.dll` + `firmware.bin` (+ `Documentation`), unpacked from the user's `.msi` by `tirevive-access.ps1` (`msiexec /a`: no custom actions, no drivers); a full Access installation of TI Revive 0.7 is offered for removal (files kept) |
| Personal Libraries | `%USERPROFILE%\Documents\Access Music\Virus TI\Patches` |

Installed by TI Revive's MSI (`native/packaging/windows/native.wxs`): the bundle, the ASIO driver (registered as MSI
registry values), clean-up of the test builds. Real PC (user, 0.8.0 test builds): sound, editor, sync, timing (304 of
304 notes on the grid, no drift), ASIO clean.

**ASIO driver (`asio/asiodriver.cpp`, target `VirusTINative_ASIO`):** the Virus as a sound card for any Windows
ASIO host, without the Access/Ploytec driver. It reads the same audio ring as the plug-in and writes the outputs
through the broker.

| topic | how |
|---|---|
| identity | COM in-proc server, CLSID `{F1DA6F3F-7A48-49A9-97F8-C3A8C008EABD}`, ASIO name "TI Revive Virus Audio" (registration removes the test builds' "Virus TI (native)" entry); `regsvr32` writes `HKLM\SOFTWARE\Classes\CLSID\...` and `HKLM\SOFTWARE\ASIO\TI Revive Virus Audio` |
| channels | 6 in (Analog In L/R, USB 1 L/R, USB 2 L/R), 2 out (Virus Out L/R); Int32LSB |
| sample rate | 44.1 or 48 kHz (round 0.8.32, not yet tested on hardware): `setSampleRate` asks the broker, which switches only while no plug-in owns the Virus (`owner.h` `vti_rate_setter`); a rate it keeps is refused (`ASE_NoClock`). Init asks for no rate (keeps the one that is set). A switch by the owning plug-in gives the host `sampleRateDidChange` and `kAsioResetRequest` |
| buffers | 64-2048 frames, preferred 256 |
| clock | the Virus is the master: a thread waits on the ring's event (`Local\vtinative_audio_<pid>`), and each full buffer of new frames is one `bufferSwitch`; the outputs are queued to the broker right after it |
| latency | in = buffer, out = buffer + 512 (broker prefill) + 88 |
| restarts | inputs muted for 100 ms after a (re)start (the first USB block can carry a full-scale spike); a restarted ring sends `kAsioResetRequest` |
| test host | `tools/asiohost.c` (`asiohost.exe`): opens the driver, prints channels, counts switches, can play a tone |

Tested: Ableton Live 11 on a real Windows 11 PC, playback through the Virus outputs clean (user). In a QEMU VM the
output crackles because QEMU's USB passthrough corrupts isochronous OUT data (TI Revive E083, E142).

### macOS (universal arm64 + x86_64, tested in Ableton Live 12 on an M3)
| topic | how |
|---|---|
| plug-in format | VST3 bundle `~/Library/Audio/Plug-Ins/VST3/TI Revive Virus Control.vst3`, universal (runs natively on Apple Silicon, no Rosetta), built with Xcode in CI (artifacts `VirusTINative-macos`, `tools-macos`) |
| UI | VSTGUI with Cocoa / CoreGraphics; our own popup (above) in non-activating NSPanels (`popupwindow_mac.mm`) |
| permissions | the DAW needs microphone access (System Settings > Privacy > Microphone): without it Core Audio delivers silent input buffers |
| broker | same program and protocol as on Linux (`/tmp/vtibroker-<uid>.sock`); no socket activation on macOS, so the plug-in starts `/Library/Application Support/TI Revive/vtibroker` itself when nobody answers (like TI Revive's libVirusUSB; `$TIREVIVE_BROKER` overrides) |
| audio | **not** through the broker: the Virus' USB audio function is class compliant, so Core Audio offers it as an input device. The plug-in finds it (name contains "Virus", 6 inputs), sets 44.1 kHz and fills the same ring structure (`vti_audio_shm`) from an IOProc in its own memory; drift handling and latency are shared with the other platforms |
| cache | `~/Library/Application Support/virus-ti-native/cache` |
| Access files | `/Library/Application Support/Access Music/Virus TI/Common/Virus Control.bundle/Contents/Resources` (+ `firmware.bin` next to it); only read, the Intel-only Access plug-in is never loaded |
| Personal Libraries | `~/Documents/Access Music/Virus TI/Patches` |
| Audio Unit | `au/tirevive_au.mm` (target `VirusTINative_AU`): Steinberg's AUv2 wrapper (VST3 SDK, MIT, compiled into this file) around the VST3 plug-in, which the component carries as `Contents/Resources/plugin.vst3`; Apple's AudioUnitSDK 1.4 (Apache 2.0) built from source by CMake (C++23). Codes of the classic TI Revive Audio Unit (`aumu`/`VTIr`/`TIRv`), version major<<16 \| minor<<8 \| patch; `RestoreState` translates the classic unit's state (AU preset key `data`) to the wrapper's `Processor State`, so old Logic / GarageBand projects open. Checked: `auval` passes (CI), GarageBand plays and shows the editor, an old-format state restores part 1's patch (`tools/autest.mm`: loads a component in-process, ClassInfo load/save, render, view PNG) |
| install | TI Revive's pkg (`scripts/mac-build-release.sh`): VST3 into `/Library/Audio/Plug-Ins/VST3`, the Audio Unit into `.../Components`, ad-hoc signed; the classic bridge (VST3 + AU, Rosetta helper, libVirusUSB) is removed. `Set up Access Virus TI files.command` unpacks the Access `.pkg` (`pkgutil --expand-full`) instead of installing it |

## 4. Building and testing

- **Unit tests:** `ctest --test-dir build` (`tests/`, a small built-in harness, no Virus needed): USB-MIDI packing,
  state container, editor records, meta mapping, conditions and dumps on a hand-written device description, the
  automation rate limit, library files. Tests on the user's Access files (every parameter's encoding, the real
  single dump, editor rendering) are skipped where the files are missing (CI). CI runs them on all platforms.
- **Sanitizers:** configure a separate build with `-fsanitize=thread` (or `address,undefined`) for compiler and
  linker and run `nativehost --stress-params --notes --editor` (2026-10-14: no findings left).
- **Partial repaint check:** `tools/uicompare/partialcmp.sh` compares every page after automation, once with partial
  repaints and once with `VTI_NATIVE_NO_PARTIAL=1`; the screenshots must be identical.

- **Build:** see the [README](../README.md): `cmake -S native -B build-native && cmake --build build-native` from the
  repository root. It needs the VST3 SDK 3.8 (submodule `native/external/vst3sdk`) and CMake 3.25+; the broker
  headers come from `src/broker`. Releases: TI Revive's workflow (`.github/workflows/build.yml`) builds the plug-in
  on Windows (MSVC), macOS (universal) and Linux (Ubuntu 22.04) on a version tag. The tools also need
  X11/Xtst (nativehost) and cairo (vtirender).
- **Headless tests:** `nativehost` loads the plug-in without a DAW. It can apply states and parameters, play notes,
  render WAV, and open the editor on a virtual X display, where it can click, drag, type and take screenshots.
  - `VTI_NATIVE_DRAWTIME=1` logs every repaint.
  - Running it in a private `/tmp` (`bwrap --tmpfs /tmp`) keeps it away from the Virus while a DAW is using it.
- **The original as a reference:** it runs under Wine with TI Revive's `vtihost`. Its USB traffic, audio and
  screens are recorded and compared; recordings stay in `work/` (git-ignored).
