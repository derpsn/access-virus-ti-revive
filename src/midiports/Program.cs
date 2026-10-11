// vtimidiports - feasibility prototype: create a Windows MIDI Services virtual device "Virus TI" with two function
// blocks ("Virus TI Synth" = group 1, "Virus TI MIDI" = group 2) so Windows offers MIDI 1.0 ports for them, and
// log what MIDI programs send. Relaying to the synth through vtibroker comes next (findings/design-midi-ports.md).
// usage: vtimidiports [seconds]
using Windows.Devices.Midi2;
using Windows.Devices.Midi2.Enumeration;
using Windows.Devices.Midi2.Transports.Virtual;

int seconds = args.Length > 0 ? int.Parse(args[0]) : 30;

if (!MidiApi.EnsureServiceAvailable())
{
    Console.WriteLine("Windows MIDI Services not available (service missing or could not be started).");
    return 1;
}
Console.WriteLine($"virtual device transport available: {MidiVirtualDeviceManager.IsTransportAvailable}");

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
if (device == null)
{
    Console.WriteLine("CreateVirtualDevice failed.");
    return 1;
}
var conn = session.CreateEndpointConnection(device.DeviceEndpointDeviceId);
if (conn == null || conn.AddMessageProcessingPlugin(device) != MidiMessageProcessingPluginAddResult.Succeeded)
{
    Console.WriteLine("Could not attach the virtual device to its connection.");
    return 1;
}
conn.MessageReceived += (s, e) =>
{
    Console.WriteLine($"rx {e.MessageType}: {e.PeekFirstWord():X8}");
};
if (!conn.Open())
{
    Console.WriteLine("Could not open the device-side connection.");
    return 1;
}
Console.WriteLine($"virtual device up: {device.DeviceEndpointDeviceId}");
Console.WriteLine($"client endpoint: {MidiVirtualDeviceManager.GetAssociatedClientEndpointDeviceId(device.AssociationId)}");
Thread.Sleep(seconds * 1000);
conn.RemoveMessageProcessingPlugin(device.PluginId);
return 0;
