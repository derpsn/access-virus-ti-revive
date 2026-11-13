# tirevive-virusstate.ps1 - what the Virus TI is bound to during the driver step, dot-sourced by tirevive-driver.ps1.
# Why (issue #31, Windows 10 in VirtualBox, Snow): with the Virus connected on Access' drivers, unloading them crashed
# Windows (bug check 0xCE in vtimidi.sys) during our install, an Access repair and a plain unplug; and the "now:" check
# ran before Windows had detected the Virus again, so driver.log missed that it came back on VIRUSUSB.
# Plain functions on plain objects (InstanceId, Present, Service), so Pester tests them without a device.

# the Access driver services a Virus node can be on (the device itself VIRUSUSB, the MIDI interface vtimidi)
$script:AccessServices = 'VIRUSUSB', 'vtimidi', 'vtiaudio'

# present Virus nodes (VID 133E) that run on an Access driver
function Get-AccessBoundVirus($Devices) {
    @($Devices | Where-Object {
            $_.Present -and $_.InstanceId -match 'VID_133E' -and $_.Service -and ($script:AccessServices -contains $_.Service)
        })
}

# the Virus is back after a re-detection: its device node (not an interface, no MI_) is present and has a driver
function Test-VirusDetected($Devices) {
    @($Devices | Where-Object {
            $_.Present -and $_.InstanceId -match 'VID_133E&PID_[0-9A-F]{4}\\' -and $_.Service
        }).Count -gt 0
}

# polls $Condition every $StepSeconds until it is true or $TimeoutSeconds have passed; returns whether it came true
function Wait-Until([scriptblock]$Condition, [int]$TimeoutSeconds, [double]$StepSeconds = 1) {
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ($true) {
        if (& $Condition) { return $true }
        if ((Get-Date) -ge $deadline) { return $false }
        Start-Sleep -Milliseconds ([int]($StepSeconds * 1000))
    }
}

# the Virus nodes as plain objects (Windows only)
function Get-VirusDevices {
    @(Get-PnpDevice -ErrorAction SilentlyContinue | Where-Object InstanceId -match 'VID_133E' | ForEach-Object {
            [pscustomobject]@{
                InstanceId = $_.InstanceId
                Present    = [bool]$_.Present
                Service    = (Get-PnpDeviceProperty -InstanceId $_.InstanceId -KeyName 'DEVPKEY_Device_Service' `
                        -ErrorAction SilentlyContinue).Data
            }
        })
}

# a message box on the logged-in user's desktop from the SYSTEM custom action (WTSSendMessage; msg.exe is missing on
# Windows Home). It does not wait for the click: the caller polls the device. Returns $false when no one is logged in.
function Show-UserMessage([string]$Title, [string]$Text, [int]$TimeoutSeconds) {
    if (-not ('TIRevive.Wts' -as [type])) {
        Add-Type -Namespace TIRevive -Name Wts -MemberDefinition @'
[DllImport("kernel32.dll")] public static extern uint WTSGetActiveConsoleSessionId();
[DllImport("wtsapi32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
public static extern bool WTSSendMessage(IntPtr server, uint session, string title, uint titleLength, string message,
    uint messageLength, uint style, uint timeout, out uint response, bool wait);
'@
    }
    $session = [TIRevive.Wts]::WTSGetActiveConsoleSessionId()
    if ($session -eq [uint32]::MaxValue) { return $false } # no one logged in at the console
    [uint32]$r = 0 # the "out uint" response (unused: we do not wait for the click)
    # MB_OK | MB_ICONINFORMATION | MB_TOPMOST | MB_SETFOREGROUND
    [TIRevive.Wts]::WTSSendMessage([IntPtr]::Zero, $session, $Title, [uint32]($Title.Length * 2), $Text,
        [uint32]($Text.Length * 2), 0x00050040, [uint32]$TimeoutSeconds, [ref]$r, $false)
}
