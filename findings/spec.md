# Virus TI USB protocol spec (draft)

If these findings help your project, we would be happy about a mention, for example: "Protocol knowledge from TI Revive (https://github.com/derpsn/ti-revive)". Corrections and new observations are welcome as issues, so everyone benefits.

Status: EMPTY. Fill only with evidence-backed content (cite evidence IDs).

## 1. Device and interfaces
## 2. Enumeration and initialization (control requests, alt settings)
### 2.1 Vendor memory read (on plugin open, E026)
- OUT bmRequestType 0x41, bRequest 1, wIndex 0x12, 3 data bytes = address (big-endian, e.g. FF7FA0, 800000).
- Poll IN 0xC1 / 0x81 / wIndex 1 (1 byte status = progress counter 00..10, 0x10 = ready, ~12 ms per poll, 100-250 ms per block); read IN 0xC1 / 0x81 / wIndex 0x30 (16 bytes).
- Contents: "FLSH200801010000" at FF7FC0, "01" at FF7FD0, "1.7.00" at 0x800030. IN wIndex 0x22 stalls.
- Through WinUSB use recipient device (0x40 / 0xC0): WinUSB rewrites wIndex of interface-recipient requests (E040).
## 3. Handshake / enable sequence
- After SET_CONFIGURATION the driver sends raw `4E 73 52 01` (not USB-MIDI) as an interrupt OUT on EP 0x05 (E022, E028, E030). Linux snd-usb-audio sends the same bytes.
- Device answers on EP 0x85 cable 1: `F0 00 20 33 01 10 73 40 40 00 F7` (E022).
- When the plugin opens: Identity Request `F0 7E 7F 06 01 F7` on EP 0x05 (cables 0 and 1), then a vendor control flash read (§2.1), then the SysEx sync (§6).
## 4. MI_03 (EP 0x05 / 0x85) framing
Standard USB-MIDI 4-byte event packets (cable<<4 | CIN), interrupt EPs, bInterval 1. EP 0x05 OUT carries the handshake and the Identity Request; EP 0x85 IN cable 1 carries the handshake reply and hardware-knob CCs (E023, E024).
## 5. MI_04 (EP 0x06 / 0x86) framing
Same packet format. Host->device on EP 0x06: cable 2 (CC, Program Change, SysEx requests, FE, F8), cable 3 (Single Dumps to edit buffer), cable 5 (F5 xx yy). Device->host on EP 0x86: cable 2 (parameter reports, e.g. 72 00 21 pp), cable 3 (dumps, 74 reports). Device streams one 4-byte packet per IN transfer (E032).
## 6. SysEx (F0 00 20 33 ...) transport and chunking
Header `F0 00 20 33 01 10` (Access, product 01, device 0x10). Observed commands (E027, E031, E033):
| Cmd | Dir | Meaning (inferred) |
|-----|-----|--------------------|
| 10 bb pp ... | both | Single Dump (bb 00 = edit buffer) |
| 30 bb pp | host | Single Dump Request (bank, program) |
| 72 pp ii vv | both | Multi/part parameter (ii 0x21 = program) |
| 73 pp ii vv | both | Global parameter (ii 0x54 = Sync Information Event) |
| 74 .. / 39 nn / 14 nn | host / host / device | unknown (bank info?) |
| 6E pp ii vv | host | parameter page C? |
Preset switch (E033): plugin -> Bank Select + Program Change; hardware -> device reports `72 00 21 pp`. Either way the host then writes its cached 524-byte Single Dump `10 00 00 ...` to the edit buffer (cable 3), followed by CC 113 / Poly Pressure A0 / 6E on cable 2.
Routing (E039): global 0x3A = Sounddump cable, 0x3B = MIDI Data cable, 0x3C = Panel Data cable; device sends cable 1 on EP 0x85 (MI_03), cables 2/3 on EP 0x86 (MI_04). Plugin sets 3A=3, 3B=1, 3C=2, 52=1 at sync start.
Plugin sync: 73 x29, 72 00..0F, 10 00 x16, 39 01..04, then 30 bb pp for every program of each RAM bank (~1 s per program in the VM).
## 7. Keepalive / clock / TI-mode requirements
Host sends Active Sensing FE (cable 2) every ~0.31 s, F5 xx yy (cable 5) every ~0.74 s, and F8 clock (cable 2) every ~2.0 s, independently (E025). Plugin requires a host audio buffer of 64/128/256/512 samples or it reports "communication problems" (E031).
## 8. Audio configuration steps (MI_00..02)
## 9. State machine
## 10. Open questions

## 11. Windows driver IOCTL interface (VirusUSB.sys), from static analysis
Device interface GUID {8002DBFF-C173-4BFF-80BE-6815D7E13071} (E017). All codes METHOD_BUFFERED, FILE_ANY_ACCESS, device type 0x22. Semantics not yet decoded; "out" = nOutBufferSize at the VirusUSB_X64.dll call site (E019).

| Code | fn | out size | call site(s) in VirusUSB_X64.dll |
|------|----|----------|----------------------------------|
| 0x220000 | 0x00 | 0xff | 0x18000afee |
| 0x220004 | 0x01 | 0xff | 0x18000b103 |
| 0x22000c | 0x03 | 0x18118 | 0x18000b16c |
| 0x220010 | 0x04 | 0x18 | 0x18000b1cc, 0x18000bbfd, 0x18000e826 |
| 0x220014 | 0x05 | 0x4c | 0x18000be41 |
| 0x220024 | 0x09 | 0x18 | 0x18000bff7 |
| 0x220030 | 0x0c | 0x10 | 0x18000bcec |
| 0x22004c | 0x13 | 0x4 | 0x18000b2bc |
| 0x220064 | 0x19 | 0xf04 | 0x18000c3cf |
| 0x220068 | 0x1a | 0xf04 | 0x18000c304 |
| 0x220070 | 0x1c | 0x4 | 0x18000b510 |
| 0x22009c | 0x27 | 0x4 | 0x18000b563 |
| 0x2200a4 | 0x29 | 0x4 | 0x18000b269 |
| 0x2200b0 | 0x2c | 0x4 | 0x18000b851 |
| 0x2200b4 | 0x2d | 0x4 | 0x18000c4c2 |
| 0x2200b8 | 0x2e | 0x4 | 0x18000c552 |
| 0x2200bc | 0x2f | 0x8 | 0x18000c5f5 |
| 0x2200d0 | 0x34 | 0x104 | 0x18000c6e3 |
| 0x2200e4 | 0x39 | 0x18 | 0x18000df12, 0x18000e0d9 |
| 0x2200e8 | 0x3a | 0x18 | 0x18000e35b, 0x18000e419 |
| 0x220408 | 0x102 | 0x4c | 0x18000a446 |
| 0x220410 | 0x104 | ? (not in 25-insn window) | 0x18000a547 |

### 11.1 Dispatcher semantics (VirusUSB.sys 2.9.66, E034/E035)
Buffers: METHOD_BUFFERED; "out" = OutputBufferLength checked by the driver; UB = Irp->UserBuffer used directly.
| Code | Size | Semantics | Used by |
|------|------|-----------|---------|
| 0x220000 / 0x220004 | n | copy version string A / B | ASIO, VC |
| 0x220008 | >=8 | STD_VENDOR_REQUEST: bmRequestType, bRequest, wValue, wIndex + data (control transfer pass-through; flash read E026) | VC |
| 0x22000C | 0x18118 | read large state block | ASIO, VC |
| 0x220010 | 0x18 | generic PtIOCTL request | ASIO, VC |
| 0x220014 | 0x4C | read info block | ASIO, VC |
| 0x220024 | 0x18 (UB) | set (FUN_f1016ee0) | ASIO, VC |
| 0x220028 / 0x22002C | 0x560 | Read/WriteMixerData (US3xx_UI) | - |
| 0x220030 | 0x10 (UB) | per-client set | ASIO, VC |
| 0x22004C | 4 | const 0x001FFDD8 | ASIO, VC |
| 0x220064 / 0x220068 | 0xF04 | write / read config block | ASIO, VC |
| 0x220070 | 4 | read value | ASIO, VC |
| 0x22007C / 0x220080 | 4 | wMaxPacketSize IN / OUT pipe | VC |
| 0x220084 | n (UB) | raw pipe WRITE (USB-MIDI packets, verbatim) | VC |
| 0x220088 | 0x8020 (UB) | raw pipe OPEN (shared RX ring + event handle) | VC |
| 0x22008C | - | flush RX ring | VC |
| 0x220090 | - | raw pipe CLOSE | VC |
| 0x22009C | 4 | read byte | ASIO, VC |
| 0x2200A4 | 4 | const 0x02094200 (driver 2.9.66) | ASIO, VC |
| 0x2200A8 / 0x2200AC | 8 / - | TX ring status / flush | VC |
| 0x2200B0..0x2200BC | 4/4/4/8 | per-client ASIO params | ASIO, VC |
| 0x2200D0 / 0x2200D4 | 0x104 | read / per-client query | ASIO / VC |
| 0x2200E4 / 0x2200E8 | 0x18 (UB) | REGISTER_APC_BS / _Notify (ASIO buffer switch) | ASIO, VC |
| 0x220408 / 0x220410 | 0x4C / 30 | NOT VirusUSB: IOCTL_USB_GET_NODE_INFORMATION / _GET_DESCRIPTOR_FROM_NODE_CONNECTION on the USB root hub (E036) | ASIO, VC |

### 11.2 Raw pipe ring (E041)
```
struct ring { uint32_t wr, rd, used, size; uint8_t data[size]; };   // data at +0x10
plugin RX buffer (0x8020 B): struct ring (size 0x8000) | +0x8010 HANDLE event | +0x8018 u32 closing
```
Producer (driver/emulation): n = len & ~3; if (size - used >= n) write at wr (wrap), used += n; SetEvent(event) after every IN transfer.
Consumer (plugin): reads at rd, used -= n.

### 11.3 Plugin audio (AudioSM shared memory, E051)
0x220030 {u8 start; u8 pad[7]; void *buf} on the \IO_SINGLE_INSTANCE handle: start=1 maps buf (0x810D0 B):
```
+0x00000  int32 samples[0x20000] (interleaved, nCh per frame)
+0x80000  u32 nChannels   +0x80004 u32 maxFrames (0x20000/nCh)   +0x80008/+0x8000C/+0x80010 u32 positions
+0x81030  u32 (dev+0xA830)  +0x810B0 = that/4   +0x81038/+0x81060/+0x81088 3 x 0x28-byte sub-structs
+0x810B4  u32 1   +0x810B8 u32 switch threshold (frames)   +0x810BC u32 read pos (init 0xFFFFFFFF)   +0x810C8 u64 QPC of first switch
```
Buffer switch when frames available since +0x810BC >= +0x810B8: user APC (routine from 0x2200E4) or SetEvent (routine 0, "threaded").
Field meanings to be confirmed by the vtihook AudioSM monitor on the original driver.

### 11.4 Plugin audio input (driver-mapped AudioSM, E055)
IOCTL 0x220010 -> {u32 0x18, u32 1, u32 1, u32 0x810C0, u64 B}. B is a 0x810C0-byte region mapped into the caller:
```
B+0       u64 0x001FFDD8 (magic = IOCTL 0x22004C value)
S = B+8   AudioSM: int32 samples[0x20000] (int16 << 16), nCh at S+0x80000 (6), maxFrames S+0x80004 (0x5555),
          write counter S+0x81028 (u32, driver), sample rate S+0x81030 (44100), S+0x810B0 = rate/4, S+0x810B4 = 1
B+0x81020 u64 owner instance, B+0x81028 u64 exclusive flag (0 = every plugin instance outputs the audio)
```
The plugin keeps its own read position; its audio thread is clocked by the threaded buffer-switch event (0x2200E4).
