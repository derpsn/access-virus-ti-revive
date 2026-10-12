# Design: macOS stage 2 - native bridge plug-in + Rosetta helper (first Mac release)

Status: design + prototypes (2026-10-12). Goal (user): Ableton runs **natively** (arm64); only a helper process
runs under Rosetta and hosts the original Intel-only Access plug-in. Stage 1 (E099-E103) proved that the original
works without Access drivers through our `libVirusUSB.dylib` + `vtibroker`; stage 2 keeps all of that and moves the
original out of the DAW process.

```
 Ableton (arm64, native)                          vtihelper (x86_64, Rosetta)                 vtibroker (arm64)
 ┌──────────────────────────────┐   control: Unix socket  ┌─────────────────────────────┐  Unix socket ┌───────────┐
 │ "Virus TI (TI Revive)" VST3  │ ──────────────────────▶ │ hosts "Virus TI.vst3"       │ ───────────▶ │ libusb    │
 │  arm64, per instance:        │   audio/MIDI: shm +     │  (original, unmodified)     │  shm/ports   │ MI_03/04  │
 │  process() ⇄ shm, semaphores │ ◀──────────────────────▶│  + our libVirusUSB.dylib    │ ◀──────────▶ │ MIDI slot │
 │  editor = mirror of helper   │   editor: pixels shm +  │  audio: Core Audio IOProc   │              └───────────┘
 │  view, events forwarded      │   input events          │  on "Virus TI" (AppleUSBAudio)
 └──────────────────────────────┘                         └─────────────────────────────┘
```

## Facts this builds on

- Original VST3 (probe `src/mac/helper` E104): single-component (controller = component), buses: audio in
  "Dummy In" 2 ch + "Sidechain" 2 ch (aux), event in "Midi In" 16 ch, audio out "Out 1".."Out 3" 2 ch each;
  10404 parameters (mostly "(unmapped)" automation slots, last = "Pitchbend" id 1110145). Hosting it in our own
  x86_64 process works (initialize, setupProcessing, setActive, MIDI slot handshake 'st03').
- The plug-in needs the **main thread's CFRunLoop** running (timers for MIDI/device polling); the helper is an
  NSApplication-style process whose audio work runs on its own thread(s).
- Editor: VSTGUI (`NSViewFrame`, CoreGraphics only, no OpenGL; WebKit for the welcome page). A view of this kind
  can be rendered into a bitmap by its own process (`-[NSView cacheDisplayInRect:toBitmapImageRep:]`), without
  the Screen Recording permission.
- Audio input needs the **Microphone permission** of the *responsible* app (E103). A helper spawned by the DAW
  inherits the DAW's responsibility (TCC attributes child processes to the launching app), so Ableton's permission
  covers it; a helper started by launchd would need its own entry. -> the bridge spawns the helper itself.

## Process model

- **One helper per DAW process**, hosting all bridge instances of that DAW (the original expects all its instances in
  one process: plug-in master/slave, shared device objects). The first bridge instance spawns it (`posix_spawn`,
  binary inside the bridge bundle `Contents/Resources/vtihelper`, x86_64 -> Rosetta), arguments: control socket path
  `/tmp/tirevive-helper-<uid>-<daw pid>.sock`. The helper exits when its last instance is gone or the DAW pid dies.
- The broker stays as in stage 1 (LaunchAgent, socket activation).

## Control channel (Unix socket, request/response + notifications)

Messages `{u32 type, u32 instance, u32 seq, u32 len, payload}`:
CREATE (-> instance id, shm name, bus layout, latency), DESTROY, PARAM_INFO (all ParameterInfo, once),
GET_STATE / SET_STATE (component state blob; the original is single-component, so one blob), SETUP (rate, max block,
offline/realtime), ACTIVE (on/off), EDITOR_OPEN (-> size, pixel shm) / EDITOR_CLOSE / EDITOR_EVENT (mouse, wheel,
key), and notifications from the helper: PARAM_EDIT (begin/perform/end from the original's UI -> bridge's
IComponentHandler), RESTART (flags), EDITOR_RESIZE, EDITOR_DIRTY.

## Audio path (per instance shared memory, lock-free, synchronous)

`/tirevive-<daw pid>-<instance>`: header (state, frames, seq numbers), transport (ProcessContext fields), up to 512
events (note on/off, poly pressure, data/SysEx up to a small inline size; sample offsets kept), parameter changes
(id, offset, value; up to 256 per block), outputs 3 x 2 ch x max block float32.
The bridge's process(): write inputs, bump `req`, post semaphore A; the helper's process thread waits on A, calls
the original's process(), writes outputs, bumps `done`, posts B; the bridge waits on B with a deadline of one block
duration, else outputs silence for that block and counts a miss (never blocks the DAW). No added latency.
Semaphores: POSIX named semaphores (`sem_open`, work across arm64/x86_64), later possibly os_sync_wait_on_address.

## Editor (mirror)

The helper opens the original's IPlugView in an **invisible borderless window** of its own, placed at the same screen
position as the bridge's view (so popup menus and text fields of VSTGUI appear at the right spot), ordered behind
the DAW window. It renders the view into a pixel buffer in shared memory on invalidation / at most ~30 fps; the
bridge's NSView (arm64) draws the latest frame and forwards mouse, wheel and key events, which the helper replays
into its window (`sendEvent:`). Fallback if replay is not good enough: show the helper's window itself on top of
the bridge's view and keep it aligned (simpler, but z-order/focus quirks).

## Parameters, state, presets

- The bridge exposes the original's parameter list 1:1 (same ids), so automation recorded with the bridge is stable.
- State: the bridge stores the original's state blob plus a small header (bridge version); projects saved with the
  stage 1 setup (original plug-in under Rosetta) need a different plug-in, as on Windows ("Virus TI (TI Revive)").

## Steps

1. Probe: host the original in an x86_64 process (done, E104); MIDI from the probe needs the run loop (to verify).
2. `tirbridge.h`: shared layout + control protocol (C, fixed-width, identical for arm64 and x86_64).
3. Helper server mode: control socket, instances, process thread, state, params.
4. Bridge VST3 (arm64, pluginterfaces only, like the Windows wrapper without SDK): factory, component +
   processor + controller, process() via shm, params/state pass-through; bundle with the helper inside.
5. Ableton test: audio + MIDI + automation + save/load (Ableton native, Microphone permission for Ableton).
6. Editor mirror; then AU (Logic, GarageBand) with the same helper.
7. Packaging: .pkg (bridge VST3/AU, helper, libVirusUSB.dylib, broker + LaunchAgent; Access plug-ins from the
   user's own installer, without the driver package), README incl. Microphone permission.
