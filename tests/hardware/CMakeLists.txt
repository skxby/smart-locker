find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME hardware.bench_client_boundaries
    COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/test_bench_client.py")
set_tests_properties(hardware.bench_client_boundaries PROPERTIES TIMEOUT 20)
