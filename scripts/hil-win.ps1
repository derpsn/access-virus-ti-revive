# hil-win.ps1 - the automated hardware run of scripts/hil.sh on Windows (findings/testing.md): the installed plug-in
# in nativehost (no DAW), C4 every second on part 1, Out 1 recorded, graded by hil_check.py -folder. Covers WC1 (cold
# start of the broker), WC2 (reopen after the broker's idle exit, twice), WC3 (export, paced to the Virus, playback
# after it) and H6 (the broker killed while playing; on Windows a hard kill, like a crash). Not covered: a DAW, the
# editor. Start it with hil-win.cmd (double-click) in the desktop session: the broker's shared memory is per session,
# a run over ssh would start a broker the session's plug-in cannot see (E047).
# Needs next to it: nativehost.exe, hil_check.py, timing_analysis.py (the CI artifact TI-Revive-hil-windows).
# Rule 2: sends only what the plug-in sends in a DAW (start-up, the part states, notes), never stores anything.
param([string]$Plugin = "$env:CommonProgramFiles\VST3\TI Revive Virus Control.vst3", [string]$Out = "")
$ErrorActionPreference = 'Continue'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$hostExe = Join-Path $here 'nativehost.exe'
if (-not $Out) { $Out = Join-Path $here ('results-' + (Get-Date -Format 'yyyyMMdd-HHmmss')) }
$script:fails = 0

if (-not (Test-Path $hostExe)) { Write-Host "no nativehost.exe next to this script ($here)"; exit 2 }
if (-not (Test-Path $Plugin)) { Write-Host "TI Revive is not installed ($Plugin missing)"; exit 2 }
if (-not (Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue | Where-Object InstanceId -match 'VID_133E')) {
    Write-Host 'no Virus TI on USB'; exit 2
}
# the broker serves one owner: a DAW with the plug-in would make this run wait (and the run would take its Virus)
$busy = Get-Process -ErrorAction SilentlyContinue |
    Where-Object { $_.ProcessName -match '^(Ableton Live.*|reaper|Bitwig Studio|Cubase.*|FL64|Studio One|nativehost|TIReviveControlCenter)$' }
if ($busy) { Write-Host "close first: $(($busy.ProcessName | Sort-Object -Unique) -join ', ')"; exit 2 }
New-Item -ItemType Directory -Force $Out | Out-Null
$summary = Join-Path $Out 'summary.txt'
function Say([string]$s) { Write-Host $s; Add-Content -Path $summary -Value $s }

# the broker exits 30 s after its last client; the plug-in starts it again on the next connect
function BrokerGone {
    for ($t = 0; Get-Process vtibroker -ErrorAction SilentlyContinue; $t += 2) {
        if ($t -ge 120) { Say '  broker still running after 120 s'; return $false }
        Start-Sleep 2
    }
    return $true
}

# StartItem NAME SECONDS ARGS: play and record; WaitItem: wait for it (killed after SECONDS + 60)
function StartItem([string]$name, [int]$secs, [string]$extra = '') {
    $a = "`"$Plugin`" --notes --seconds $secs --wav `"$Out\$name.wav`" $extra"
    $p = Start-Process -FilePath $hostExe -ArgumentList $a -NoNewWindow -PassThru `
        -RedirectStandardOutput "$Out\$name.out" -RedirectStandardError "$Out\$name.err"
    $null = $p.Handle # Windows PowerShell 5.1 keeps the exit code only for a process whose handle was taken
    $p
}
function WaitItem($p, [int]$secs) {
    if (-not $p.WaitForExit(($secs + 60) * 1000)) { $p.Kill(); Say '  nativehost hung: killed'; return $false }
    if ($p.ExitCode -ne 0) { Say "  nativehost exit $($p.ExitCode)"; return $false }
    return $true
}
function RunItem([string]$name, [int]$secs, [string]$extra = '') {
    if (-not (WaitItem (StartItem $name $secs $extra) $secs)) { $script:fails++ }
}

$so = (Get-Item (Join-Path $Plugin 'Contents\x86_64-win\*.vst3') -ErrorAction SilentlyContinue | Select-Object -First 1)
Say "TI Revive hardware run (Windows) $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss'), $([Environment]::OSVersion.VersionString)"
Say "plug-in $(if ($so) { $so.LastWriteTime.ToString('yyyy-MM-dd HH:mm') }), results in $Out"
Say 'about 5 minutes; keep the Virus on and this PC awake'

Say 'H1 cold start of the broker'
if (BrokerGone) { RunItem H1 20 } else { $script:fails++ }
Say "H2.1 reopen after the broker's idle exit"
if (BrokerGone) { RunItem H2-1 20 } else { $script:fails++ }
Say "H2.2 reopen after the broker's idle exit"
if (BrokerGone) { RunItem H2-2 20 } else { $script:fails++ }
Say 'H3 export: offline from 12 s to 36 s, stopped until 40 s, playback to 54 s'
RunItem H3 54 '--offline 12 --offline-end 36 --notes-off 36:40'
Say 'H6 broker killed at 20 s while playing, playback to 60 s'
$p = StartItem H6 60
Start-Sleep 20
Stop-Process -Name vtibroker -Force -ErrorAction SilentlyContinue
if (-not (WaitItem $p 60)) { $script:fails++ }

Say ''
$py = $null
foreach ($c in 'py', 'python3', 'python') {
    # the Store's python.exe alias prints nothing and opens the Store: only a real Python answers
    try { if ((& $c -c 'print(42)' 2>$null) -eq '42') { $py = $c; break } } catch { }
}
if ($py) {
    $r = & $py -I (Join-Path $here 'hil_check.py') --folder $Out
    $r | ForEach-Object { Say $_ }
    if ($LASTEXITCODE -ne 0) { $script:fails++ }
} else {
    Say "no Python here: send the folder $Out to be graded"
}
Say "$(if ($script:fails -eq 0) { 'done, nothing failed' } else { "$($script:fails) step(s) failed" }) - results in $Out"
exit ([int]($script:fails -gt 0))
