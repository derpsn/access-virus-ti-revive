# Test helper (Windows): run in a VM with "powershell -ExecutionPolicy Bypass -File <script>"
# log everything arriving on the WinMM input "Virus TI" for N seconds
param([int]$Seconds = 60)
$src = @"
using System; using System.Runtime.InteropServices; using System.Collections.Concurrent;
public static class MidiMon {
  public delegate void MidiInProc(IntPtr h, uint msg, IntPtr inst, IntPtr p1, IntPtr p2);
  [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)] public struct MIDIINCAPS { public ushort wMid, wPid; public uint vDriverVersion; [MarshalAs(UnmanagedType.ByValTStr, SizeConst=32)] public string szPname; public uint dwSupport; }
  [DllImport("winmm.dll")] public static extern uint midiInGetNumDevs();
  [DllImport("winmm.dll", CharSet=CharSet.Unicode)] public static extern uint midiInGetDevCaps(UIntPtr id, ref MIDIINCAPS caps, uint size);
  [DllImport("winmm.dll")] public static extern uint midiInOpen(out IntPtr h, uint id, MidiInProc cb, IntPtr inst, uint flags);
  [DllImport("winmm.dll")] public static extern uint midiInStart(IntPtr h);
  [DllImport("winmm.dll")] public static extern uint midiInStop(IntPtr h);
  [DllImport("winmm.dll")] public static extern uint midiInClose(IntPtr h);
  public static ConcurrentQueue<string> Q = new ConcurrentQueue<string>();
  public static MidiInProc Cb = (h, msg, inst, p1, p2) => { if (msg == 0x3C3) Q.Enqueue(string.Format("{0:X6} t={1}", (uint)p1.ToInt64() & 0xFFFFFF, p2.ToInt64())); else if (msg == 0x3C4) Q.Enqueue("long data (sysex)"); };
  public static uint Find(string name) { for (uint i = 0; i < midiInGetNumDevs(); i++) { var c = new MIDIINCAPS(); midiInGetDevCaps((UIntPtr)i, ref c, (uint)Marshal.SizeOf(c)); if (c.szPname == name) return i; } return 0xFFFFFFFF; }
}
"@
Add-Type $src
$id = [MidiMon]::Find("Virus TI"); "input port id $id"
$h = [IntPtr]::Zero
"open " + [MidiMon]::midiInOpen([ref]$h, $id, [MidiMon]::Cb, [IntPtr]::Zero, 0x30000)  # CALLBACK_FUNCTION
"start " + [MidiMon]::midiInStart($h)
$end = (Get-Date).AddSeconds($Seconds); $n = 0
while ((Get-Date) -lt $end) { $m = $null; while ([MidiMon]::Q.TryDequeue([ref]$m)) { $n++; if ($n -le 40) { "$(Get-Date -Format HH:mm:ss.fff) $m" } }; Start-Sleep -Milliseconds 100 }
"messages: $n"
[void][MidiMon]::midiInStop($h); [void][MidiMon]::midiInClose($h)
