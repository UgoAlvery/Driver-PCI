#!/usr/bin/env bash
#
# Régénère compile_commands.json (base de compilation pour l'indexation
# CLion / clangd) en compilant réellement le module et le test DMA avec
# `bear`, qui capture les invocations gcc effectives.
#
# Pourquoi pas un CMakeLists.txt : ce module est un module noyau kbuild
# (obj-m), pas un projet cmake. kbuild génère ses flags (-D__KERNEL__,
# -nostdinc, chemins d'includes générés, etc.) dynamiquement à partir des
# headers du noyau cible ; les reproduire à la main dans un CMakeLists
# serait redondant et se désynchroniserait à la moindre montée de version.
# compile_commands.json capture ces flags tels qu'ils sortent réellement
# de kbuild — c'est l'approche que CLion documente pour les modules noyau.
#
# Attention : le Makefile compile normalement contre le noyau invité QEMU
# (voir scripts/run_qemu.sh — WSL2 n'a pas de headers pour son propre
# noyau). Ce script compile donc contre les headers *hôte* disponibles
# localement (le noyau générique le plus récent sous /lib/modules), ce qui
# suffit pour l'indexation IDE (types/macros kernel stables) mais ne
# remplace pas une vraie compilation dans le guest.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Dernier noyau générique disponible localement (pas forcément celui qui
# tourne : sous WSL2, `uname -r` pointe vers un noyau Microsoft sans
# headers installables).
KDIR="$(find /lib/modules -maxdepth 1 -name '*-generic' -type d 2>/dev/null | sort -V | tail -1)/build"

if [ ! -d "$KDIR" ]; then
	echo "error: no local generic kernel headers found under /lib/modules/*-generic" >&2
	echo "install one with: sudo apt-get install linux-headers-generic" >&2
	exit 1
fi

echo "Using KDIR=$KDIR"

cd "$PROJECT_ROOT"
# make clean au préalable : sinon kbuild saute les fichiers déjà à jour et
# bear ne capture aucune invocation gcc. (make clean supprime au passage
# compile_commands.json lui-même — kbuild le traite comme un fichier
# généré — d'où la régénération après coup.)
make KDIR="$KDIR" clean
bear -- make KDIR="$KDIR"

cd "$PROJECT_ROOT/tests"
make clean
bear --output "$PROJECT_ROOT/compile_commands.json" --append -- make

echo "compile_commands.json regenerated at $PROJECT_ROOT/compile_commands.json"
