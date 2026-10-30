# Final scalar ACK cursor validation after the one sanitizer pass (P2).
set -eu
w="$HOME/locker-workspace"
l="$w/logs/S2-04/ack-guard"
mkdir -p "$l"
run() { name="$1"; shift; set +e; "$@" > "$l/$name.log" 2>&1; rc=$?; set -e; echo "$name exit=$rc" >> "$l/results.log"; if [ "$rc" -ne 0 ]; then cat "$l/$name.log"; exit "$rc"; fi; }
: > "$l/results.log"
run debug-build cmake --build "$w/build/S2-04/debug-final" -j2
run debug-ctest ctest --test-dir "$w/build/S2-04/debug-final" --output-on-failure
run host-ctest ctest --test-dir "$w/S1-07/batches/s107-matrix01/build-debug" --output-on-failure
cat "$l/results.log" "$l/debug-ctest.log"
tail -4 "$l/host-ctest.log"
