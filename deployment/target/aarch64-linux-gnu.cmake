# S2-06: aarch64 cross toolchain for the RK3568 cabinet controller.
#
# The board runs Buildroot 2018.02-rc3 (aarch64, glibc older than the build
# host's). A dynamically linked binary would fail there with
# "GLIBC_2.xx not found", so the service is linked statically; that is the whole
# reason this file also forces -static rather than leaving it to the caller.
#
#   cmake -S . -B build/rk3568 \
#       -DCMAKE_TOOLCHAIN_FILE=deployment/target/aarch64-linux-gnu.cmake \
#       -DLOCKER_BUILD_HOST=ON -DLOCKER_BUILD_NETWORK=ON -DLOCKER_BUILD_TESTS=OFF
#   cmake --build build/rk3568 -j
#
# Tests are not built here: the target has no test harness on the board and the
# host suite runs natively on x86_64.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

set(CMAKE_FIND_ROOT_PATH /usr/aarch64-linux-gnu)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")
