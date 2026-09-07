#!/usr/bin/env bash
#
# tests/run_tests.sh
#
# Suite de tests d'intégration pour edu_char.ko, exécutée à distance (SSH)
# contre une VM QEMU déjà démarrée avec scripts/run_qemu.sh
# (image kernel_training.qcow2, SSH redirigé sur le port local via
# -nic user,...hostfwd=tcp::2222-:22).
#
# Ce script ne démarre PAS la VM lui-même : il suppose qu'elle tourne déjà
# et attend que SSH réponde. C'est un choix délibéré, pour pouvoir garder
# la VM ouverte entre deux exécutions successives pendant le développement
# au lieu de payer un boot complet à chaque test.
#
# Prérequis côté guest :
#   - edu_char.ko présent (copié via le partage 9p, mount_tag=workdir,
#     monté par ex. sur /mnt/workdir dans la VM)
#   - tests/dma_roundtrip_test compilé et copié au même endroit
#     (voir tests/Makefile)
#
# Variables d'environnement personnalisables :
#   SSH_HOST, SSH_PORT, SSH_USER, MODULE_PATH, DEVICE

set -uo pipefail

SSH_HOST="${SSH_HOST:-localhost}"
SSH_PORT="${SSH_PORT:-2222}"
SSH_USER="${SSH_USER:-root}"
MODULE_PATH="${MODULE_PATH:-/mnt/workdir/edu.ko}"
DMA_TEST_BIN="${DMA_TEST_BIN:-/mnt/workdir/dma_roundtrip_test}"
DEVICE="${DEVICE:-/dev/edu-fact0}"

SSH_OPTS=(-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null
	  -o ConnectTimeout=5 -p "$SSH_PORT")

PASS=0
FAIL=0

pass() { echo "[PASS] $1"; PASS=$((PASS + 1)); }
fail() { echo "[FAIL] $1"; FAIL=$((FAIL + 1)); }

ssh_cmd() {
	ssh "${SSH_OPTS[@]}" "$SSH_USER@$SSH_HOST" "$@"
}

echo "==> Waiting for SSH on $SSH_HOST:$SSH_PORT"
ready=0
for _ in $(seq 1 30); do
	if ssh_cmd true 2>/dev/null; then
		ready=1
		break
	fi
	sleep 2
done
if [ "$ready" -ne 1 ]; then
	echo "ERROR: SSH never became available. Is scripts/run_qemu.sh running?" >&2
	exit 1
fi

echo "==> Loading module ($MODULE_PATH)"
if ssh_cmd "lsmod | grep -q '^edu ' || insmod $MODULE_PATH"; then
	pass "module loaded"
else
	fail "module load"
	echo "$FAIL failed, $PASS passed -- aborting, nothing else can run" >&2
	exit 1
fi

echo "==> Checking device node"
if ssh_cmd "test -c $DEVICE"; then
	pass "device node $DEVICE exists"
else
	fail "device node $DEVICE missing"
fi

echo "==> Factorial: 8! == 40320"
result=$(ssh_cmd "echo 8 > $DEVICE && cat $DEVICE" 2>/dev/null | tr -d '\r\n')
if [ "$result" = "40320" ]; then
	pass "8! == 40320"
else
	fail "8! expected 40320, got '$result'"
fi

echo "==> Factorial: 0! == 1 (edge case)"
result=$(ssh_cmd "echo 0 > $DEVICE && cat $DEVICE" 2>/dev/null | tr -d '\r\n')
if [ "$result" = "1" ]; then
	pass "0! == 1"
else
	fail "0! expected 1, got '$result'"
fi

echo "==> sysfs introspection: irq_count reflects completed factorials"
minor=$(basename "$DEVICE" | tr -dc '0-9')
irq_count=$(ssh_cmd "cat /sys/class/edu-fact/edu-fact${minor}/irq_count" 2>/dev/null | tr -d '\r\n')
if [ -n "$irq_count" ] && [ "$irq_count" -ge 2 ]; then
	pass "irq_count == $irq_count (>= 2 factorials run above)"
else
	fail "irq_count missing or unexpectedly low ('$irq_count')"
fi

echo "==> DMA round-trip (RAM->EDU->RAM data integrity)"
if ssh_cmd "test -x $DMA_TEST_BIN"; then
	if ssh_cmd "$DMA_TEST_BIN $DEVICE"; then
		pass "DMA round-trip data integrity"
	else
		fail "DMA round-trip data integrity"
	fi
else
	echo "  (skipped: $DMA_TEST_BIN not found -- build it with tests/Makefile first)"
fi

echo "==> Module unload"
if ssh_cmd "rmmod edu"; then
	pass "module unload"
else
	fail "module unload"
fi

echo
echo "===================="
echo "$PASS passed, $FAIL failed"
echo "===================="
[ "$FAIL" -eq 0 ]
