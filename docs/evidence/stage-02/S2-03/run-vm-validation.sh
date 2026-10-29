set -eu
w="$HOME/locker-workspace"
s="$w/S2-03/src"
l="$w/logs/S2-03/final"
mkdir -p "$l"
tar -xf "$w/artifacts/S2-03/source.tar" -C "$s"
cd "$s"
python3 -c 'import hashlib,json,pathlib; m=json.loads(pathlib.Path("/home/%USER%/locker-workspace/artifacts/S2-03/source-manifest.json").read_text()); assert all(hashlib.sha256(pathlib.Path(n).read_bytes()).hexdigest()==h for n,h in m.items()); print("SHA256 matched",len(m),"files")' > "$l/source-hashes.log"
uname -a > "$l/environment.log"
gcc --version >> "$l/environment.log"
cmake --version >> "$l/environment.log"
run() { name="$1"; shift; set +e; "$@" > "$l/$name.log" 2>&1; rc=$?; set -e; echo "$name exit=$rc" >> "$l/results.log"; if [ "$rc" -ne 0 ]; then cat "$l/$name.log"; exit "$rc"; fi; }
: > "$l/results.log"
run debug-configure cmake -S "$s/tests/firmware/transport" -B "$w/build/S2-03/debug" -DCMAKE_BUILD_TYPE=Debug
run debug-build cmake --build "$w/build/S2-03/debug" -j2
run debug-ctest ctest --test-dir "$w/build/S2-03/debug" --output-on-failure
run asan-configure cmake -S "$s/tests/firmware/transport" -B "$w/build/S2-03/asan" -DCMAKE_BUILD_TYPE=Debug '-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie' '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined -no-pie'
run asan-build cmake --build "$w/build/S2-03/asan" -j2
run asan-ctest env ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir "$w/build/S2-03/asan" --output-on-failure
run tsan-configure cmake -S "$s/tests/firmware/transport" -B "$w/build/S2-03/tsan" -DCMAKE_BUILD_TYPE=Debug '-DCMAKE_C_FLAGS=-fsanitize=thread -fno-omit-frame-pointer -g' '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=thread'
run tsan-build cmake --build "$w/build/S2-03/tsan" -j2
run tsan-ctest env TSAN_OPTIONS=halt_on_error=1 ctest --test-dir "$w/build/S2-03/tsan" --output-on-failure
run host-build cmake --build "$w/S1-07/batches/s107-matrix01/build-debug" -j2
run host-ctest ctest --test-dir "$w/S1-07/batches/s107-matrix01/build-debug" --output-on-failure
cat "$l/results.log"
cat "$l/debug-ctest.log" "$l/asan-ctest.log" "$l/tsan-ctest.log"
tail -4 "$l/host-ctest.log"
