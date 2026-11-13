# tirevive-driver.ps1 install|uninstall - driver part of the TI Revive MSI (deferred custom action, SYSTEM).
# install:   bind Windows' own WinUSB driver to the Virus TI MIDI interfaces (vti_winusb.inf, no kernel code of ours):
#            self-signed code-signing cert -> catalog for the INF -> signature -> cert's PUBLIC part trusted in
#            LocalMachine Root + TrustedPublisher -> the private key is deleted (cannot sign anything else) ->
#            pnputil /add-driver /install. Removes Access driver packages (blocked on Win11 anyway).
# uninstall: pnputil /delete-driver for our package, remove the trusted cert.
# State: HKLM\SOFTWARE\TI Revive (DriverPackage, CertThumbprint). Log: %ProgramData%\TI Revive\driver.log
param([ValidateSet('install', 'uninstall')][string]$Action = 'install')
$ErrorActionPreference = 'Continue'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$logDir = Join-Path $env:ProgramData 'TI Revive'
New-Item -ItemType Directory -Force $logDir | Out-Null
Start-Transcript -Path (Join-Path $logDir 'driver.log') -Append | Out-Null
$reg = 'HKLM:\SOFTWARE\TI Revive'
# NOT New-Item -Force on every run: on an existing key it recreates it empty, which wiped DriverPackage /
# CertThumbprint before uninstall could read them (0.1.0 left the driver package and cert behind, E063)
if ($Action -eq 'install' -and -not (Test-Path $reg)) { New-Item -Path $reg | Out-Null }

function Trust-Store([string]$name, $cert, [switch]$Remove) {
    $s = New-Object System.Security.Cryptography.X509Certificates.X509Store($name, 'LocalMachine')
    $s.Open('ReadWrite')
    if ($Remove) { $s.Certificates | Where-Object Thumbprint -eq $cert | ForEach-Object { $s.Remove($_) } }
    else { $s.Add($cert) }
    $s.Close()
}

if ($Action -eq 'install') {
    . (Join-Path $here 'tirevive-virusstate.ps1')
    # a Virus connected on Access' drivers: ask to unplug it before those drivers are removed. Unloading them with the
    # Virus present crashed Windows (0xCE in vtimidi.sys) in issue #31 (VirtualBox; real PCs not known). If it stays
    # connected (nobody logged in, silent install, no reaction in 2 minutes), carry on as before.
    $wasBound = @(Get-AccessBoundVirus (Get-VirusDevices)).Count -gt 0 # @(): one item has no .Count in PowerShell 5.1
    if ($wasBound) {
        "a Virus is connected on the Access driver: asking to unplug it (issue #31)"
        $shown = Show-UserMessage 'TI Revive setup' ("Please unplug the Virus TI's USB cable now.`n`n" +
            "Setup removes the old Access drivers and continues by itself once the Virus is unplugged. " +
            "Afterwards plug it back in, and switch the Virus off and on before you use it with TI Revive.") 120
        "message shown: $shown"
        if (Wait-Until { @(Get-AccessBoundVirus (Get-VirusDevices)).Count -eq 0 } 120 1) { "Virus unplugged" }
        else { "WARNING: the Virus is still connected on the Access driver; removing the Access packages anyway" }
    }
    # Access driver packages would be preferred for the device and are blocked by Code Integrity on Win11.
    $blocks = (pnputil /enum-drivers | Out-String) -split "(\r?\n){2,}"
    foreach ($b in $blocks) {
        # the package name first: a later -match replaces $Matches, and the Access pattern has no group, so $Matches[1]
        # was empty and pnputil ran without a package: the Access driver stayed (issue #31, fix by vmunoz82)
        if ($b -match '(oem\d+\.inf)') {
            $pkg = $Matches[1]
            if ($b -match '(?i)virususb\.inf|vtimidi\.inf|vtiaudio\.inf') {
                "removing Access driver package $pkg"
                pnputil /delete-driver $pkg /uninstall /force | Out-String
            }
        }
    }
    $stage = Join-Path $env:TEMP ('tirevive-' + [guid]::NewGuid())
    New-Item -ItemType Directory $stage | Out-Null
    Copy-Item (Join-Path $here 'vti_winusb.inf') $stage
    $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject 'CN=TI Revive local driver package' `
        -CertStoreLocation Cert:\LocalMachine\My -NotAfter (Get-Date).AddYears(20) -KeyExportPolicy NonExportable
    # trust the public part first, so the signature status below is "Valid" (not "UnknownError")
    $pub = New-Object System.Security.Cryptography.X509Certificates.X509Certificate2(, $cert.Export('Cert'))
    Trust-Store 'Root' $pub
    Trust-Store 'TrustedPublisher' $pub
    New-FileCatalog -Path $stage -CatalogFilePath (Join-Path $stage 'vti_winusb.cat') -CatalogVersion 2.0 | Out-Null
    $sig = Set-AuthenticodeSignature -FilePath (Join-Path $stage 'vti_winusb.cat') -Certificate $cert -HashAlgorithm SHA256
    "catalog signature: $($sig.Status)"
    Remove-Item "Cert:\LocalMachine\My\$($cert.Thumbprint)" -DeleteKey # private key gone: signs nothing else
    Set-ItemProperty $reg CertThumbprint $cert.Thumbprint
    $out = pnputil /add-driver (Join-Path $stage 'vti_winusb.inf') /install | Out-String
    $out
    if ($out -match '(oem\d+\.inf)') { Set-ItemProperty $reg DriverPackage $Matches[1] }
    # a Virus still running on the Access USB driver (service VIRUSUSB, e.g. Access software installed or repaired
    # after TI Revive) keeps it until the device is re-detected: remove those device nodes, the rescan below brings
    # the Virus back on the generic composite driver with our WinUSB interfaces (issue #3, Snow)
    $connected = @(Get-VirusDevices | Where-Object Present).Count -gt 0 # before the nodes vanish for the re-detection
    Get-PnpDevice -ErrorAction SilentlyContinue | Where-Object InstanceId -match 'VID_133E' | ForEach-Object {
        $svc = (Get-PnpDeviceProperty -InstanceId $_.InstanceId -KeyName 'DEVPKEY_Device_Service' -ErrorAction SilentlyContinue).Data
        if ($svc -eq 'VIRUSUSB') {
            "re-detecting $($_.InstanceId) (was on the Access USB driver)"
            pnputil /remove-device $_.InstanceId | Out-String
        }
    }
    pnputil /scan-devices | Out-Null
    # what the Virus ended up on, so that driver.log shows whether the switch worked. A connected Virus can take more
    # than 3 s to come back after the re-detection (#31 run 2: no "now:" line although it was on VIRUSUSB again), so
    # wait for it, up to 15 s; an unplugged one is not waited for.
    if ($connected -and -not (Wait-Until { Test-VirusDetected (Get-VirusDevices) } 15 1)) {
        "WARNING: the Virus was not detected again within 15 s"
    }
    Start-Sleep -Seconds 1 # its interfaces follow the device node
    $now = @(Get-VirusDevices | Where-Object Present)
    if (-not $now) { "now: no Virus connected (plug it in after setup)" }
    foreach ($d in $now) {
        "now: $($d.InstanceId) -> $($d.Service)"
        if ($d.Service -eq 'VIRUSUSB') { "WARNING: the Virus is still on the Access USB driver" }
    }
    Remove-Item $stage -Recurse -Force
    & (Join-Path $here 'tirevive-shortcuts.ps1') uninstall # 0.8.0: no Control Center launcher - shortcuts of 0.7.x back to Access'
} else {
    & (Join-Path $here 'tirevive-shortcuts.ps1') uninstall
    $p = Get-ItemProperty $reg -ErrorAction SilentlyContinue
    Get-Process vtibroker -ErrorAction SilentlyContinue | Stop-Process -Force
    # driver package(s): the recorded one plus any vti_winusb.inf package found in the driver store
    $pkgs = @($p.DriverPackage) + @(((pnputil /enum-drivers | Out-String) -split "(\r?\n){2,}") |
        Where-Object { $_ -match '(?i)vti_winusb\.inf' -and $_ -match '(oem\d+\.inf)' } | ForEach-Object { $Matches[1] })
    foreach ($pkg in ($pkgs | Where-Object { $_ } | Select-Object -Unique)) {
        "removing driver package $pkg"
        pnputil /delete-driver $pkg /uninstall /force | Out-String
    }
    # certificates: the recorded thumbprint plus any "TI Revive local driver package" cert
    foreach ($store in 'Root', 'TrustedPublisher') {
        $thumbs = @($p.CertThumbprint) + @(Get-ChildItem "Cert:\LocalMachine\$store" |
            Where-Object Subject -eq 'CN=TI Revive local driver package' | ForEach-Object Thumbprint)
        foreach ($t in ($thumbs | Where-Object { $_ } | Select-Object -Unique)) {
            "removing certificate $t from $store"
            Trust-Store $store $t -Remove
        }
    }
    pnputil /scan-devices | Out-Null
    Remove-Item $reg -Recurse -Force -ErrorAction SilentlyContinue
}
Stop-Transcript | Out-Null
exit 0
