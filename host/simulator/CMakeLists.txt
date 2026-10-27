# S1-03 host simulator (docs/stages/stage-01-task-cards.md:58-65).
#
# Two targets:
#   locker_simulator    - the socket-free device engine (src/sim.c). It owns the
#                         UART v1 device state machine and its own SQLite journal;
#                         unit tests drive it in-process, so nothing here opens a
#                         socket or touches the business database.
#   locker-device-sim   - the daemon (src/sim_main.c) that exposes the engine over
#                         two AF_UNIX SOCK_STREAM sockets created with mode 0600.
#
# The vendored SQLite amalgamation is reused from host/storage/third_party so the
# device journal has no system dependency. It is a PRIVATE link: the simulator's
# public header exposes no SQLite type.

add_library(locker_simulator STATIC
    src/sim.c
)

target_include_directories(locker_simulator
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${LOCKER_CONTRACTS_INCLUDE_DIR}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(locker_simulator
    PUBLIC
        locker_common
        locker_device
    PRIVATE
        locker_storage_thirdparty
)

set_target_properties(locker_simulator PROPERTIES POSITION_INDEPENDENT_CODE ON)

locker_apply_warnings(locker_simulator)

add_executable(locker-device-sim src/sim_main.c)

target_link_libraries(locker-device-sim PRIVATE locker_simulator)

set_target_properties(locker-device-sim PROPERTIES POSITION_INDEPENDENT_CODE ON)

locker_apply_warnings(locker-device-sim)
