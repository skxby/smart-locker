# S1-03 contract tests: golden frames and the DevicePort transport against a
# real AF_UNIX socket pair (no external daemon, no network, no HTTP).
#
# These tests link the daemon engine and the host DevicePort together so the
# byte-level contract (contracts/examples/uart-golden.json) is exercised end to
# end through the same code paths production uses.

set(LOCKER_DEVICE_CONTRACT_TESTS
    test_golden_frames
    test_device_port
)

foreach(test_name IN LISTS LOCKER_DEVICE_CONTRACT_TESTS)
    add_executable(${test_name} ${test_name}.c)
    target_link_libraries(${test_name} PRIVATE locker_simulator)
    target_include_directories(${test_name} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../unit/common"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../unit/device")
    set_target_properties(${test_name} PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED ON)
    locker_apply_warnings(${test_name})
    add_test(NAME contract.device.${test_name} COMMAND ${test_name})
    set_tests_properties(contract.device.${test_name} PROPERTIES TIMEOUT 120)
endforeach()
# The daemon itself (sim_main.c) is not linked into the C tests above: they own
# an in-process socket pair. The daemon's own surface — two 0600 AF_UNIX sockets,
# UART-v1 bytes in and out, the private control entry, the independent journal —
# is checked by driving the real executable. Python is required for acceptance;
# an absent interpreter must never reduce the test count into a false green.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(
        NAME contract.device.daemon_smoke
        COMMAND "${Python3_EXECUTABLE}" -B
                "${CMAKE_SOURCE_DIR}/tests/contract/device/daemon_smoke.py"
                "${CMAKE_BINARY_DIR}/host/simulator/locker-device-sim"
                "${CMAKE_CURRENT_BINARY_DIR}/daemon-smoke")
    set_tests_properties(contract.device.daemon_smoke PROPERTIES TIMEOUT 120)
    message(STATUS "device contract tests: daemon smoke enabled (${Python3_EXECUTABLE})")
else()
    message(FATAL_ERROR "device daemon acceptance NOT_RUN: Python 3 interpreter required")
endif()
