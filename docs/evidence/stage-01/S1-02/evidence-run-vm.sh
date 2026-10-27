#!/bin/bash
set -u
RUN=20261024-213500
WS=$HOME/locker-workspace
SRC=$WS/S1-02/src
BUILD=$WS/build/S1-02/$RUN
LOGS=$WS/logs/S1-02/$RUN
ART=$WS/artifacts/S1-02
TAR=/tmp/s102_snapshot_r9.tar.gz
mkdir -p "$BUILD" "$LOGS" "$ART" "$SRC"

rm -rf "$SRC"; mkdir -p "$SRC"
tar -xzf "$TAR" -C "$SRC"
echo "EXTRACT_EXIT=$?" > "$LOGS/extract.log"

{
echo "host=$(hostname)"
echo "os=$(. /etc/os-release; echo "$PRETTY_NAME")"
echo "kernel=$(uname -r)"
echo "arch=$(uname -m)"
echo "nproc=$(nproc)"
echo "cmake=$(cmake --version | head -1)"
echo "gcc=$(gcc --version | head -1)"
echo "make=$(make --version | head -1)"
echo "ctest=$(ctest --version | head -1)"
echo "python=$(python3 --version)"
echo "fs=$(df -T / | tail -1 | awk '{print $2}')"
} > "$LOGS/environment.log" 2>&1

# manifest check
python3 -B - "$SRC" "$LOGS/manifest.sha256" > "$LOGS/source-verify.log" 2>&1 <<'PYEOF'
import hashlib, os, sys
src = sys.argv[1]
out = sys.argv[2]
manifest = os.path.join(src, 'docs/evidence/stage-01/S1-02/source-manifest.txt')
ok = bad = 0
lines = 0
with open(manifest, 'r', encoding='utf-8') as f:
    for line in f:
        line = line.rstrip('\n')
        if not line or line.startswith('#'):
            continue
        lines += 1
        parts = line.split('  ')
        want, size, rel = parts
        p = os.path.join(src, rel)
        if not os.path.exists(p):
            print('MISSING', rel); bad += 1; continue
        h = hashlib.sha256(open(p, 'rb').read()).hexdigest()
        actual = os.path.getsize(p)
        if h == want and str(actual) == size:
            print(rel + ': OK'); ok += 1
        else:
            print(rel + ': MISMATCH'); bad += 1
print('MANIFEST_LINES=%d' % lines)
print('SHACHECK_OK=%d' % ok)
print('SHACHECK_FAILED=%d' % bad)
print('SHACHECK_EXIT=%d' % (0 if bad == 0 else 1))
with open(out, 'w', encoding='utf-8') as g:
    pass
PYEOF

# stray artifacts in card scope
python3 -B - "$SRC" >> "$LOGS/source-verify.log" 2>&1 <<'PYEOF'
import os, sys
src = sys.argv[1]
roots = [os.path.join(src, 'host'), os.path.join(src, 'tests')]
stray = []
for r in roots:
    for dp, dn, fn in os.walk(r):
        for f in fn:
            if f.endswith(('.o', '.a', '.so', '.pyc')) or f == 'CMakeCache.txt':
                stray.append(os.path.join(dp, f))
print('SRC_STRAY_ARTIFACTS=%d' % len(stray))
for s in stray[:10]:
    print('STRAY', s)
print('SRC_FILES=%d' % sum(len(fn) for _, _, fn in os.walk(src)))
PYEOF

cmake -S "$SRC" -B "$BUILD" -DCMAKE_BUILD_TYPE=Debug -DLOCKER_BUILD_HOST=ON -DLOCKER_BUILD_TESTS=ON -DLOCKER_WARNINGS_AS_ERRORS=ON > "$LOGS/configure.log" 2>&1
CE=$?
echo "CONFIGURE_EXIT=$CE" >> "$LOGS/configure.log"
echo "CONFIGURE_WARNING_LINES=$(grep -ci 'warning' "$LOGS/configure.log")" >> "$LOGS/configure.log"

cmake --build "$BUILD" -j2 > "$LOGS/build.log" 2>&1
BE=$?
echo "BUILD_EXIT=$BE" >> "$LOGS/build.log"
echo "BUILD_WARNING_LINES=$(grep -ci 'warning' "$LOGS/build.log")" >> "$LOGS/build.log"

ctest --test-dir "$BUILD" --output-on-failure > "$LOGS/ctest.log" 2>&1
TE=$?
echo "CTEST_EXIT=$TE" >> "$LOGS/ctest.log"

: > "$LOGS/unit-tests.log"
TOTAL=0
run_one() {
  name="$1"; bin="$2"
  if [ ! -x "$bin" ]; then echo "MISSING_BIN $name" >> "$LOGS/unit-tests.log"; return; fi
  out=$("$bin" 2>&1); rc=$?
  line=$(echo "$out" | grep -E '^PASS ' | head -1)
  n=$(echo "$line" | sed -n 's/.*(\([0-9]*\) checks).*/\1/p')
  echo "$line" >> "$LOGS/unit-tests.log"
  echo "rc=$rc test=$name" >> "$LOGS/unit-tests.log"
  if [ -n "$n" ]; then TOTAL=$((TOTAL+n)); fi
}
run_one common.bounded_queue "$BUILD/tests/unit/common/test_bounded_queue"
run_one common.build_info    "$BUILD/tests/unit/common/test_build_info"
run_one common.clock         "$BUILD/tests/unit/common/test_clock"
run_one common.log           "$BUILD/tests/unit/common/test_log"
run_one common.numeric       "$BUILD/tests/unit/common/test_numeric"
run_one common.uuid          "$BUILD/tests/unit/common/test_uuid"
run_one storage.event_key    "$BUILD/tests/unit/storage/test_event_key"
run_one storage.regex        "$BUILD/tests/unit/storage/test_regex"
run_one storage.repository   "$BUILD/tests/unit/storage/test_repository"
run_one storage.schema       "$BUILD/tests/unit/storage/test_schema"
run_one storage.u64          "$BUILD/tests/unit/storage/test_u64"
echo "TOTAL_ASSERTIONS=$TOTAL" >> "$LOGS/unit-tests.log"

# ASan / UBSan
cmake -S "$SRC" -B "$BUILD-asan" -DCMAKE_BUILD_TYPE=Debug -DLOCKER_BUILD_HOST=ON -DLOCKER_BUILD_TESTS=ON -DLOCKER_WARNINGS_AS_ERRORS=ON -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined" > "$LOGS/asan-configure.log" 2>&1
echo "ASAN_CONFIGURE_EXIT=$?" >> "$LOGS/asan-configure.log"
cmake --build "$BUILD-asan" -j2 > "$LOGS/asan-build.log" 2>&1
echo "ASAN_BUILD_EXIT=$?" >> "$LOGS/asan-build.log"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1 ctest --test-dir "$BUILD-asan" --output-on-failure > "$LOGS/asan-ctest.log" 2>&1
echo "ASAN_CTEST_EXIT=$?" >> "$LOGS/asan-ctest.log"
echo "ASAN_REPORTS=$(grep -cE 'ERROR: (AddressSanitizer|LeakSanitizer)|runtime error:' "$LOGS/asan-ctest.log")" >> "$LOGS/asan-ctest.log"

# TSan
cmake -S "$SRC" -B "$BUILD-tsan" -DCMAKE_BUILD_TYPE=Debug -DLOCKER_BUILD_HOST=ON -DLOCKER_BUILD_TESTS=ON -DLOCKER_WARNINGS_AS_ERRORS=ON -DCMAKE_C_FLAGS="-fsanitize=thread -g" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread" > "$LOGS/tsan-configure.log" 2>&1
echo "TSAN_CONFIGURE_EXIT=$?" >> "$LOGS/tsan-configure.log"
cmake --build "$BUILD-tsan" -j2 > "$LOGS/tsan-build.log" 2>&1
echo "TSAN_BUILD_EXIT=$?" >> "$LOGS/tsan-build.log"
TSAN_OPTIONS=halt_on_error=0 ctest --test-dir "$BUILD-tsan" --output-on-failure > "$LOGS/tsan-ctest.log" 2>&1
echo "TSAN_CTEST_EXIT=$?" >> "$LOGS/tsan-ctest.log"
echo "TSAN_HITS=$(grep -c 'WARNING: ThreadSanitizer' "$LOGS/tsan-ctest.log")" >> "$LOGS/tsan-ctest.log"

{
echo "# design checks $(date -Iseconds)"
for s in check_contracts check_stage_plan check_vision_contracts check_feasibility; do
  echo "===== $s ====="
  python3 -B "$SRC/scripts/$s.py" > "$LOGS/$s.json" 2>&1
  echo "EXIT=$?"
  cat "$LOGS/$s.json"
done
} > "$LOGS/design-checks.log" 2>&1

{
echo "run_id=$RUN"
echo "snapshot_sha256=$(sha256sum $TAR | cut -d' ' -f1)"
echo "snapshot_bytes=$(stat -c %s $TAR)"
echo "src=$SRC"
echo "build=$BUILD"
echo "logs=$LOGS"
echo "artifacts=$ART"
grep -E 'Total Test time' "$LOGS/ctest.log" | sed 's/^/ctest_/'
grep -E 'Total Test time' "$LOGS/asan-ctest.log" | sed 's/^/asan_/'
grep -E 'Total Test time' "$LOGS/tsan-ctest.log" | sed 's/^/tsan_/'
grep -E 'CONFIGURE_EXIT|BUILD_EXIT|CTEST_EXIT' "$LOGS/configure.log" "$LOGS/build.log" "$LOGS/ctest.log"
grep -E 'ASAN_.*EXIT|ASAN_REPORTS' "$LOGS/asan-ctest.log"
grep -E 'TSAN_.*EXIT|TSAN_HITS' "$LOGS/tsan-ctest.log"
grep -E '^EXIT=' "$LOGS/design-checks.log"
} > "$LOGS/run-summary.log" 2>&1

cd "$LOGS" && tar -czf /tmp/s102_vm_evidence_r9.tgz environment.log configure.log build.log ctest.log unit-tests.log design-checks.log asan-configure.log asan-build.log asan-ctest.log tsan-configure.log tsan-build.log tsan-ctest.log source-verify.log manifest.sha256 run-summary.log
echo "EVIDENCE_TGZ_SHA256=$(sha256sum /tmp/s102_vm_evidence_r9.tgz | cut -d' ' -f1)"
echo "EVIDENCE_TGZ_BYTES=$(stat -c %s /tmp/s102_vm_evidence_r9.tgz)"
echo "RUN=$RUN"
cat "$LOGS/run-summary.log"
