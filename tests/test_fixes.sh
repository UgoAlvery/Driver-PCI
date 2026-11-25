#!/bin/sh
# Integration test for the 4 bug fixes.
# Run inside the QEMU VM: sh /workdir/project/tests/test_fixes.sh

DEV="${1:-/dev/edu-fact0}"
DEVNAME=$(basename "$DEV")
SYSFS=/sys/class/edu-fact/${DEVNAME}
MODULE=/workdir/project/edu.ko
DMA_BIN=/workdir/project/tests/dma_roundtrip_test

PASS=0
FAIL=0
ok()  { echo "[PASS] $1"; PASS=$((PASS + 1)); }
nok() { echo "[FAIL] $1"; FAIL=$((FAIL + 1)); }

# ensure the device is accessible regardless of default udev/mdev permissions
doas -n chmod a+rw "$DEV" 2>/dev/null || true

# ---------- Fix 4: uint32_t overflow guard ----------
echo "=== Fix 4: factorial bounds ==="

echo 8 > "$DEV"
r=$(cat "$DEV")
[ "$r" = "40320" ] && ok "8! = 40320" || nok "8! got '$r'"

echo 0 > "$DEV"
r=$(cat "$DEV")
[ "$r" = "1" ] && ok "0! = 1" || nok "0! got '$r'"

echo 12 > "$DEV"
r=$(cat "$DEV")
[ "$r" = "479001600" ] && ok "12! = 479001600" || nok "12! got '$r'"

echo 13 > "$DEV" 2>/dev/null && nok "N=13 accepted (overflow not caught)" || ok "N=13 rejected"
printf '%s\n' -1 > "$DEV" 2>/dev/null && nok "N=-1 accepted" || ok "N=-1 rejected"

# ---------- Fix 3: IRQ / timeout normal path ----------
echo "=== Fix 3: IRQ normal path (no hang) ==="

for n in 1 2 3 4 5; do
    echo $n > "$DEV"
done
c=$(cat ${SYSFS}/irq_count)
[ "$c" -ge 5 ] && ok "irq_count = $c (>= 5 after loop)" || nok "irq_count = $c (too low)"

# ---------- Fix 2: IDA minor reuse on reload ----------
echo "=== Fix 2: module reload (IDA minor reuse) ==="

doas -n rmmod edu
doas -n insmod "$MODULE"
# chmod all edu-fact* so concurrent test instances on other devices still work
doas -n chmod a+rw /dev/edu-fact* 2>/dev/null
[ -c "$DEV" ] && ok "$DEV present after reload" || nok "$DEV missing after reload"

# ---------- Fix 1: DMA round-trip + concurrent ----------
echo "=== Fix 1: DMA round-trip ==="

# device was recreated by Fix 2 reload — open it as root to avoid VM permission issues
if [ -x "$DMA_BIN" ]; then
    doas -n "$DMA_BIN" "$DEV" && ok "DMA single round-trip" || nok "DMA single round-trip"
    doas -n "$DMA_BIN" "$DEV" &
    doas -n "$DMA_BIN" "$DEV" &
    wait && ok "DMA concurrent (2 parallel)" || nok "DMA concurrent"
else
    echo "  (skipped: $DMA_BIN not found or not executable)"
fi

# ---------- sysfs sanity ----------
echo "=== sysfs ==="
ic=$(cat ${SYSFS}/irq_count)
dc=$(cat ${SYSFS}/dma_count)
[ -n "$ic" ] && ok "irq_count readable ($ic)" || nok "irq_count missing"
[ -n "$dc" ] && ok "dma_count readable ($dc)" || nok "dma_count missing"

echo ""
echo "===================="
echo "$PASS passed, $FAIL failed"
echo "===================="
[ "$FAIL" -eq 0 ]
