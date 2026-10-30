add_executable(test_flash ${CMAKE_CURRENT_LIST_DIR}/test_flash.c
    ${CMAKE_CURRENT_LIST_DIR}/../../../firmware/drivers/flash/w25q128.c)
target_include_directories(test_flash PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../../contracts/include
    ${CMAKE_CURRENT_LIST_DIR}/../../../firmware/drivers/flash
    ${CMAKE_CURRENT_LIST_DIR}/../../../firmware/third_party/littlefs)
target_compile_options(test_flash PRIVATE -Wall -Wextra -Werror -UNDEBUG)
add_test(NAME firmware.flash_commands_and_failures COMMAND test_flash)
