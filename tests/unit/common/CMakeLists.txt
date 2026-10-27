# S1-01 unit tests for host/common. One executable per test file, registered
# with CTest (docs/stages/stage-01-task-cards.md:123-135).
#
# No third-party test framework is used: the locked dependency set does not
# include one, and offline builds must not fetch anything.

set(LOCKER_COMMON_TESTS
    test_bounded_queue
    test_build_info
    test_clock
    test_log
    test_numeric
    test_uuid
)

foreach(test_name IN LISTS LOCKER_COMMON_TESTS)
    add_executable(${test_name} ${test_name}.c)
    target_link_libraries(${test_name} PRIVATE locker_common)
    target_include_directories(${test_name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    set_target_properties(${test_name} PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
    locker_apply_warnings(${test_name})
    add_test(NAME common.${test_name} COMMAND ${test_name})
    set_tests_properties(common.${test_name} PROPERTIES TIMEOUT 60)
endforeach()
