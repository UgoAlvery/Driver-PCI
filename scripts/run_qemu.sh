#!/usr/bin/env bash
#
# Démarre la VM de développement/test (image kernel_training.qcow2, non
# fournie dans ce repo — image du cours EPITA, propriétaire, à placer dans
# ce même dossier scripts/).
#
# - device edu : le périphérique PCI ciblé par ce driver
# - hostfwd tcp::2222-:22 : SSH accessible depuis l'hôte sur localhost:2222
# - virtfs workdir : partage 9p entre la racine du projet (hôte) et
#   /workdir (guest). La racine du projet est calculée à partir de
#   l'emplacement de CE script, pas du répertoire courant : le script
#   fonctionne donc de la même façon quel que soit l'endroit d'où tu le
#   lances. C'est important pour compiler : le module doit être construit
#   DANS le guest (seul son kernel a des headers correspondants -- WSL2
#   tourne un kernel Microsoft sans headers installables), donc /workdir
#   doit contenir tout l'arbre source, pas juste des binaires précompilés.
#
# -nographic : pas de fenêtre GTK. Toute l'interaction se fait via SSH
# (hostfwd ci-dessus) ou directement dans ce terminal (console série).
# Choix délibéré : ce projet n'a besoin d'aucun affichage graphique, et
# ça évite au passage un bug connu de WSLg (crash Wayland/GTK de la
# fenêtre QEMU, notamment en plein écran). Les devices USB HID
# (qemu-xhci, usb-tablet) de la version originale du script sont donc
# retirés : ils ne servent qu'à un pointeur souris dans une fenêtre
# graphique, inutile ici.
# Pour quitter : Ctrl-A puis X (kill immédiat), ou "poweroff" / "halt"
# depuis un shell dans le guest.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
IMG="$SCRIPT_DIR/kernel_training.qcow2"
QEMU=qemu-system-x86_64

# -enable-kvm suppose /dev/kvm disponible (virtualisation imbriquée activée
# sous WSL2). Si absent, retirer -enable-kvm ci-dessous : QEMU bascule sur
# l'émulation logicielle (TCG), plus lente au boot mais fonctionnellement
# identique pour ce projet.
$QEMU -m 4096 -smp 2 -enable-kvm \
	-nographic \
	-nic user,model=virtio-net-pci,hostfwd=tcp::2222-:22 \
	-device pci-testdev \
	-device edu \
	-virtfs local,path="$PROJECT_ROOT",mount_tag=workdir,security_model=mapped \
	-hda "$IMG" "$@"
