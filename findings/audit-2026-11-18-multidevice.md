# Audit 2026-11-18: several Viruses (#45), fix list

`/ponytail:ponytail-review` of the multi-device commits f4c7479 and f540fdb on round-0.8.42 (one broker per Virus,
plug-in slot claim, audio ring and RAM cache per Virus, macOS Core Audio pick, systemd template units). Read-only
review, nothing run on hardware for it. Each fix: failing test first (TDD), then the code; the Linux HIL run on our
TI2 Desktop afterwards. Expected load: one musician, 1 to 4 Viruses, one computer.

Status: `open`, `in progress`, `done`, `dropped` (with the reason).

Fixed 2026-11-18 (tests first: `test_slotpick.cpp` +3 cases, 837/837): #1/#5 `slot_choose(taken, n, preferred, skip)`
skips the slot just given up once; #4 `slot_give_up(slot, present, elsewhere, ms)` also gives up a slot another
program has (broker state 2), the worker loop handles both in one branch; #2 `slot_to_save`; #3 the broker says "not
found" once until found (checked: broker 2 alone, 1 line in 7 s; no unit test, a static flag in `vti_open`), the
plug-in logs a wait once (`skip_slot_`); #6 one message; #7 the parse result kept. Linux HIL afterwards: see HANDOFF.

## Must fix

| # | Status | Finding | Where | Fix |
|---|---|---|---|---|
| 1 | done | A project saved with Virus 2 never falls back to Virus 1: after the give-up the next claim takes the preferred (free, empty) slot 2 again, every ~5 s | `virusplugin.cpp` give-up block, `slotpick.h` `slot_choose` | after giving up a slot, the next choice skips it; `slot_pref_` stays for saving |
| 2 | done | `getState` erases the `TIRevive/Virus` record and writes it only while `slot() > 0`: a save between two tries or before the first connect forgets the project's Virus | `virusplugin.cpp` `getState` | save `slot ? slot : preferred` (`slot_to_save`) |

## Should fix

| # | Status | Finding | Where | Fix |
|---|---|---|---|---|
| 3 | done | One Virus, two instances: the second cycles through broker 2 every ~5 s, broker 2 never idles out and logs "Virus 2 not found" every 2 s (~43,000 lines a day, no rotation); diag.log "no Virus 2 - waiting" every 5 s | `src/broker/vticore.c` `vti_open`, `virusplugin.cpp` give-up log | "not found" only on a change; the give-up logged once per wait |
| 4 | done | DAWs with one process per plug-in (Bitwig sandbox, two DAWs): every instance claims slot 1 in its own process, the second waits on Virus 1 for good although Virus 2 is there | `virusplugin.cpp` `claim_owner`, the "in use elsewhere" wait | a slot another program has (broker state 2) is given up like an empty one and skipped once |
| 5 | done | The claim / give-up / re-claim order lives in `run()` without a test (finding 1 slipped through) | `virusplugin.cpp` `run` | the next-slot decision as one tested function in `slotpick.h` |

## Nice to have

| # | Status | Finding | Where | Fix |
|---|---|---|---|---|
| 6 | done | Two near-identical "not found" messages for slot 1 and slot N | `vticore.c` `vti_open` | one message (with fix 3) |
| 7 | done | `vti_slot_parse` called twice | `src/midiports-win/tirevive-midiports.cpp` `wmain` | keep the result |

Not checked by the review: macOS and MSVC builds, the Windows `CM_DRP_LOCATION_PATHS` key on a real PC, two real
Viruses, the macOS libusb root package (broker 2 started by the plug-in has no root, so no audio for Virus 2 there).
