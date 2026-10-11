// vtimidiports - Windows MIDI ports for the Virus TI without a kernel driver (findings/design-midi-ports.md, E089).
//
// Creates a Windows MIDI Services virtual device "Virus TI" with two function blocks; Windows offers MIDI 1.0
// (WinMM) ports for them. Traffic is relayed through vtibroker (it owns the Virus' vendor interface MI_03) like
// the Access driver's vtimidi.sys did:
//   group 1 <-> USB-MIDI cable 1 on MI_03 = "Virus TI Synth" (the synth engine)
//   group 2 <-> USB-MIDI cable 0 on MI_03 = "Virus TI MIDI"  (the Virus' MIDI In/Out sockets)
// Port -> Virus: UMP (MIDI 1.0 channel voice, system, SysEx7) -> MIDI 1.0 bytes -> USB-MIDI packets on EP 0x05.
// Virus -> port: USB-MIDI packets from EP 0x85 (broker RX) -> UMP to the matching group.
// usage: vtimidiports [seconds]   (0 or none: run until killed)
using System.IO.Pipes;
using Microsoft.Win32;
using Windows.Devices.Midi2;
using Windows.Devices.Midi2.Enumeration;
using Windows.Devices.Midi2.Transports.Virtual;

int seconds = args.Length > 0 ? int.Parse(args[0]) : 0;
void Log(string s) { Console.WriteLine($"{DateTime.Now:HH:mm:ss.fff} {s}"); Console.Out.Flush(); }

if (!MidiApi.EnsureServiceAvailable())
{
    Log("Windows MIDI Services not available (service missing or could not be started).");
    return 1;
}

// ------------------------------------------------------------------------------------------- broker connection
var broker = Broker.Connect(Log);
if (broker == null)
    return 1;

// ------------------------------------------------------------------------------------------- virtual device
var info = new MidiDeclaredEndpointInfo
{
    Name = "Virus TI",
    ProductInstanceId = "TIREVIVE-VIRUSTI",
    SpecificationVersionMajor = 1,
    SpecificationVersionMinor = 1,
    SupportsMidi10Protocol = true,
    SupportsMidi20Protocol = false,
    HasStaticFunctionBlocks = true
};
var config = new MidiVirtualDeviceCreationConfig("Virus TI", "Access Virus TI (TI Revive)", "TI Revive", info);
config.CreateOnlyUmpEndpoints = false; // also MIDI 1.0 (WinMM) ports
string[] names = { "Virus TI Synth", "Virus TI MIDI" };
byte[] groupToCable = { 1, 0 }; // group index -> MI_03 cable
for (byte i = 0; i < names.Length; i++)
{
    config.FunctionBlocks.Add(new MidiFunctionBlock
    {
        Number = i,
        Name = names[i],
        IsActive = true,
        Direction = MidiFunctionBlockDirection.Bidirectional,
        UIHint = MidiFunctionBlockUIHint.Bidirectional,
        RepresentsMidi10Connection = MidiFunctionBlockRepresentsMidi10Connection.YesBandwidthUnrestricted,
        FirstGroup = new MidiGroup(i),
        GroupCount = 1
    });
}

using var session = MidiSession.Create("TI Revive MIDI ports");
var device = MidiVirtualDeviceManager.CreateVirtualDevice(config);
var conn = device == null ? null : session.CreateEndpointConnection(device.DeviceEndpointDeviceId);
if (device == null || conn == null || conn.AddMessageProcessingPlugin(device) != MidiMessageProcessingPluginAddResult.Succeeded)
{
    Log("Could not create the virtual device.");
    return 1;
}
device.SuppressHandledMessages = true; // endpoint discovery is answered by the device, not relayed

// Port -> Virus
var sysexOut = new List<byte>[16];
conn.MessageReceived += (s, e) =>
{
    uint w0 = 0, w1 = 0, w2 = 0, w3 = 0;
    e.FillWords(out w0, out w1, out w2, out w3);
    int type = (int)(w0 >> 28), group = (int)((w0 >> 24) & 0xF);
    if (group >= groupToCable.Length)
        return;
    byte cable = groupToCable[group];
    var midi = new List<byte>();
    switch (type)
    {
    case 1: // system common / real time
    case 2: // MIDI 1.0 channel voice
        {
            byte st = (byte)(w0 >> 16), d1 = (byte)((w0 >> 8) & 0x7F), d2 = (byte)(w0 & 0x7F);
            int n = Midi.Length(st);
            midi.Add(st);
            if (n > 1) midi.Add(d1);
            if (n > 2) midi.Add(d2);
            broker.Write(0x05, UsbMidi.Pack(cable, midi));
            break;
        }
    case 3: // SysEx7, up to 6 bytes per packet
        {
            int status = (int)((w0 >> 20) & 0xF), count = (int)((w0 >> 16) & 0xF);
            byte[] d = { (byte)(w0 >> 8), (byte)w0, (byte)(w1 >> 24), (byte)(w1 >> 16), (byte)(w1 >> 8), (byte)w1 };
            var buf = sysexOut[group] ??= new List<byte>();
            if (status == 0 || status == 1) { buf.Clear(); buf.Add(0xF0); }
            for (int i = 0; i < count && i < 6; i++) buf.Add((byte)(d[i] & 0x7F));
            if (status == 0 || status == 3)
            {
                buf.Add(0xF7);
                broker.Write(0x05, UsbMidi.Pack(cable, buf));
                buf.Clear();
            }
            break;
        }
    }
};

// Virus -> port
var sysexIn = new List<byte>[16];
broker.Received = (ep, data) =>
{
    if (ep != 0x85)
        return;
    for (int i = 0; i + 3 < data.Length; i += 4)
    {
        int cable = data[i] >> 4, cin = data[i] & 0xF;
        int group = Array.IndexOf(groupToCable, (byte)cable);
        if (group < 0 || data[i] == 0)
            continue;
        uint g = (uint)group << 24;
        if (cin >= 0x8 && cin <= 0xE) // channel voice
            conn.SendSingleMessageWords(0, 0x20000000u | g | (uint)data[i + 1] << 16 | (uint)data[i + 2] << 8 | data[i + 3]);
        else if (cin == 0xF || ((cin == 2 || cin == 3 || cin == 5) && data[i + 1] != 0xF0 && data[i + 1] != 0xF7 &&
                                !(sysexIn[group]?.Count > 0)))
            conn.SendSingleMessageWords(0, 0x10000000u | g | (uint)data[i + 1] << 16 | (uint)data[i + 2] << 8 | data[i + 3]);
        else if (cin >= 4 && cin <= 7) // SysEx: 4 = 3 bytes continue, 5/6/7 = end with 1/2/3 bytes
        {
            var buf = sysexIn[group] ??= new List<byte>();
            int n = cin == 4 ? 3 : cin - 4;
            for (int k = 1; k <= n; k++)
                buf.Add(data[i + k]);
            if (cin != 4)
            {
                SendSysEx(conn, g, buf);
                buf.Clear();
            }
        }
    }
};

if (!conn.Open())
{
    Log("Could not open the device-side connection.");
    return 1;
}
Log("MIDI ports up: \"Virus TI\" (Synth = group 1, MIDI = group 2)");
if (seconds > 0)
    Thread.Sleep(seconds * 1000);
else
    Thread.Sleep(Timeout.Infinite);
conn.RemoveMessageProcessingPlugin(device.PluginId);
broker.Dispose();
return 0;

static void SendSysEx(MidiEndpointConnection conn, uint g, List<byte> msg)
{
    // strip F0 / F7, send SysEx7 UMP packets of up to 6 bytes
    int start = msg.Count > 0 && msg[0] == 0xF0 ? 1 : 0, end = msg.Count > 0 && msg[^1] == 0xF7 ? msg.Count - 1 : msg.Count;
    int total = end - start;
    for (int off = 0; off < total || (total == 0 && off == 0); off += 6)
    {
        int n = Math.Min(6, total - off);
        int status = total <= 6 ? 0 : off == 0 ? 1 : off + 6 >= total ? 3 : 2;
        byte[] d = new byte[6];
        for (int k = 0; k < n; k++)
            d[k] = msg[start + off + k];
        uint w0 = 0x30000000u | g | (uint)status << 20 | (uint)n << 16 | (uint)d[0] << 8 | d[1];
        uint w1 = (uint)d[2] << 24 | (uint)d[3] << 16 | (uint)d[4] << 8 | d[5];
        conn.SendSingleMessageWords(0, w0, w1);
        if (total == 0)
            break;
    }
}

static class Midi
{
    // Bytes of a MIDI 1.0 message by status byte (channel voice, system common, real time)
    public static int Length(byte st) => (st & 0xF0) switch
    {
        0xC0 or 0xD0 => 2,
        0xF0 => st switch { 0xF1 or 0xF3 => 2, 0xF2 => 3, _ => 1 },
        _ => 3
    };
}

static class UsbMidi
{
    // MIDI 1.0 bytes of one message (or a complete F0..F7 SysEx) -> USB-MIDI 4-byte event packets on `cable`
    public static byte[] Pack(byte cable, List<byte> m)
    {
        var o = new List<byte>();
        int c = cable << 4;
        if (m.Count > 0 && m[0] == 0xF0)
        {
            int i = 0;
            for (; m.Count - i > 3; i += 3)
                o.AddRange(new byte[] { (byte)(c | 0x4), m[i], m[i + 1], m[i + 2] });
            int r = m.Count - i; // 1..3 bytes, the last is F7
            o.Add((byte)(c | (0x4 + r)));
            for (int k = 0; k < 3; k++)
                o.Add(k < r ? m[i + k] : (byte)0);
            return o.ToArray();
        }
        byte st = m[0];
        int cin = st >= 0xF8 ? 0xF : st >= 0xF0 ? (m.Count == 1 ? 0x5 : m.Count == 2 ? 0x2 : 0x3) : st >> 4;
        o.Add((byte)(c | cin));
        for (int k = 0; k < 3; k++)
            o.Add(k < m.Count ? m[k] : (byte)0);
        return o.ToArray();
    }
}

// vtibroker client (src/broker/vtiproto.h): header {u16 type, u16 len} + payload over \\.\pipe\vtibroker
sealed class Broker : IDisposable
{
    const ushort HELLO = 1, WRITE = 2, RX = 0x81, STATE = 0x84;
    readonly NamedPipeClientStream pipe;
    readonly object wr = new();
    public Action<byte, byte[]>? Received;

    Broker(NamedPipeClientStream p) { pipe = p; }

    public static Broker? Connect(Action<string> log)
    {
        for (int attempt = 0; attempt < 2; attempt++)
        {
            var p = new NamedPipeClientStream(".", "vtibroker", PipeDirection.InOut, PipeOptions.Asynchronous);
            try
            {
                p.Connect(attempt == 0 ? 500 : 5000);
                var b = new Broker(p);
                b.Send(HELLO, BitConverter.GetBytes(2u));
                new Thread(b.ReadLoop) { IsBackground = true, Name = "broker-rx" }.Start();
                log("connected to vtibroker");
                return b;
            }
            catch (TimeoutException)
            {
                p.Dispose();
                if (attempt == 0 && !StartBroker(log))
                    break;
            }
        }
        log("vtibroker not reachable");
        return null;
    }

    static bool StartBroker(Action<string> log)
    {
        var dir = Registry.GetValue(@"HKEY_LOCAL_MACHINE\SOFTWARE\TI Revive", "InstallDir", null) as string;
        var exe = dir == null ? null : Path.Combine(dir, "vtibroker.exe");
        if (exe == null || !File.Exists(exe))
        {
            log("vtibroker.exe not found (TI Revive not installed?)");
            return false;
        }
        System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo(exe) { UseShellExecute = false, CreateNoWindow = true });
        log("started vtibroker");
        return true;
    }

    void Send(ushort type, byte[] payload)
    {
        var h = new byte[4 + payload.Length];
        BitConverter.GetBytes(type).CopyTo(h, 0);
        BitConverter.GetBytes((ushort)payload.Length).CopyTo(h, 2);
        payload.CopyTo(h, 4);
        lock (wr)
            pipe.Write(h, 0, h.Length);
    }

    public void Write(byte ep, byte[] data)
    {
        var p = new byte[1 + data.Length];
        p[0] = ep;
        data.CopyTo(p, 1);
        Send(WRITE, p);
    }

    void ReadLoop()
    {
        var h = new byte[4];
        try
        {
            while (ReadExact(h, 4))
            {
                ushort type = BitConverter.ToUInt16(h, 0), len = BitConverter.ToUInt16(h, 2);
                var p = new byte[len];
                if (!ReadExact(p, len))
                    break;
                if (type == RX && len >= 1)
                    Received?.Invoke(p[0], p[1..]);
                else if (type == STATE && len >= 4)
                    Console.WriteLine($"broker: device {(BitConverter.ToUInt32(p, 0) != 0 ? "connected" : "disconnected")}");
            }
        }
        catch (IOException) { }
        Console.WriteLine("broker connection lost");
    }

    bool ReadExact(byte[] b, int n)
    {
        for (int o = 0; o < n;)
        {
            int r = pipe.Read(b, o, n - o);
            if (r <= 0)
                return false;
            o += r;
        }
        return true;
    }

    public void Dispose() => pipe.Dispose();
}
