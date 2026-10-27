#ifndef LOCKER_DOMAIN_JSON_H
#define LOCKER_DOMAIN_JSON_H

/*
 * S1-04 业务核心私有 JSON 层。
 *
 * 为什么不用 cJSON：cJSON 属于 host/storage 的私有依赖（host/storage/README.md），
 * 业务核心不得包含仓储或 HTTP 实现类型（docs/architecture/module-ownership.md:23）。
 * 因此这里自带一个**有界**的写者与读者：
 *   - 写入：目标缓冲区固定，越界只置 failed 标志，绝不写穿；
 *   - 读取：节点与成员全部落在调用方提供的定长池里，深度和规模都有上限，
 *     拒绝浮点、指数与尾随垃圾，解析失败一律 fail closed。
 * 两者都不分配内存，也不依赖 locale。
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "locker_contracts.h"

#define LOCKER_DOMAIN_JSON_MAX_NODES 640u
#define LOCKER_DOMAIN_JSON_MAX_MEMBERS 640u
#define LOCKER_DOMAIN_JSON_MAX_DEPTH 12u

typedef struct locker_json_writer {
    char *buffer;
    size_t capacity;
    size_t length;
    bool failed;
} locker_json_writer_t;

void locker_domain_json_writer_init(locker_json_writer_t *writer, char *buffer, size_t capacity);
bool locker_domain_json_writer_ok(const locker_json_writer_t *writer);

/* 结构字符（{ } [ ] , :）专用；调用方自己保证配对。 */
void locker_domain_json_literal(locker_json_writer_t *writer, const char *literal);
void locker_domain_json_key(locker_json_writer_t *writer, const char *key);
void locker_domain_json_string(locker_json_writer_t *writer, const char *text);
void locker_domain_json_string_n(locker_json_writer_t *writer, const char *text, size_t length);
void locker_domain_json_u64(locker_json_writer_t *writer, uint64_t value);
void locker_domain_json_int(locker_json_writer_t *writer, int64_t value);
void locker_domain_json_bool(locker_json_writer_t *writer, bool value);
void locker_domain_json_null(locker_json_writer_t *writer);

typedef enum locker_json_type {
    LOCKER_JSON_NULL = 0,
    LOCKER_JSON_BOOL = 1,
    LOCKER_JSON_INT = 2,
    LOCKER_JSON_STRING = 3,
    LOCKER_JSON_ARRAY = 4,
    LOCKER_JSON_OBJECT = 5
} locker_json_type_t;

typedef struct locker_json_node {
    uint8_t type;
    bool boolean;
    int64_t integer;
    uint32_t text_offset;
    uint32_t text_length;
    uint32_t first; /* 首个子项的成员下标；空为 UINT32_MAX */
    uint32_t last;  /* 末个子项的成员下标；空为 UINT32_MAX */
    uint32_t count;
} locker_json_node_t;

typedef struct locker_json_member {
    uint32_t key_offset;
    uint32_t key_length;
    uint32_t node;
    uint32_t next; /* 同一父节点内的下一个兄弟；末端为 UINT32_MAX */
} locker_json_member_t;

typedef struct locker_json_document {
    const char *text;
    size_t length;
    locker_json_node_t *nodes;
    uint32_t node_capacity;
    uint32_t node_count;
    locker_json_member_t *members;
    uint32_t member_capacity;
    uint32_t member_count;
    uint32_t root;
} locker_json_document_t;

/* 解析失败返回 false；成功时 doc->root 是根节点下标。 */
bool locker_domain_json_parse(locker_json_document_t *doc, const char *text, size_t length);
locker_status_t locker_domain_json_parse_status(const locker_json_document_t *doc);

const locker_json_node_t *locker_domain_json_root(const locker_json_document_t *doc);
/* 对象成员查找（区分大小写）；找不到返回 NULL。 */
const locker_json_node_t *locker_domain_json_get(const locker_json_document_t *doc,
                                                 const locker_json_node_t *object, const char *key);
const locker_json_node_t *locker_domain_json_at(const locker_json_document_t *doc,
                                                const locker_json_node_t *array, uint32_t index);

/* 取字符串值并反转义到 out（含 NUL）；超长或类型不符返回 false。 */
bool locker_domain_json_text(const locker_json_document_t *doc, const locker_json_node_t *node,
                             char *out, size_t out_size, size_t *out_length);
bool locker_domain_json_int64(const locker_json_document_t *doc, const locker_json_node_t *node,
                              int64_t *out_value);
bool locker_domain_json_boolean(const locker_json_document_t *doc, const locker_json_node_t *node,
                                bool *out_value);

/* 把节点重新序列化成紧凑文本（用于幂等响应重放与文档投影）。 */
bool locker_domain_json_render(const locker_json_document_t *doc, const locker_json_node_t *node,
                               char *out, size_t out_size, size_t *out_length);

#endif /* LOCKER_DOMAIN_JSON_H */
