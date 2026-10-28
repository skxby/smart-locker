add_library(locker_ipc STATIC ipc.c)
target_include_directories(locker_ipc PRIVATE "${CMAKE_SOURCE_DIR}/host/domain/src" PUBLIC "${LOCKER_CONTRACTS_INCLUDE_DIR}")
target_link_libraries(locker_ipc PRIVATE locker_domain locker_sodium Threads::Threads)
locker_apply_warnings(locker_ipc)
