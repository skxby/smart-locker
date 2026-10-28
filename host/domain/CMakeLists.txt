# S1-04 业务核心（host/domain）。
#
# 边界（docs/architecture/module-ownership.md:23）：不包含 CivetWeb、SQLite、
# HAL 头，不调用网络或串口阻塞 IO。依赖只来自契约头与 host/common 的公共 API；
# 仓储与设备都经契约端口借用，Core 不认识它们的实现类型。

add_library(locker_domain STATIC
    src/domain_change.c
    src/domain_commands.c
    src/domain_core.c
    src/domain_auth.c
    src/domain_users.c
    src/domain_history.c
    src/domain_device.c
    src/domain_docs.c
    src/domain_import.c
    src/domain_json.c
    src/domain_loan.c
    src/domain_operation.c
    src/domain_repo.c
    src/domain_sha256.c
    src/domain_state.c
    src/domain_text.c
    src/domain_time.c
    src/domain_time_anchor.c
)

target_include_directories(locker_domain
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${LOCKER_CONTRACTS_INCLUDE_DIR}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(locker_domain
    PUBLIC locker_common
    PRIVATE Threads::Threads
)

set_target_properties(locker_domain PROPERTIES POSITION_INDEPENDENT_CODE ON)

locker_apply_warnings(locker_domain)
if(LOCKER_BUILD_NETWORK)
    target_compile_definitions(locker_domain PRIVATE LOCKER_NETWORK_AUTH=1)
    target_link_libraries(locker_domain PRIVATE locker_sodium)
endif()
