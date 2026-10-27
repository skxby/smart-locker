# S1-03 host device adapter (docs/stages/stage-01-task-cards.md:58-65).
#
# locker_device owns the UART v1 byte framing, the payload codec and the
# DevicePort implementation that talks to the simulator over AF_UNIX
# SOCK_STREAM. It holds no business state and no HTTP surface.

add_library(locker_device STATIC
    src/codec.c
    src/port.c
    src/transport.c
    src/wire.c
)

target_include_directories(locker_device
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${LOCKER_CONTRACTS_INCLUDE_DIR}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(locker_device
    PUBLIC
        locker_common
    PRIVATE
        Threads::Threads
)

set_target_properties(locker_device PROPERTIES POSITION_INDEPENDENT_CODE ON)

locker_apply_warnings(locker_device)
