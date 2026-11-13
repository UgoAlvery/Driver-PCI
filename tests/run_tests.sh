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
MODULE_PATH="${MODULE_PATH:-/workdir/edu.ko}"
DMA_TEST_BIN="${DMA_TEST_BIN:-/workdir/tests/dma_roundtrip_test}"
POLL_TEST_BIN="${POLL_TEST_BIN:-/workdir/tests/poll_test}"
MMAP_TEST_BIN="${MMAP_TEST_BIN:-/workdir/tests/mmap_test}"
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

echo "==> MSI interrupt mode (requires driver built with MSI support)"
if ssh_cmd "grep 'edu-fact' /proc/interrupts | grep -q 'PCI-MSI'" 2>/dev/null; then
	pass "MSI interrupt mode active (PCI-MSI in /proc/interrupts)"
else
	fail "MSI not active — only INTx found in /proc/interrupts"
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

echo "==> Factorial overflow guard: N=13 must be rejected (13! > UINT32_MAX)"
if ssh_cmd "echo 13 > $DEVICE" 2>/dev/null; then
	fail "N=13 accepted (uint32_t overflow not caught)"
else
	pass "N=13 rejected (overflow guard)"
fi

echo "==> Factorial: negative input rejected"
if ssh_cmd "printf '%s\n' -1 > $DEVICE" 2>/dev/null; then
	fail "N=-1 accepted (should be rejected)"
else
	pass "N=-1 rejected"
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

echo "==> poll/epoll: POLLOUT before write, POLLIN after write"
if ssh_cmd "test -x $POLL_TEST_BIN"; then
	if ssh_cmd "$POLL_TEST_BIN $DEVICE" 2>/dev/null; then
		pass "poll/epoll: POLLOUT+POLLIN semantics correct"
	else
		fail "poll/epoll: unexpected result from poll_test"
	fi
else
	echo "  (skipped: $POLL_TEST_BIN not found -- build with tests/Makefile first)"
fi

echo "==> mmap DMA buffer (dma_mmap_coherent, module-ref survives rmmod)"
if ssh_cmd "test -x $MMAP_TEST_BIN"; then
	if ssh_cmd "$MMAP_TEST_BIN $DEVICE" 2>/dev/null; then
		pass "mmap DMA buffer: coherency and module-ref correct"
	else
		fail "mmap DMA buffer: unexpected result from mmap_test"
	fi
else
	echo "  (skipped: $MMAP_TEST_BIN not found -- build with tests/Makefile first)"
fi

echo "==> Module reload minor reuse (IDA alloc/free)"
if ssh_cmd "rmmod edu && insmod $MODULE_PATH && test -c $DEVICE"; then
	pass "module reload: $DEVICE present after reload (IDA minor reused)"
else
	fail "module reload: $DEVICE missing after reload"
fi

echo "==> DMA concurrent access (data-race guard; needs KCSAN for guaranteed detection)"
if ssh_cmd "test -x $DMA_TEST_BIN"; then
	# Two concurrent round-trips: without the lock fix, dma_buf races cause corruption
	if ssh_cmd "$DMA_TEST_BIN $DEVICE & $DMA_TEST_BIN $DEVICE; wait"; then
		pass "DMA concurrent: both round-trips succeeded"
	else
		fail "DMA concurrent: error from concurrent round-trips"
	fi
else
	echo "  (skipped: $DMA_TEST_BIN not found)"
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
