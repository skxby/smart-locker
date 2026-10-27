#!/bin/bash
set -u
SRC="/mnt/e/stm32项目"
OUT=/mnt/c/Users/Public
cd "$SRC" || exit 1
rm -f "$OUT/s102_snapshot_r9.tar.gz"
tar --exclude=.git --exclude='./build' --exclude='./build2' --exclude='build-*' \
    --exclude='*.o' --exclude='*.a' --exclude='*.so' --exclude='*.pyc' --exclude='__pycache__' \
    --exclude='*.log' \
    -czf "$OUT/s102_snapshot_r9.tar.gz" . 2>/dev/null
echo "TAR_EXIT=$?"
echo "SNAPSHOT_SHA256=$(sha256sum "$OUT/s102_snapshot_r9.tar.gz" | cut -d' ' -f1)"
echo "SNAPSHOT_BYTES=$(stat -c %s "$OUT/s102_snapshot_r9.tar.gz")"
echo "TAR_ENTRIES=$(tar -tzf "$OUT/s102_snapshot_r9.tar.gz" | wc -l)"
