cmake_minimum_required(VERSION 3.16)
project(locker_control_tests C)
enable_testing()
add_executable(test_control test_control.c ../journal/nor_fixture.c
    ../../../firmware/control/control.c ../../../firmware/drivers/inputs.c
    ../../../firmware/journal/journal.c
    ../../../firmware/third_party/littlefs/lfs.c
    ../../../firmware/third_party/littlefs/lfs_util.c)
target_include_directories(test_control PRIVATE ../../../firmware/control
    ../../../firmware/board ../../../firmware/drivers ../../../firmware/journal
    ../../../firmware/third_party/littlefs ../../../contracts/include
    ../../../host/device/include ../journal)
target_compile_definitions(test_control PRIVATE LFS_NO_MALLOC LFS_NO_DEBUG LFS_NO_WARN LFS_NO_ERROR)
target_compile_options(test_control PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
set_target_properties(test_control PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED YES)
add_test(NAME firmware.control_persistent_guards COMMAND test_control)
foreach(qualified IN ITEMS 0 1)
    add_executable(test_lock_${qualified} test_lock_output.c ../../../firmware/board/lock_output.c)
    target_include_directories(test_lock_${qualified} PRIVATE lock-hal-shim ../../../firmware/board ../../../contracts/include)
    target_compile_definitions(test_lock_${qualified} PRIVATE BOARD_LOCK_QUALIFIED=${qualified})
    target_compile_options(test_lock_${qualified} PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
    add_test(NAME firmware.lock_hal_${qualified} COMMAND test_lock_${qualified})
endforeach()
if(UNIX)
    find_package(Threads REQUIRED)
    add_executable(test_assembly test_assembly.c)
    target_include_directories(test_assembly PRIVATE hal-shim ../../../firmware/control
        ../../../firmware/tasks ../../../firmware/transport ../../../firmware/board
        ../../../firmware/drivers ../../../firmware/drivers/flash ../../../firmware/journal
        ../../../firmware/third_party/littlefs ../../../contracts/include ../../../host/device/include)
    target_compile_options(test_assembly PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG -ffunction-sections -fdata-sections)
    target_link_options(test_assembly PRIVATE -Wl,--gc-sections)
    target_link_libraries(test_assembly PRIVATE Threads::Threads)
    set_target_properties(test_assembly PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)
    add_test(NAME firmware.assembly_concurrent_mailboxes COMMAND test_assembly)
    set_tests_properties(firmware.assembly_concurrent_mailboxes PROPERTIES TIMEOUT 30)
endif()
add_subdirectory(../journal journal)
add_subdirectory(../transport transport)
add_subdirectory(../inputs inputs)
add_subdirectory(../board board)
