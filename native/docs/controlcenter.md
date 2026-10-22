# Control Center

Access' "Virus Control Center" was a separate app next to the plug-in: backup and restore of the Virus' RAM banks,
"Burn To Flash", OS updates and an "Advanced" menu (factory reset, device name). It needs the old Access driver, so it
does not work with TI Revive. Its replacement is built in steps: a core library, a command line tool and an app.

## Scope

| Feature | State |
|---|---|
| Device info (USB identity, RAM banks compared with the cache) | app (Refresh), `tirevive-cc info` |
| Backup of the RAM banks as patch libraries (`RAM A.mid` .. `RAM D.mid`) | app, `tirevive-cc backup` |
| List / convert patch libraries (.mid <-> .syx), no Virus needed | app (Convert library), `tirevive-cc list`, `export` |
| Nerd mode: plug-in and broker log live, plug-in log switch, logs for a bug report (anonymized) | app (View > Nerd mode, or the button), `tirevive-cc logs`, `debug on/off` |
| Nerd mode "Live": broker status (device, owner, clients, audio, counters) and the traffic, decoded | app (Live tab), `tirevive-cc watch`; all three brokers |
| Restore a RAM bank from a backup or library: only the programs that differ, single dumps into RAM like the plug-in's browser, each read back; never flash (E313) | app (Restore..., Access look: RESTORE), `tirevive-cc restore A\|B\|C\|D FILE [--yes]`; since 0.8.30 |
| Firmware / OS update, Burn To Flash, factory reset, device name | **not supported, on purpose** |

The last row stays out for good: these functions write the Virus' flash, and a failure there can brick the device
(AGENTS.md rule 2). If you really need them, use Access' original software on a system that still runs it.

## Where it is installed

| | App "TI Revive Control Center" | Command line |
|---|---|---|
| Windows (MSI) | `Program Files\TI Revive\TI Revive Control Center.exe`, Start menu "TI Revive" | `Program Files\TI Revive\tirevive-cc.exe` |
| macOS (pkg) | `/Applications/TI Revive Control Center.app` | `/Library/Application Support/TI Revive/tirevive-cc` |
| Linux (install.sh) | `~/.local/bin/tirevive-control-center` + menu entry (needs gtkmm 3) | `~/.local/bin/tirevive-cc` |

The uninstallers remove them. The release workflow builds both with the plug-in on each platform.

## The app

`ccapp/ccapp.cpp`, VSTGUI's standalone library (the toolkit of the plug-in's editor; on Linux it uses GTK 3 / gtkmm
for the window and file dialogs). One window: the device line and the four RAM banks (Refresh), the backup folder
(Change...), "Back up RAM banks" with progress and Cancel, "Convert library..." (open a .mid/.syx, save as the other).
The work runs on a worker thread; the app connects to the broker only while an action runs.

**Nerd mode** (`ccapp/nerdmode.cpp`, `src/cc/diag.*`): a second page of the same window (a second window stayed
blank on Linux/GTK under Xvfb). It shows the end of the plug-in's `diag.log` or of the broker's log (Windows
`%TEMP%\vtibroker.log`, macOS `/tmp/tirevive-vtibroker.log`, Linux the user journal), refreshed every second, with
Pause; switches the plug-in log (`diag.on`, read when the plug-in starts); "Save logs..." writes one text file for a
bug report with the home folder replaced by `~` and the user name by `<user>`.

**Live** (protocol 4, `src/broker/vtiproto.h`): the page connects to the broker as a read-only *observer* while the
tab is shown. The broker never makes an observer the owner (it hands the Virus on if it had it), refuses its
writes, USB requests and audio, and sends it a copy of the traffic it passes on (the same records as its
`VTI_TRACE` file), its log lines and a status line once a second, through a 512 KB queue with its own writer
thread: if the observer falls behind, only its copy is dropped (counted, shown as "Dropped here"); the plug-in
and the libusb thread never wait for it. The client checks the broker's `OBSERVE_REPLY`: an older broker would keep
it as the owner, so it disconnects and says the broker is too old. `src/cc/decode.*` puts USB-MIDI packets back
together and names them: notes, CCs, clock, the Access SysEx commands documented in findings/spec.md §6 (single
dump / request, multi dump, bank info / request, sync ping), parameter changes by name through the device model
(the user's Access files; without them as page/id/value). Clients name themselves in HELLO (plug-in "TI Revive
Virus Control", ASIO driver, Control Center, tirevive-cc). The Windows broker has no owner (every client
shares the Virus, as before); there an observer simply gets no raw RX and the status says "owner=shared".

## The Access look

When the user's Access installer contains Access' "Virus Control Center" (Windows/Linux: `Virus Control
Center.exe`; the setup scripts copy it next to `Virus Control.dll`, the Wine prefix of 0.7 also works), the app looks
like it: `AccessFiles::open_control_center` reads its resources at runtime (layout `editor.xml`, 103 PNGs; nothing of
it is in this repository), `src/cc/ccskin.*` reads the layout (containers and controls with positions, images and
visibility expressions), `ccapp/skinview.*` draws it with the original's image conventions (two-frame buttons,
pressed `_PRS` images, the pulsing OK strip, the 6 x 9 LCD font) and `ccapp/skinpage.*` puts TI Revive's logic behind
it (`engine.xml`, the original's logic, is not used):

- start and device selector as in the original (one Virus; OS version "-": no request for it is documented). USB
  does not tell Desktop, Keyboard and Polar apart: a click on the model picture switches between them (kept in the
  app's preferences) and picks the matching backup page; a Snow is recognised by its USB product string;
- Backup & Restore: BACKUP proposes a dated folder in `Virus TI/Backups` (a click on the path field chooses
  another), OK runs the backup; progress and result in the footer. RESTORE asks for a library, then for the RAM bank to
  overwrite (since 0.8.30, E313);
- Burn Flash ROM and OS Updater: the original pages with a note that TI Revive does not offer them; their controls
  are hidden or dimmed;
- our additions in the original's empty footer (with its blank button image): NERD MODE, CONVERT LIBRARY, PLAIN VIEW;
  the plain page has an "Access look" button back.

Without the file the app shows its plain page. macOS: the original is an old 32-bit app whose images and layout are
plain files in `Contents/Resources`; "Set up Access Virus TI files" copies only those (PNG, XML) to
`/Library/Application Support/Access Music/Virus TI/Common/Virus Control Center Resources`, and an existing Access
installation in `/Applications` works too (checked on the M3: 106 files, layout read).

## How it works

- `src/cc/controlcenter.*` (in `vticore`), `tools/tirevive-cc.cpp` and `ccapp/ccapp.cpp`. They talk to the Virus through the TI Revive
  broker like the plug-in (`src/device/broker.*`), so the broker must be installed; it is started on demand.
- Only requests documented in `findings/evidence.md` are sent: standard USB GET_DESCRIPTOR (device, manufacturer and
  product string; not the serial number, which would end up in bug reports) (E115), bank info `39` -> `14` and
  single dump request `30` -> `10` (E027, E031, E037). Nothing is written to the Virus.
- Backups reuse the plug-in's bank cache (`MemorySync`): programs whose fingerprint still matches come from the cache,
  the others are read at the Virus' pace of about 1 s each (a full bank: about 2 minutes). The cache is updated on
  the way.
- **Close the plug-in (or the DAW) first.** The broker serves one program at a time; while the plug-in has the
  Virus, the tool reports "no Virus". The tool connects only while a command runs, so it never blocks the plug-in.
- Backups go to `<Documents>/Access Music/Virus TI/Backups/<date time>/` (next to `Patches`, so the plug-in's
  browser does not list every backup). The files are in the original's library format: copy one into `Patches` to
  open it in the plug-in's browser; Access' Virus Control reads them too.
- `.syx` export: the single dumps back to back, as patch librarians expect. `.mid` export renumbers the patches by
  position (bank byte 1, program = position) and recomputes the checksums, like the plug-in's library save.

## Tested

- 2026-10-15, MacBook Air M3, Virus TI2 Desktop (built with clang++ from the sources, not yet in a package):
  `info` (identity 133e:0815, bcdDevice 3.00, "Access Music" / "Virus TI"; 4 x 128 programs current in the cache,
  4 s); `backup` from a current cache (3.5 s); `backup --banks 4` with the RAM D cache moved aside: 128 programs read
  in 2 min 9 s, the library byte-identical to the one from the cache.
- 2026-10-15, Linux, Virus TI2 Desktop: the app on a virtual X display (Xvfb, clicks through XTest): Refresh shows
  the device and 4 x 128 programs current in the cache; "Back up RAM banks" writes RAM A..D.mid (progress, Cancel
  active while running, done message), libraries readable with `tirevive-cc list`. "Convert library..." opens the
  zenity file dialog (the conversion itself is the tested `export` code).
- 2026-10-15, Windows 11 VM (no Virus): the app from the MSI of CI run 38713953474 starts and shows "No Virus". The
  first build crashed at start: VSTGUI's menu building calls the window controller without a null check, so the app
  now passes one.
- macOS: the app bundle from the pkg is universal and signed. 2026-10-15, the user on the M3 with the TI2 (test
  package of CI run 38732935398): the app starts in the Access look and works; sound in the DAW with the new broker
  (MIDI stand-in off) is fine.
- 2026-10-15, Linux, TI2, Access look (Xvfb + XTest, Access' Control Center from the Wine prefix): device selector
  with the Virus, OK to Backup & Restore, BACKUP + OK wrote RAM A..D (cache current, 0 read), Burn Flash and OS
  Updater show the note, footer buttons to Nerd mode / plain view and back. Not checked: the folder dialog (zenity's
  GTK 4 dialog does not draw on a bare Xvfb).
- 2026-10-15, Windows 11 VM (no Virus; Access' Control Center exe copied in for the test and removed again): the app
  from the MSI of CI run 38723649021 shows the Access look with the empty device selector.
- 2026-10-15, Linux, TI2 (new broker run by hand, the installed one stopped meanwhile): `tirevive-cc watch` as
  observer while `tirevive-cc info` owned the Virus (both in the status; descriptor requests and bank info requests /
  replies decoded) and while the plug-in started in `nativehost` (globals by name, multi dump, 16 single dumps
  " -Init-", notes off at the end; 0 dropped over 1266 lines). The app's Live tab on Xvfb shows the same.
- 2026-10-15, Windows 11 VM (no Virus): new vtibroker.exe by hand, `tirevive-cc watch` (mingw build) as observer
  while `tirevive-cc info` connected: status, client names, log lines. Against the installed 0.8.1 broker on Linux
  `watch` reports "broker too old" and disconnects after 1.5 s (the old broker had made it the owner meanwhile).
- Linux: builds, `list`/`export` round trips on factory libraries (patch data unchanged; only program byte and
  checksums differ). Windows: cross-compiles with mingw-w64.
- Packages (CI run 38709651579, manual, test version 0.0.1): the pkg's binary is universal and signed and runs
  `info` on the M3 with the TI2 (not installed, run from the expanded pkg); the MSI's `tirevive-cc.exe` runs in a
  Windows 11 VM (`list`, `export`; `info` without a Virus: "no Virus", exit 1); the Linux tarball contains it.
