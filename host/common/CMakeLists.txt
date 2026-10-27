# S1-01 host/common: bounded queues, clocks, UUID/u64 helpers, redacting logs.
# No third-party dependencies and no business state
# (docs/architecture/module-ownership.md:22).

add_library(locker_common STATIC
    src/bounded_queue.c
    src/build_info.c
    src/clock.c
    src/log.c
    src/numeric.c
    src/uuid.c
)

target_include_directories(locker_common
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${LOCKER_CONTRACTS_INCLUDE_DIR}"
)

target_link_libraries(locker_common PUBLIC Threads::Threads)

set_target_properties(locker_common PROPERTIES POSITION_INDEPENDENT_CODE ON)

locker_apply_warnings(locker_common)
