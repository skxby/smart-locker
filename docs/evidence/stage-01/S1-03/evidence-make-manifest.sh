#!/bin/bash
# S1-03 source manifest generator.
# Usage: bash evidence-make-manifest.sh <repo-root>
# The repo root is an argument so no environment-specific mount path is baked in.
set -u
if [ $# -ne 1 ]; then
    echo "usage: $0 <repo-root>" >&2
    exit 2
fi
SRC=$1
OUT="$SRC/docs/evidence/stage-01/S1-03/source-manifest.txt"
if [ ! -f "$SRC/CMakeLists.txt" ]; then
    echo "REPO_ROOT_INVALID $SRC" >&2
    exit 3
fi
cd "$SRC" || exit 1
{
  echo "# S1-03 source manifest"
  echo "# scope=root CMakeLists.txt + host/device/ + host/simulator/ + tests/unit/device/ + tests/contract/device/"
  echo "# generated=$(date -Iseconds)"
  # R9-N-4: generated Python caches are not source; the snapshot script already
  # excludes them, so the manifest must not pick them up either.
  for rel in CMakeLists.txt $(find host/device host/simulator tests/unit/device tests/contract/device \
      -type f ! -name '*.pyc' ! -path '*/__pycache__/*' | sort); do
    h=$(sha256sum "$rel" | awk '{print $1}')
    b=$(stat -c %s "$rel")
    echo "$h  $b  $rel"
  done
} > "$OUT"
echo "MANIFEST_LINES=$(grep -vc '^#' "$OUT")"
echo "MANIFEST_BYTES=$(stat -c %s "$OUT")"
echo "MANIFEST_SHA256=$(sha256sum "$OUT" | cut -d' ' -f1)"
