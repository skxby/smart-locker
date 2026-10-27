#include "locker/storage/embedded.h"

/*
 * 本文件是手写的；相邻的 .inc 由 host/storage/CMakeLists.txt 在构建期用
 * cmake/embed_file.cmake 从 host/storage/schema-v1.sql 与
 * host/storage/src/schema_records.json 生成（逐字节 (char)0xNN，二进制安全）。
 * 生成物只在构建目录里，不落回源码树。
 *
 * 末尾显式补 (char)0x00 作为 NUL 终止符，所以 size 用 sizeof - 1u 得到真源字节数。
 */

const char locker_storage_schema_sql[] = {
#include "schema_sql.inc"
    (char)0x00};

const size_t locker_storage_schema_sql_size = sizeof(locker_storage_schema_sql) - 1u;

const char locker_storage_schema_json[] = {
#include "schema_json.inc"
    (char)0x00};

const size_t locker_storage_schema_json_size = sizeof(locker_storage_schema_json) - 1u;
