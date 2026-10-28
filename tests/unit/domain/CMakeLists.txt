# S1-04 business core unit tests.
#
# Same rule as S1-01..S1-03: no third-party test framework, one executable per
# test file, one CTest case per executable, non-zero exit is the only failure
# signal. The core is driven in-process through its public ports plus the
# private domain entry points; the repository, the device adapter and the
# publisher are the fakes in domain_test_support.h, so no SQLite file, no
# socket and no thread scheduling race is involved.

set(LOCKER_DOMAIN_TESTS
    test_domain_text
    test_domain_json
    test_domain_repo
    test_domain_state
    test_domain_device
    test_domain_commands
    test_domain_core
)

# 开发期只编一个文件时用 -DLOCKER_DOMAIN_TESTS_FILTER=<name>；默认编全部。
if(DEFINED LOCKER_DOMAIN_TESTS_FILTER)
    set(LOCKER_DOMAIN_TESTS ${LOCKER_DOMAIN_TESTS_FILTER})
endif()

foreach(test_name IN LISTS LOCKER_DOMAIN_TESTS)
    add_executable(${test_name} ${test_name}.c)
    target_link_libraries(${test_name} PRIVATE locker_domain Threads::Threads)
    target_include_directories(${test_name} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../common"
        "${CMAKE_SOURCE_DIR}/host/domain/src")
    set_target_properties(${test_name} PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED ON)
    locker_apply_warnings(${test_name})
    if(LOCKER_BUILD_NETWORK)
        target_compile_definitions(${test_name} PRIVATE LOCKER_TEST_NETWORK_AUTH=1)
    endif()
    add_test(NAME domain.${test_name} COMMAND ${test_name})
    set_tests_properties(domain.${test_name} PROPERTIES TIMEOUT 120)
endforeach()
