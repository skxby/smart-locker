add_executable(locker-admin main.c)
target_link_libraries(locker-admin PRIVATE locker_runtime locker_storage locker_sodium locker_storage_thirdparty)
locker_apply_warnings(locker-admin)
