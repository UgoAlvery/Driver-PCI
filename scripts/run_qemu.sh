#!/usr/bin/env bash
#
# Démarre la VM de développement/test (image kernel_training.qcow2, non
# fournie dans ce repo — image du cours EPITA, propriétaire, à copier
# manuellement à côté de ce script).
#
# - device edu : le périphérique PCI ciblé par ce driver
# - hostfwd tcp::2222-:22 : SSH accessible depuis l'hôte sur localhost:2222
# - virtfs workdir : partage 9p entre $PWD/workdir (hôte) et /mnt/workdir
#   (guest, à monter manuellement : mount -t 9p -o trans=virtio workdir /mnt/workdir)
#   -> utilisé pour copier edu.ko et tests/dma_roundtrip_test dans la VM.

IMG=$(dirname "$0")/kernel_training.qcow2
QEMU=qemu-system-x86_64

# -enable-kvm suppose /dev/kvm disponible (virtualisation imbriquée activée
# sous WSL2). Si absent, retirer -enable-kvm ci-dessous : QEMU bascule sur
# l'émulation logicielle (TCG), plus lente au boot mais fonctionnellement
# identique pour ce projet.
$QEMU -m 4096 -smp 2 -enable-kvm \
	-nic user,model=virtio-net-pci,hostfwd=tcp::2222-:22 \
	-device qemu-xhci -device usb-tablet \
	-device pci-testdev \
	-device edu \
	-virtfs local,path="$PWD/workdir",mount_tag=workdir,security_model=mapped \
	-hda "$IMG" "$@"
