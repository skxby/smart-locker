#!/bin/bash
set -u
SRC="/mnt/e/stm32项目"
OUT=/mnt/c/Users/Public/s102_dc2
rm -rf "$OUT"; mkdir -p "$OUT"
{
echo "# design checks $(date -Iseconds)"
for s in check_contracts check_stage_plan check_vision_contracts check_feasibility; do
  echo "===== $s ====="
  python3 -B "$SRC/scripts/$s.py" > "$OUT/$s.json" 2>&1
  echo "EXIT=$?"
  cat "$OUT/$s.json"
done
} > "$OUT/design-checks.log" 2>&1
grep -c 'EXIT=0' "$OUT/design-checks.log"
