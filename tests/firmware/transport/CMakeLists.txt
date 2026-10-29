cmake_minimum_required(VERSION 3.16)
project(locker_transport C)
enable_testing()
add_executable(test_transport test_transport.c
    ../../../firmware/transport/transport.c
    ../../../host/device/src/wire.c ../../../host/device/src/codec.c)
target_include_directories(test_transport PRIVATE ../../../firmware/transport
    ../../../host/device/include ../../../contracts/include)
set_target_properties(test_transport PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED YES)
target_compile_options(test_transport PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
add_test(NAME firmware.transport COMMAND test_transport)
include(hal-test.cmake OPTIONAL)
