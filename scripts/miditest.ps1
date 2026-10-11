# Test helper (Windows): run in a VM with "powershell -ExecutionPolicy Bypass -File <script>"
# send a note and a SysEx identity request through the WinMM output "Virus TI" (first one with that name)
$src = @"
using System; using System.Runtime.InteropServices; using System.Text;
public static class WinMM {
  [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)] public struct MIDIOUTCAPS { public ushort wMid, wPid; public uint vDriverVersion; [MarshalAs(UnmanagedType.ByValTStr, SizeConst=32)] public string szPname; public ushort wTechnology, wVoices, wNotes, wChannelMask; public uint dwSupport; }
  [StructLayout(LayoutKind.Sequential)] public struct MIDIHDR { public IntPtr lpData; public uint dwBufferLength, dwBytesRecorded; public IntPtr dwUser; public uint dwFlags; public IntPtr lpNext, reserved; public uint dwOffset; [MarshalAs(UnmanagedType.ByValArray, SizeConst=8)] public IntPtr[] dwReserved; }
  [DllImport("winmm.dll")] public static extern uint midiOutGetNumDevs();
  [DllImport("winmm.dll", CharSet=CharSet.Unicode)] public static extern uint midiOutGetDevCaps(UIntPtr id, ref MIDIOUTCAPS caps, uint size);
  [DllImport("winmm.dll")] public static extern uint midiOutOpen(out IntPtr h, uint id, IntPtr cb, IntPtr inst, uint flags);
  [DllImport("winmm.dll")] public static extern uint midiOutShortMsg(IntPtr h, uint msg);
  [DllImport("winmm.dll")] public static extern uint midiOutPrepareHeader(IntPtr h, IntPtr hdr, uint size);
  [DllImport("winmm.dll")] public static extern uint midiOutLongMsg(IntPtr h, IntPtr hdr, uint size);
  [DllImport("winmm.dll")] public static extern uint midiOutUnprepareHeader(IntPtr h, IntPtr hdr, uint size);
  [DllImport("winmm.dll")] public static extern uint midiOutClose(IntPtr h);
  public static uint Find(string name) { for (uint i = 0; i < midiOutGetNumDevs(); i++) { var c = new MIDIOUTCAPS(); midiOutGetDevCaps((UIntPtr)i, ref c, (uint)Marshal.SizeOf(c)); if (c.szPname == name) return i; } return 0xFFFFFFFF; }
  public static uint SysEx(IntPtr h, byte[] d) { var buf = Marshal.AllocHGlobal(d.Length); Marshal.Copy(d, 0, buf, d.Length); var hdr = new MIDIHDR { lpData = buf, dwBufferLength = (uint)d.Length, dwBytesRecorded = (uint)d.Length, dwReserved = new IntPtr[8] }; int sz = Marshal.SizeOf(hdr); var p = Marshal.AllocHGlobal(sz); Marshal.StructureToPtr(hdr, p, false); midiOutPrepareHeader(h, p, (uint)sz); uint r = midiOutLongMsg(h, p, (uint)sz); System.Threading.Thread.Sleep(200); midiOutUnprepareHeader(h, p, (uint)sz); return r; }
}
"@
Add-Type $src
$id = [WinMM]::Find("Virus TI"); "port id $id"
$h = [IntPtr]::Zero; "open " + [WinMM]::midiOutOpen([ref]$h, $id, [IntPtr]::Zero, [IntPtr]::Zero, 0)
"note on  " + [WinMM]::midiOutShortMsg($h, 0x643C90)   # ch1 note 60 vel 100
Start-Sleep -Milliseconds 800
"note off " + [WinMM]::midiOutShortMsg($h, 0x003C80)
"sysex    " + [WinMM]::SysEx($h, [byte[]](0xF0,0x7E,0x7F,0x06,0x01,0xF7))   # identity request
Start-Sleep 1
"close " + [WinMM]::midiOutClose($h)
