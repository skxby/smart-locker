foreach(qualified IN ITEMS 0 1)
    add_executable(test_communication_${qualified} test_communication.c
        ../../../firmware/transport/transport.c
        ../../../host/device/src/wire.c ../../../host/device/src/codec.c)
    target_include_directories(test_communication_${qualified} PRIVATE
        hal-shim ../../../firmware/tasks ../../../firmware/board
        ../../../firmware/transport ../../../host/device/include ../../../contracts/include)
    target_compile_definitions(test_communication_${qualified} PRIVATE BOARD_UART_QUALIFIED=${qualified})
    target_compile_options(test_communication_${qualified} PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
    set_target_properties(test_communication_${qualified} PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED YES)
    add_test(NAME firmware.communication_${qualified} COMMAND test_communication_${qualified})
endforeach()
