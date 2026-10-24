# tirevive-access.ps1 - provide the files of the user's own Access "Virus TI Software Suite" that TI Revive's plug-in
# reads, WITHOUT installing the Access software (launched from the TI Revive exit dialog / Start menu):
#   Virus Control.dll (skin + device description) and firmware.bin (ROM banks) -> Program Files\Access Music\Virus TI\
#   Common (where the plug-in looks), the manuals -> ...\Virus TI\Documentation, the factory patch libraries ->
#   Documents\Access Music\Virus TI\Patches (only files that are not there yet; the user's own libraries stay).
# The .msi is unpacked (administrative extraction: no custom actions, no drivers, nothing registered). Nothing of Access
# is shipped with TI Revive. A full Access installation (TI Revive 0.7 and older installed it: original plug-in, Access
# ASIO and USB drivers, Control Center) is offered for removal; the files above stay.
# -Quiet: no dialogs (tests): remove a full Access installation, report on stdout.
param([string]$Msi, [string]$SourceDir, [string]$FromMsi, [switch]$Quiet,
      [switch]$Elevated, [string]$Extracted, [switch]$RemoveAccess) # -Elevated: the admin part (one UAC prompt)
# from the setup.exe the folder ends with '\', which escapes the closing quote on the command line: C:\dir"
if ($SourceDir) { $SourceDir = $SourceDir.Trim().Trim('"').TrimEnd('\') }
if ($FromMsi -and -not $SourceDir) { $SourceDir = Split-Path -Parent $FromMsi }
$ErrorActionPreference = 'Stop'
$title = 'TI Revive'
$target = Join-Path $env:ProgramW6432 'Access Music\Virus TI'
$needed = 'Virus Control.dll', 'firmware.bin' # in ...\Common
$optional = 'Virus Control Center.exe'      # ...\Common: the TI Revive Control Center takes its look from it
function Say([string]$text, [string]$icon = 'Information') {
    if ($Quiet) { Write-Host "$icon`: $text"; return }
    Add-Type -AssemblyName System.Windows.Forms
    [System.Windows.Forms.MessageBox]::Show($text, $title, 'OK', $icon) | Out-Null
}
function Ask([string]$text) {
    if ($Quiet) { Write-Host "Question (yes): $text"; return $true }
    Add-Type -AssemblyName System.Windows.Forms
    return [System.Windows.Forms.MessageBox]::Show($text, $title, 'YesNo', 'Question') -eq 'Yes'
}
function AccessProduct {
    Get-ItemProperty HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\*,
                     HKLM:\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\* -ErrorAction SilentlyContinue |
        Where-Object { $_.DisplayName -match '^Virus TI Software Suite' -and $_.Publisher -match 'Access' } | Select-Object -First 1
}
function Is64([string]$file) { # a 64-bit Windows image (PE32+): the plug-in reads only the 64-bit Virus Control.dll (E345)
    try { $b = [byte[]]::new(0x400); $f = [IO.File]::OpenRead($file); $n = $f.Read($b, 0, $b.Length); $f.Close() } catch { return $false }
    if ($n -lt 0x40 -or $b[0] -ne 0x4D -or $b[1] -ne 0x5A) { return $false }
    $pe = [BitConverter]::ToInt32($b, 0x3C)
    return ($pe -gt 0 -and $pe + 26 -le $n -and [BitConverter]::ToUInt16($b, $pe + 24) -eq 0x20B)
}
function HaveFiles {
    -not ($needed | Where-Object { -not (Test-Path (Join-Path $target "Common\$_")) }) -and (Is64 (Join-Path $target 'Common\Virus Control.dll'))
}
# An administrative extract (msiexec /a) puts the x64 and the x86 files into one Common folder and the 32-bit Virus
# Control.dll wins. The 64-bit one is taken from the .msi's embedded cabinet instead: its File table names the cabinet
# entries, Windows' msi.dll reads the stream, expand.exe unpacks the entry.
function Extract64Dll([string]$msiPath, [string]$common) {
    if (-not ('TIReviveMsi' -as [type])) {
        Add-Type -TypeDefinition @'
using System; using System.Collections.Generic; using System.IO; using System.Runtime.InteropServices; using System.Text;
public static class TIReviveMsi {
    [DllImport("msi.dll", CharSet = CharSet.Unicode)] static extern uint MsiOpenDatabase(string path, IntPtr persist, out IntPtr db);
    [DllImport("msi.dll", CharSet = CharSet.Unicode)] static extern uint MsiDatabaseOpenView(IntPtr db, string query, out IntPtr view);
    [DllImport("msi.dll")] static extern uint MsiViewExecute(IntPtr view, IntPtr rec);
    [DllImport("msi.dll")] static extern uint MsiViewFetch(IntPtr view, out IntPtr rec);
    [DllImport("msi.dll", CharSet = CharSet.Unicode)] static extern uint MsiRecordGetString(IntPtr rec, uint field, StringBuilder buf, ref uint size);
    [DllImport("msi.dll")] static extern uint MsiRecordReadStream(IntPtr rec, uint field, byte[] buf, ref uint size);
    [DllImport("msi.dll")] static extern uint MsiCloseHandle(IntPtr h);
    static List<IntPtr> Rows(IntPtr db, string query) {
        var rows = new List<IntPtr>(); IntPtr view, rec;
        if (MsiDatabaseOpenView(db, query, out view) != 0) return rows;
        if (MsiViewExecute(view, IntPtr.Zero) == 0) while (MsiViewFetch(view, out rec) == 0) rows.Add(rec);
        MsiCloseHandle(view);
        return rows;
    }
    static string Str(IntPtr rec, uint field) {
        var sb = new StringBuilder(512); uint n = 512;
        return MsiRecordGetString(rec, field, sb, ref n) == 0 ? sb.ToString() : "";
    }
    // the cabinet entries (File keys) of every file named `name`, and the embedded cabinets saved into `dir`
    public static string[] Extract(string msi, string name, string dir, List<string> cabs) {
        IntPtr db; var keys = new List<string>();
        if (MsiOpenDatabase(msi, IntPtr.Zero, out db) != 0) return keys.ToArray();
        foreach (var r in Rows(db, "SELECT `File`, `FileName` FROM `File`")) {
            string fn = Str(r, 2); int bar = fn.IndexOf('|');
            if (string.Equals(bar >= 0 ? fn.Substring(bar + 1) : fn, name, StringComparison.OrdinalIgnoreCase)) keys.Add(Str(r, 1));
            MsiCloseHandle(r);
        }
        foreach (var m in Rows(db, "SELECT `Cabinet` FROM `Media`")) {
            string cab = Str(m, 1); MsiCloseHandle(m);
            if (!cab.StartsWith("#")) continue; // only embedded cabinets
            foreach (var r in Rows(db, "SELECT `Data` FROM `_Streams` WHERE `Name`='" + cab.Substring(1).Replace("'", "") + "'")) {
                string outPath = Path.Combine(dir, cab.Substring(1));
                using (var f = File.Create(outPath)) {
                    var buf = new byte[1 << 16]; uint n;
                    do { n = (uint)buf.Length; if (MsiRecordReadStream(r, 1, buf, ref n) != 0) break; f.Write(buf, 0, (int)n); } while (n > 0);
                }
                cabs.Add(outPath); MsiCloseHandle(r);
            }
        }
        MsiCloseHandle(db);
        return keys.ToArray();
    }
}
'@
    }
    $work = Join-Path $env:TEMP 'tirevive-access-dll64'
    Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force $work | Out-Null
    try {
        $cabs = New-Object 'System.Collections.Generic.List[string]'
        $keys = [TIReviveMsi]::Extract($msiPath, 'Virus Control.dll', $work, $cabs)
        foreach ($cab in $cabs) {
            foreach ($k in $keys) {
                & "$env:SystemRoot\System32\expand.exe" $cab "-F:$k" $work | Out-Null
                $dll = Join-Path $work $k
                if ((Test-Path $dll) -and (Is64 $dll)) { Copy-Item $dll (Join-Path $common 'Virus Control.dll') -Force; return $true }
            }
        }
        return $false
    } catch { Write-Host "taking the 64-bit Virus Control.dll from the .msi failed: $_"; return $false }
    finally { Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue }
}

# ---- the admin part: remove the Access installation (keeping the files), copy the extracted files -----------------
if ($Elevated) {
    $keep = Join-Path $env:TEMP 'tirevive-access-keep'
    if ($RemoveAccess -and ($p = AccessProduct)) {
        Remove-Item -Recurse -Force $keep -ErrorAction SilentlyContinue
        New-Item -ItemType Directory -Force "$keep\Common" | Out-Null
        foreach ($f in $needed + $optional) { Copy-Item (Join-Path $target "Common\$f") "$keep\Common\" -ErrorAction SilentlyContinue }
        if (Test-Path "$target\Documentation") { Copy-Item -Recurse "$target\Documentation" $keep }
        $r = Start-Process msiexec.exe -ArgumentList "/x $($p.PSChildName) /qb /norestart" -Wait -PassThru
        Write-Host "removing the Access installation: $($r.ExitCode)"
        New-Item -ItemType Directory -Force "$target\Common" | Out-Null
        Copy-Item "$keep\Common\*" "$target\Common\" -Force -ErrorAction SilentlyContinue
        if (Test-Path "$keep\Documentation") { Copy-Item -Recurse -Force "$keep\Documentation" $target }
        Remove-Item -Recurse -Force $keep -ErrorAction SilentlyContinue
    }
    # Access' uninstaller leaves its ASIO entry behind: a dead "Virus USB ASIO driver" in every DAW's device list
    $asio = 'HKLM:\SOFTWARE\ASIO\Virus USB ASIO driver'
    if (Test-Path $asio) {
        $clsid = (Get-ItemProperty $asio -ErrorAction SilentlyContinue).CLSID
        $dll = if ($clsid) { (Get-ItemProperty "HKLM:\SOFTWARE\Classes\CLSID\$clsid\InprocServer32" -ErrorAction SilentlyContinue).'(default)' }
        if (-not $dll -or -not (Test-Path $dll)) { Remove-Item -Recurse -Force $asio; Write-Host 'removed the dead Access ASIO entry' }
    }
    if ($Extracted) {
        $src = Join-Path $Extracted 'Access Music\Virus TI'
        New-Item -ItemType Directory -Force "$target\Common" | Out-Null
        foreach ($f in $needed) { Copy-Item (Join-Path $src "Common\$f") "$target\Common\" -Force }
        foreach ($f in $optional) { Copy-Item (Join-Path $src "Common\$f") "$target\Common\" -Force -ErrorAction SilentlyContinue }
        if (Test-Path "$src\Documentation") { Copy-Item -Recurse -Force "$src\Documentation" $target }
    }
    exit 0
}

# ---- the user part --------------------------------------------------------------------------------------------------
$product = AccessProduct
$remove = $false
if ($product) {
    $remove = Ask ("The full Access Virus TI software is installed (from an earlier TI Revive): the original plug-in " +
        "`"Virus TI`", the Access ASIO and USB drivers and the Control Center. TI Revive no longer uses them.`n`n" +
        "Remove it? The files TI Revive's plug-in needs (skin, ROM banks, manuals) and your patch libraries stay.")
}
$extracted = $null
# set up before 0.8.2, the Control Center's look was never copied (only the needed files decide): unpack the Access
# .msi again when one is at hand, without asking for it (E184)
$needed_missing = -not (HaveFiles)
$look_missing = -not ($optional | Where-Object { Test-Path (Join-Path $target "Common\$_") })
if ($needed_missing -or $look_missing) {
    if (-not $Msi) {
        $dirs = @($SourceDir, (Join-Path $env:USERPROFILE 'Downloads'), (Join-Path $env:USERPROFILE 'Desktop')) | Where-Object { $_ }
        $Msi = $dirs | ForEach-Object { Get-ChildItem $_ -Filter '*Virus TI*.msi' -ErrorAction SilentlyContinue } |
            Where-Object Name -NotMatch 'TI Revive' | Sort-Object LastWriteTime -Descending | Select-Object -First 1 -ExpandProperty FullName
    }
    if (-not $Msi -and -not $Quiet -and $needed_missing) {
        Add-Type -AssemblyName System.Windows.Forms
        $dlg = New-Object System.Windows.Forms.OpenFileDialog
        $dlg.Title = 'Select the Access "Virus TI Software Suite" installer (64-bit .msi)'
        $dlg.Filter = 'Windows Installer (*.msi)|*.msi'
        if ($dlg.ShowDialog() -eq 'OK') { $Msi = $dlg.FileName }
    }
    if (-not $Msi -and $needed_missing) { Say 'No Access "Virus TI Software Suite" .msi selected: nothing was set up.' 'Warning'; exit 1 }
}
if ($Msi -and ($needed_missing -or $look_missing)) {
    $extracted = Join-Path $env:TEMP 'tirevive-access-extract'
    Remove-Item -Recurse -Force $extracted -ErrorAction SilentlyContinue
    for ($i = 0; $i -lt 60; $i++) { # launched from the TI Revive exit dialog: that installation may still hold the mutex
        $p = Start-Process msiexec.exe -ArgumentList "/a `"$Msi`" /qn TARGETDIR=`"$extracted`"" -Wait -PassThru
        if ($p.ExitCode -ne 1618) { break }
        Start-Sleep 2
    }
    if ($p.ExitCode -ne 0 -or -not (Test-Path (Join-Path $extracted 'Access Music\Virus TI\Common\Virus Control.dll'))) {
        Say "Unpacking the Access installer failed (code $($p.ExitCode)). Is it the 64-bit `"Virus TI Software Suite`" .msi?" 'Error'
        exit 1
    }
    $common = Join-Path $extracted 'Access Music\Virus TI\Common'
    if (-not (Is64 (Join-Path $common 'Virus Control.dll')) -and -not (Extract64Dll $Msi $common)) {
        Say "The Access installer holds no 64-bit Virus Control.dll. Is it the 64-bit `"Virus TI Software Suite`" .msi?" 'Error'
        exit 1
    }
    # the factory patch libraries into the user's Documents (no admin needed; existing files stay untouched)
    $docs = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'Access Music\Virus TI\Patches'
    $lib = Join-Path $extracted 'PersonalFiles\Access Music\Virus TI\Patches'
    if (Test-Path $lib) {
        Get-ChildItem $lib -Recurse -File | ForEach-Object {
            $dst = Join-Path $docs $_.FullName.Substring($lib.Length).TrimStart('\')
            if (-not (Test-Path $dst)) {
                New-Item -ItemType Directory -Force (Split-Path -Parent $dst) | Out-Null
                Copy-Item $_.FullName $dst
            }
        }
    }
}
# the patch libraries in Documents survive the removal of the Access software (it would take its factory files along)
$docs = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'Access Music\Virus TI\Patches'
$backup = Join-Path $env:TEMP 'tirevive-access-patches'
if ($remove -and (Test-Path $docs)) {
    Remove-Item -Recurse -Force $backup -ErrorAction SilentlyContinue
    Copy-Item -Recurse $docs $backup
}
if ($remove -or $extracted) { # one UAC prompt for both
    $argline = "-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File `"$PSCommandPath`" -Elevated"
    if ($remove) { $argline += ' -RemoveAccess' }
    if ($extracted) { $argline += " -Extracted `"$extracted`"" }
    if ($Quiet) { & $PSCommandPath -Elevated -RemoveAccess:$remove -Extracted $extracted }
    else { Start-Process powershell.exe -Verb RunAs -Wait -WindowStyle Hidden -ArgumentList $argline }
}
if (Test-Path $backup) {
    Get-ChildItem $backup -Recurse -File | ForEach-Object {
        $dst = Join-Path $docs $_.FullName.Substring($backup.Length).TrimStart('\')
        if (-not (Test-Path $dst)) {
            New-Item -ItemType Directory -Force (Split-Path -Parent $dst) | Out-Null
            Copy-Item $_.FullName $dst
        }
    }
    Remove-Item -Recurse -Force $backup -ErrorAction SilentlyContinue
}
if ($extracted) { Remove-Item -Recurse -Force $extracted -ErrorAction SilentlyContinue }
if (HaveFiles) {
    $msg = 'The Access Virus TI files are set up. Use the plug-in "TI Revive Virus Control" in your DAW'
    if ($remove -and -not (AccessProduct)) { $msg += '; the old Access software was removed' }
    Say "$msg."
    exit 0
}
Say 'The Access Virus TI files are still missing (was the admin prompt declined?). Run "Set up Access Virus TI files" from the Start menu again.' 'Warning'
exit 1
