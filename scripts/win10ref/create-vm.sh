#!/usr/bin/env bash
# create-vm.sh - define and start the Windows 10 reference VM "win10ref" and run the unattended install. Only for the
# original Virus Control 5.1.7 with the Access drivers (to compare TI Revive against), never for TI Revive builds.
# Prerequisites (pool "default"): Windows_10.iso (de-DE), win10ref-unattend.iso (make-unattend-iso.sh, uploaded with
# virsh vol-upload). The disk win10ref.qcow2 (80G) is created here. Legacy BIOS without Secure Boot/TPM, like the old
# win10 VM the 2014 Access driver ran on. SATA disk + e1000e: no extra drivers needed.
# Rules (findings/log/): attach the Virus once after boot, never power-cycle or replug it while attached (bug checks
# 0xCE / 0x139), close the DAW before a detach; snapshot ref-clean before the first attach.
set -euo pipefail
export LIBVIRT_DEFAULT_URI=qemu:///system
VM=win10ref
virsh vol-info --pool default "$VM.qcow2" >/dev/null 2>&1 ||
  virsh vol-create-as default "$VM.qcow2" 80G --format qcow2
virt-install \
  --name "$VM" --memory 8192 --vcpus 4 --cpu host-passthrough \
  --machine q35 \
  --boot hd,cdrom \
  --disk vol=default/$VM.qcow2,bus=sata \
  --disk vol=default/Windows_10.iso,device=cdrom,bus=sata \
  --disk vol=default/$VM-unattend.iso,device=cdrom,bus=sata \
  --network network=default,model=e1000e \
  --graphics spice --video qxl --sound ich9 \
  --osinfo win10 --noautoconsole --wait 0
# The Windows ISO asks "Press any key to boot from CD or DVD" for a few seconds: answer it.
for i in $(seq 1 15); do
  virsh send-key "$VM" KEY_ENTER >/dev/null 2>&1 || true
  sleep 1
done
echo "$VM started; installation runs unattended (watch with virt-manager / virt-viewer)"
