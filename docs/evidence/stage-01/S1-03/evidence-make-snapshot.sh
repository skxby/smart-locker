#!/bin/bash
# S1-03 source snapshot for the VMware canonical run.
# Packaging helper only; it is NOT a verification step.
# Usage: bash evidence-make-snapshot.sh <repo-root> <output-tar>
#   Both arguments are required so no environment-specific mount path is baked in.
set -u
if [ $# -ne 2 ]; then
    echo "usage: $0 <repo-root> <output-tar>" >&2
    exit 2
fi
SRC=$1
OUT=$2
if [ ! -f "$SRC/CMakeLists.txt" ]; then
    echo "REPO_ROOT_INVALID $SRC" >&2
    exit 3
fi
cd "$SRC" || exit 1
rm -f "$OUT"
tar --exclude=.git --exclude=build --exclude="build-*" \
    --exclude="*.o" --exclude="*.a" --exclude="*.so" --exclude="*.pyc" --exclude=__pycache__ \
    -czf "$OUT" . 2>/dev/null
echo "TAR_EXIT=$?"
echo "SNAPSHOT_SHA256=$(sha256sum "$OUT" | cut -d' ' -f1)"
echo "SNAPSHOT_BYTES=$(stat -c %s "$OUT")"
echo "TAR_ENTRIES=$(tar -tzf "$OUT" | wc -l)"
