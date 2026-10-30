set -eu
w="$HOME/locker-workspace"
s="$w/S2-05/src-final"
l="$w/logs/S2-05/final"
rm -rf "$s"
mkdir -p "$s" "$l"
tar -xf "$w/artifacts/S2-05/source-final.tar" -C "$s"
cd "$s"
python3 - "$w/artifacts/S2-05/source-final-manifest.json" "$l/source-hashes.log" <<'PY'
import hashlib, json, pathlib, sys
manifest = json.loads(pathlib.Path(sys.argv[1]).read_text())
bad = [n for n, h in manifest.items()
       if hashlib.sha256(pathlib.Path(n).read_bytes()).hexdigest() != h]
assert not bad, bad[:5]
pathlib.Path(sys.argv[2]).write_text('SHA256 matched %d files\n' % len(manifest))
print('SHA256 matched', len(manifest), 'files')
PY
uname -a > "$l/environment.log"
gcc --version | head -1 >> "$l/environment.log"
cmake --version | head -1 >> "$l/environment.log"
run() {
  name="$1"; shift
  set +e; "$@" > "$l/$name.log" 2>&1; rc=$?; set -e
  echo "$name exit=$rc" >> "$l/results.log"
  if [ "$rc" -ne 0 ]; then tail -40 "$l/$name.log"; exit "$rc"; fi
}
: > "$l/results.log"
run debug-configure cmake -S tests/firmware/control -B "$w/build/S2-05/debug-final" -DCMAKE_BUILD_TYPE=Debug
run debug-build cmake --build "$w/build/S2-05/debug-final" -j2
run debug-ctest ctest --test-dir "$w/build/S2-05/debug-final" --output-on-failure
run asan-configure cmake -S tests/firmware/control -B "$w/build/S2-05/asan" -DCMAKE_BUILD_TYPE=Debug \
  '-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie' \
  '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined -no-pie'
run asan-build cmake --build "$w/build/S2-05/asan" -j2
run asan-ctest env ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  ctest --test-dir "$w/build/S2-05/asan" --output-on-failure
run tsan-configure cmake -S tests/firmware/control -B "$w/build/S2-05/tsan" -DCMAKE_BUILD_TYPE=Debug \
  '-DCMAKE_C_FLAGS=-fsanitize=thread -fno-omit-frame-pointer -g' \
  '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=thread'
run tsan-build cmake --build "$w/build/S2-05/tsan" -j2
run tsan-ctest env TSAN_OPTIONS=halt_on_error=1 ctest --test-dir "$w/build/S2-05/tsan" --output-on-failure
cat "$l/results.log"
grep -E "tests passed|tests failed" "$l"/debug-ctest.log "$l"/asan-ctest.log "$l"/tsan-ctest.log
