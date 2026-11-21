# TI Revive (native plug-in)

A plug-in for the Access Virus TI, written from scratch: it works like the original "Virus Control"
(same pages, browser, libraries, automation IDs, project state) but runs natively on Windows, macOS (Apple Silicon
and Intel) and Linux - without the Intel-only Access plug-in, Rosetta, Wine or yabridge. The Virus is reached
through the TI Revive broker (USB MIDI and audio). On Windows, the ASIO driver "TI Revive Audio" also makes
the Virus a sound card without the Access driver.

Users install it with TI Revive (MSI, Mac pkg, Linux `install.sh`); this folder (`native/` in the TI Revive
repository, moved in with its full history on 2026-10-15) is for building and development.

| platform | status |
|---|---|
| Linux (x86_64, X11/XWayland) | working; tested in REAPER 7.82 (44.1 and 48 kHz): sound, automation, browser, LOAD/SAVE, libraries, sync, timing |
| Windows | working; tested in Ableton Live 11 on a Windows 11 PC: sound, timing, UI; ASIO driver clean |
| macOS | working; universal VST3, tested in Ableton Live 12 on an M3: sound, UI, sync, timing |
| Virus TI Snow | not supported yet |

**How it works:** [docs/architecture.md](docs/architecture.md). More detail: protocol in
[docs/protocol.md](docs/protocol.md), file and render formats in [docs/formats.md](docs/formats.md), progress and
measurements in [docs/status.md](docs/status.md), the plan in [docs/plan.md](docs/plan.md).

**Rule:** no Access material in git (graphics, XML, firmware, decompiled code). The plug-in loads the skin, the
device description and the ROM from the user's own Access installation at runtime. Recordings stay in `work/`.

## Building (all platforms)

From the repository root (TI Revive), with CMake 3.25+ and the VST3 SDK submodule:

```sh
git submodule update --init --recursive native/external/vst3sdk
cmake -S native -B build-native -DCMAKE_BUILD_TYPE=Release
cmake --build build-native --config Release
ctest --test-dir build-native -C Release        # unit tests, no Virus needed
```

- **Windows:** Visual Studio 2022 (`-G "Visual Studio 17 2022" -A x64`); targets `VirusTINative` (plug-in) and
  `VirusTINative_ASIO` (ASIO driver). **macOS:** Xcode, universal with `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"`.
  **Linux:** the X11/xcb/cairo/pango development packages listed in `.github/workflows/build.yml` (job
  `native-linux`).
- The broker headers come from `../src/broker`. Users get the plug-in through TI Revive's installers (MSI, Mac pkg,
  Linux `install.sh`); the release workflow builds it on a version tag.
- Development install on Linux: `cp -r "build-native/VST3/Release/TI Revive.vst3" ~/.vst3/` (the TI
  Revive broker must be installed; the Access files are found through `~/.config/tirevive/access-dir` or the Wine
  prefix).

## Using it in REAPER (Linux)

1. Close every other program that uses the Virus: only one session can own it.
2. Start REAPER (any sample rate; 44.1 kHz avoids the conversion). With PipeWire for example `pw-jack reaper`.
3. Insert "TI Revive". On the first start it syncs RAM A–D from the Virus. That takes about 8 minutes;
   the progress bar shows it, and you can play meanwhile (the sync pauses while notes arrive). The banks are cached in `~/.local/share/virus-ti-native/cache`; rename that folder
   to force a new sync.
4. Where the other banks come from:
   - ROM A–Z: the user's `firmware.bin`.
   - Personal Libraries: `Documents/Access Music/Virus TI/Patches`, including subfolders.

Environment settings for testing:
- **Timing:** `VTI_NATIVE_S` (stamp offset, default 5000), `VTI_NATIVE_M` (FE/F2 lead, default 1024),
  `VTI_NATIVE_PING`, `VTI_NATIVE_K`. See docs/status.md, R5.
- **Repaint log:** `VTI_NATIVE_DRAWTIME`. **Full repaints only** (fallback): `VTI_NATIVE_NO_PARTIAL`.

## Source layout

| path | content |
|---|---|
| `src/model` | `RemoteDevice`: parameters, MIDI/SysEx encode/decode, dumps, upgrader, automation map |
| `src/editor` | `EditorLayout`, `Conditions`, `EditorRenderer` (draws the original UI on any canvas) |
| `src/access` | Access files (DLL/bundle resources), firmware ROM banks, Personal Libraries (SMF) |
| `src/device` | broker client, shared-memory audio ring, memory sync, USB-MIDI packing |
| `plugin/source` | the VST3 plug-in (`VirusPlugin`) and its VSTGUI editor (`VirusEditor`) |
| `asio` | Windows ASIO driver: the Virus as a sound card through the broker |
| `tools` | developer tools, see below |
| `tests` | unit tests (`vtitests`, run by ctest) |

## Tools

| tool | purpose |
|---|---|
| `vtimodel` | inspect the Access files: params, messages, dumps, categories, automation, ROM, libraries, init multi |
| `virusctl` | talk to the Virus through the broker: state, set, note, sysex, timing tests |
| `virusmon` | watch the traffic to and from the Virus read-only (broker observer), decoded like Nerd mode: `virusmon [seconds] [--clock] [--hex]` |
| `virussync` | bank sync without a host |
| `asiohost` | Windows: load an ASIO driver, list channels, count buffer switches, play a test tone |
| `nativehost` | minimal VST3 host: states, params, notes, WAV, editor on X11 with click/drag/type/screenshot |
| `vtirender` | render the editor with cairo and compare it against screenshots of the original |
| `xclick`, `xtype` | drive the original under Wine/Xvfb |
| `DecompileAll.java`, `msgsummary.py` | Ghidra headless decompile into `work/`, trace summaries |
