#!/usr/bin/env bash
# Formal S2-07 fixed-snapshot developer validation; no hardware actuation.
set -euo pipefail
w=/home/%USER%/locker-workspace
s="$w/S2-07/batches/20261030-01/src"
b="$w/build/S2-07/20261030-01"
l="$w/logs/S2-07/20261030-01"
mkdir -p "$b" "$l"
cd "$s"
python3 -B - <<'PY' > "$l/source-match.log"
import hashlib,json
from pathlib import Path
manifest=json.loads(Path('.s207-source-manifest.json').read_text())
bad=[p for p,h in manifest.items() if hashlib.sha256(Path(p).read_bytes()).hexdigest()!=h]
assert not bad,bad
print('PASS',len(manifest),'fixed snapshot files match')
PY
{ uname -a; python3 --version; cmake --version; gcc --version; findmnt -T "$s"; } > "$l/environment.log"
run() {
  local label="$1"
  shift
  printf '%s started\n' "$label"
  local rc=0
  "$@" > "$l/$label.log" 2>&1 || rc=$?
  printf '%s exit=%s\n' "$label" "$rc" | tee -a "$l/results.log"
  if [ "$rc" != 0 ]; then tail -50 "$l/$label.log"; return "$rc"; fi
}
run debug-configure cmake -S "$s" -B "$b" -DCMAKE_BUILD_TYPE=Debug \
  -DLOCKER_BUILD_HOST=ON -DLOCKER_BUILD_NETWORK=ON -DLOCKER_BUILD_TESTS=ON -DLOCKER_WARNINGS_AS_ERRORS=ON
run debug-build cmake --build "$b" -j2
run debug-ctest ctest --test-dir "$b" --output-on-failure
run asan-configure cmake -S "$s" -B "$b" \
  '-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie' \
  '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined -no-pie'
run asan-build cmake --build "$b" -j2
run asan-ctest env ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir "$b" --output-on-failure
