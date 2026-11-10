# Contributing to TI Revive

Thanks for helping to keep the Access Virus TI alive! Bug reports, tests on your setup and pull requests are all
welcome. Many people use AI assistants for this, and that is fine: the rules below are the same either way, and
[AGENTS.md](AGENTS.md) has the version written for coding agents.

## Ground rules

1. **No Access material in this repository.** No Access binaries, installers, plug-ins, firmware, skin graphics,
   XML files, patch banks or decompiled Access code — not even small excerpts. TI Revive works with each user's own
   copy of the Access software. Describe what you found in your own words (offsets and function addresses are fine).
2. **Evidence, not guesses.** Every statement about the Virus' USB protocol or the Access software needs a source:
   a capture, a log, a test against the hardware, or a static-analysis reference. New observations go into
   [`findings/evidence.md`](findings/evidence.md) as a numbered entry (see "Evidence entries" below).
3. **Be careful with the hardware.** Do not send unknown vendor requests or SysEx to a Virus to "see what happens",
   never start firmware or OS updates through TI Revive, and keep MIDI rates reasonable: a flood of parameter
   messages can hang the Virus' audio until it is power-cycled.
4. **Say what you tested.** Name the platform, Virus model (TI / TI2 / Snow / Polar / Keyboard), DAW and TI Revive
   version, and say clearly what you could *not* test (for example "built only, no hardware here").
5. **Privacy.** Logs can contain user names and paths. Remove what you would not want to publish.

## Testing your setup

Tests on setups we do not have are the most useful contribution: the **Virus TI Snow** (works per user reports, not tested by us),
Polar and Keyboard models, Logic Pro and other DAWs, Intel Macs, other Linux distributions, Windows 10. Follow the
[testing guide](.claude/skills/tirevive-test/SKILL.md) (plain text, also used by AI coding agents) and post a
[test report](https://github.com/derpsn/access-virus-ti-revive/issues/new?template=test_report.yml). Report each
stage as pass, fail or not tested - partial results help.

## Reporting a bug

Open an [issue](https://github.com/derpsn/access-virus-ti-revive/issues/new/choose) and attach the logs for your
platform. They are what makes a quick answer possible:

| Platform | Logs |
|---|---|
| all (plug-in "TI Revive Virus Control") | the plug-in log is on by default since 0.8.41 (with 0.8.40 or older create an empty file `diag.on` first): reproduce the problem, then *Save logs...* in the Control Center's Nerd mode, or attach `diag.log` from the plug-in's folder (Windows `%LOCALAPPDATA%\virus-ti-native`, macOS `~/Library/Application Support/virus-ti-native`, Linux `~/.local/share/virus-ti-native`) |
| Windows | run [`scripts/tirevive-usbinfo.ps1`](scripts/tirevive-usbinfo.ps1) (right-click → *Run with PowerShell*, or `powershell -ExecutionPolicy Bypass -File tirevive-usbinfo.ps1`) with the Virus connected; attach `TI-Revive-usbinfo.txt` from your Desktop. It also lists the newest `vtiemu-*.log`; `%TEMP%\vtibroker.log` helps too |
| macOS | `/tmp/tirevive-bridge.log`, `/tmp/tirevive-helper.log`, `/tmp/tirevive-libvirususb.log`, `/tmp/tirevive-vtibroker.log`; for "not connected": `ioreg -p IOUSB -l -w0 \| grep -i -A30 virus` and `ls "/Library/Audio/MIDI Drivers" "/Library/Audio/Plug-Ins/HAL"` |
| Linux | `journalctl --user -u tirevive-broker -b` |

Please also tell us: what you did, what you expected, what happened, and whether it worked before.

If we ask you for something (a log, a test, an answer) and do not hear back for a week, we close the issue
to keep the list manageable. That is no judgement on the report: leave a comment at any time and we reopen it.

## Pull requests

1. Fork, create a branch, keep the change focused on one problem.
2. Follow the style of the surrounding code (C/C++, comment density, naming). No new dependencies without a reason.
3. Build what you touched (see [README → Building](README.md#building)). Windows parts cross-compile on Linux with
   mingw-w64; the macOS parts need a Mac.
4. Test on hardware if you can, and write down the result. If you cannot, say so — someone with the hardware can
   test it.
5. Add or update an entry in `findings/evidence.md` when you learned something new about the device or the
   Access software, and update the user documentation (README) when behaviour changes.
6. Fill in the pull request template. If an AI assistant wrote or substantially shaped the change, say so in the
   description (one line is enough) — it helps the review, it is not held against you.

### Evidence entries

`findings/evidence.md` is one table row per observation:

```
| E141 | 2026-10-20 | What was observed, under which conditions, with numbers | source (capture, log, test) | file or function it led to | HIGH / MEDIUM / LOW / open |
```

Use the next free number, write the date of the observation, and keep wrong turns: a later entry that corrects an
earlier one is more useful than a silently edited old one. Community claims that were checked go into
`findings/claims.md`.

## Where things are

See [README → Repository layout](README.md#repository-layout). The working log of the project is
[`findings/HANDOFF.md`](findings/HANDOFF.md); the protocol description is [`findings/spec.md`](findings/spec.md).

## License

By contributing you agree that your contribution is published under the repository's [license](LICENSE).
