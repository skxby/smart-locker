# Source-only, immutable inputs. All configure/build/install output stays in build.
include(ExternalProject)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
find_package(Perl REQUIRED)
find_program(LOCKER_MAKE NAMES gmake make REQUIRED)
execute_process(COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_SOURCE_DIR}/scripts/check_network_dependencies.py"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" RESULT_VARIABLE verified)
if(NOT verified EQUAL 0)
    message(FATAL_ERROR "Network dependency fingerprint/lock check failed")
endif()
# Two qualified targets: the host simulator (Linux x86_64) and the RK3568
# cabinet controller (Linux aarch64, S2-06). Any other triple must be qualified
# before it is built rather than silently producing an unverified library.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "Network dependencies are only wired for Linux targets")
endif()
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64")
    set(LOCKER_OPENSSL_TARGET "linux-x86_64")
    set(LOCKER_OPENSSL_CROSS_ARG "")
    set(LOCKER_SODIUM_HOST_ARGS "")
    message(STATUS "Network dependencies: linux-x86_64 (native)")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    set(LOCKER_OPENSSL_TARGET "linux-aarch64")
    set(LOCKER_OPENSSL_CROSS_ARG "--cross-compile-prefix=aarch64-linux-gnu-")
    set(LOCKER_SODIUM_HOST_ARGS "--host=aarch64-linux-gnu")
    message(STATUS "Network dependencies: linux-aarch64 (cross)")
else()
    message(FATAL_ERROR
        "Network dependencies are not qualified for ${CMAKE_SYSTEM_PROCESSOR}; "
        "add a target triple here before building the service for it")
endif()
set(prefix "${CMAKE_BINARY_DIR}/network-dependencies")
file(MAKE_DIRECTORY "${prefix}/include" "${prefix}/lib")
ExternalProject_Add(locker_openssl_build
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/openssl"
    BINARY_DIR "${prefix}/openssl-build"
    DOWNLOAD_COMMAND "" UPDATE_COMMAND "" PATCH_COMMAND ""
    CONFIGURE_COMMAND "${PERL_EXECUTABLE}" <SOURCE_DIR>/Configure ${LOCKER_OPENSSL_TARGET}
        no-shared no-tests no-docs no-module "--prefix=${prefix}" --libdir=lib -fPIC
        ${LOCKER_OPENSSL_CROSS_ARG}
    BUILD_COMMAND "${LOCKER_MAKE}" -j2
    INSTALL_COMMAND "${LOCKER_MAKE}" install_sw
    BUILD_BYPRODUCTS "${prefix}/lib/libssl.a" "${prefix}/lib/libcrypto.a"
    LOG_CONFIGURE ON LOG_BUILD ON LOG_INSTALL ON)
ExternalProject_Add(locker_sodium_build
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libsodium"
    BINARY_DIR "${prefix}/sodium-build"
    DOWNLOAD_COMMAND "" UPDATE_COMMAND "" PATCH_COMMAND ""
    CONFIGURE_COMMAND sh <SOURCE_DIR>/configure --disable-shared --enable-static
        --with-pic "--prefix=${prefix}" ${LOCKER_SODIUM_HOST_ARGS}
    BUILD_COMMAND "${LOCKER_MAKE}" -j2
    INSTALL_COMMAND "${LOCKER_MAKE}" install
    BUILD_BYPRODUCTS "${prefix}/lib/libsodium.a"
    LOG_CONFIGURE ON LOG_BUILD ON LOG_INSTALL ON)
foreach(name IN ITEMS ssl crypto sodium)
    add_library(locker_${name} STATIC IMPORTED GLOBAL)
    set_target_properties(locker_${name} PROPERTIES
        IMPORTED_LOCATION "${prefix}/lib/lib${name}.a"
        INTERFACE_INCLUDE_DIRECTORIES "${prefix}/include")
endforeach()
add_dependencies(locker_ssl locker_openssl_build)
add_dependencies(locker_crypto locker_openssl_build)
add_dependencies(locker_sodium locker_sodium_build)
target_link_libraries(locker_ssl INTERFACE locker_crypto Threads::Threads ${CMAKE_DL_LIBS})
# Preserve pinned vendor bytes. Apply the service's receive-only control-frame
# policy to a generated build copy before CivetWeb can allocate payload memory.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/civetweb/src/civetweb.c" locker_civetweb_source)
set(locker_ws_anchor "unsigned char *data = mem;")
string(REGEX MATCHALL "unsigned char [*]data = mem" locker_ws_matches "${locker_civetweb_source}")
list(LENGTH locker_ws_matches locker_ws_match_count)
if(NOT locker_ws_match_count EQUAL 1)
    message(FATAL_ERROR "Pinned CivetWeb websocket frame guard anchor changed")
endif()
set(locker_ws_guard "if (data_len > 125 || mask_len != 4 || !(buf[0] & 0x80) || (buf[0] & 0x70) || ((buf[0] & 15) != 8 && (buf[0] & 15) != 9 && (buf[0] & 15) != 10)) { break; }\nunsigned char *data = mem;")
string(REPLACE "${locker_ws_anchor}" "${locker_ws_guard}" locker_civetweb_bounded "${locker_civetweb_source}")
# CivetWeb 1.16's default volatile stop flag is shared by mg_stop/workers.
# C11 atomics preserve its int layout while providing real synchronization.
string(REGEX MATCHALL "typedef int volatile stop_flag_t" locker_stop_matches "${locker_civetweb_bounded}")
list(LENGTH locker_stop_matches locker_stop_match_count)
if(NOT locker_stop_match_count EQUAL 1)
    message(FATAL_ERROR "Pinned CivetWeb stop flag integration anchor changed")
endif()
string(REPLACE "typedef int volatile stop_flag_t;" "typedef _Atomic int stop_flag_t;" locker_civetweb_bounded "${locker_civetweb_bounded}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/civetweb-bounded.c" "${locker_civetweb_bounded}")
add_library(locker_civetweb STATIC "${CMAKE_CURRENT_BINARY_DIR}/civetweb-bounded.c")
target_compile_features(locker_civetweb PRIVATE c_std_11)
target_include_directories(locker_civetweb PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/civetweb/src")
target_include_directories(locker_civetweb PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/civetweb/include")
target_compile_definitions(locker_civetweb PRIVATE USE_WEBSOCKET NO_SSL_DL
    OPENSSL_API_3_0 NO_CGI NO_CACHING NO_FILES)
target_link_libraries(locker_civetweb PUBLIC locker_ssl Threads::Threads ${CMAKE_DL_LIBS})
set_target_properties(locker_civetweb PROPERTIES POSITION_INDEPENDENT_CODE ON)
