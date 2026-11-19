# build.ps1 - build "TI Revive.msi" with WiX 5.
#   -Bin     folder with vtibroker.exe
#   -Src     repository src\ folder (inf\, installer\)
#   -Out     output folder
#   -Version MSI ProductVersion (major.minor.build)
# Used by the GitHub workflow (.github/workflows/build.yml) and by scripts/win11/build-msi.sh (build VM, where
# .NET and WiX live in C:\build\dotnet and C:\build\wix).
param([string]$Root = 'C:\build\tirevive', [string]$Bin, [string]$Src, [string]$Out, [string]$Version = '0.1.1',
      [switch]$Bundle,
      [string]$NativeBin) # -NativeBin: folder with "TI Revive.vst3\" + VirusTINative_ASIO.dll + tirevive-cc.exe + TIReviveControlCenter.exe (native plug-in) # -Bundle: also TI Revive setup.exe around the MSI (TIReviveSetup.wxs, needs WixToolset.BootstrapperApplications.wixext)
$ErrorActionPreference = 'Stop'
if (-not $Bin) { $Bin = Join-Path $Root 'bin' }
if (-not $Src) { $Src = Join-Path $Root 'src' }
if (-not $Out) { $Out = Join-Path $Root 'out' }
if (Test-Path 'C:\build\dotnet') { # build VM layout
    $env:DOTNET_ROOT = 'C:\build\dotnet'
    $env:PATH = "C:\build\dotnet;C:\build\wix;$env:PATH"
}
$env:DOTNET_CLI_TELEMETRY_OPTOUT = '1'
New-Item -ItemType Directory -Force $Out | Out-Null
$sources = @(Join-Path $Src 'installer\TIRevive.wxs')
$native = '0'
if ($NativeBin) { # native/packaging/windows/native.wxs next to src\ (repository layout)
    $sources += Join-Path (Split-Path $Src -Parent) 'native\packaging\windows\native.wxs'
    $native = '1'
}
wix build @sources -arch x64 `
    -ext WixToolset.UI.wixext -ext WixToolset.Util.wixext `
    -d "Bin=$Bin" -d "Src=$Src" -d "Version=$Version" -d "Native=$native" -d "NativeBin=$NativeBin" `
    -o (Join-Path $Out 'TI Revive.msi')
if ($LASTEXITCODE) { throw "wix build failed ($LASTEXITCODE)" }
Get-Item (Join-Path $Out 'TI Revive.msi') | Select-Object Name, Length
if ($Bundle) {
    wix build (Join-Path $Src 'installer\TIReviveSetup.wxs') -arch x64 -ext WixToolset.BootstrapperApplications.wixext `
        -d "Msi=$(Join-Path $Out 'TI Revive.msi')" -d "Version=$Version" `
        -o (Join-Path $Out 'TI Revive setup.exe')
    if ($LASTEXITCODE) { throw "wix build (bundle) failed ($LASTEXITCODE)" }
    Get-Item (Join-Path $Out 'TI Revive setup.exe') | Select-Object Name, Length
}
