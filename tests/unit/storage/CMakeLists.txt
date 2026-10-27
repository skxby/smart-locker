# 存储模块单元测试：与 S1-01 相同的无第三方框架（一个可执行文件即一个 CTest 用例）。

set(LOCKER_STORAGE_TESTS
    test_event_key
    test_regex
    test_u64
    test_schema
    test_repository)

foreach(test_name IN LISTS LOCKER_STORAGE_TESTS)
    add_executable(${test_name} ${test_name}.c)
    target_link_libraries(${test_name} PRIVATE locker_storage)
    # 测试需要用 cJSON 读查询结果、用 sqlite3 直接构造「损坏/元数据被篡改」夹具，
    # 因此额外暴露 third_party 头目录。这是测试本地接入，不进入 locker_storage 的
    # 公共包含路径——产品代码仍然只能通过契约端口访问仓储。
    target_include_directories(${test_name} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../common"
        "${CMAKE_SOURCE_DIR}/host/storage/third_party/cjson"
        "${CMAKE_SOURCE_DIR}/host/storage/third_party/sqlite")
    set_target_properties(${test_name} PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED ON)
    locker_apply_warnings(${test_name})
    add_test(NAME storage.${test_name} COMMAND ${test_name})
    set_tests_properties(storage.${test_name} PROPERTIES TIMEOUT 120)
endforeach()

# Contract-drift gate for the frozen schema subset (audit N5 of S1-02).
#
# host/storage/src/schema_records.json is derived mechanically from
# contracts/internal-data.schema.json by host/storage/tools/extract_schema.py.
# Embedding a stale copy would silently validate business documents against an
# outdated contract, so the derivation is re-checked as a CTest case. Without a
# Python 3 interpreter the gate is reported as SKIPPED: the C tests are the real
# acceptance and must stay runnable on a machine without Python.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(
        NAME storage.schema_source_drift
        COMMAND "${Python3_EXECUTABLE}" -B
                "${CMAKE_SOURCE_DIR}/host/storage/tools/extract_schema.py" --check
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    set_tests_properties(storage.schema_source_drift PROPERTIES TIMEOUT 120)
    message(STATUS "storage tests: contract-drift gate enabled (${Python3_EXECUTABLE})")
else()
    message(STATUS "storage tests: contract-drift gate SKIPPED (no Python 3 interpreter)")
endif()
