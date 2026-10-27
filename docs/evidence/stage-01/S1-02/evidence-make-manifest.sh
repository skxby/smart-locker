#!/bin/bash
set -u
SRC="/mnt/e/stm32项目"
LIST=/mnt/c/Users/Public/s102_manifest_paths.txt
OUT=/mnt/c/Users/Public/s102_source-manifest.txt
{
echo "# S1-02 source manifest"
echo "# scope=root CMakeLists.txt + host/storage/ + tests/unit/storage/"
echo "# generated=$(date -Iseconds)"
while IFS= read -r rel; do
  [ -z "$rel" ] && continue
  p="$SRC/$rel"
  h=$(sha256sum "$p" | awk '{print $1}')
  b=$(stat -c %s "$p")
  echo "$h  $b  $rel"
done < "$LIST"
} > "$OUT"
wc -l "$OUT"
sha256sum "$OUT"
awk '{print $1" "$3}' "$OUT" | grep -v '^#' > /mnt/c/Users/Public/s102_manifest.sha256
wc -l /mnt/c/Users/Public/s102_manifest.sha256
