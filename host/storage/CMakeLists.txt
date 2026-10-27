# S1-02 host/storage: the single SQLite repository (docs/architecture/module-ownership.md:24).
#
# Three targets:
#   locker_storage_thirdparty  - locked SQLite 3.54.1 amalgamation + cJSON 1.7.19
#                                (vendored sources, see deployment/host/dependencies.lock.json).
#                                Compiled without the project warning gate: third-party
#                                code is not edited here and must not fail -Werror.
#   locker_storage_generated   - build-time embedding (add_custom_command) of
#                                schema-v1.sql and the frozen contract schema subset
#                                into C string literals.
#   locker_storage             - the repository itself (src/*.c).
#
# Build artifacts (generated .inc) only ever land in the build directory.

set(LOCKER_STORAGE_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(LOCKER_STORAGE_SCHEMA_SQL_INC "${LOCKER_STORAGE_GENERATED_DIR}/schema_sql.inc")
set(LOCKER_STORAGE_SCHEMA_JSON_INC "${LOCKER_STORAGE_GENERATED_DIR}/schema_json.inc")

add_library(locker_storage_thirdparty STATIC
    third_party/sqlite/sqlite3.c
    third_party/cjson/cJSON.c
)

target_include_directories(locker_storage_thirdparty
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cjson"
)

# Locked build shape of SQLite (no extension loading, no dlopen, no shared cache
# surprises). JSON1 is compiled in by default since 3.38 and is used by the
# repository only for its own validation helpers, never for business decisions.
target_compile_definitions(locker_storage_thirdparty PRIVATE
    SQLITE_THREADSAFE=1
    SQLITE_OMIT_LOAD_EXTENSION=1
    SQLITE_DEFAULT_MEMSTATUS=0
    SQLITE_DQS=0
    SQLITE_USE_URI=1
)

set_target_properties(locker_storage_thirdparty PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_custom_command(
    OUTPUT "${LOCKER_STORAGE_SCHEMA_SQL_INC}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${LOCKER_STORAGE_GENERATED_DIR}"
    COMMAND "${CMAKE_COMMAND}"
            "-DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/schema-v1.sql"
            "-DOUTPUT=${LOCKER_STORAGE_SCHEMA_SQL_INC}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_file.cmake"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/schema-v1.sql"
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_file.cmake"
    COMMENT "Embedding host/storage/schema-v1.sql"
    VERBATIM
)

add_custom_command(
    OUTPUT "${LOCKER_STORAGE_SCHEMA_JSON_INC}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${LOCKER_STORAGE_GENERATED_DIR}"
    COMMAND "${CMAKE_COMMAND}"
            "-DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/src/schema_records.json"
            "-DOUTPUT=${LOCKER_STORAGE_SCHEMA_JSON_INC}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_file.cmake"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/schema_records.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_file.cmake"
    COMMENT "Embedding host/storage/src/schema_records.json"
    VERBATIM
)

add_custom_target(locker_storage_generated
    DEPENDS "${LOCKER_STORAGE_SCHEMA_SQL_INC}" "${LOCKER_STORAGE_SCHEMA_JSON_INC}"
)

add_library(locker_storage STATIC
    src/event_key.c
    src/regex.c
    src/repository.c
    src/schema.c
    src/schema_embedded.c
    src/u64.c
)

add_dependencies(locker_storage locker_storage_generated)

target_include_directories(locker_storage
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${LOCKER_CONTRACTS_INCLUDE_DIR}"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${LOCKER_STORAGE_GENERATED_DIR}"
)

target_link_libraries(locker_storage
    PUBLIC locker_common
    PRIVATE locker_storage_thirdparty
)

set_target_properties(locker_storage PROPERTIES POSITION_INDEPENDENT_CODE ON)

locker_apply_warnings(locker_storage)
