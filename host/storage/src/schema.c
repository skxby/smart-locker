#include "schema_internal.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "locker/common/numeric.h"
#include "locker/storage/embedded.h"
#include "locker/storage/regex.h"

#define LOCKER_SCHEMA_DEPTH_MAX 64
#define LOCKER_SCHEMA_PATTERN_CACHE 16u
#define LOCKER_SCHEMA_PATTERN_TEXT_MAX 128u
#define LOCKER_SCHEMA_REF_PREFIX "#/$defs/"
#define LOCKER_SCHEMA_REF_PREFIX_LENGTH 8u

typedef struct {
    char text[LOCKER_SCHEMA_PATTERN_TEXT_MAX];
    locker_storage_regex_t *regex;
    bool failed;
} pattern_cache_entry_t;

struct locker_storage_schema {
    cJSON *root;
    const cJSON *defs;
    char collection_records[LOCKER_STORAGE_COLLECTION_COUNT][LOCKER_STORAGE_COLLECTION_NAME_MAX];
    pattern_cache_entry_t patterns[LOCKER_SCHEMA_PATTERN_CACHE];
};

/* cJSON 把 \u0000 解码成真正的 U+0000 存进 valuestring，但 C 侧所有下游
   （strlen、utf8_count、u64 文本校验、SQLite 绑定）都按 NUL 结尾处理，于是
   "0\u0000junk" 会被当成 "0" 放行并在序列化时丢掉尾巴。在把字节交给 cJSON
   之前先按原始文本扫描，凡是无法无损表示的文本一律拒绝。
   扫描按「转义奇偶」区分字面反斜杠：\\u0000 是反斜杠加字面文本，不是 NUL。 */
bool locker_storage_json_text_has_embedded_nul(const char *json, size_t length)
{
    if (json == NULL) {
        return true;
    }
    size_t index = 0u;
    while (index < length) {
        if (json[index] == '\0') {
            /* 原始 NUL 字节：JSON 文本里出现即为非法/无法表示。 */
            return true;
        }
        if (json[index] != '\\') {
            index += 1u;
            continue;
        }
        size_t run = 0u;
        while (index + run < length && json[index + run] == '\\') {
            run += 1u;
        }
        /* 未转义的反斜杠后面若是 U+0000 转义（u 加四个 0），解码后就是 NUL。 */
        if ((run % 2u) == 1u && index + run + 4u < length && json[index + run] == 'u' &&
            json[index + run + 1u] == '0' && json[index + run + 2u] == '0' &&
            json[index + run + 3u] == '0' && json[index + run + 4u] == '0') {
            return true;
        }
        index += run;
    }
    return false;
}

/* 冻结子集里出现过的全部关键字；出现别的关键字一律 fail closed。 */
static const char *const k_supported_keywords[] = {
    "$ref",      "type",       "properties", "required", "additionalProperties",
    "const",     "enum",       "pattern",    "format",   "minLength",
    "maxLength", "minimum",    "maximum",    "minItems", "maxItems",
    "items",     "anyOf",      "allOf",      "if",       "then"};

static bool validate_node(locker_storage_schema_t *schema, const cJSON *value, const cJSON *node,
                          int depth);

static bool text_equals(const char *left, size_t left_length, const char *right)
{
    size_t right_length = strlen(right);
    return left_length == right_length && memcmp(left, right, right_length) == 0;
}

static bool copy_text(char *out, size_t out_size, const char *text)
{
    size_t length = strlen(text);
    if (out_size == 0u || length >= out_size) {
        return false;
    }
    memcpy(out, text, length);
    out[length] = '\0';
    return true;
}

/* JSON Schema 的长度关键字按 Unicode 码点数计，不是字节数。 */
static bool utf8_count(const char *text, size_t length, size_t *out_count)
{
    size_t index = 0u;
    size_t count = 0u;
    while (index < length) {
        unsigned char lead = (unsigned char)text[index];
        size_t extra;
        if (lead < 0x80u) {
            extra = 0u;
        } else if ((lead & 0xE0u) == 0xC0u) {
            extra = 1u;
        } else if ((lead & 0xF0u) == 0xE0u) {
            extra = 2u;
        } else if ((lead & 0xF8u) == 0xF0u) {
            extra = 3u;
        } else {
            return false;
        }
        if (index + extra >= length) {
            return false;
        }
        for (size_t offset = 1u; offset <= extra; offset++) {
            if (((unsigned char)text[index + offset] & 0xC0u) != 0x80u) {
                return false;
            }
        }
        index += extra + 1u;
        count++;
    }
    *out_count = count;
    return true;
}

static bool is_lower_hex(char character)
{
    return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f');
}

static bool format_uuid_ok(const char *text, size_t length)
{
    if (length != 36u) {
        return false;
    }
    for (size_t index = 0u; index < length; index++) {
        if (index == 8u || index == 13u || index == 18u || index == 23u) {
            if (text[index] != '-') {
                return false;
            }
        } else if (!is_lower_hex(text[index])) {
            return false;
        }
    }
    return true;
}

static bool two_digits(const char *text, size_t index, unsigned *out_value)
{
    if (text[index] < '0' || text[index] > '9' || text[index + 1u] < '0' ||
        text[index + 1u] > '9') {
        return false;
    }
    *out_value = (unsigned)(text[index] - '0') * 10u + (unsigned)(text[index + 1u] - '0');
    return true;
}

/* RFC 3339 date-time 的受限实现：只接受契约样例用到的形态。 */
static bool format_date_time_ok(const char *text, size_t length)
{
    if (length < 20u) {
        return false;
    }
    for (size_t index = 0u; index < 19u; index++) {
        if (index == 4u || index == 7u) {
            if (text[index] != '-') {
                return false;
            }
        } else if (index == 10u) {
            if (text[index] != 'T') {
                return false;
            }
        } else if (index == 13u || index == 16u) {
            if (text[index] != ':') {
                return false;
            }
        } else if (text[index] < '0' || text[index] > '9') {
            return false;
        }
    }
    unsigned year = 0u;
    unsigned month = 0u;
    unsigned day = 0u;
    unsigned hour = 0u;
    unsigned minute = 0u;
    unsigned second = 0u;
    for (size_t index = 0u; index < 4u; index++) {
        year = year * 10u + (unsigned)(text[index] - '0');
    }
    if (year < 1u || !two_digits(text, 5u, &month) || !two_digits(text, 8u, &day) ||
        !two_digits(text, 11u, &hour) || !two_digits(text, 14u, &minute) ||
        !two_digits(text, 17u, &second)) {
        return false;
    }
    if (month < 1u || month > 12u || day < 1u || day > 31u || hour > 23u || minute > 59u ||
        second > 60u) {
        return false;
    }
    size_t index = 19u;
    if (index < length && text[index] == '.') {
        index++;
        size_t digits = 0u;
        while (index < length && text[index] >= '0' && text[index] <= '9') {
            index++;
            digits++;
        }
        if (digits == 0u) {
            return false;
        }
    }
    if (index >= length) {
        return false;
    }
    if (text[index] == 'Z') {
        return index + 1u == length;
    }
    if (text[index] != '+' && text[index] != '-') {
        return false;
    }
    unsigned offset_hour = 0u;
    unsigned offset_minute = 0u;
    if (index + 6u != length || !two_digits(text, index + 1u, &offset_hour) ||
        text[index + 3u] != ':' || !two_digits(text, index + 4u, &offset_minute)) {
        return false;
    }
    return offset_hour <= 23u && offset_minute <= 59u;
}

static locker_storage_regex_t *pattern_regex(locker_storage_schema_t *schema, const char *pattern,
                                             bool *out_ok)
{
    size_t length = strlen(pattern);
    *out_ok = false;
    if (length == 0u || length >= LOCKER_SCHEMA_PATTERN_TEXT_MAX) {
        return NULL;
    }
    for (size_t index = 0u; index < LOCKER_SCHEMA_PATTERN_CACHE; index++) {
        if (schema->patterns[index].text[0] != '\0' &&
            strcmp(schema->patterns[index].text, pattern) == 0) {
            *out_ok = !schema->patterns[index].failed;
            return schema->patterns[index].regex;
        }
    }
    for (size_t index = 0u; index < LOCKER_SCHEMA_PATTERN_CACHE; index++) {
        if (schema->patterns[index].text[0] == '\0') {
            memcpy(schema->patterns[index].text, pattern, length);
            schema->patterns[index].text[length] = '\0';
            schema->patterns[index].regex = locker_storage_regex_compile(pattern);
            schema->patterns[index].failed = schema->patterns[index].regex == NULL;
            *out_ok = !schema->patterns[index].failed;
            return schema->patterns[index].regex;
        }
    }
    return NULL;
}

static bool is_integral_number(const cJSON *value)
{
    double number = value->valuedouble;
    if (number < -9007199254740992.0 || number > 9007199254740992.0) {
        return false;
    }
    return (double)(long long)number == number;
}

static bool json_type_matches(const cJSON *value, const char *type)
{
    if (strcmp(type, "string") == 0) {
        return cJSON_IsString(value) != 0;
    }
    if (strcmp(type, "object") == 0) {
        return cJSON_IsObject(value) != 0;
    }
    if (strcmp(type, "array") == 0) {
        return cJSON_IsArray(value) != 0;
    }
    if (strcmp(type, "boolean") == 0) {
        return cJSON_IsBool(value) != 0;
    }
    if (strcmp(type, "null") == 0) {
        return cJSON_IsNull(value) != 0;
    }
    if (strcmp(type, "integer") == 0) {
        return cJSON_IsNumber(value) != 0 && is_integral_number(value);
    }
    if (strcmp(type, "number") == 0) {
        return cJSON_IsNumber(value) != 0;
    }
    return false;
}

/* U64 是「已校验的十进制文本」（x-rules 12）：pattern 只约束形态
   ^(0|[1-9][0-9]{0,19})$，20 位但超过 UINT64_MAX 的值同样匹配，必须另行拒绝，
   否则下游按数值比较会失真。 */
static bool u64_text_ok(const char *text)
{
    if (text == NULL) {
        return false;
    }
    /* 规范十进制 + 不溢出由 host/common 的 numeric.c 统一判定（审计 N5）：
       这里只做 strlen 取长，不复制第二份位数/溢出逻辑。is_canonical 只查拼写，
       20 位超 UINT64_MAX 的文本仍要通过 parse_decimal 的溢出检查。 */
    uint64_t value = 0u;
    return locker_u64_parse_decimal(text, strlen(text), &value) == LOCKER_STATUS_OK;
}

static const cJSON *resolve_ref(locker_storage_schema_t *schema, const char *ref)
{
    if (ref == NULL || strncmp(ref, LOCKER_SCHEMA_REF_PREFIX, LOCKER_SCHEMA_REF_PREFIX_LENGTH) != 0) {
        return NULL;
    }
    return cJSON_GetObjectItemCaseSensitive(schema->defs, ref + LOCKER_SCHEMA_REF_PREFIX_LENGTH);
}

static bool validate_string_keywords(locker_storage_schema_t *schema, const cJSON *value,
                                     const cJSON *node)
{
    const char *text = cJSON_GetStringValue(value);
    size_t length = strlen(text);

    const cJSON *min_length = cJSON_GetObjectItemCaseSensitive(node, "minLength");
    const cJSON *max_length = cJSON_GetObjectItemCaseSensitive(node, "maxLength");
    if (min_length != NULL || max_length != NULL) {
        size_t count = 0u;
        if (!utf8_count(text, length, &count)) {
            return false;
        }
        if (min_length != NULL &&
            (cJSON_IsNumber(min_length) == 0 || (double)count < min_length->valuedouble)) {
            return false;
        }
        if (max_length != NULL &&
            (cJSON_IsNumber(max_length) == 0 || (double)count > max_length->valuedouble)) {
            return false;
        }
    }

    const cJSON *pattern = cJSON_GetObjectItemCaseSensitive(node, "pattern");
    if (pattern != NULL) {
        const char *pattern_text = cJSON_GetStringValue(pattern);
        bool ok = false;
        locker_storage_regex_t *regex =
            pattern_regex(schema, pattern_text == NULL ? "" : pattern_text, &ok);
        if (!ok || regex == NULL || !locker_storage_regex_match(regex, text, length)) {
            return false;
        }
    }

    const cJSON *format = cJSON_GetObjectItemCaseSensitive(node, "format");
    if (format != NULL) {
        const char *format_text = cJSON_GetStringValue(format);
        if (format_text == NULL) {
            return false;
        }
        if (strcmp(format_text, "uuid") == 0) {
            if (!format_uuid_ok(text, length)) {
                return false;
            }
        } else if (strcmp(format_text, "date-time") == 0) {
            if (!format_date_time_ok(text, length)) {
                return false;
            }
        } else {
            return false;
        }
    }
    return true;
}

static bool validate_object_keywords(locker_storage_schema_t *schema, const cJSON *value,
                                     const cJSON *node, int depth)
{
    const cJSON *properties = cJSON_GetObjectItemCaseSensitive(node, "properties");
    if (properties != NULL && cJSON_IsObject(properties) == 0) {
        return false;
    }

    const cJSON *required = cJSON_GetObjectItemCaseSensitive(node, "required");
    if (required != NULL) {
        if (cJSON_IsArray(required) == 0) {
            return false;
        }
        const cJSON *name = NULL;
        cJSON_ArrayForEach(name, required)
        {
            const char *name_text = cJSON_GetStringValue(name);
            if (name_text == NULL || cJSON_GetObjectItemCaseSensitive(value, name_text) == NULL) {
                return false;
            }
        }
    }

    if (properties != NULL) {
        const cJSON *property = NULL;
        cJSON_ArrayForEach(property, properties)
        {
            const char *property_name = property->string;
            const cJSON *property_value =
                property_name == NULL ? NULL
                                      : cJSON_GetObjectItemCaseSensitive(value, property_name);
            if (property_value != NULL &&
                !validate_node(schema, property_value, property, depth + 1)) {
                return false;
            }
        }
    }

    const cJSON *additional = cJSON_GetObjectItemCaseSensitive(node, "additionalProperties");
    if (additional != NULL) {
        if (cJSON_IsBool(additional) == 0) {
            return false;
        }
        if (cJSON_IsFalse(additional) != 0) {
            const cJSON *member = NULL;
            cJSON_ArrayForEach(member, value)
            {
                const char *member_name = member->string;
                if (member_name == NULL) {
                    return false;
                }
                if (properties == NULL ||
                    cJSON_GetObjectItemCaseSensitive(properties, member_name) == NULL) {
                    return false;
                }
            }
        }
    }
    return true;
}

static bool validate_array_keywords(locker_storage_schema_t *schema, const cJSON *value,
                                    const cJSON *node, int depth)
{
    const cJSON *min_items = cJSON_GetObjectItemCaseSensitive(node, "minItems");
    const cJSON *max_items = cJSON_GetObjectItemCaseSensitive(node, "maxItems");
    int count = cJSON_GetArraySize(value);
    if (min_items != NULL &&
        (cJSON_IsNumber(min_items) == 0 || (double)count < min_items->valuedouble)) {
        return false;
    }
    if (max_items != NULL &&
        (cJSON_IsNumber(max_items) == 0 || (double)count > max_items->valuedouble)) {
        return false;
    }
    const cJSON *items = cJSON_GetObjectItemCaseSensitive(node, "items");
    if (items != NULL) {
        const cJSON *item = NULL;
        cJSON_ArrayForEach(item, value)
        {
            if (!validate_node(schema, item, items, depth + 1)) {
                return false;
            }
        }
    }
    return true;
}

static bool keyword_supported(const char *keyword)
{
    for (size_t index = 0u; index < sizeof(k_supported_keywords) / sizeof(k_supported_keywords[0]);
         index++) {
        if (strcmp(k_supported_keywords[index], keyword) == 0) {
            return true;
        }
    }
    return false;
}

static bool validate_node(locker_storage_schema_t *schema, const cJSON *value, const cJSON *node,
                          int depth)
{
    if (value == NULL || node == NULL || depth > LOCKER_SCHEMA_DEPTH_MAX ||
        cJSON_IsObject(node) == 0) {
        return false;
    }

    const cJSON *member = NULL;
    cJSON_ArrayForEach(member, node)
    {
        if (member->string == NULL || !keyword_supported(member->string)) {
            return false;
        }
    }

    const cJSON *ref = cJSON_GetObjectItemCaseSensitive(node, "$ref");
    if (ref != NULL) {
        const char *ref_text = cJSON_GetStringValue(ref);
        const cJSON *target = resolve_ref(schema, ref_text);
        if (target == NULL) {
            return false;
        }
        if (ref_text != NULL && strcmp(ref_text, "#/$defs/U64") == 0 && !u64_text_ok(cJSON_GetStringValue(value))) {
            return false;
        }
        return validate_node(schema, value, target, depth + 1);
    }

    const cJSON *type = cJSON_GetObjectItemCaseSensitive(node, "type");
    if (type != NULL) {
        if (cJSON_IsString(type) != 0) {
            const char *type_text = cJSON_GetStringValue(type);
            if (type_text == NULL || !json_type_matches(value, type_text)) {
                return false;
            }
        } else if (cJSON_IsArray(type) != 0) {
            bool matched = false;
            const cJSON *candidate = NULL;
            cJSON_ArrayForEach(candidate, type)
            {
                const char *candidate_text = cJSON_GetStringValue(candidate);
                if (candidate_text != NULL && json_type_matches(value, candidate_text)) {
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                return false;
            }
        } else {
            return false;
        }
    }

    const cJSON *const_value = cJSON_GetObjectItemCaseSensitive(node, "const");
    if (const_value != NULL && cJSON_Compare(value, const_value, 1) == 0) {
        return false;
    }

    const cJSON *enum_value = cJSON_GetObjectItemCaseSensitive(node, "enum");
    if (enum_value != NULL) {
        if (cJSON_IsArray(enum_value) == 0) {
            return false;
        }
        bool matched = false;
        const cJSON *candidate = NULL;
        cJSON_ArrayForEach(candidate, enum_value)
        {
            if (cJSON_Compare(value, candidate, 1) != 0) {
                matched = true;
                break;
            }
        }
        if (!matched) {
            return false;
        }
    }

    if (cJSON_IsString(value) != 0 && !validate_string_keywords(schema, value, node)) {
        return false;
    }

    if (cJSON_IsNumber(value) != 0) {
        const cJSON *minimum = cJSON_GetObjectItemCaseSensitive(node, "minimum");
        const cJSON *maximum = cJSON_GetObjectItemCaseSensitive(node, "maximum");
        if (minimum != NULL &&
            (cJSON_IsNumber(minimum) == 0 || value->valuedouble < minimum->valuedouble)) {
            return false;
        }
        if (maximum != NULL &&
            (cJSON_IsNumber(maximum) == 0 || value->valuedouble > maximum->valuedouble)) {
            return false;
        }
    }

    if (cJSON_IsObject(value) != 0 && !validate_object_keywords(schema, value, node, depth)) {
        return false;
    }

    if (cJSON_IsArray(value) != 0 && !validate_array_keywords(schema, value, node, depth)) {
        return false;
    }

    const cJSON *all_of = cJSON_GetObjectItemCaseSensitive(node, "allOf");
    if (all_of != NULL) {
        if (cJSON_IsArray(all_of) == 0) {
            return false;
        }
        const cJSON *candidate = NULL;
        cJSON_ArrayForEach(candidate, all_of)
        {
            if (!validate_node(schema, value, candidate, depth + 1)) {
                return false;
            }
        }
    }

    const cJSON *any_of = cJSON_GetObjectItemCaseSensitive(node, "anyOf");
    if (any_of != NULL) {
        if (cJSON_IsArray(any_of) == 0) {
            return false;
        }
        bool matched = false;
        const cJSON *candidate = NULL;
        cJSON_ArrayForEach(candidate, any_of)
        {
            if (validate_node(schema, value, candidate, depth + 1)) {
                matched = true;
                break;
            }
        }
        if (!matched) {
            return false;
        }
    }

    const cJSON *condition = cJSON_GetObjectItemCaseSensitive(node, "if");
    if (condition != NULL) {
        if (validate_node(schema, value, condition, depth + 1)) {
            const cJSON *then_node = cJSON_GetObjectItemCaseSensitive(node, "then");
            if (then_node != NULL && !validate_node(schema, value, then_node, depth + 1)) {
                return false;
            }
        }
    }

    return true;
}

locker_storage_schema_t *locker_storage_schema_create(void)
{
    locker_storage_schema_t *schema = (locker_storage_schema_t *)calloc(1u, sizeof(*schema));
    if (schema == NULL) {
        return NULL;
    }
    schema->root = cJSON_ParseWithLength(locker_storage_schema_json, locker_storage_schema_json_size);
    if (schema->root == NULL) {
        free(schema);
        return NULL;
    }
    schema->defs = cJSON_GetObjectItemCaseSensitive(schema->root, "defs");
    const cJSON *collections = cJSON_GetObjectItemCaseSensitive(schema->root, "collections");
    if (cJSON_IsObject(schema->defs) == 0 || cJSON_IsObject(collections) == 0 ||
        cJSON_GetArraySize(collections) != (int)LOCKER_STORAGE_COLLECTION_COUNT) {
        locker_storage_schema_destroy(schema);
        return NULL;
    }
    size_t index = 0u;
    const cJSON *entry = NULL;
    cJSON_ArrayForEach(entry, collections)
    {
        const char *record = cJSON_GetStringValue(entry);
        if (index >= LOCKER_STORAGE_COLLECTION_COUNT || entry->string == NULL || record == NULL ||
            strcmp(entry->string, locker_storage_schema_collection_name(index)) != 0 ||
            !copy_text(schema->collection_records[index], LOCKER_STORAGE_COLLECTION_NAME_MAX,
                       record) ||
            cJSON_GetObjectItemCaseSensitive(schema->defs, record) == NULL) {
            locker_storage_schema_destroy(schema);
            return NULL;
        }
        index++;
    }
    return schema;
}

void locker_storage_schema_destroy(locker_storage_schema_t *schema)
{
    if (schema == NULL) {
        return;
    }
    for (size_t index = 0u; index < LOCKER_SCHEMA_PATTERN_CACHE; index++) {
        if (schema->patterns[index].regex != NULL) {
            locker_storage_regex_free(schema->patterns[index].regex);
        }
    }
    if (schema->root != NULL) {
        cJSON_Delete(schema->root);
    }
    free(schema);
}

const char *locker_storage_schema_collection_name(size_t index)
{
    /* 静态表顺序必须与 create 时对 collections 的遍历顺序一致（按名字排序）。 */
    static const char *const names[LOCKER_STORAGE_COLLECTION_COUNT] = {
        "audits",          "card_uid_index", "cards",           "device_events",
        "device_state",    "idempotency",    "import_cursors",  "loans",
        "operations",      "outbox",         "sessions",        "slot_guards",
        "tools",           "username_index", "users",           "vision_captures",
        "vision_evidence", "vision_operations"};
    if (index >= LOCKER_STORAGE_COLLECTION_COUNT) {
        return NULL;
    }
    return names[index];
}

bool locker_storage_schema_collection_lookup(const char *name, size_t length, size_t *out_index)
{
    if (name == NULL || out_index == NULL) {
        return false;
    }
    for (size_t index = 0u; index < LOCKER_STORAGE_COLLECTION_COUNT; index++) {
        const char *candidate = locker_storage_schema_collection_name(index);
        if (candidate != NULL && text_equals(name, length, candidate)) {
            *out_index = index;
            return true;
        }
    }
    return false;
}

bool locker_storage_schema_validate_def_value(locker_storage_schema_t *schema,
                                                     const char *def_name, const cJSON *value)
{
    if (schema == NULL || def_name == NULL || value == NULL) {
        return false;
    }
    const cJSON *target = cJSON_GetObjectItemCaseSensitive(schema->defs, def_name);
    if (target == NULL) {
        return false;
    }
    return validate_node(schema, value, target, 0);
}

bool locker_storage_schema_validate_document_value(locker_storage_schema_t *schema,
                                                   size_t collection_index, const cJSON *value)
{
    if (schema == NULL || collection_index >= LOCKER_STORAGE_COLLECTION_COUNT) {
        return false;
    }
    return locker_storage_schema_validate_def_value(
        schema, schema->collection_records[collection_index], value);
}

bool locker_storage_schema_validate_json(locker_storage_schema_t *schema, const char *def_name,
                                         const char *json, size_t length)
{
    if (schema == NULL || def_name == NULL || json == NULL || length == 0u) {
        return false;
    }
    if (locker_storage_json_text_has_embedded_nul(json, length)) {
        return false;
    }
    cJSON *value = cJSON_ParseWithLength(json, length);
    if (value == NULL) {
        return false;
    }
    bool ok = locker_storage_schema_validate_def_value(schema, def_name, value);
    cJSON_Delete(value);
    return ok;
}

bool locker_storage_schema_validate_document_json(locker_storage_schema_t *schema,
                                                  size_t collection_index, const char *json,
                                                  size_t length)
{
    if (schema == NULL || json == NULL || length == 0u) {
        return false;
    }
    if (locker_storage_json_text_has_embedded_nul(json, length)) {
        return false;
    }
    cJSON *value = cJSON_ParseWithLength(json, length);
    if (value == NULL) {
        return false;
    }
    bool ok = locker_storage_schema_validate_document_value(schema, collection_index, value);
    cJSON_Delete(value);
    return ok;
}
