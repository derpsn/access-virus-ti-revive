# tirevive-usbinfo.ps1 - collect USB information about a connected Access Virus TI (any model: TI, TI2, Snow, Polar,
# Keyboard) for the TI Revive developers. Read-only: changes nothing on the PC.
# Writes "TI-Revive-usbinfo.txt" to your Desktop; please attach that file to a GitHub issue.
#
# Run: right-click the file -> "Run with PowerShell"   (or: powershell -ExecutionPolicy Bypass -File tirevive-usbinfo.ps1)
$ErrorActionPreference = 'Continue'
$out = Join-Path ([Environment]::GetFolderPath('Desktop')) 'TI-Revive-usbinfo.txt'
$lines = New-Object System.Collections.Generic.List[string]
function Add([string]$s) { $lines.Add($s) }

Add "TI Revive USB info - $(Get-Date -Format s)"
Add "Windows: $((Get-CimInstance Win32_OperatingSystem).Caption) $([Environment]::OSVersion.Version)"
$ti = Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*' -ErrorAction SilentlyContinue |
    Where-Object DisplayName -match 'TI Revive|Virus TI' | ForEach-Object { "$($_.DisplayName) $($_.DisplayVersion)" }
Add "Installed: $($ti -join '; ')"
Add ''

# every device node of the Virus (vendor 133E): the composite device and its interfaces (MI_xx)
$devs = Get-PnpDevice -ErrorAction SilentlyContinue | Where-Object InstanceId -match 'VID_133E'
if (-not $devs) {
    Add 'No Access Virus device (USB vendor 133E) found. Is the Virus switched on and connected by USB?'
}
foreach ($d in $devs | Sort-Object InstanceId) {
    Add "== $($d.InstanceId)"
    Add "   Present: $($d.Present)  Status: $($d.Status)  Class: $($d.Class)  Name: $($d.FriendlyName)"
    foreach ($key in 'DEVPKEY_Device_BusReportedDeviceDesc', 'DEVPKEY_Device_HardwareIds', 'DEVPKEY_Device_Service',
                     'DEVPKEY_Device_DriverDesc', 'DEVPKEY_Device_DriverVersion', 'DEVPKEY_Device_DriverInfPath') {
        $p = Get-PnpDeviceProperty -InstanceId $d.InstanceId -KeyName $key -ErrorAction SilentlyContinue
        if ($p -and $p.Data) { Add ("   {0}: {1}" -f ($key -replace 'DEVPKEY_Device_', ''), (($p.Data | ForEach-Object { "$_" }) -join ', ')) }
    }
}

# verdict: TI Revive needs the device on the generic composite driver with MI_00 + MI_04 on WinUSB. The old Access
# kernel driver (service VIRUSUSB, "Virus TI USB 2.9.66") takes the whole device instead, so those interfaces never
# appear (seen with a Snow, issue #3).
Add ''
$present = @($devs | Where-Object Present)
$access = @($present | Where-Object { (Get-PnpDeviceProperty -InstanceId $_.InstanceId -KeyName 'DEVPKEY_Device_Service' -ErrorAction SilentlyContinue).Data -eq 'VIRUSUSB' })
$winusb = @($present | Where-Object { $_.InstanceId -match 'MI_0[04]' -and (Get-PnpDeviceProperty -InstanceId $_.InstanceId -KeyName 'DEVPKEY_Device_Service' -ErrorAction SilentlyContinue).Data -eq 'WINUSB' })
if ($access) {
    $inf = (Get-PnpDeviceProperty -InstanceId $access[0].InstanceId -KeyName 'DEVPKEY_Device_DriverInfPath' -ErrorAction SilentlyContinue).Data
    Add "VERDICT: the Virus is driven by the old Access USB driver ($inf), so TI Revive cannot reach it."
    Add "         Fix (PowerShell as administrator): pnputil /delete-driver $inf /uninstall /force"
    Add '         then unplug the Virus, wait 5 s, plug it in again (or repair TI Revive in Apps > Installed apps).'
} elseif ($winusb.Count -ge 2) {
    Add 'VERDICT: driver setup OK (MI_00 and MI_04 on WinUSB).'
} elseif ($present) {
    Add 'VERDICT: the Virus is present, but MI_00/MI_04 are not on WinUSB - please repair TI Revive and reconnect.'
} elseif ($devs) {
    Add 'VERDICT: no Virus connected right now (only entries Windows remembers) - connect it and run this again.'
}

# the USB device descriptor's release number (bcdDevice) is part of the hardware ID "REV_xxxx"; the product string is
# BusReportedDeviceDesc above. The model (TI / TI2 / Snow) is decided from these two.
Add ''
Add 'TI Revive logs (newest):'
# 0.8+: the plug-in "TI Revive" writes diag.log when diag.on exists in its folder; the broker its own log
$diag = Join-Path $env:LOCALAPPDATA 'virus-ti-native\diag.log'
if (Test-Path $diag) {
    Add "   $diag"
    Select-String -Path $diag -Pattern 'Virus TI native:' |
        Select-Object -Last 15 | ForEach-Object { Add "   $($_.Line.Trim())" }
} elseif (-not (Test-Path (Join-Path $env:LOCALAPPDATA 'virus-ti-native\diag.on'))) {
    Add '   (plug-in log off: create an empty file diag.on in %LOCALAPPDATA%\virus-ti-native, reproduce, run this again)'
}
$blog = Join-Path $env:TEMP 'vtibroker.log'
if (Test-Path $blog) {
    Add "   $blog"
    Select-String -Path $blog -Pattern 'device open|claim|not found|no device|error|failed' |
        Select-Object -Last 10 | ForEach-Object { Add "   $($_.Line.Trim())" }
}
# 0.7.x: the classic plug-in's emulation log
Get-ChildItem $env:TEMP -Filter 'vtiemu-*.log' -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending |
    Select-Object -First 1 | ForEach-Object {
        Add "   $($_.FullName)"
        Select-String -Path $_.FullName -Pattern 'device identity|device open|vtibroker|no device|not found' |
            Select-Object -First 15 | ForEach-Object { Add "   $($_.Line.Trim())" }
    }

$lines | Set-Content -Path $out -Encoding UTF8
$lines | ForEach-Object { Write-Host $_ }
Write-Host ''
Write-Host "Saved to $out - please attach it to a GitHub issue. Press Enter to close."
[void](Read-Host)
