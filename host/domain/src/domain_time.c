#define _POSIX_C_SOURCE 200809L

#include "domain_time.h"

#include <string.h>

static bool is_leap_year(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

static int days_in_month(int year, int month)
{
    static const int k_days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) {
        return 0;
    }
    if (month == 2 && is_leap_year(year)) {
        return 29;
    }
    return k_days[month - 1];
}

/* 公历日期 -> 1970-01-01 起的天数（Howard Hinnant 的 days_from_civil）。 */
static int64_t days_from_civil(int year, unsigned month, unsigned day)
{
    int64_t y = (int64_t)year - (month <= 2u ? 1 : 0);
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153u * (month + (month > 2u ? (unsigned)-3 : 9u)) + 2u) / 5u + day - 1u;
    unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

static void civil_from_days(int64_t days, int *out_year, unsigned *out_month, unsigned *out_day)
{
    int64_t z = days + 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
    int64_t y = (int64_t)yoe + era * 400;
    unsigned doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    unsigned mp = (5u * doy + 2u) / 153u;
    unsigned day = doy - (153u * mp + 2u) / 5u + 1u;
    unsigned month = mp + (mp < 10u ? 3u : (unsigned)-9);
    *out_year = (int)(y + (month <= 2u ? 1 : 0));
    *out_month = month;
    *out_day = day;
}

bool locker_domain_time_format_utc(uint64_t wall_utc_ms, char *out, size_t out_size)
{
    if (out == NULL || out_size < 21u) {
        return false;
    }
    uint64_t seconds = wall_utc_ms / 1000u;
    int64_t days = (int64_t)(seconds / 86400u);
    uint64_t remainder = seconds % 86400u;
    int year = 0;
    unsigned month = 0u;
    unsigned day = 0u;
    civil_from_days(days, &year, &month, &day);
    unsigned hour = (unsigned)(remainder / 3600u);
    unsigned minute = (unsigned)((remainder % 3600u) / 60u);
    unsigned second = (unsigned)(remainder % 60u);
    if (year < 1 || year > 9999) {
        return false;
    }
    static const char k_digits[] = "0123456789";
    char buffer[21];
    buffer[0] = k_digits[(year / 1000) % 10];
    buffer[1] = k_digits[(year / 100) % 10];
    buffer[2] = k_digits[(year / 10) % 10];
    buffer[3] = k_digits[year % 10];
    buffer[4] = '-';
    buffer[5] = k_digits[(month / 10) % 10];
    buffer[6] = k_digits[month % 10];
    buffer[7] = '-';
    buffer[8] = k_digits[(day / 10) % 10];
    buffer[9] = k_digits[day % 10];
    buffer[10] = 'T';
    buffer[11] = k_digits[(hour / 10) % 10];
    buffer[12] = k_digits[hour % 10];
    buffer[13] = ':';
    buffer[14] = k_digits[(minute / 10) % 10];
    buffer[15] = k_digits[minute % 10];
    buffer[16] = ':';
    buffer[17] = k_digits[(second / 10) % 10];
    buffer[18] = k_digits[second % 10];
    buffer[19] = 'Z';
    buffer[20] = '\0';
    memcpy(out, buffer, sizeof(buffer));
    return true;
}

static bool parse_fixed(const char *text, size_t length, size_t offset, size_t count, unsigned *out)
{
    if (offset + count > length) {
        return false;
    }
    unsigned value = 0u;
    for (size_t i = 0u; i < count; ++i) {
        char byte = text[offset + i];
        if (byte < '0' || byte > '9') {
            return false;
        }
        value = value * 10u + (unsigned)(byte - '0');
    }
    *out = value;
    return true;
}

bool locker_domain_time_parse_utc(const char *text, size_t length, uint64_t *out_wall_ms)
{
    if (text == NULL || out_wall_ms == NULL || length < 20u) {
        return false;
    }
    unsigned year = 0u;
    unsigned month = 0u;
    unsigned day = 0u;
    unsigned hour = 0u;
    unsigned minute = 0u;
    unsigned second = 0u;
    if (!parse_fixed(text, length, 0u, 4u, &year) || !parse_fixed(text, length, 5u, 2u, &month) ||
        !parse_fixed(text, length, 8u, 2u, &day) || !parse_fixed(text, length, 11u, 2u, &hour) ||
        !parse_fixed(text, length, 14u, 2u, &minute) || !parse_fixed(text, length, 17u, 2u, &second)) {
        return false;
    }
    if (text[4] != '-' || text[7] != '-' || text[10] != 'T' || text[13] != ':' || text[16] != ':') {
        return false;
    }
    if (year < 1u || year > 9999u || month < 1u || month > 12u || day < 1u ||
        day > (unsigned)days_in_month((int)year, (int)month) || hour > 23u || minute > 59u || second > 59u) {
        return false;
    }
    size_t position = 19u;
    uint64_t fraction_ms = 0u;
    if (position < length && text[position] == '.') {
        position += 1u;
        size_t digits = 0u;
        uint64_t scale = 100u;
        while (position < length && text[position] >= '0' && text[position] <= '9') {
            if (digits < 3u) {
                fraction_ms += (uint64_t)(text[position] - '0') * scale;
                scale /= 10u;
            }
            digits += 1u;
            position += 1u;
        }
        if (digits == 0u) {
            return false;
        }
    }
    int64_t offset_ms = 0;
    if (position >= length) {
        return false;
    }
    if (text[position] == 'Z' || text[position] == 'z') {
        position += 1u;
    } else if (text[position] == '+' || text[position] == '-') {
        bool negative = text[position] == '-';
        position += 1u;
        unsigned offset_hour = 0u;
        unsigned offset_minute = 0u;
        if (!parse_fixed(text, length, position, 2u, &offset_hour)) {
            return false;
        }
        position += 2u;
        if (position >= length || text[position] != ':') {
            return false;
        }
        position += 1u;
        if (!parse_fixed(text, length, position, 2u, &offset_minute)) {
            return false;
        }
        position += 2u;
        if (offset_hour > 23u || offset_minute > 59u) {
            return false;
        }
        offset_ms = (int64_t)(offset_hour * 3600u + offset_minute * 60u) * 1000;
        if (negative) {
            offset_ms = -offset_ms;
        }
    } else {
        return false;
    }
    if (position != length) {
        return false;
    }
    int64_t days = days_from_civil((int)year, month, day);
    int64_t seconds = days * 86400 + (int64_t)(hour * 3600u + minute * 60u + second);
    int64_t total_ms = seconds * 1000 + (int64_t)fraction_ms - offset_ms;
    if (total_ms < 0) {
        return false;
    }
    *out_wall_ms = (uint64_t)total_ms;
    return true;
}
