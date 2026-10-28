# S1-01 root host build entry (docs/architecture/module-ownership.md:33).
#
# Single host build entry point: module cards add their own local CMakeLists.txt
# and are pulled in from here. This file only wires options, the generated
# contract baseline and subdirectories; it holds no business sources.
#
# Documented commands (docs/stages/stage-01-task-cards.md:125-135):
#   cmake -S . -B build/host -DLOCKER_BUILD_HOST=ON -DLOCKER_BUILD_TESTS=ON
#   cmake --build build/host
#   ctest --test-dir build/host --output-on-failure

cmake_minimum_required(VERSION 3.16)

project(locker_host LANGUAGES C)

option(LOCKER_BUILD_HOST "Build the host service libraries" ON)
option(LOCKER_BUILD_TESTS "Build the host unit test suite (CTest)" OFF)
option(LOCKER_WARNINGS_AS_ERRORS "Promote compiler warnings to errors" OFF)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "Build type" FORCE)
endif()

set(LOCKER_CONTRACTS_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/contracts/include"
    CACHE PATH "Directory holding the generated contract headers")
set(LOCKER_CONTRACTS_HEADER "${LOCKER_CONTRACTS_INCLUDE_DIR}/locker_contracts.h")
set(LOCKER_CONTRACT_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/contracts/contract-source.json")

if(NOT EXISTS "${LOCKER_CONTRACTS_HEADER}")
    message(FATAL_ERROR
        "Generated contract header not found: ${LOCKER_CONTRACTS_HEADER} - regenerate with: python -B scripts/build_contracts.py")
endif()
if(NOT EXISTS "${LOCKER_CONTRACT_SOURCE}")
    message(FATAL_ERROR "Contract source not found: ${LOCKER_CONTRACT_SOURCE}")
endif()

# Configure-time drift gate: the header the service compiles against must match
# the contract source it was generated from. This does not replace
# scripts/check_contracts.py; it fails the build early on a stale copy.
file(READ "${LOCKER_CONTRACTS_HEADER}" LOCKER_CONTRACTS_HEADER_TEXT)
string(REGEX MATCH "#define LOCKER_CONTRACT_VERSION \"([0-9]+\\.[0-9]+\\.[0-9]+)\"" _locker_header_match "${LOCKER_CONTRACTS_HEADER_TEXT}")
set(LOCKER_HEADER_CONTRACT_VERSION "${CMAKE_MATCH_1}")

file(READ "${LOCKER_CONTRACT_SOURCE}" LOCKER_CONTRACT_SOURCE_TEXT)
string(REGEX MATCH "\"contract_version\"[ \\t]*:[ \\t]*\"([0-9]+\\.[0-9]+\\.[0-9]+)\"" _locker_source_match "${LOCKER_CONTRACT_SOURCE_TEXT}")
set(LOCKER_SOURCE_CONTRACT_VERSION "${CMAKE_MATCH_1}")

if(LOCKER_HEADER_CONTRACT_VERSION STREQUAL "")
    message(FATAL_ERROR "Could not read LOCKER_CONTRACT_VERSION from ${LOCKER_CONTRACTS_HEADER}")
endif()
if(LOCKER_SOURCE_CONTRACT_VERSION STREQUAL "")
    message(FATAL_ERROR "Could not read contract_version from ${LOCKER_CONTRACT_SOURCE}")
endif()
if(NOT LOCKER_HEADER_CONTRACT_VERSION STREQUAL LOCKER_SOURCE_CONTRACT_VERSION)
    message(FATAL_ERROR
        "Contract version drift: header=${LOCKER_HEADER_CONTRACT_VERSION} source=${LOCKER_SOURCE_CONTRACT_VERSION} - regenerate with: python -B scripts/build_contracts.py")
endif()

message(STATUS "locker host build: contract ${LOCKER_HEADER_CONTRACT_VERSION}")

function(locker_apply_warnings target)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        if(LOCKER_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    elseif(MSVC)
        target_compile_options(${target} PRIVATE /W4)
        if(LOCKER_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    endif()
endfunction()

if(NOT LOCKER_BUILD_HOST)
    message(STATUS "LOCKER_BUILD_HOST=OFF: nothing to build")
    return()
endif()

find_package(Threads REQUIRED)

add_subdirectory(host/common)
add_subdirectory(host/storage)
add_subdirectory(host/device)
add_subdirectory(host/simulator)
add_subdirectory(host/domain)

if(LOCKER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests/unit/common)
    add_subdirectory(tests/unit/storage)
    add_subdirectory(tests/unit/device)
    add_subdirectory(tests/unit/domain)
    add_subdirectory(tests/contract/device)
    add_subdirectory(tests/integration)
endif()
