# tirevive-bundles.ps1 -Run - remove what earlier TI Revive setup.exe installs left behind (E186).
# A setup.exe (Burn bundle, TIReviveSetup.wxs) registers its own uninstall entry and the MSI as its package. When the
# .msi is later installed directly, its MajorUpgrade replaces the bundle's MSI, but nothing removes the bundle's entry,
# so "TI Revive 0.8.0" stayed in the list next to the new version (also a setup.exe closed at its first dialog leaves
# one). Deferred custom action of the MSI (SYSTEM), only when no setup.exe runs the MSI: Burn removes older bundles
# itself, and the running bundle must keep its own entry.
# Per stale bundle: Uninstall\{id}, Installer\Dependencies\{id}, Package Cache\{id}; and the MSI package providers
# ({ProductCode}_v<version>) only these bundles depended on, with their cached MSI (Package Cache\{ProductCode}v<version>).
# Log: bundles.log in the install folder. Tests: tests/tirevive-bundles.Tests.ps1 (dot-sources this file).
param([switch]$Run)

$BundleUpgradeCode = '{B785EEA0-32B6-4B72-BD0E-62ADA99417BA}' # TIReviveSetup.wxs
$Guid = '\{[0-9A-Fa-f]{8}-(?:[0-9A-Fa-f]{4}-){3}[0-9A-Fa-f]{12}\}'

function Step([string]$kind, [string[]]$name) { [pscustomobject]@{ Kind = $kind; Name = $name } }

# Which keys and folders to remove. $Bundles: {Id, UpgradeCodes}; $Providers: {Name, DisplayName, Dependents}.
# Names are checked against GUID patterns, so a strange entry can never point outside its key or folder.
function Get-StaleBundlePlan($Bundles, $Providers) {
    $stale = @($Bundles | Where-Object { $_.Id -match "^$Guid$" -and @($_.UpgradeCodes) -contains $BundleUpgradeCode } |
        ForEach-Object Id)
    foreach ($id in $stale) { Step Uninstall $id; Step Dependencies $id; Step Cache $id }
    foreach ($p in $Providers) {
        if ($p.Name -notmatch "^($Guid)_v([0-9.]+)$" -or $p.DisplayName -ne 'TI Revive') { continue }
        $cached = "$($Matches[1])v$($Matches[2])"
        $gone = @($p.Dependents | Where-Object { $stale -contains $_ })
        if (-not $gone.Count) { continue }
        if (@($p.Dependents | Where-Object { $stale -notcontains $_ }).Count) {
            # another (still installed) bundle uses this package: only our stale links go
            foreach ($d in $gone) { Step Dependent @($p.Name, 'Dependents', $d) }
        } else { Step Provider $p.Name; Step Cache $cached }
    }
}

function Invoke-StaleBundlePlan($Plan, [string]$UninstallRoot, [string]$DependenciesRoot, [string]$CacheRoot) {
    $roots = @{ Uninstall = $UninstallRoot; Dependencies = $DependenciesRoot; Dependent = $DependenciesRoot
        Provider = $DependenciesRoot; Cache = $CacheRoot }
    foreach ($s in $Plan) {
        $path = $roots[$s.Kind]
        foreach ($part in $s.Name) { $path = Join-Path $path $part }
        if (Test-Path -LiteralPath $path) {
            "removing $path"
            Remove-Item -LiteralPath $path -Recurse -Force -ErrorAction Continue
        }
    }
}

function Get-BundleEntries([string]$UninstallRoot) {
    Get-ChildItem -LiteralPath $UninstallRoot -ErrorAction SilentlyContinue | ForEach-Object {
        $codes = $_.GetValue('BundleUpgradeCode')
        if ($codes) { [pscustomobject]@{ Id = $_.PSChildName; UpgradeCodes = @($codes) } }
    }
}

function Get-ProviderEntries([string]$DependenciesRoot) {
    Get-ChildItem -LiteralPath $DependenciesRoot -ErrorAction SilentlyContinue | ForEach-Object {
        $deps = Get-ChildItem -LiteralPath (Join-Path $_.PSPath 'Dependents') -ErrorAction SilentlyContinue
        [pscustomobject]@{ Name = $_.PSChildName; DisplayName = $_.GetValue('DisplayName')
            Dependents = @($deps | ForEach-Object PSChildName) }
    }
}

if ($Run) {
    $ErrorActionPreference = 'Continue'
    $logDir = $PSScriptRoot # Program Files, not ProgramData: a standard user cannot plant anything here (audit #28)
    Start-Transcript -Path (Join-Path $logDir 'bundles.log') -Append | Out-Null
    $uninstall = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall'
    $deps = 'HKLM:\SOFTWARE\Classes\Installer\Dependencies'
    $plan = @(Get-StaleBundlePlan @(Get-BundleEntries $uninstall) @(Get-ProviderEntries $deps))
    "$($plan.Count) leftovers of earlier setup.exe installs"
    Invoke-StaleBundlePlan $plan $uninstall $deps (Join-Path $env:ProgramData 'Package Cache')
    Stop-Transcript | Out-Null
}
