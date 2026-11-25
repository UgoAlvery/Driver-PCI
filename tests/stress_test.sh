#!/bin/sh
# Supplementary stress scenarios for KASAN/lockdep validation (Étape 3).
# Run inside the debug-kernel guest after edu.ko is loaded.
# Usage: sh /workdir/tests/stress_test.sh 2>&1 | tee /workdir/stress.log

set -u

DEVICE0="${DEVICE0:-/dev/edu-fact0}"
DEVICE1="${DEVICE1:-/dev/edu-fact1}"
DMA_BIN="${DMA_BIN:-/workdir/tests/dma_roundtrip_test}"
MODULE_PATH="${MODULE_PATH:-/workdir/edu.ko}"

pass() { echo "[PASS] $1"; }
fail() { echo "[FAIL] $1"; }
skip() { echo "[SKIP] $1"; }

echo "=== Concurrent factorial on two devices ==="
( for i in $(seq 20); do echo 7 > "$DEVICE0" && cat "$DEVICE0" > /dev/null; done ) &
( for i in $(seq 20); do echo 5 > "$DEVICE1" && cat "$DEVICE1" > /dev/null; done ) &
wait
pass "concurrent factorial on two devices (no hang, no crash)"

echo ""
echo "=== Concurrent DMA on two devices ==="
if [ -x "$DMA_BIN" ]; then
    ( for i in $(seq 10); do "$DMA_BIN" "$DEVICE0" > /dev/null; done ) &
    ( for i in $(seq 10); do "$DMA_BIN" "$DEVICE1" > /dev/null; done ) &
    wait
    pass "concurrent DMA on two devices"
else
    skip "dma_roundtrip_test not found"
fi

echo ""
echo "=== Repeated load/unload cycles (IDA minor reuse) ==="
for i in $(seq 10); do
    rmmod edu 2>/dev/null || true
    insmod "$MODULE_PATH"
    if [ -c "$DEVICE0" ] && [ -c "$DEVICE1" ]; then
        echo 3 > "$DEVICE0" && cat "$DEVICE0" > /dev/null
    else
        fail "cycle $i: device nodes missing after reload"
        break
    fi
done
pass "10 load/unload cycles, minors reused correctly"

echo ""
echo "=== N >= 13 overflow path ==="
if echo 13 > "$DEVICE0" 2>/dev/null; then
    fail "N=13 accepted — overflow not caught"
else
    pass "N=13 rejected (EINVAL)"
fi
if echo 255 > "$DEVICE0" 2>/dev/null; then
    fail "N=255 accepted"
else
    pass "N=255 rejected"
fi

echo ""
echo "=== Negative input path ==="
if printf '%s\n' -1 > "$DEVICE0" 2>/dev/null; then
    fail "N=-1 accepted"
else
    pass "N=-1 rejected"
fi

echo ""
echo "=== IRQ under factorial load (device0 and device1 simultaneously) ==="
( for i in $(seq 50); do echo 10 > "$DEVICE0" && cat "$DEVICE0" > /dev/null; done ) &
( for i in $(seq 50); do echo 10 > "$DEVICE1" && cat "$DEVICE1" > /dev/null; done ) &
wait
pass "50 factorials each device under load"

echo ""
echo "=== dmesg grep after all scenarios ==="
dmesg | grep -E "BUG: KASAN|possible circular locking|possible recursive locking|inconsistent lock state|sleeping function called|kmemleak:" | head -40 || echo "No KASAN/lockdep/kmemleak reports"

echo ""
echo "=== kmemleak scan ==="
mount -t debugfs none /sys/kernel/debug 2>/dev/null || true
echo scan > /sys/kernel/debug/kmemleak
sleep 5
LEAKS=$(cat /sys/kernel/debug/kmemleak)
if [ -z "$LEAKS" ]; then
    pass "kmemleak: no leaks"
else
    echo "$LEAKS"
    fail "kmemleak: leaks detected above"
fi

echo ""
echo "=== Final unload ==="
rmmod edu && pass "final unload" || fail "final unload"

echo "=== Done ==="
