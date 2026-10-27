#define _POSIX_C_SOURCE 200809L

/*
 * S1-04 单元测试：Core 私有的有界 JSON 层（domain_json.c）。
 *
 * 这一层的存在理由写在 domain_json.h:4-14：业务核心不得包含 cJSON（仓储私有依赖），
 * 所以自带的写者/读者必须自己证明三件事：
 *   1. 写入越界只置 failed，绝不写穿目标缓冲区（响应体、change 体、doc 体都是定长池）。
 *   2. 读取拒绝一切非规范 JSON：浮点、指数、前导零、重复键、尾随垃圾、超深嵌套、
 *      控制字符、非 ASCII \u 转义。放宽任何一条都会让「同一份库文档有两种解释」。
 *   3. U64 走十进制文本、时间走 RFC 3339 —— 这里的 u64/int 序列化是幂等哈希与
 *      change 体 expected/new revision 的唯一出口。
 */

#include "domain_json.h"
#include "test_support.h"

#include <string.h>

#define JT_NODES 512u
#define JT_MEMBERS 512u

typedef struct jt_doc {
    locker_json_document_t doc;
    locker_json_node_t nodes[JT_NODES];
    locker_json_member_t members[JT_MEMBERS];
} jt_doc_t;

static void jt_init(jt_doc_t *holder)
{
    memset(holder, 0, sizeof(*holder));
    holder->doc.nodes = holder->nodes;
    holder->doc.node_capacity = JT_NODES;
    holder->doc.members = holder->members;
    holder->doc.member_capacity = JT_MEMBERS;
}

static bool jt_parse(jt_doc_t *holder, const char *text)
{
    jt_init(holder);
    return locker_domain_json_parse(&holder->doc, text, strlen(text));
}

/* 写入后取文本；失败时返回 "<failed>" 让断言能直接看出差异。 */
static const char *jt_text(locker_json_writer_t *writer, char *buffer)
{
    if (!locker_domain_json_writer_ok(writer)) {
        return "<failed>";
    }
    return buffer;
}

static void test_writer_scalars(void)
{
    char buffer[128];
    locker_json_writer_t writer;

    locker_domain_json_writer_init(&writer, buffer, sizeof(buffer));
    TEST_CHECK(locker_domain_json_writer_ok(&writer));
    TEST_CHECK_U64(0u, (uint64_t)writer.length);
    TEST_CHECK_STRING("", buffer);

    locker_domain_json_u64(&writer, 0u);
    locker_domain_json_literal(&writer, "|");
    locker_domain_json_u64(&writer, 18446744073709551615ull);
    locker_domain_json_literal(&writer, "|");
    locker_domain_json_int(&writer, -9223372036854775807ll - 1ll);
    locker_domain_json_literal(&writer, "|");
    locker_domain_json_int(&writer, 0);
    locker_domain_json_literal(&writer, "|");
    locker_domain_json_bool(&writer, true);
    locker_domain_json_literal(&writer, "|");
    locker_domain_json_bool(&writer, false);
    locker_domain_json_literal(&writer, "|");
    locker_domain_json_null(&writer);
    TEST_CHECK_STRING("0|18446744073709551615|-9223372036854775808|0|true|false|null",
                      jt_text(&writer, buffer));

    /* key 只是 string + ':' 的简写。 */
    locker_domain_json_writer_init(&writer, buffer, sizeof(buffer));
    locker_domain_json_literal(&writer, "{");
    locker_domain_json_key(&writer, "a");
    locker_domain_json_u64(&writer, 1u);
    locker_domain_json_literal(&writer, "}");
    TEST_CHECK_STRING("{\"a\":1}", jt_text(&writer, buffer));
}

static void test_writer_escaping(void)
{
    char buffer[256];
    locker_json_writer_t writer;

    locker_domain_json_writer_init(&writer, buffer, sizeof(buffer));
    locker_domain_json_string(&writer, "q\"b\\s");
    TEST_CHECK_STRING("\"q\\\"b\\\\s\"", jt_text(&writer, buffer));

    locker_domain_json_writer_init(&writer, buffer, sizeof(buffer));
    locker_domain_json_string(&writer, "\b\f\n\r\t");
    TEST_CHECK_STRING("\"\\b\\f\\n\\r\\t\"", jt_text(&writer, buffer));

    /* 其余控制字节走 \u00XX（小写十六进制）。 */
    locker_domain_json_writer_init(&writer, buffer, sizeof(buffer));
    locker_domain_json_string(&writer, "\x01\x1f");
    TEST_CHECK_STRING("\"\\u0001\\u001f\"", jt_text(&writer, buffer));

    /* 0x20 及以上不转义（含 0x7f 与 UTF-8 原文）。 */
    locker_domain_json_writer_init(&writer, buffer, sizeof(buffer));
    locker_domain_json_string(&writer, " ~\x7f\xe4\xb8\xad");
    TEST_CHECK_STRING("\" ~\x7f\xe4\xb8\xad\"", jt_text(&writer, buffer));

    /* 带内嵌 NUL 的定长串：string_n 按长度写，NUL 变 \u0000。 */
    locker_domain_json_writer_init(&writer, buffer, sizeof(buffer));
    locker_domain_json_string_n(&writer, "a\0b", 3u);
    TEST_CHECK_STRING("\"a\\u0000b\"", jt_text(&writer, buffer));

    /* NULL 文本一律 fail closed，不写半截。 */
    locker_domain_json_writer_init(&writer, buffer, sizeof(buffer));
    locker_domain_json_string(&writer, NULL);
    TEST_CHECK(!locker_domain_json_writer_ok(&writer));
    locker_domain_json_writer_init(&writer, buffer, sizeof(buffer));
    locker_domain_json_literal(&writer, NULL);
    TEST_CHECK(!locker_domain_json_writer_ok(&writer));
    locker_domain_json_writer_init(&writer, buffer, sizeof(buffer));
    locker_domain_json_string_n(&writer, NULL, 3u);
    TEST_CHECK(!locker_domain_json_writer_ok(&writer));
}

static void test_writer_bounds(void)
{
    char buffer[8];
    locker_json_writer_t writer;

    /* 恰好填满（capacity-1 个字符 + NUL）必须成功。 */
    locker_domain_json_writer_init(&writer, buffer, 4u);
    locker_domain_json_literal(&writer, "abc");
    TEST_CHECK(locker_domain_json_writer_ok(&writer));
    TEST_CHECK_STRING("abc", buffer);

    /* 多一个字节就失败，且已写内容原样保留、不写穿。 */
    locker_domain_json_writer_init(&writer, buffer, 4u);
    locker_domain_json_literal(&writer, "abc");
    locker_domain_json_literal(&writer, "d");
    TEST_CHECK(!locker_domain_json_writer_ok(&writer));
    TEST_CHECK_STRING("abc", buffer);
    TEST_CHECK_U64(3u, (uint64_t)writer.length);

    /* 失败后继续写不改变任何字节。 */
    locker_domain_json_literal(&writer, "zzz");
    TEST_CHECK_STRING("abc", buffer);

    /* capacity 为 0 / buffer 为 NULL 时初始即 failed。 */
    locker_domain_json_writer_init(&writer, buffer, 0u);
    TEST_CHECK(!locker_domain_json_writer_ok(&writer));
    locker_domain_json_writer_init(&writer, NULL, sizeof(buffer));
    TEST_CHECK(!locker_domain_json_writer_ok(&writer));
    locker_domain_json_writer_init(&writer, buffer, 1u);
    locker_domain_json_literal(&writer, "a");
    TEST_CHECK(!locker_domain_json_writer_ok(&writer));

    /* NULL writer 与 NULL 判空：不崩溃。 */
    locker_domain_json_literal(NULL, "x");
    locker_domain_json_string(NULL, "x");
    TEST_CHECK(locker_domain_json_writer_ok(NULL) == false);
}

static void test_parse_accepts(void)
{
    jt_doc_t holder;
    const locker_json_node_t *root;
    const locker_json_node_t *node;
    char text[128];
    size_t length = 0u;

    TEST_CHECK(jt_parse(&holder, "{\"a\":1,\"b\":[true,false,null,\"x\"]}"));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_domain_json_parse_status(&holder.doc));
    root = locker_domain_json_root(&holder.doc);
    TEST_CHECK(root != NULL);
    TEST_CHECK(root->type == LOCKER_JSON_OBJECT);
    TEST_CHECK_U64(2u, (uint64_t)root->count);

    int64_t number = 0;
    node = locker_domain_json_get(&holder.doc, root, "a");
    TEST_CHECK(node != NULL);
    TEST_CHECK(node->type == LOCKER_JSON_INT);
    TEST_CHECK(locker_domain_json_int64(&holder.doc, node, &number) == true);
    TEST_CHECK_U64(1u, (uint64_t)number);

    node = locker_domain_json_get(&holder.doc, root, "b");
    TEST_CHECK(node != NULL);
    TEST_CHECK(node->type == LOCKER_JSON_ARRAY);
    TEST_CHECK_U64(4u, (uint64_t)node->count);
    TEST_CHECK(locker_domain_json_at(&holder.doc, node, 0u)->type == LOCKER_JSON_BOOL);
    TEST_CHECK(locker_domain_json_at(&holder.doc, node, 2u)->type == LOCKER_JSON_NULL);

    node = locker_domain_json_at(&holder.doc, locker_domain_json_get(&holder.doc, root, "b"), 3u);
    TEST_CHECK(node != NULL);
    TEST_CHECK(locker_domain_json_text(&holder.doc, node, text, sizeof(text), &length));
    TEST_CHECK_STRING("x", text);
    TEST_CHECK_U64(1u, (uint64_t)length);

    /* 前后空白允许，中间空白允许。 */
    TEST_CHECK(jt_parse(&holder, " \t\r\n{ \"a\" : [ 1 , 2 ] } \n"));
    /* 空对象/空数组。 */
    TEST_CHECK(jt_parse(&holder, "{}"));
    TEST_CHECK(jt_parse(&holder, "[]"));
    /* 标量根也合法（幂等响应重放要能 render 任意值）。 */
    TEST_CHECK(jt_parse(&holder, "42"));
    TEST_CHECK(jt_parse(&holder, "\"s\""));
}

static void test_parse_escapes(void)
{
    jt_doc_t holder;
    const locker_json_node_t *root;
    char text[64];
    size_t length = 0u;

    TEST_CHECK(jt_parse(&holder, "\"a\\\"b\\\\c\\/d\\be\\ff\\ng\\rh\\ti\""));
    root = locker_domain_json_root(&holder.doc);
    TEST_CHECK(locker_domain_json_text(&holder.doc, root, text, sizeof(text), &length));
    TEST_CHECK_STRING("a\"b\\c/d\be\ff\ng\rh\ti", text);
    TEST_CHECK_U64(17u, (uint64_t)length);

    /* \u00XX 只接受 ASCII 可见字符。 */
    TEST_CHECK(jt_parse(&holder, "\"\\u0041\\u007e\""));
    root = locker_domain_json_root(&holder.doc);
    TEST_CHECK(locker_domain_json_text(&holder.doc, root, text, sizeof(text), &length));
    TEST_CHECK_STRING("A~", text);

    /* 非 ASCII 的 \u 转义在解码阶段被拒（原文应走 UTF-8）。 */
    TEST_CHECK(jt_parse(&holder, "\"\\u00e9\""));
    root = locker_domain_json_root(&holder.doc);
    TEST_CHECK(!locker_domain_json_text(&holder.doc, root, text, sizeof(text), &length));

    /* \u0000 解码会截断字符串，必须拒绝。 */
    TEST_CHECK(jt_parse(&holder, "\"a\\u0000b\""));
    root = locker_domain_json_root(&holder.doc);
    TEST_CHECK(!locker_domain_json_text(&holder.doc, root, text, sizeof(text), &length));

    /* 输出缓冲不足整体失败，不返回截断串。 */
    TEST_CHECK(jt_parse(&holder, "\"abcd\""));
    root = locker_domain_json_root(&holder.doc);
    TEST_CHECK(!locker_domain_json_text(&holder.doc, root, text, 4u, &length));
    TEST_CHECK(locker_domain_json_text(&holder.doc, root, text, 5u, &length));
    TEST_CHECK_STRING("abcd", text);
    /* 空串需要 1 字节放 NUL，0 字节缓冲拒绝。 */
    TEST_CHECK(jt_parse(&holder, "\"\""));
    root = locker_domain_json_root(&holder.doc);
    TEST_CHECK(!locker_domain_json_text(&holder.doc, root, text, 0u, &length));
    TEST_CHECK(locker_domain_json_text(&holder.doc, root, text, 1u, &length));
    TEST_CHECK_STRING("", text);
}

static void test_parse_rejects_syntax(void)
{
    jt_doc_t holder;

    TEST_CHECK(!jt_parse(&holder, ""));
    TEST_CHECK(!jt_parse(&holder, "   "));
    TEST_CHECK(!jt_parse(&holder, "{"));
    TEST_CHECK(!jt_parse(&holder, "}"));
    TEST_CHECK(!jt_parse(&holder, "[1,]"));
    TEST_CHECK(!jt_parse(&holder, "{\"a\":1,}"));
    TEST_CHECK(!jt_parse(&holder, "{\"a\" 1}"));
    TEST_CHECK(!jt_parse(&holder, "{\"a\":}"));
    TEST_CHECK(!jt_parse(&holder, "{a:1}"));
    TEST_CHECK(!jt_parse(&holder, "\"unterminated"));
    TEST_CHECK(!jt_parse(&holder, "\"bad\\xescape\""));
    TEST_CHECK(!jt_parse(&holder, "\"short\\u12\""));
    TEST_CHECK(!jt_parse(&holder, "\"short\\u12g4\""));
    /* 尾随垃圾：解析成功但位置不等于长度。 */
    TEST_CHECK(!jt_parse(&holder, "{} {}"));
    TEST_CHECK(!jt_parse(&holder, "1 2"));
    TEST_CHECK(!jt_parse(&holder, "null x"));
    /* 重复键会造成「取第一个还是最后一个」的歧义。 */
    TEST_CHECK(!jt_parse(&holder, "{\"a\":1,\"a\":2}"));
    TEST_CHECK(!jt_parse(&holder, "{\"a\":{\"b\":1,\"b\":2}}"));
    /* 原始控制字符（<0x20）不得出现在字符串里。 */
    TEST_CHECK(!jt_parse(&holder, "\"a\nb\""));
    TEST_CHECK(!jt_parse(&holder, "\"a\x01b\""));
    /* 解析失败时 root 保持 UINT32_MAX，root() 返回 NULL。 */
    TEST_CHECK(locker_domain_json_root(&holder.doc) == NULL);
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_domain_json_parse_status(&holder.doc));
}

static void test_parse_rejects_numbers(void)
{
    jt_doc_t holder;
    const locker_json_node_t *root;
    int64_t value = 0;

    TEST_CHECK(jt_parse(&holder, "0"));
    root = locker_domain_json_root(&holder.doc);
    TEST_CHECK(locker_domain_json_int64(&holder.doc, root, &value));
    TEST_CHECK_U64(0u, (uint64_t)value);

    TEST_CHECK(jt_parse(&holder, "-0"));
    root = locker_domain_json_root(&holder.doc);
    TEST_CHECK(locker_domain_json_int64(&holder.doc, root, &value));
    TEST_CHECK_U64(0u, (uint64_t)value);

    TEST_CHECK(jt_parse(&holder, "9223372036854775807"));
    root = locker_domain_json_root(&holder.doc);
    TEST_CHECK(locker_domain_json_int64(&holder.doc, root, &value));
    TEST_CHECK_U64(9223372036854775807ull, (uint64_t)value);

    TEST_CHECK(jt_parse(&holder, "-9223372036854775808"));
    root = locker_domain_json_root(&holder.doc);
    TEST_CHECK(locker_domain_json_int64(&holder.doc, root, &value));
    TEST_CHECK_U64(0x8000000000000000ull, (uint64_t)value);

    /* 越界：正数超 INT64_MAX、负数超 INT64_MIN、20 位超 UINT64。 */
    TEST_CHECK(!jt_parse(&holder, "9223372036854775808"));
    TEST_CHECK(!jt_parse(&holder, "-9223372036854775809"));
    TEST_CHECK(!jt_parse(&holder, "18446744073709551615"));
    TEST_CHECK(!jt_parse(&holder, "18446744073709551616"));
    /* 前导零、浮点、指数一律拒绝（U64 走十进制文本，不走 JSON 数字）。 */
    TEST_CHECK(!jt_parse(&holder, "01"));
    TEST_CHECK(!jt_parse(&holder, "00"));
    TEST_CHECK(!jt_parse(&holder, "-01"));
    TEST_CHECK(!jt_parse(&holder, "1.0"));
    TEST_CHECK(!jt_parse(&holder, "1e5"));
    TEST_CHECK(!jt_parse(&holder, "1E5"));
    TEST_CHECK(!jt_parse(&holder, "-"));
    TEST_CHECK(!jt_parse(&holder, "+1"));
    TEST_CHECK(!jt_parse(&holder, "1a"));
}

static void test_parse_depth_and_pool(void)
{
    jt_doc_t holder;
    char text[64];
    size_t i;

    /* 12 层嵌套是上限；13 层必须拒绝。 */
    for (i = 0u; i < 12u; i++) {
        text[i] = '[';
        text[12u + i] = ']';
    }
    text[24] = '\0';
    TEST_CHECK(jt_parse(&holder, text));

    for (i = 0u; i < 13u; i++) {
        text[i] = '[';
        text[13u + i] = ']';
    }
    text[26] = '\0';
    TEST_CHECK(!jt_parse(&holder, text));

    /* 节点池耗尽时解析失败（不是静默截断）。 */
    memset(&holder, 0, sizeof(holder));
    holder.doc.nodes = holder.nodes;
    holder.doc.node_capacity = 2u;
    holder.doc.members = holder.members;
    holder.doc.member_capacity = JT_MEMBERS;
    TEST_CHECK(!locker_domain_json_parse(&holder.doc, "[1,2,3]", 7u));
    holder.doc.node_count = 0u;
    holder.doc.member_count = 0u;
    holder.doc.node_capacity = JT_NODES;
    TEST_CHECK(locker_domain_json_parse(&holder.doc, "[1,2,3]", 7u));

    /* 成员池耗尽同样失败。 */
    holder.doc.member_count = 0u;
    holder.doc.node_count = 0u;
    holder.doc.member_capacity = 2u;
    TEST_CHECK(!locker_domain_json_parse(&holder.doc, "[1,2,3]", 7u));

    /* 参数校验：NULL 与空长度。 */
    TEST_CHECK(!locker_domain_json_parse(NULL, "1", 1u));
    TEST_CHECK(!locker_domain_json_parse(&holder.doc, NULL, 1u));
    TEST_CHECK(!locker_domain_json_parse(&holder.doc, "1", 0u));
}

static void test_accessors(void)
{
    jt_doc_t holder;
    const locker_json_node_t *root;
    bool flag = false;
    int64_t value = 0;

    TEST_CHECK(jt_parse(&holder, "{\"A\":true,\"a\":false,\"n\":1}"));
    root = locker_domain_json_root(&holder.doc);
    /* 键区分大小写。 */
    TEST_CHECK(locker_domain_json_get(&holder.doc, root, "A") != NULL);
    TEST_CHECK(locker_domain_json_get(&holder.doc, root, "a") != NULL);
    TEST_CHECK(locker_domain_json_get(&holder.doc, root, "B") == NULL);
    TEST_CHECK(locker_domain_json_get(&holder.doc, root, "") == NULL);
    TEST_CHECK(locker_domain_json_get(&holder.doc, root, NULL) == NULL);

    TEST_CHECK(locker_domain_json_boolean(&holder.doc, locker_domain_json_get(&holder.doc, root, "A"),
                                          &flag));
    TEST_CHECK(flag == true);
    TEST_CHECK(locker_domain_json_boolean(&holder.doc, locker_domain_json_get(&holder.doc, root, "a"),
                                          &flag));
    TEST_CHECK(flag == false);
    /* 类型不符返回 false，且不改 out。 */
    flag = true;
    TEST_CHECK(!locker_domain_json_boolean(&holder.doc, locker_domain_json_get(&holder.doc, root, "n"),
                                           &flag));
    TEST_CHECK(flag == true);
    TEST_CHECK(!locker_domain_json_int64(&holder.doc, locker_domain_json_get(&holder.doc, root, "A"),
                                         &value));
    /* 对非对象调用 get 返回 NULL。 */
    TEST_CHECK(locker_domain_json_get(&holder.doc, NULL, "A") == NULL);
    TEST_CHECK(jt_parse(&holder, "[1,2]"));
    root = locker_domain_json_root(&holder.doc);
    TEST_CHECK(locker_domain_json_get(&holder.doc, root, "0") == NULL);
    /* at 越界与非数组返回 NULL。 */
    TEST_CHECK(locker_domain_json_at(&holder.doc, root, 0u) != NULL);
    TEST_CHECK(locker_domain_json_at(&holder.doc, root, 2u) == NULL);
    TEST_CHECK(jt_parse(&holder, "{\"a\":1}"));
    root = locker_domain_json_root(&holder.doc);
    TEST_CHECK(locker_domain_json_at(&holder.doc, root, 0u) == NULL);
    TEST_CHECK(locker_domain_json_at(&holder.doc, NULL, 0u) == NULL);
    /* 空数组的 at 一律 NULL（first 是 UINT32_MAX）。 */
    TEST_CHECK(jt_parse(&holder, "[]"));
    root = locker_domain_json_root(&holder.doc);
    TEST_CHECK(locker_domain_json_at(&holder.doc, root, 0u) == NULL);
}

static void test_render(void)
{
    jt_doc_t holder;
    const locker_json_node_t *root;
    char out[256];
    size_t length = 0u;

    /* 紧凑重序列化：空白被去掉，转义被规范化。 */
    TEST_CHECK(jt_parse(&holder, " { \"a\" : [ 1 , true , null ] , \"b\" : \"x\\u0041\" } "));
    root = locker_domain_json_root(&holder.doc);
    TEST_CHECK(locker_domain_json_render(&holder.doc, root, out, sizeof(out), &length));
    TEST_CHECK_STRING("{\"a\":[1,true,null],\"b\":\"xA\"}", out);
    TEST_CHECK_U64(strlen(out), (uint64_t)length);

    /* 需要重新转义的字符串。 */
    TEST_CHECK(jt_parse(&holder, "\"a\\nb\\\"c\""));
    root = locker_domain_json_root(&holder.doc);
    TEST_CHECK(locker_domain_json_render(&holder.doc, root, out, sizeof(out), &length));
    TEST_CHECK_STRING("\"a\\nb\\\"c\"", out);

    /* 缓冲不足整体失败。 */
    TEST_CHECK(!locker_domain_json_render(&holder.doc, root, out, 4u, &length));
    TEST_CHECK(!locker_domain_json_render(&holder.doc, root, out, 0u, &length));
    TEST_CHECK(!locker_domain_json_render(&holder.doc, NULL, out, sizeof(out), &length));
    TEST_CHECK(!locker_domain_json_render(NULL, root, out, sizeof(out), &length));

    /* 渲染结果必须能被自己重新解析（往返一致）。
       第二趟必须换缓冲：round.doc.text 就指向 out，再写 out 会边读边改。 */
    char second[256];
    TEST_CHECK(jt_parse(&holder, "{\"k\":[-1,0,9223372036854775807]}"));
    root = locker_domain_json_root(&holder.doc);
    TEST_CHECK(locker_domain_json_render(&holder.doc, root, out, sizeof(out), &length));
    jt_doc_t round;
    TEST_CHECK(jt_parse(&round, out));
    TEST_CHECK(locker_domain_json_render(&round.doc, locker_domain_json_root(&round.doc), second,
                                         sizeof(second), &length));
    TEST_CHECK_STRING("{\"k\":[-1,0,9223372036854775807]}", second);
}

int main(void)
{
    test_writer_scalars();
    test_writer_escaping();
    test_writer_bounds();
    test_parse_accepts();
    test_parse_escapes();
    test_parse_rejects_syntax();
    test_parse_rejects_numbers();
    test_parse_depth_and_pool();
    test_accessors();
    test_render();
    TEST_REPORT("domain.test_domain_json");
}
