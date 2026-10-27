#define _POSIX_C_SOURCE 200809L

#include "domain_json.h"

#include <string.h>

/* ---------- 写者 ---------- */

void locker_domain_json_writer_init(locker_json_writer_t *writer, char *buffer, size_t capacity)
{
    if (writer == NULL) {
        return;
    }
    writer->buffer = buffer;
    writer->capacity = capacity;
    writer->length = 0u;
    writer->failed = (buffer == NULL || capacity == 0u);
    if (!writer->failed) {
        buffer[0] = '\0';
    }
}

bool locker_domain_json_writer_ok(const locker_json_writer_t *writer)
{
    return writer != NULL && !writer->failed;
}

static void writer_append(locker_json_writer_t *writer, const char *text, size_t length)
{
    if (writer == NULL || writer->failed) {
        return;
    }
    if (writer->buffer == NULL || writer->capacity == 0u || writer->length + 1u > writer->capacity) {
        writer->failed = true;
        return;
    }
    if (length > writer->capacity - writer->length - 1u) {
        writer->failed = true;
        return;
    }
    memcpy(writer->buffer + writer->length, text, length);
    writer->length += length;
    writer->buffer[writer->length] = '\0';
}

void locker_domain_json_literal(locker_json_writer_t *writer, const char *literal)
{
    if (literal == NULL) {
        if (writer != NULL) {
            writer->failed = true;
        }
        return;
    }
    writer_append(writer, literal, strlen(literal));
}

/* 只写转义后的内容，不加首尾引号：render 需要「解码后再转义」分段输出。 */
static void writer_escaped_body(locker_json_writer_t *writer, const char *text, size_t length)
{
    static const char k_hex[] = "0123456789abcdef";
    if (writer == NULL || writer->failed) {
        return;
    }
    if (text == NULL) {
        writer->failed = true;
        return;
    }
    size_t start = 0u;
    for (size_t i = 0u; i < length; ++i) {
        unsigned char byte = (unsigned char)text[i];
        const char *escape = NULL;
        char buffer[7];
        size_t escape_length = 0u;
        switch (byte) {
        case '"':
            escape = "\\\"";
            escape_length = 2u;
            break;
        case '\\':
            escape = "\\\\";
            escape_length = 2u;
            break;
        case '\b':
            escape = "\\b";
            escape_length = 2u;
            break;
        case '\f':
            escape = "\\f";
            escape_length = 2u;
            break;
        case '\n':
            escape = "\\n";
            escape_length = 2u;
            break;
        case '\r':
            escape = "\\r";
            escape_length = 2u;
            break;
        case '\t':
            escape = "\\t";
            escape_length = 2u;
            break;
        default:
            break;
        }
        if (escape == NULL && byte < 0x20u) {
            buffer[0] = '\\';
            buffer[1] = 'u';
            buffer[2] = '0';
            buffer[3] = '0';
            buffer[4] = k_hex[(byte >> 4) & 0x0fu];
            buffer[5] = k_hex[byte & 0x0fu];
            buffer[6] = '\0';
            escape = buffer;
            escape_length = 6u;
        }
        if (escape != NULL) {
            if (i > start) {
                writer_append(writer, text + start, i - start);
            }
            writer_append(writer, escape, escape_length);
            start = i + 1u;
        }
    }
    if (length > start) {
        writer_append(writer, text + start, length - start);
    }
}

static void writer_quoted(locker_json_writer_t *writer, const char *text, size_t length)
{
    if (writer == NULL || writer->failed) {
        return;
    }
    writer_append(writer, "\"", 1u);
    writer_escaped_body(writer, text, length);
    writer_append(writer, "\"", 1u);
}

void locker_domain_json_string(locker_json_writer_t *writer, const char *text)
{
    if (text == NULL) {
        if (writer != NULL) {
            writer->failed = true;
        }
        return;
    }
    writer_quoted(writer, text, strlen(text));
}

void locker_domain_json_string_n(locker_json_writer_t *writer, const char *text, size_t length)
{
    writer_quoted(writer, text, length);
}

void locker_domain_json_key(locker_json_writer_t *writer, const char *key)
{
    locker_domain_json_string(writer, key);
    locker_domain_json_literal(writer, ":");
}

void locker_domain_json_u64(locker_json_writer_t *writer, uint64_t value)
{
    char digits[21];
    size_t count = 0u;
    if (value == 0u) {
        digits[count++] = '0';
    } else {
        char reversed[21];
        size_t used = 0u;
        while (value > 0u) {
            reversed[used++] = (char)('0' + (int)(value % 10u));
            value /= 10u;
        }
        while (used > 0u) {
            digits[count++] = reversed[--used];
        }
    }
    digits[count] = '\0';
    writer_append(writer, digits, count);
}

void locker_domain_json_int(locker_json_writer_t *writer, int64_t value)
{
    if (value < 0) {
        writer_append(writer, "-", 1u);
        uint64_t magnitude = (uint64_t)(-(value + 1)) + 1u;
        locker_domain_json_u64(writer, magnitude);
        return;
    }
    locker_domain_json_u64(writer, (uint64_t)value);
}

void locker_domain_json_bool(locker_json_writer_t *writer, bool value)
{
    writer_append(writer, value ? "true" : "false", value ? 4u : 5u);
}

void locker_domain_json_null(locker_json_writer_t *writer)
{
    writer_append(writer, "null", 4u);
}

/* ---------- 读者 ---------- */

typedef struct json_parser {
    const char *text;
    size_t length;
    size_t position;
    locker_json_document_t *doc;
    unsigned depth;
    bool failed;
} json_parser_t;

static locker_status_t parser_error(void)
{
    return LOCKER_STATUS_INVALID;
}

static void skip_whitespace(json_parser_t *parser)
{
    while (parser->position < parser->length) {
        char byte = parser->text[parser->position];
        if (byte == ' ' || byte == '\t' || byte == '\n' || byte == '\r') {
            parser->position += 1u;
        } else {
            break;
        }
    }
}

static uint32_t node_new(json_parser_t *parser, locker_json_type_t type)
{
    if (parser->doc->node_count >= parser->doc->node_capacity) {
        parser->failed = true;
        return UINT32_MAX;
    }
    uint32_t index = parser->doc->node_count++;
    locker_json_node_t *node = &parser->doc->nodes[index];
    memset(node, 0, sizeof(*node));
    node->type = (uint8_t)type;
    node->first = UINT32_MAX;
    node->last = UINT32_MAX;
    return index;
}

static uint32_t member_new(json_parser_t *parser, uint32_t key_offset, uint32_t key_length,
                           uint32_t node)
{
    if (parser->doc->member_count >= parser->doc->member_capacity) {
        parser->failed = true;
        return UINT32_MAX;
    }
    uint32_t index = parser->doc->member_count++;
    locker_json_member_t *member = &parser->doc->members[index];
    member->key_offset = key_offset;
    member->key_length = key_length;
    member->node = node;
    member->next = UINT32_MAX;
    return index;
}

static bool parse_value(json_parser_t *parser, uint32_t *out_index);

static bool parse_string_token(json_parser_t *parser, uint32_t *out_offset, uint32_t *out_length)
{
    if (parser->position >= parser->length || parser->text[parser->position] != '"') {
        return false;
    }
    parser->position += 1u;
    size_t start = parser->position;
    bool escaped = false;
    while (parser->position < parser->length) {
        unsigned char byte = (unsigned char)parser->text[parser->position];
        if (byte == (unsigned char)'"') {
            *out_offset = (uint32_t)start;
            *out_length = (uint32_t)(parser->position - start);
            parser->position += 1u;
            return !escaped || true;
        }
        if (byte < 0x20u) {
            return false;
        }
        if (byte == (unsigned char)'\\') {
            escaped = true;
            parser->position += 1u;
            if (parser->position >= parser->length) {
                return false;
            }
            char escape = parser->text[parser->position];
            if (escape == 'u') {
                if (parser->position + 4u >= parser->length) {
                    return false;
                }
                for (size_t i = 1u; i <= 4u; ++i) {
                    char digit = parser->text[parser->position + i];
                    bool hex = (digit >= '0' && digit <= '9') || (digit >= 'a' && digit <= 'f') ||
                               (digit >= 'A' && digit <= 'F');
                    if (!hex) {
                        return false;
                    }
                }
                parser->position += 4u;
            } else if (escape != '"' && escape != '\\' && escape != '/' && escape != 'b' &&
                       escape != 'f' && escape != 'n' && escape != 'r' && escape != 't') {
                return false;
            }
            parser->position += 1u;
            continue;
        }
        parser->position += 1u;
    }
    return false;
}

static bool parse_number(json_parser_t *parser, int64_t *out_value)
{
    size_t start = parser->position;
    bool negative = false;
    if (parser->position < parser->length && parser->text[parser->position] == '-') {
        negative = true;
        parser->position += 1u;
    }
    size_t digits_start = parser->position;
    while (parser->position < parser->length) {
        char byte = parser->text[parser->position];
        if (byte < '0' || byte > '9') {
            break;
        }
        parser->position += 1u;
    }
    size_t digits = parser->position - digits_start;
    if (digits == 0u) {
        return false;
    }
    /* 只接受整数：拒绝小数点、指数与多余前导零（除单独的 0）。 */
    if (parser->position < parser->length) {
        char byte = parser->text[parser->position];
        if (byte == '.' || byte == 'e' || byte == 'E') {
            return false;
        }
    }
    if (digits > 1u && parser->text[digits_start] == '0') {
        return false;
    }
    if (digits > 19u) {
        return false;
    }
    uint64_t magnitude = 0u;
    for (size_t i = 0u; i < digits; ++i) {
        unsigned digit = (unsigned)(parser->text[digits_start + i] - '0');
        if (magnitude > (UINT64_MAX - digit) / 10u) {
            return false;
        }
        magnitude = magnitude * 10u + digit;
    }
    if (negative) {
        if (magnitude > (uint64_t)INT64_MAX + 1u) {
            return false;
        }
        *out_value = (magnitude == (uint64_t)INT64_MAX + 1u) ? INT64_MIN : -(int64_t)magnitude;
    } else {
        if (magnitude > (uint64_t)INT64_MAX) {
            return false;
        }
        *out_value = (int64_t)magnitude;
    }
    (void)start;
    return true;
}

static bool parse_literal(json_parser_t *parser, const char *literal, size_t length)
{
    if (parser->position + length > parser->length) {
        return false;
    }
    if (memcmp(parser->text + parser->position, literal, length) != 0) {
        return false;
    }
    parser->position += length;
    return true;
}

static bool parse_array(json_parser_t *parser, uint32_t node_index)
{
    if (parser->depth >= LOCKER_DOMAIN_JSON_MAX_DEPTH) {
        return false;
    }
    parser->depth += 1u;
    parser->position += 1u; /* [ */
    skip_whitespace(parser);
    uint32_t first = UINT32_MAX;
    uint32_t last = UINT32_MAX;
    uint32_t count = 0u;
    if (parser->position < parser->length && parser->text[parser->position] == ']') {
        parser->position += 1u;
        parser->doc->nodes[node_index].first = UINT32_MAX;
        parser->doc->nodes[node_index].last = UINT32_MAX;
        parser->doc->nodes[node_index].count = 0u;
        parser->depth -= 1u;
        return true;
    }
    for (;;) {
        skip_whitespace(parser);
        uint32_t child = UINT32_MAX;
        if (!parse_value(parser, &child)) {
            return false;
        }
        uint32_t member = member_new(parser, 0u, 0u, child);
        if (member == UINT32_MAX) {
            return false;
        }
        if (first == UINT32_MAX) {
            first = member;
        } else {
            parser->doc->members[last].next = member;
        }
        last = member;
        count += 1u;
        skip_whitespace(parser);
        if (parser->position >= parser->length) {
            return false;
        }
        char byte = parser->text[parser->position];
        if (byte == ',') {
            parser->position += 1u;
            continue;
        }
        if (byte == ']') {
            parser->position += 1u;
            break;
        }
        return false;
    }
    parser->doc->nodes[node_index].first = first;
    parser->doc->nodes[node_index].last = last;
    parser->doc->nodes[node_index].count = count;
    parser->depth -= 1u;
    return true;
}

static bool parse_object(json_parser_t *parser, uint32_t node_index)
{
    if (parser->depth >= LOCKER_DOMAIN_JSON_MAX_DEPTH) {
        return false;
    }
    parser->depth += 1u;
    parser->position += 1u; /* { */
    skip_whitespace(parser);
    uint32_t first = UINT32_MAX;
    uint32_t last = UINT32_MAX;
    uint32_t count = 0u;
    if (parser->position < parser->length && parser->text[parser->position] == '}') {
        parser->position += 1u;
        parser->doc->nodes[node_index].first = UINT32_MAX;
        parser->doc->nodes[node_index].last = UINT32_MAX;
        parser->doc->nodes[node_index].count = 0u;
        parser->depth -= 1u;
        return true;
    }
    for (;;) {
        skip_whitespace(parser);
        uint32_t key_offset = 0u;
        uint32_t key_length = 0u;
        if (!parse_string_token(parser, &key_offset, &key_length)) {
            return false;
        }
        skip_whitespace(parser);
        if (parser->position >= parser->length || parser->text[parser->position] != ':') {
            return false;
        }
        parser->position += 1u;
        skip_whitespace(parser);
        uint32_t child = UINT32_MAX;
        if (!parse_value(parser, &child)) {
            return false;
        }
        /* 重复键直接拒绝：调用方按第一个匹配取值，重复键会造成歧义。 */
        for (uint32_t walk = first; walk != UINT32_MAX; walk = parser->doc->members[walk].next) {
            const locker_json_member_t *existing = &parser->doc->members[walk];
            if (existing->key_length == key_length &&
                memcmp(parser->text + existing->key_offset, parser->text + key_offset, key_length) == 0) {
                return false;
            }
        }
        uint32_t member = member_new(parser, key_offset, key_length, child);
        if (member == UINT32_MAX) {
            return false;
        }
        if (first == UINT32_MAX) {
            first = member;
        } else {
            parser->doc->members[last].next = member;
        }
        last = member;
        count += 1u;
        skip_whitespace(parser);
        if (parser->position >= parser->length) {
            return false;
        }
        char byte = parser->text[parser->position];
        if (byte == ',') {
            parser->position += 1u;
            continue;
        }
        if (byte == '}') {
            parser->position += 1u;
            break;
        }
        return false;
    }
    parser->doc->nodes[node_index].first = first;
    parser->doc->nodes[node_index].last = last;
    parser->doc->nodes[node_index].count = count;
    parser->depth -= 1u;
    return true;
}

static bool parse_value(json_parser_t *parser, uint32_t *out_index)
{
    if (parser->failed || parser->position >= parser->length) {
        return false;
    }
    char byte = parser->text[parser->position];
    if (byte == '{') {
        uint32_t index = node_new(parser, LOCKER_JSON_OBJECT);
        if (index == UINT32_MAX) {
            return false;
        }
        if (!parse_object(parser, index)) {
            return false;
        }
        *out_index = index;
        return true;
    }
    if (byte == '[') {
        uint32_t index = node_new(parser, LOCKER_JSON_ARRAY);
        if (index == UINT32_MAX) {
            return false;
        }
        if (!parse_array(parser, index)) {
            return false;
        }
        *out_index = index;
        return true;
    }
    if (byte == '"') {
        uint32_t offset = 0u;
        uint32_t length = 0u;
        if (!parse_string_token(parser, &offset, &length)) {
            return false;
        }
        uint32_t index = node_new(parser, LOCKER_JSON_STRING);
        if (index == UINT32_MAX) {
            return false;
        }
        parser->doc->nodes[index].text_offset = offset;
        parser->doc->nodes[index].text_length = length;
        *out_index = index;
        return true;
    }
    if (byte == 't') {
        if (!parse_literal(parser, "true", 4u)) {
            return false;
        }
        uint32_t index = node_new(parser, LOCKER_JSON_BOOL);
        if (index == UINT32_MAX) {
            return false;
        }
        parser->doc->nodes[index].boolean = true;
        *out_index = index;
        return true;
    }
    if (byte == 'f') {
        if (!parse_literal(parser, "false", 5u)) {
            return false;
        }
        uint32_t index = node_new(parser, LOCKER_JSON_BOOL);
        if (index == UINT32_MAX) {
            return false;
        }
        parser->doc->nodes[index].boolean = false;
        *out_index = index;
        return true;
    }
    if (byte == 'n') {
        if (!parse_literal(parser, "null", 4u)) {
            return false;
        }
        uint32_t index = node_new(parser, LOCKER_JSON_NULL);
        if (index == UINT32_MAX) {
            return false;
        }
        *out_index = index;
        return true;
    }
    int64_t number = 0;
    if (!parse_number(parser, &number)) {
        return false;
    }
    uint32_t index = node_new(parser, LOCKER_JSON_INT);
    if (index == UINT32_MAX) {
        return false;
    }
    parser->doc->nodes[index].integer = number;
    *out_index = index;
    return true;
}

bool locker_domain_json_parse(locker_json_document_t *doc, const char *text, size_t length)
{
    if (doc == NULL || text == NULL || length == 0u || doc->nodes == NULL || doc->members == NULL ||
        doc->node_capacity == 0u || doc->member_capacity == 0u) {
        return false;
    }
    doc->text = text;
    doc->length = length;
    doc->node_count = 0u;
    doc->member_count = 0u;
    doc->root = UINT32_MAX;

    json_parser_t parser;
    memset(&parser, 0, sizeof(parser));
    parser.text = text;
    parser.length = length;
    parser.doc = doc;

    skip_whitespace(&parser);
    uint32_t root = UINT32_MAX;
    if (!parse_value(&parser, &root)) {
        return false;
    }
    skip_whitespace(&parser);
    if (parser.position != parser.length) {
        return false;
    }
    doc->root = root;
    return true;
}

locker_status_t locker_domain_json_parse_status(const locker_json_document_t *doc)
{
    return (doc != NULL && doc->root != UINT32_MAX) ? LOCKER_STATUS_OK : parser_error();
}

const locker_json_node_t *locker_domain_json_root(const locker_json_document_t *doc)
{
    if (doc == NULL || doc->root == UINT32_MAX || doc->root >= doc->node_count) {
        return NULL;
    }
    return &doc->nodes[doc->root];
}

/* 按序号取子项成员：沿兄弟链前进。子项不是连续存放的（递归解析先分配子节点的成员），
   所以绝不能用 first + index 直接下标。链长由 member_count 兜底，内部不一致时返回 NULL。 */
static const locker_json_member_t *member_at(const locker_json_document_t *doc,
                                             const locker_json_node_t *node, uint32_t index)
{
    if (doc == NULL || node == NULL || index >= node->count) {
        return NULL;
    }
    uint32_t walk = node->first;
    uint32_t seen = 0u;
    while (walk != UINT32_MAX && walk < doc->member_count) {
        if (seen == index) {
            return &doc->members[walk];
        }
        walk = doc->members[walk].next;
        seen += 1u;
    }
    return NULL;
}

const locker_json_node_t *locker_domain_json_get(const locker_json_document_t *doc,
                                                 const locker_json_node_t *object, const char *key)
{
    if (doc == NULL || object == NULL || key == NULL || object->type != LOCKER_JSON_OBJECT ||
        object->first == UINT32_MAX) {
        return NULL;
    }
    size_t key_length = strlen(key);
    uint32_t walk = object->first;
    uint32_t seen = 0u;
    while (walk != UINT32_MAX && walk < doc->member_count && seen < object->count) {
        const locker_json_member_t *member = &doc->members[walk];
        if (member->key_length == key_length &&
            memcmp(doc->text + member->key_offset, key, key_length) == 0) {
            if (member->node >= doc->node_count) {
                return NULL;
            }
            return &doc->nodes[member->node];
        }
        walk = member->next;
        seen += 1u;
    }
    return NULL;
}

const locker_json_node_t *locker_domain_json_at(const locker_json_document_t *doc,
                                                const locker_json_node_t *array, uint32_t index)
{
    if (doc == NULL || array == NULL || array->type != LOCKER_JSON_ARRAY) {
        return NULL;
    }
    const locker_json_member_t *member = member_at(doc, array, index);
    if (member == NULL || member->node >= doc->node_count) {
        return NULL;
    }
    return &doc->nodes[member->node];
}

static int hex_digit(char byte)
{
    if (byte >= '0' && byte <= '9') {
        return byte - '0';
    }
    if (byte >= 'a' && byte <= 'f') {
        return byte - 'a' + 10;
    }
    if (byte >= 'A' && byte <= 'F') {
        return byte - 'A' + 10;
    }
    return -1;
}

bool locker_domain_json_text(const locker_json_document_t *doc, const locker_json_node_t *node,
                             char *out, size_t out_size, size_t *out_length)
{
    if (doc == NULL || node == NULL || out == NULL || out_size == 0u) {
        return false;
    }
    if (node->type != LOCKER_JSON_STRING) {
        return false;
    }
    const char *text = doc->text + node->text_offset;
    size_t written = 0u;
    for (uint32_t i = 0u; i < node->text_length; ++i) {
        char byte = text[i];
        if (byte == '\\') {
            if (i + 1u >= node->text_length) {
                return false;
            }
            char escape = text[++i];
            char decoded = 0;
            switch (escape) {
            case '"':
                decoded = '"';
                break;
            case '\\':
                decoded = '\\';
                break;
            case '/':
                decoded = '/';
                break;
            case 'b':
                decoded = '\b';
                break;
            case 'f':
                decoded = '\f';
                break;
            case 'n':
                decoded = '\n';
                break;
            case 'r':
                decoded = '\r';
                break;
            case 't':
                decoded = '\t';
                break;
            case 'u': {
                if (i + 4u >= node->text_length) {
                    return false;
                }
                int high = hex_digit(text[i + 1u]);
                int mid = hex_digit(text[i + 2u]);
                int low_a = hex_digit(text[i + 3u]);
                int low_b = hex_digit(text[i + 4u]);
                if (high < 0 || mid < 0 || low_a < 0 || low_b < 0) {
                    return false;
                }
                unsigned code = (unsigned)((high << 12) | (mid << 8) | (low_a << 4) | low_b);
                if (code == 0u || code > 0x7fu) {
                    /* 只接受 ASCII 转义：非 ASCII 一律按 UTF-8 原文传输。 */
                    return false;
                }
                decoded = (char)code;
                i += 4u;
                break;
            }
            default:
                return false;
            }
            if (written + 1u >= out_size) {
                return false;
            }
            out[written++] = decoded;
            continue;
        }
        if (written + 1u >= out_size) {
            return false;
        }
        out[written++] = byte;
    }
    out[written] = '\0';
    if (out_length != NULL) {
        *out_length = written;
    }
    return true;
}

bool locker_domain_json_int64(const locker_json_document_t *doc, const locker_json_node_t *node,
                              int64_t *out_value)
{
    if (doc == NULL || node == NULL || out_value == NULL || node->type != LOCKER_JSON_INT) {
        return false;
    }
    *out_value = node->integer;
    return true;
}

bool locker_domain_json_boolean(const locker_json_document_t *doc, const locker_json_node_t *node,
                                bool *out_value)
{
    if (doc == NULL || node == NULL || out_value == NULL || node->type != LOCKER_JSON_BOOL) {
        return false;
    }
    *out_value = node->boolean;
    return true;
}

/*
 * 把原文 token 解码后按规范形式重新转义输出。
 *
 * render 的用途是「幂等重放同一份响应体」与「投影比较」（domain_json.h 的往返要求），
 * 所以必须产出规范化文本：直接抄原文会把 \u0041 与 A 当成两个不同响应体，
 * 让同一请求的两次重放看起来不一致。解码规则与 locker_domain_json_text 完全一致：
 * 只接受 ASCII 可见范围的 \u 转义，\u0000 与非 ASCII 一律拒绝（原文应走 UTF-8）。
 */
static bool render_string_token(const locker_json_document_t *doc, uint32_t offset, uint32_t length,
                                locker_json_writer_t *writer)
{
    const char *text = doc->text + offset;
    uint32_t start = 0u;
    locker_domain_json_literal(writer, "\"");
    for (uint32_t i = 0u; i < length; ++i) {
        if (text[i] != '\\') {
            continue;
        }
        if (i > start) {
            writer_escaped_body(writer, text + start, (size_t)(i - start));
        }
        if (i + 1u >= length) {
            return false;
        }
        char escape = text[i + 1u];
        char decoded = 0;
        uint32_t consumed = 2u;
        switch (escape) {
        case '"':
            decoded = '"';
            break;
        case '\\':
            decoded = '\\';
            break;
        case '/':
            decoded = '/';
            break;
        case 'b':
            decoded = '\b';
            break;
        case 'f':
            decoded = '\f';
            break;
        case 'n':
            decoded = '\n';
            break;
        case 'r':
            decoded = '\r';
            break;
        case 't':
            decoded = '\t';
            break;
        case 'u': {
            if (i + 5u >= length) {
                return false;
            }
            int high = hex_digit(text[i + 2u]);
            int mid = hex_digit(text[i + 3u]);
            int low_a = hex_digit(text[i + 4u]);
            int low_b = hex_digit(text[i + 5u]);
            if (high < 0 || mid < 0 || low_a < 0 || low_b < 0) {
                return false;
            }
            unsigned code = (unsigned)((high << 12) | (mid << 8) | (low_a << 4) | low_b);
            if (code == 0u || code > 0x7fu) {
                return false;
            }
            decoded = (char)code;
            consumed = 6u;
            break;
        }
        default:
            return false;
        }
        writer_escaped_body(writer, &decoded, 1u);
        i += consumed - 1u;
        start = i + 1u;
    }
    if (length > start) {
        writer_escaped_body(writer, text + start, (size_t)(length - start));
    }
    locker_domain_json_literal(writer, "\"");
    return locker_domain_json_writer_ok(writer);
}

static bool render_node(const locker_json_document_t *doc, const locker_json_node_t *node,
                        locker_json_writer_t *writer)
{
    if (node == NULL) {
        return false;
    }
    switch ((locker_json_type_t)node->type) {
    case LOCKER_JSON_NULL:
        locker_domain_json_null(writer);
        break;
    case LOCKER_JSON_BOOL:
        locker_domain_json_bool(writer, node->boolean);
        break;
    case LOCKER_JSON_INT:
        locker_domain_json_int(writer, node->integer);
        break;
    case LOCKER_JSON_STRING:
        if (!render_string_token(doc, node->text_offset, node->text_length, writer)) {
            return false;
        }
        break;
    case LOCKER_JSON_ARRAY: {
        locker_domain_json_literal(writer, "[");
        for (uint32_t i = 0u; i < node->count; ++i) {
            if (i > 0u) {
                locker_domain_json_literal(writer, ",");
            }
            if (!render_node(doc, locker_domain_json_at(doc, node, i), writer)) {
                return false;
            }
        }
        locker_domain_json_literal(writer, "]");
        break;
    }
    case LOCKER_JSON_OBJECT: {
        locker_domain_json_literal(writer, "{");
        for (uint32_t i = 0u; i < node->count; ++i) {
            const locker_json_member_t *member = member_at(doc, node, i);
            if (member == NULL || member->node >= doc->node_count) {
                return false;
            }
            if (i > 0u) {
                locker_domain_json_literal(writer, ",");
            }
            if (!render_string_token(doc, member->key_offset, member->key_length, writer)) {
                return false;
            }
            locker_domain_json_literal(writer, ":");
            if (!render_node(doc, &doc->nodes[member->node], writer)) {
                return false;
            }
        }
        locker_domain_json_literal(writer, "}");
        break;
    }
    default:
        return false;
    }
    return true;
}

bool locker_domain_json_render(const locker_json_document_t *doc, const locker_json_node_t *node,
                               char *out, size_t out_size, size_t *out_length)
{
    if (doc == NULL || node == NULL || out == NULL || out_size == 0u) {
        return false;
    }
    locker_json_writer_t writer;
    locker_domain_json_writer_init(&writer, out, out_size);
    if (!render_node(doc, node, &writer) || !locker_domain_json_writer_ok(&writer)) {
        return false;
    }
    if (out_length != NULL) {
        *out_length = writer.length;
    }
    return true;
}
