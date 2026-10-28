#!/bin/sh
# Refresh the changed integration executable only; production sources are
# identical to the already fully sanitized takeover-20261028-final snapshot.
set -eu
batch="$HOME/locker-workspace/S1-04/batches/takeover-20261028-return"
logs="$HOME/locker-workspace/logs/S1-04/takeover-20261028-return"
for name in asan tsan; do
    if [ "$name" = asan ]; then
        flags="-fsanitize=address,undefined -fno-omit-frame-pointer -g"
    else
        flags="-fsanitize=thread -fno-omit-frame-pointer -g"
    fi
    build="$batch/build-$name"
    cmake -S "$batch/src" -B "$build" -DCMAKE_BUILD_TYPE=Debug -DLOCKER_BUILD_TESTS=ON -DLOCKER_WARNINGS_AS_ERRORS=ON -DCMAKE_C_FLAGS="$flags" > "$logs/$name-configure.log" 2>&1
    cmake --build "$build" --target test_core_interrupt_matrix -j2 > "$logs/$name-build.log" 2>&1
    ctest --test-dir "$build" --output-on-failure -R '^integration.test_core_interrupt_matrix$' > "$logs/$name-ctest.log" 2>&1 || { cat "$logs/$name-ctest.log"; exit 1; }
    echo "$name CONFIGURE=0 BUILD=0 INTEGRATION_CTEST=0"
    tail -5 "$logs/$name-ctest.log"
done
