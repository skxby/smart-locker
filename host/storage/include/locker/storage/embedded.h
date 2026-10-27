#ifndef LOCKER_STORAGE_EMBEDDED_H
#define LOCKER_STORAGE_EMBEDDED_H

/*
 * 构建期嵌入的只读资源（模块内部头文件，不是契约公共 API）。
 *
 * CMake 在构建期（add_custom_command）用 cmake/embed_file.cmake 把两个真源文件转成 C 字符串字面量：
 *   - schema-v1.sql                      -> locker_storage_schema_sql
 *   - src/schema_records.json            -> locker_storage_schema_json
 * 运行时不再从磁盘读取 DDL 或 schema 子集，部署目录里少一个文件就少一种漂移
 * （schema-v1.sql:12-13 的设计决定）。
 *
 * 两个数组都以 NUL 结尾，size 不含结尾 NUL；内容与真源文件逐字节相同。
 */

#include <stddef.h>

extern const char locker_storage_schema_sql[];
extern const size_t locker_storage_schema_sql_size;
extern const char locker_storage_schema_json[];
extern const size_t locker_storage_schema_json_size;

#endif
