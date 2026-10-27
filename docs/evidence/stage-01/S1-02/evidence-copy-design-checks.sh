#!/bin/bash
set -u
SRC="/mnt/e/stm32项目"
OUT=/mnt/c/Users/Public/s102_dc2
D="$SRC/docs/evidence/stage-01/S1-02"
cp "$OUT/design-checks.log" "$D/design-checks.log"
for s in check_contracts check_stage_plan check_vision_contracts check_feasibility; do
  cp "$OUT/$s.json" "$D/$s.json"
  cp "$OUT/$s.json" "$D/$s.log"
done
head -1 "$D/design-checks.log"
grep -c 'EXIT=0' "$D/design-checks.log"
ls "$D" | wc -l
