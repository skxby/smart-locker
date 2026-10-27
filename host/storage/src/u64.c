#include "locker/storage/u64.h"

#include <string.h>

#include "locker/common/numeric.h"

/* 薄适配层（审计 N5）：解析/格式化/数值比较的语义由 host/common 的 numeric.c
 * 统一实现（S1-01 已交付并单独测试），这里只做两件事：
 *   1) 保持本模块自己的头文件签名，不让仓储层直接依赖 common 的 status 枚举；
 *   2) compare_text 允许「带前导零的十进制文本」——事件键的定宽补零字段用它比较，
 *      而 common 的 locker_u64_text_compare 只接受规范文本。
 * 不在这里复制第二份溢出/位数判断逻辑。 */

bool locker_storage_u64_parse(const char *text, size_t length, uint64_t *out)
{
    uint64_t value = 0u;
    if (out == NULL || locker_u64_parse_decimal(text, length, &value) != LOCKER_STATUS_OK) {
        return false;
    }
    *out = value;
    return true;
}

bool locker_storage_u64_format(uint64_t value, char *out, size_t out_size)
{
    if (out == NULL || out_size < 2u) {
        return false;
    }
    return locker_u64_format_decimal(value, out, out_size) == LOCKER_STATUS_OK;
}

int locker_storage_u64_compare_text(const char *left, const char *right)
{
    if (left == NULL || right == NULL) {
        return (left == right) ? 0 : (left == NULL ? -1 : 1);
    }
    /* 去掉前导零得到规范切片，再交给 common 做数值比较（含溢出与位数检查）。 */
    size_t left_length = strlen(left);
    size_t right_length = strlen(right);
    while (left_length > 1u && left[0] == '0') {
        left += 1;
        left_length -= 1u;
    }
    while (right_length > 1u && right[0] == '0') {
        right += 1;
        right_length -= 1u;
    }
    int order = 0;
    if (locker_u64_text_compare(left, left_length, right, right_length, &order) != LOCKER_STATUS_OK) {
        /* 非法文本：保持 fail closed 的确定性顺序（短的在前），调用方另有校验。 */
        if (left_length != right_length) {
            return left_length < right_length ? -1 : 1;
        }
        const int diff = memcmp(left, right, left_length);
        return diff == 0 ? 0 : (diff < 0 ? -1 : 1);
    }
    return order;
}
