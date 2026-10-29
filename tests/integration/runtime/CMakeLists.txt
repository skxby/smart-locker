find_package(Python3 COMPONENTS Interpreter REQUIRED)

add_executable(test_runtime_health test_health.c)
target_link_libraries(test_runtime_health PRIVATE locker_runtime)
locker_apply_warnings(test_runtime_health)
add_test(NAME runtime.health COMMAND test_runtime_health)
set_tests_properties(runtime.health PROPERTIES TIMEOUT 10)

add_test(NAME runtime.lifecycle COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_lifecycle.py"
    $<TARGET_FILE:locker-service> $<TARGET_FILE:locker-admin>
    $<TARGET_FILE:locker-device-sim>
    "${CMAKE_BINARY_DIR}/network-dependencies/bin/openssl")
set_tests_properties(runtime.lifecycle PROPERTIES TIMEOUT 180)
