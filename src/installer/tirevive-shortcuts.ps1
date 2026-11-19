# tirevive-shortcuts.ps1 uninstall - put the Access "Virus Control Center" shortcuts of TI Revive 0.7.x back.
# 0.7.x replaced the Access MSI's advertised shortcuts (target ...\Installer\{GUID}\CCIcon.exe, E061) with ones through
# its launcher vtilaunch.exe; since 0.8.0 there is no launcher, so every all-users Start menu / public desktop
# "Virus Control Center*.lnk" (not ours) becomes a plain shortcut to Virus Control Center.exe again.
# Needs admin. Called by tirevive-driver.ps1 (MSI install and uninstall).
param([ValidateSet('uninstall')][string]$Action = 'uninstall')
$cc = Join-Path ${env:ProgramFiles} 'Access Music\Virus TI\Common\Virus Control Center.exe'
if (-not (Test-Path $cc)) { "Access Virus Control Center not installed: nothing to do"; exit 0 }
$ws = New-Object -ComObject WScript.Shell
$roots = (Join-Path $env:ProgramData 'Microsoft\Windows\Start Menu\Programs'), (Join-Path $env:PUBLIC 'Desktop')
Get-ChildItem $roots -Recurse -Filter 'Virus Control Center*.lnk' -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -notmatch '\\TI Revive\\' } | ForEach-Object {
        $path = $_.FullName
        Remove-Item $path -Force
        $l = $ws.CreateShortcut($path)
        $l.TargetPath = $cc
        $l.Arguments = ''
        $l.WorkingDirectory = Split-Path -Parent $cc
        $l.Description = 'Virus Control Center'
        $l.IconLocation = "$cc,0"
        $l.Save()
        "$Action shortcut: $path"
    }
exit 0
