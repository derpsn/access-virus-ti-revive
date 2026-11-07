# Keyed Virus TI (Keyboard, Polar): what we know, and the plan for #29

Status: 2026-11-04, analysis only, nothing built. Target: 0.8.38 rc2. Evidence: E390, E391, E392, E007, E024, E039.
Our own Virus is a Desktop, so nothing here was seen on a unit with keys.

## The behaviour to reproduce

The original never carried the keys in the plug-in. Its plug-in has no MIDI output (E104). The Access driver offered
ports "Virus TI MIDI" and "Virus TI Synth" (E007, seen in Live on win10ref), and the DAW track's input took the keys
from there (Setup Guide, FL Studio section; Quickstart names "Virus TI Synth"). Local stays off, so a note sounds once:
key -> USB -> DAW track -> plug-in "Midi In" -> Virus.

TI Revive has the last two steps. It lacks the port, so with Local off the keys do nothing.

## What is known, and how well

| Fact | Source | Confidence |
|---|---|---|
| Original's start-up has Local off, no keyed-model branch found | XML, parser 0x18007b650 | HIGH / bounded |
| The handshake alone turns Local off, before any plug-in | firmware (E392 5), reply on the wire | MED |
| Local gates only the internal voice, never the output | firmware (E392 2) | MED |
| Keys leave on EP 0x85 cable 1 while in REMOTE, not on DIN | firmware (E392 3), cable setting (E039) | MED, not observed |
| Wheels, pressure, pedals take the same path | firmware (E392 4) | MED, not observed |
| Original at close: cables 1, Mode SINGLE twice, 350 ms apart | static + wire (E391) | HIGH |
| TI Revive has no close sequence | source (E391) | HIGH |
| Leaving REMOTE restores a snapshot including Local | firmware (E392 7) | MED |
| After a TI Revive session, keys silent standalone | prediction (E392 8) | LOW |

## What only a keyed unit can tell us

The questions, and what each answer changes. The request to the reporter is in the #29 thread.

| Question | Test | If yes | If no |
|---|---|---|---|
| Do notes reach the broker with the plug-in open? | A: `<- Virus Note` lines in `tirevive-cc watch` | build the port as planned | a setting or path is missing: go back to the firmware before building anything |
| Lowest and highest note, velocity range | A | sanity check of the note base (E392 1) | investigate transposition |
| Which messages do wheels, pressure, pedals send? | A | confirms the port's filter | widen the filter |
| Do knob moves arrive too? (E024) | A | decide: pass or drop on the port | nothing to filter |
| Does the channel follow the panel part? | A | one channel per part, documented | document that DAW routing decides |
| Is Local off after a power cycle? (the prediction) | B | close sequence is a real fix, rc2 | close sequence is only parity |
| Does Local on change the sent notes? | C | adjust | confirms E392 2 |
| Exact Local warning text | answered, E396: "Local On is not recommended when using VirusControl Plug-In" | README note | - |
| Polar same as Keyboard? Real TI1 same as TI2? (code identical, E393) | same test, other owners | one code path | model-specific handling, or an older OS |

Tool: `tirevive-cc capture` (rc2, also Record in Nerd mode, `cc/capture.h`): every transfer to and from the Virus, raw and
decoded with endpoint and cable, the broker's own handshake and note-offs and control requests included (tap in `vti_write`
and `vti_control`), plus a pre-roll of what happened before it connected (`trafficring.h`), losses marked where they happen.
Not in it: the isochronous audio stream, and the rate's SET_CUR (`vtiusbaudio*.c`, log lines only).

The reporter's unit: bcdDevice 1.00 (#29), by E099 a TI or Snow; with keys, a first-generation TI Keyboard. Its 5.1.7 code is
the same as the TI2's (E393), so E392 applies if it runs that OS: the test asks for the OS version.

## What we check ourselves first

On our Desktop, before asking for more. Rule 2: persistent writes on our own Virus first.

1. Original on win10ref with a capture: a knob on the "Virus TI Synth" input in Live, and which port reacts.
2. Original's close on the display: does the Virus leave Remote mode (win10ref).
3. TI Revive on Linux: knob lines in `tirevive-cc watch`, as the baseline for test A.
4. Close sequence on our Virus: cables and Mode after it, audio and sync on the next start, written up as evidence.
5. A MIDI keyboard on DIN In, if at hand: firmware says it goes to USB in REMOTE, the closest we can get to keys.

## Implementation plan (user 2026-11-04: all of it in rc2, before test A)

Each with a failing test first.

1. **Capture:** built (see above), hardware test open.
2. **Filter, shared code (keyport.h, done):** from EP 0x85 cable 1 pass notes, pressure, bend, controllers; never SysEx (bank replies use this
   cable outside a session, protocol.md), never clock or Active Sensing. Unit tests on captured frames.
3. **macOS port in the broker (vtikeyport_mac.c, written, first compile in CI):** CoreMIDI source "Virus TI Synth" with a stable unique ID (DAWs remember ports by it). The
   broker already starts in the GUI session (broker.cpp). Virus to DAW only: no destination, so no new route for unknown
   SysEx into the Virus (rule 2). The port exists only while the broker runs: document it, check how Live reconnects.
4. **Close sequence as the original:** done (closeseq.h, E395), on the wire on our Virus; only the owning instance, once, not on a crash. What it changes on a keyed Virus is test B's question.
5. **Docs:** README and site, "tested on" only with what a tester reported.

Open design points: "Virus TI MIDI" (DIN In) too, or only Synth at first; double notes if a user keeps Local on and monitors
the track (the firmware warns against Local on, E392 2). Linux (ALSA sequencer, a new dependency) and Windows
(design-midi-ports.md: needs Windows MIDI Services) come later.
