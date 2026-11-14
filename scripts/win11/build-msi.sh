#!/usr/bin/env bash
# build-msi.sh - cross-build our binaries on Linux, build "TI Revive.msi" with WiX in the win11 VM, fetch it to
# work/release/. The VM needs C:\build\dotnet + C:\build\wix (see findings/log/, item M6).
set -euo pipefail
cd "$(dirname "$0")/../.."
export VM=win11
make -C src/broker >/dev/null
make -C src/wrapper >/dev/null
R='C:/build/tirevive'
scripts/vm.sh ps 'Remove-Item -Recurse -Force C:\build\tirevive -ErrorAction SilentlyContinue; New-Item -ItemType Directory -Force C:\build\tirevive\bin, C:\build\tirevive\src\inf, C:\build\tirevive\src\installer | Out-Null' >/dev/null
for f in work/build/vtibroker.exe work/build/vtilaunch.exe work/build/vtiemu-pipe.dll work/build/VirusTI_WinUSB.vst3 work/build/TIRevive_ASIO.dll; do
  scripts/vm.sh push "$f" "$R/bin/"
done
scripts/vm.sh push src/inf/vti_winusb.inf "$R/src/inf/"
scripts/vm.sh push src/installer "$R/src/" 
V=${1:-0.1.1} # version (optional): build-msi.sh 0.6.4 -> also "TI Revive setup.exe" (needs WixToolset.BootstrapperApplications.wixext in the VM)
scripts/vm.sh ps "powershell -ExecutionPolicy Bypass -File C:\\build\\tirevive\\src\\installer\\build.ps1 -Version $V -Bundle 2>&1 | Out-String -Width 200"
mkdir -p work/release
scripts/vm.sh pull "$R/out/TI Revive.msi" work/release/
scripts/vm.sh pull "$R/out/TI Revive setup.exe" work/release/
ls -la work/release/
