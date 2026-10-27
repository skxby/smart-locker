#!/bin/bash
# S1-03 canonical evidence run (VMware Ubuntu 26.04). Run inside the VM.
set -u
RUN=$(date +%Y%m%d-%H%M%S)
WS=$HOME/locker-workspace
# R2-B-1: every run unpacks into its own batch directory. The live tree
# $WS/S1-03/src is never deleted, moved or reused as an extraction target, and
# the run aborts before any write when the workspace or batch path is unsafe.
BATCH=$WS/S1-03/batches/$RUN
SRC=$BATCH/src
BUILD=$WS/build/S1-03/$RUN
LOGS=$WS/logs/S1-03/$RUN
ART=$WS/artifacts/S1-03
TAR=/tmp/s103_snapshot.tar.gz
if [ ! -d "$WS" ]; then
  echo "SRC_GUARD_FAILED workspace_missing ws=$WS" >&2
  exit 2
fi
mkdir -p "$BUILD" "$LOGS" "$ART"
LIVE=$WS/S1-03/src
# R5-N-2: a path+size listing does not change when a file is rewritten in place
# with the same length, so the fingerprint must hash file CONTENT. Both samples
# are taken before the snapshot is unpacked, which is what makes them "before".
live_tree_files() {
  if [ -d "$LIVE" ]; then
    find "$LIVE" -type f 2>/dev/null | wc -l | tr -d ' '
  else
    echo "absent"
  fi
}
live_tree_fp() {
  if [ -d "$LIVE" ]; then
    ( cd "$LIVE" && find . -type f -print0 2>/dev/null | LC_ALL=C sort -z |
        xargs -0 -r sha256sum | sha256sum ) | awk '{print $1}'
  else
    echo "absent"
  fi
}
LIVE_FILES_BEFORE=$(live_tree_files)
LIVE_BEFORE=$(live_tree_fp)

if [ -L "$BATCH" ] || [ -e "$BATCH" ]; then
  echo "SRC_GUARD_FAILED batch_already_exists batch=$BATCH" > "$LOGS/extract.log"
  cat "$LOGS/extract.log" >&2
  exit 2
fi
if [ ! -f "$TAR" ]; then
  echo "TAR_MISSING $TAR" > "$LOGS/extract.log"
  cat "$LOGS/extract.log" >&2
  exit 3
fi
mkdir -p "$SRC"
tar -xzf "$TAR" -C "$SRC"
EX=$?
echo "EXTRACT_EXIT=$EX" > "$LOGS/extract.log"
echo "batch=$BATCH" >> "$LOGS/extract.log"
echo "extract_target=$SRC" >> "$LOGS/extract.log"
echo "live_tree=$LIVE" >> "$LOGS/extract.log"
echo "live_tree_files_before=$LIVE_FILES_BEFORE" >> "$LOGS/extract.log"
echo "live_tree_fingerprint_before=$LIVE_BEFORE" >> "$LOGS/extract.log"
if [ $EX -ne 0 ]; then
  echo "EXTRACT_FAILED exit=$EX" >> "$LOGS/extract.log"
  cat "$LOGS/extract.log" >&2
  exit 3
fi


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
echo "run_id=$RUN"
} > "$LOGS/environment.log" 2>&1

python3 -B - "$SRC" > "$LOGS/source-verify.log" 2>&1 <<'PYEOF'
import hashlib, os, sys
src = sys.argv[1]
manifest = os.path.join(src, "docs/evidence/stage-01/S1-03/source-manifest.txt")
ok = bad = lines = 0
with open(manifest, "r", encoding="utf-8") as f:
    for line in f:
        line = line.rstrip("\n")
        if not line or line.startswith("#"):
            continue
        lines += 1
        want, size, rel = line.split("  ")
        p = os.path.join(src, rel)
        if not os.path.exists(p):
            print("MISSING", rel); bad += 1; continue
        h = hashlib.sha256(open(p, "rb").read()).hexdigest()
        actual = os.path.getsize(p)
        if h == want and str(actual) == size:
            print(rel + ": OK"); ok += 1
        else:
            print(rel + ": MISMATCH"); bad += 1
print("MANIFEST_LINES=%d" % lines)
print("SHACHECK_OK=%d" % ok)
print("SHACHECK_FAILED=%d" % bad)
print("SHACHECK_EXIT=%d" % (0 if bad == 0 else 1))
PYEOF

python3 -B - "$SRC" >> "$LOGS/source-verify.log" 2>&1 <<'PYEOF'
import os, sys
src = sys.argv[1]
roots = [os.path.join(src, "host"), os.path.join(src, "tests")]
stray = []
for r in roots:
    for dp, dn, fn in os.walk(r):
        for f in fn:
            if f.endswith((".o", ".a", ".so", ".pyc")) or f == "CMakeCache.txt":
                stray.append(os.path.join(dp, f))
print("SRC_STRAY_ARTIFACTS=%d" % len(stray))
for s in stray[:10]:
    print("STRAY", s)
print("SRC_FILES=%d" % sum(len(fn) for _, _, fn in os.walk(src)))
PYEOF

# B-16: a failed manifest / stray-artifact check must stop the run, not just print.
SC=$(sed -n 's/^SHACHECK_EXIT=//p' "$LOGS/source-verify.log" | tail -1)
STRAY=$(sed -n 's/^SRC_STRAY_ARTIFACTS=//p' "$LOGS/source-verify.log" | tail -1)
if [ "${SC:-1}" != "0" ] || [ "${STRAY:-1}" != "0" ]; then
  echo "SOURCE_VERIFY_FAILED shacheck_exit=${SC:-missing} stray=${STRAY:-missing}" >&2
  exit 4
fi

cmake -S "$SRC" -B "$BUILD" -DCMAKE_BUILD_TYPE=Debug -DLOCKER_BUILD_HOST=ON -DLOCKER_BUILD_TESTS=ON -DLOCKER_WARNINGS_AS_ERRORS=ON > "$LOGS/configure.log" 2>&1
CE=$?
echo "CONFIGURE_EXIT=$CE" >> "$LOGS/configure.log"
echo "CONFIGURE_WARNING_LINES=$(grep -ci 'warning' "$LOGS/configure.log")" >> "$LOGS/configure.log"

cmake --build "$BUILD" -j2 > "$LOGS/build.log" 2>&1
BE=$?
echo "BUILD_EXIT=$BE" >> "$LOGS/build.log"
echo "BUILD_WARNING_LINES=$(grep -ci 'warning' "$LOGS/build.log")" >> "$LOGS/build.log"
echo "BUILD_ERROR_LINES=$(grep -ci 'error:' "$LOGS/build.log")" >> "$LOGS/build.log"

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
run_one device.test_codec    "$BUILD/tests/unit/device/test_codec"
run_one device.test_wire     "$BUILD/tests/unit/device/test_wire"
run_one device.test_sim_journal  "$BUILD/tests/unit/device/test_sim_journal"
run_one device.test_sim_protocol "$BUILD/tests/unit/device/test_sim_protocol"
run_one contract.device.test_golden_frames "$BUILD/tests/contract/device/test_golden_frames"
run_one contract.device.test_device_port   "$BUILD/tests/contract/device/test_device_port"
echo "TOTAL_ASSERTIONS=$TOTAL" >> "$LOGS/unit-tests.log"

# The daemon binary is not linked by any C test (see tests/contract/device/test_device_port.c:5-8),
# so its own process-level contract case is recorded separately here.
python3 -B "$SRC/tests/contract/device/daemon_smoke.py" "$BUILD/host/simulator/locker-device-sim" "$LOGS/daemon-smoke" > "$LOGS/daemon-smoke.log" 2>&1
echo "DAEMON_SMOKE_EXIT=$?" >> "$LOGS/daemon-smoke.log"

cmake -S "$SRC" -B "$BUILD-asan" -DCMAKE_BUILD_TYPE=Debug -DLOCKER_BUILD_HOST=ON -DLOCKER_BUILD_TESTS=ON -DLOCKER_WARNINGS_AS_ERRORS=ON -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined" > "$LOGS/asan-configure.log" 2>&1
echo "ASAN_CONFIGURE_EXIT=$?" >> "$LOGS/asan-configure.log"
cmake --build "$BUILD-asan" -j2 > "$LOGS/asan-build.log" 2>&1
echo "ASAN_BUILD_EXIT=$?" >> "$LOGS/asan-build.log"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1 ctest --test-dir "$BUILD-asan" --output-on-failure > "$LOGS/asan-ctest.log" 2>&1
echo "ASAN_CTEST_EXIT=$?" >> "$LOGS/asan-ctest.log"
echo "ASAN_REPORTS=$(grep -cE 'ERROR: (AddressSanitizer|LeakSanitizer)|runtime error:' "$LOGS/asan-ctest.log")" >> "$LOGS/asan-ctest.log"

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


# R4-N-4d: a single live_tree_files count cannot prove the live tree was not
# mutated during the run, so fingerprint it again and gate on equality.
LIVE_FILES_AFTER=$(live_tree_files)
LIVE_AFTER=$(live_tree_fp)
LIVE_UNCHANGED=no
if [ "$LIVE_AFTER" = "$LIVE_BEFORE" ] && [ "$LIVE_FILES_AFTER" = "$LIVE_FILES_BEFORE" ]; then
  LIVE_UNCHANGED=yes
fi
echo "live_tree_files_after=$LIVE_FILES_AFTER" >> "$LOGS/extract.log"
echo "live_tree_fingerprint_after=$LIVE_AFTER" >> "$LOGS/extract.log"
echo "live_tree_unchanged=$LIVE_UNCHANGED" >> "$LOGS/extract.log"

{
echo "run_id=$RUN"
echo "batch=$BATCH"
echo "src=$SRC"
echo "build=$BUILD"
echo "logs=$LOGS"
echo "live_tree=$LIVE"
echo "live_tree_fingerprint_before=$LIVE_BEFORE"
echo "live_tree_fingerprint_after=$LIVE_AFTER"
echo "live_tree_unchanged=$LIVE_UNCHANGED"
echo "artifacts=$ART"
echo "snapshot_sha256=$(sha256sum "$TAR" | cut -d' ' -f1)"
echo "snapshot_bytes=$(stat -c %s "$TAR")"
echo "live_tree_files_before=$LIVE_FILES_BEFORE"
echo "live_tree_files_after=$LIVE_FILES_AFTER"
} > "$LOGS/run-summary.log"

# R2-N-2: the gate verdict must be part of the archived evidence, not only the
# terminal. Every exit code is re-read from the log files, so a silently
# swallowed failure cannot pass; the result is appended to run-summary.log
# before the evidence tarball is built.
FAILED=0
GATES="$LOGS/gates.log"
: > "$GATES"
need_zero() {
  label="$1"; file="$2"; key="$3"
  v=$(sed -n "s/^$key=//p" "$file" 2>/dev/null | tail -1)
  if [ "${v:-missing}" != "0" ]; then
    echo "GATE_FAILED $label $key=${v:-missing}" | tee -a "$GATES" >&2
    FAILED=1
  else
    echo "GATE_OK $label $key=0" >> "$GATES"
  fi
}
need_zero configure "$LOGS/configure.log" CONFIGURE_EXIT
need_zero build "$LOGS/build.log" BUILD_EXIT
need_zero ctest "$LOGS/ctest.log" CTEST_EXIT
need_zero asan-configure "$LOGS/asan-configure.log" ASAN_CONFIGURE_EXIT
need_zero asan-build "$LOGS/asan-build.log" ASAN_BUILD_EXIT
need_zero asan-ctest "$LOGS/asan-ctest.log" ASAN_CTEST_EXIT
need_zero asan-reports "$LOGS/asan-ctest.log" ASAN_REPORTS
need_zero tsan-configure "$LOGS/tsan-configure.log" TSAN_CONFIGURE_EXIT
need_zero tsan-build "$LOGS/tsan-build.log" TSAN_BUILD_EXIT
need_zero tsan-ctest "$LOGS/tsan-ctest.log" TSAN_CTEST_EXIT
need_zero tsan-hits "$LOGS/tsan-ctest.log" TSAN_HITS
need_zero daemon-smoke "$LOGS/daemon-smoke.log" DAEMON_SMOKE_EXIT
need_zero daemon-failures "$LOGS/daemon-smoke.log" DAEMON_FAILURES
need_zero manifest "$LOGS/source-verify.log" SHACHECK_EXIT
need_zero stray-artifacts "$LOGS/source-verify.log" SRC_STRAY_ARTIFACTS
if [ "$LIVE_UNCHANGED" = "yes" ]; then
  echo "GATE_OK live-tree-unchanged live_tree_unchanged=yes" >> "$GATES"
else
  echo "GATE_FAILED live-tree-unchanged live_tree_unchanged=$LIVE_UNCHANGED" | tee -a "$GATES" >&2
  FAILED=1
fi
DESIGN_BAD=$(grep -c "^EXIT=[^0]" "$LOGS/design-checks.log" 2>/dev/null || true)
if [ "${DESIGN_BAD:-1}" != "0" ]; then
  echo "GATE_FAILED design-checks nonzero_exits=${DESIGN_BAD:-missing}" | tee -a "$GATES" >&2
  FAILED=1
else
  echo "GATE_OK design-checks nonzero_exits=0" >> "$GATES"
fi
VERDICT=$([ $FAILED -eq 0 ] && echo PASS || echo FAIL)
{
echo "gates_log=gates.log"
cat "$GATES"
echo "VM_RUN_VERDICT=$VERDICT"
} >> "$LOGS/run-summary.log"

cp "$SRC/docs/evidence/stage-01/S1-03/source-manifest.txt" "$LOGS/manifest.sha256" 2>/dev/null
cd "$LOGS" && tar -czf "$ART/s103_vm_evidence_$RUN.tgz" *.log manifest.sha256 2>/dev/null
echo "EVIDENCE_TGZ_SHA256=$(sha256sum "$ART/s103_vm_evidence_$RUN.tgz" | cut -d' ' -f1)"
echo "EVIDENCE_TGZ_BYTES=$(stat -c %s "$ART/s103_vm_evidence_$RUN.tgz")"
echo "RUN=$RUN"
echo "VM_RUN_VERDICT=$VERDICT"
cat "$LOGS/run-summary.log"
[ $FAILED -eq 0 ] || exit 5
