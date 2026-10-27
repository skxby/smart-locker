# S1-03 device/unit tests: protocol codec, byte framing and the simulator engine.
#
# Same rule as S1-01/S1-02: no third-party test framework, one executable per
# test file, one CTest case per executable. The engine is driven in-process, so
# these tests need no daemon, no socket and no network.

set(LOCKER_DEVICE_TESTS
    test_codec
    test_wire
    test_sim_journal
    test_sim_protocol
)

foreach(test_name IN LISTS LOCKER_DEVICE_TESTS)
    add_executable(${test_name} ${test_name}.c)
    target_link_libraries(${test_name} PRIVATE locker_simulator)
    target_include_directories(${test_name} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../common")
    set_target_properties(${test_name} PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED ON)
    locker_apply_warnings(${test_name})
    add_test(NAME device.${test_name} COMMAND ${test_name})
    set_tests_properties(device.${test_name} PROPERTIES TIMEOUT 120)
endforeach()
