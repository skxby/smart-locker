#include "locker/storage/regex.h"

#include <stdlib.h>
#include <string.h>

#define LOCKER_REGEX_MAX_STATES 4096u
#define LOCKER_REGEX_MAX_DEPTH 12u
#define LOCKER_REGEX_REPEAT_MAX 255u

enum regex_state_kind {
    REGEX_STATE_EPS = 0,
    REGEX_STATE_CLASS = 1,
    REGEX_STATE_BOL = 2,
    REGEX_STATE_EOL = 3,
    REGEX_STATE_MATCH = 4
};

typedef struct {
    int kind;
    unsigned char bits[32];
    int out1;
    int out2;
} regex_state_t;

typedef struct {
    int *items;
    size_t count;
    size_t capacity;
} patch_list_t;

typedef struct {
    int start;
    patch_list_t out;
} regex_frag_t;

struct locker_storage_regex {
    regex_state_t *states;
    size_t state_count;
    size_t state_capacity;
    int start;
};

typedef struct {
    const char *pattern;
    size_t length;
    size_t position;
    struct locker_storage_regex *regex;
    bool failed;
} regex_parser_t;

static void patch_list_init(patch_list_t *list)
{
    list->items = NULL;
    list->count = 0u;
    list->capacity = 0u;
}

static void patch_list_free(patch_list_t *list)
{
    free(list->items);
    list->items = NULL;
    list->count = 0u;
    list->capacity = 0u;
}

static bool patch_list_push(patch_list_t *list, int value)
{
    if (list->count == list->capacity) {
        const size_t next_capacity = (list->capacity == 0u) ? 8u : list->capacity * 2u;
        int *next_items = (int *)realloc(list->items, next_capacity * sizeof(int));
        if (next_items == NULL) {
            return false;
        }
        list->items = next_items;
        list->capacity = next_capacity;
    }
    list->items[list->count] = value;
    ++list->count;
    return true;
}

static bool patch_list_append(patch_list_t *target, const patch_list_t *source)
{
    for (size_t index = 0u; index < source->count; ++index) {
        if (!patch_list_push(target, source->items[index])) {
            return false;
        }
    }
    return true;
}

static void patch_list_apply(regex_state_t *states, patch_list_t *list, int target)
{
    for (size_t index = 0u; index < list->count; ++index) {
        const int encoded = list->items[index];
        regex_state_t *state = &states[encoded >> 1];
        if ((encoded & 1) != 0) {
            state->out2 = target;
        } else {
            state->out1 = target;
        }
    }
    list->count = 0u;
}

static int regex_add_state(regex_parser_t *parser, int kind)
{
    struct locker_storage_regex *regex = parser->regex;
    if (regex->state_count >= LOCKER_REGEX_MAX_STATES) {
        parser->failed = true;
        return -1;
    }
    if (regex->state_count == regex->state_capacity) {
        const size_t next_capacity = (regex->state_capacity == 0u) ? 32u : regex->state_capacity * 2u;
        regex_state_t *next_states =
            (regex_state_t *)realloc(regex->states, next_capacity * sizeof(regex_state_t));
        if (next_states == NULL) {
            parser->failed = true;
            return -1;
        }
        regex->states = next_states;
        regex->state_capacity = next_capacity;
    }
    regex_state_t *state = &regex->states[regex->state_count];
    memset(state, 0, sizeof(*state));
    state->kind = kind;
    state->out1 = -1;
    state->out2 = -1;
    return (int)regex->state_count++;
}

static regex_frag_t frag_empty_state(regex_parser_t *parser)
{
    regex_frag_t frag;
    patch_list_init(&frag.out);
    frag.start = regex_add_state(parser, REGEX_STATE_EPS);
    if (frag.start >= 0) {
        (void)patch_list_push(&frag.out, (frag.start << 1) | 0);
    }
    return frag;
}

static regex_frag_t frag_class_state(regex_parser_t *parser, const unsigned char bits[32])
{
    regex_frag_t frag;
    patch_list_init(&frag.out);
    frag.start = regex_add_state(parser, REGEX_STATE_CLASS);
    if (frag.start >= 0) {
        memcpy(parser->regex->states[frag.start].bits, bits, 32u);
        (void)patch_list_push(&frag.out, (frag.start << 1) | 0);
    }
    return frag;
}

static regex_frag_t frag_assert_state(regex_parser_t *parser, int kind)
{
    regex_frag_t frag;
    patch_list_init(&frag.out);
    frag.start = regex_add_state(parser, kind);
    if (frag.start >= 0) {
        (void)patch_list_push(&frag.out, (frag.start << 1) | 0);
    }
    return frag;
}

static regex_frag_t frag_concat(regex_parser_t *parser, regex_frag_t left, regex_frag_t right)
{
    regex_frag_t result;
    patch_list_init(&result.out);
    if (left.start < 0 || right.start < 0) {
        result.start = -1;
        return result;
    }
    patch_list_apply(parser->regex->states, &left.out, right.start);
    result.start = left.start;
    if (!patch_list_append(&result.out, &right.out)) {
        parser->failed = true;
    }
    patch_list_free(&left.out);
    patch_list_free(&right.out);
    return result;
}

static regex_frag_t frag_alternate(regex_parser_t *parser, regex_frag_t left, regex_frag_t right)
{
    regex_frag_t result;
    patch_list_init(&result.out);
    const int split = regex_add_state(parser, REGEX_STATE_EPS);
    if (left.start < 0 || right.start < 0 || split < 0) {
        result.start = -1;
        return result;
    }
    parser->regex->states[split].out1 = left.start;
    parser->regex->states[split].out2 = right.start;
    result.start = split;
    if (!patch_list_append(&result.out, &left.out) || !patch_list_append(&result.out, &right.out)) {
        parser->failed = true;
    }
    patch_list_free(&left.out);
    patch_list_free(&right.out);
    return result;
}

static regex_frag_t frag_optional(regex_parser_t *parser, regex_frag_t inner)
{
    regex_frag_t empty = frag_empty_state(parser);
    return frag_alternate(parser, inner, empty);
}

static regex_frag_t frag_star(regex_parser_t *parser, regex_frag_t inner)
{
    regex_frag_t result;
    patch_list_init(&result.out);
    const int split = regex_add_state(parser, REGEX_STATE_EPS);
    if (inner.start < 0 || split < 0) {
        result.start = -1;
        return result;
    }
    parser->regex->states[split].out1 = inner.start;
    patch_list_apply(parser->regex->states, &inner.out, split);
    result.start = split;
    (void)patch_list_push(&result.out, (split << 1) | 1);
    patch_list_free(&inner.out);
    return result;
}

static regex_frag_t frag_plus(regex_parser_t *parser, regex_frag_t inner)
{
    regex_frag_t result;
    patch_list_init(&result.out);
    const int split = regex_add_state(parser, REGEX_STATE_EPS);
    if (inner.start < 0 || split < 0) {
        result.start = -1;
        return result;
    }
    parser->regex->states[split].out1 = inner.start;
    patch_list_apply(parser->regex->states, &inner.out, split);
    result.start = inner.start;
    (void)patch_list_push(&result.out, (split << 1) | 1);
    patch_list_free(&inner.out);
    return result;
}

static void class_set_bit(unsigned char bits[32], unsigned char value)
{
    bits[value >> 3] = (unsigned char)(bits[value >> 3] | (unsigned char)(1u << (value & 7u)));
}

static void class_set_range(unsigned char bits[32], unsigned char low, unsigned char high)
{
    if (low > high) {
        return;
    }
    for (unsigned int value = low; value <= high; ++value) {
        class_set_bit(bits, (unsigned char)value);
    }
}

static void class_negate(unsigned char bits[32])
{
    for (size_t index = 0u; index < 32u; ++index) {
        bits[index] = (unsigned char)(~bits[index]);
    }
}

static bool is_digit_char(char character)
{
    return character >= '0' && character <= '9';
}

static regex_frag_t parse_alternation(regex_parser_t *parser, unsigned depth);

static regex_frag_t parse_atom_once(regex_parser_t *parser, unsigned depth)
{
    regex_frag_t frag;
    patch_list_init(&frag.out);
    frag.start = -1;
    if (parser->failed || depth > LOCKER_REGEX_MAX_DEPTH) {
        parser->failed = true;
        return frag;
    }
    if (parser->position >= parser->length) {
        parser->failed = true;
        return frag;
    }
    const char character = parser->pattern[parser->position];
    if (character == '(') {
        ++parser->position;
        frag = parse_alternation(parser, depth + 1u);
        if (parser->failed) {
            return frag;
        }
        if (parser->position >= parser->length || parser->pattern[parser->position] != ')') {
            parser->failed = true;
            patch_list_free(&frag.out);
            return frag;
        }
        ++parser->position;
        return frag;
    }
    if (character == '[') {
        unsigned char bits[32];
        memset(bits, 0, sizeof(bits));
        ++parser->position;
        bool negate = false;
        if (parser->position < parser->length && parser->pattern[parser->position] == '^') {
            negate = true;
            ++parser->position;
        }
        bool first = true;
        while (parser->position < parser->length) {
            const char item = parser->pattern[parser->position];
            if (item == ']' && !first) {
                break;
            }
            first = false;
            unsigned char low = (unsigned char)item;
            ++parser->position;
            if (item == '\\') {
                if (parser->position >= parser->length) {
                    parser->failed = true;
                    return frag;
                }
                const char escape = parser->pattern[parser->position];
                ++parser->position;
                if (escape == 'd') {
                    class_set_range(bits, (unsigned char)'0', (unsigned char)'9');
                    continue;
                }
                if (escape == 'w') {
                    class_set_range(bits, (unsigned char)'0', (unsigned char)'9');
                    class_set_range(bits, (unsigned char)'A', (unsigned char)'Z');
                    class_set_range(bits, (unsigned char)'a', (unsigned char)'z');
                    class_set_bit(bits, (unsigned char)'_');
                    continue;
                }
                if (escape == 's') {
                    class_set_bit(bits, (unsigned char)' ');
                    class_set_bit(bits, (unsigned char)'\t');
                    class_set_bit(bits, (unsigned char)'\n');
                    class_set_bit(bits, (unsigned char)'\r');
                    continue;
                }
                low = (unsigned char)escape;
            }
            if (parser->position + 1u < parser->length && parser->pattern[parser->position] == '-' &&
                parser->pattern[parser->position + 1u] != ']') {
                const char high = parser->pattern[parser->position + 1u];
                if (high == '\\') {
                    parser->failed = true;
                    return frag;
                }
                parser->position += 2u;
                class_set_range(bits, low, (unsigned char)high);
            } else {
                class_set_bit(bits, low);
            }
        }
        if (parser->position >= parser->length || parser->pattern[parser->position] != ']') {
            parser->failed = true;
            return frag;
        }
        ++parser->position;
        if (negate) {
            class_negate(bits);
        }
        return frag_class_state(parser, bits);
    }
    if (character == '.') {
        unsigned char bits[32];
        memset(bits, 0, sizeof(bits));
        class_negate(bits);
        ++parser->position;
        return frag_class_state(parser, bits);
    }
    if (character == '^') {
        ++parser->position;
        return frag_assert_state(parser, REGEX_STATE_BOL);
    }
    if (character == '$') {
        ++parser->position;
        return frag_assert_state(parser, REGEX_STATE_EOL);
    }
    if (character == '\\') {
        if (parser->position + 1u >= parser->length) {
            parser->failed = true;
            return frag;
        }
        const char escape = parser->pattern[parser->position + 1u];
        parser->position += 2u;
        unsigned char bits[32];
        memset(bits, 0, sizeof(bits));
        if (escape == 'd') {
            class_set_range(bits, (unsigned char)'0', (unsigned char)'9');
            return frag_class_state(parser, bits);
        }
        if (escape == 'w') {
            class_set_range(bits, (unsigned char)'0', (unsigned char)'9');
            class_set_range(bits, (unsigned char)'A', (unsigned char)'Z');
            class_set_range(bits, (unsigned char)'a', (unsigned char)'z');
            class_set_bit(bits, (unsigned char)'_');
            return frag_class_state(parser, bits);
        }
        if (escape == 's') {
            class_set_bit(bits, (unsigned char)' ');
            class_set_bit(bits, (unsigned char)'\t');
            class_set_bit(bits, (unsigned char)'\n');
            class_set_bit(bits, (unsigned char)'\r');
            return frag_class_state(parser, bits);
        }
        if (escape == 'D' || escape == 'W' || escape == 'S' || escape == 'b' || escape == 'B') {
            /* 不支持：fail closed，不放行。 */
            parser->failed = true;
            return frag;
        }
        class_set_bit(bits, (unsigned char)escape);
        return frag_class_state(parser, bits);
    }
    if (character == '|' || character == ')' || character == '*' || character == '+' || character == '?' ||
        character == '{' || character == '}') {
        parser->failed = true;
        return frag;
    }
    unsigned char bits[32];
    memset(bits, 0, sizeof(bits));
    class_set_bit(bits, (unsigned char)character);
    ++parser->position;
    return frag_class_state(parser, bits);
}

static bool parse_repeat_bounds(regex_parser_t *parser, unsigned *min_out, unsigned *max_out, bool *unbounded_out)
{
    /* 进入时 parser->pattern[position] == '{' */
    ++parser->position;
    unsigned min_value = 0u;
    bool has_min = false;
    while (parser->position < parser->length && is_digit_char(parser->pattern[parser->position])) {
        if (min_value > LOCKER_REGEX_REPEAT_MAX) {
            return false;
        }
        min_value = min_value * 10u + (unsigned)(parser->pattern[parser->position] - '0');
        has_min = true;
        ++parser->position;
    }
    if (!has_min) {
        return false;
    }
    unsigned max_value = min_value;
    bool unbounded = false;
    if (parser->position < parser->length && parser->pattern[parser->position] == ',') {
        ++parser->position;
        if (parser->position < parser->length && parser->pattern[parser->position] == '}') {
            unbounded = true;
        } else {
            max_value = 0u;
            bool has_max = false;
            while (parser->position < parser->length && is_digit_char(parser->pattern[parser->position])) {
                if (max_value > LOCKER_REGEX_REPEAT_MAX) {
                    return false;
                }
                max_value = max_value * 10u + (unsigned)(parser->pattern[parser->position] - '0');
                has_max = true;
                ++parser->position;
            }
            if (!has_max) {
                return false;
            }
        }
    }
    if (parser->position >= parser->length || parser->pattern[parser->position] != '}') {
        return false;
    }
    ++parser->position;
    if (min_value > LOCKER_REGEX_REPEAT_MAX || max_value > LOCKER_REGEX_REPEAT_MAX) {
        return false;
    }
    if (!unbounded && max_value < min_value) {
        return false;
    }
    *min_out = min_value;
    *max_out = max_value;
    *unbounded_out = unbounded;
    return true;
}

static regex_frag_t parse_repeated(regex_parser_t *parser, unsigned depth)
{
    regex_frag_t frag;
    patch_list_init(&frag.out);
    frag.start = -1;
    const size_t atom_begin = parser->position;
    regex_frag_t atom = parse_atom_once(parser, depth);
    if (parser->failed) {
        return atom;
    }
    const size_t atom_end = parser->position;
    if (parser->position >= parser->length) {
        return atom;
    }
    const char character = parser->pattern[parser->position];
    if (character == '?') {
        ++parser->position;
        return frag_optional(parser, atom);
    }
    if (character == '*') {
        ++parser->position;
        return frag_star(parser, atom);
    }
    if (character == '+') {
        ++parser->position;
        return frag_plus(parser, atom);
    }
    if (character != '{') {
        return atom;
    }
    unsigned min_value = 0u;
    unsigned max_value = 0u;
    bool unbounded = false;
    if (!parse_repeat_bounds(parser, &min_value, &max_value, &unbounded)) {
        parser->failed = true;
        patch_list_free(&atom.out);
        return frag;
    }
    const size_t span_begin = atom_begin;
    const size_t span_length = atom_end - atom_begin;
    if (min_value == 0u && !unbounded && max_value == 0u) {
        patch_list_free(&atom.out);
        return frag_empty_state(parser);
    }
    regex_frag_t result = atom;
    unsigned produced = 1u;
    if (min_value == 0u) {
        result = frag_optional(parser, atom);
        produced = 1u;
    }
    for (unsigned index = produced; index < min_value; ++index) {
        const size_t saved_position = parser->position;
        parser->position = span_begin;
        regex_frag_t copy = parse_atom_once(parser, depth);
        parser->position = saved_position;
        if (parser->failed) {
            patch_list_free(&result.out);
            return frag;
        }
        result = frag_concat(parser, result, copy);
    }
    if (unbounded) {
        const size_t saved_position = parser->position;
        parser->position = span_begin;
        regex_frag_t copy = parse_atom_once(parser, depth);
        parser->position = saved_position;
        if (parser->failed) {
            patch_list_free(&result.out);
            return frag;
        }
        result = frag_concat(parser, result, frag_star(parser, copy));
        return result;
    }
    /* 到这里已经生成的 atom 数：min_value == 0 时是上面那个可选 atom（1 个），否则是
       min_value 个。有界循环必须从「已生成数」继续，否则 {0,n} 会多接受一个 atom、
       {n,n} 会少生成（审计 round-4 N-3）。 */
    if (min_value > 0u) {
        produced = min_value;
    }
    for (unsigned index = produced; index < max_value; ++index) {
        const size_t saved_position = parser->position;
        parser->position = span_begin;
        regex_frag_t copy = parse_atom_once(parser, depth);
        parser->position = saved_position;
        if (parser->failed) {
            patch_list_free(&result.out);
            return frag;
        }
        result = frag_concat(parser, result, frag_optional(parser, copy));
    }
    (void)span_length;
    return result;
}

static regex_frag_t parse_sequence(regex_parser_t *parser, unsigned depth)
{
    regex_frag_t result = frag_empty_state(parser);
    while (!parser->failed && parser->position < parser->length) {
        const char character = parser->pattern[parser->position];
        if (character == '|' || character == ')') {
            break;
        }
        regex_frag_t next = parse_repeated(parser, depth);
        if (parser->failed) {
            patch_list_free(&result.out);
            return next;
        }
        result = frag_concat(parser, result, next);
    }
    return result;
}

static regex_frag_t parse_alternation(regex_parser_t *parser, unsigned depth)
{
    regex_frag_t result = parse_sequence(parser, depth);
    while (!parser->failed && parser->position < parser->length && parser->pattern[parser->position] == '|') {
        ++parser->position;
        regex_frag_t right = parse_sequence(parser, depth);
        if (parser->failed) {
            patch_list_free(&result.out);
            return right;
        }
        result = frag_alternate(parser, result, right);
    }
    return result;
}

locker_storage_regex_t *locker_storage_regex_compile(const char *pattern)
{
    if (pattern == NULL || pattern[0] == '\0') {
        return NULL;
    }
    struct locker_storage_regex *regex =
        (struct locker_storage_regex *)calloc(1u, sizeof(struct locker_storage_regex));
    if (regex == NULL) {
        return NULL;
    }
    regex_parser_t parser;
    memset(&parser, 0, sizeof(parser));
    parser.pattern = pattern;
    parser.length = strlen(pattern);
    parser.regex = regex;
    regex_frag_t root = parse_alternation(&parser, 0u);
    if (parser.failed || parser.position != parser.length) {
        patch_list_free(&root.out);
        locker_storage_regex_free(regex);
        return NULL;
    }
    const int accept = regex_add_state(&parser, REGEX_STATE_MATCH);
    if (accept < 0) {
        patch_list_free(&root.out);
        locker_storage_regex_free(regex);
        return NULL;
    }
    patch_list_apply(regex->states, &root.out, accept);
    regex->start = root.start;
    patch_list_free(&root.out);
    return regex;
}

void locker_storage_regex_free(locker_storage_regex_t *regex)
{
    if (regex == NULL) {
        return;
    }
    free(regex->states);
    free(regex);
}

typedef struct {
    bool *marks;
    int *stack;
    size_t stack_count;
    size_t state_count;
} regex_closure_t;

static void closure_push(regex_closure_t *closure, const regex_state_t *states, int state_index, size_t position,
                         size_t length)
{
    if (state_index < 0) {
        return;
    }
    if (closure->marks[state_index]) {
        return;
    }
    closure->marks[state_index] = true;
    closure->stack[closure->stack_count++] = state_index;
    (void)states;
    (void)position;
    (void)length;
}

static void closure_expand(regex_closure_t *closure, const regex_state_t *states, size_t position, size_t length)
{
    while (closure->stack_count > 0u) {
        const int state_index = closure->stack[--closure->stack_count];
        const regex_state_t *state = &states[state_index];
        if (state->kind == REGEX_STATE_EPS) {
            closure_push(closure, states, state->out1, position, length);
            closure_push(closure, states, state->out2, position, length);
        } else if (state->kind == REGEX_STATE_BOL) {
            if (position == 0u) {
                closure_push(closure, states, state->out1, position, length);
            }
        } else if (state->kind == REGEX_STATE_EOL) {
            if (position == length) {
                closure_push(closure, states, state->out1, position, length);
            }
        }
    }
}

bool locker_storage_regex_match(const locker_storage_regex_t *regex, const char *text, size_t length)
{
    if (regex == NULL || (text == NULL && length > 0u)) {
        return false;
    }
    const size_t state_count = regex->state_count;
    bool *current = (bool *)calloc(state_count, sizeof(bool));
    bool *next = (bool *)calloc(state_count, sizeof(bool));
    int *stack = (int *)malloc(state_count * sizeof(int));
    bool matched = false;
    if (current == NULL || next == NULL || stack == NULL) {
        free(current);
        free(next);
        free(stack);
        return false;
    }
    for (size_t position = 0u; position <= length && !matched; ++position) {
        /* 搜索语义：每个起点都允许从起始状态出发（有 ^ 的模式由 BOL 断言自然限制在 0）。 */
        regex_closure_t closure;
        closure.marks = current;
        closure.stack = stack;
        closure.stack_count = 0u;
        closure.state_count = state_count;
        closure_push(&closure, regex->states, regex->start, position, length);
        closure_expand(&closure, regex->states, position, length);
        /* 搜索语义：任意起点出发到达接受态即算命中，不要求恰好走到串尾。 */
        if (current[state_count - 1u]) {
            matched = true;
            break;
        }
        if (position == length) {
            break;
        }
        memset(next, 0, state_count * sizeof(bool));
        const unsigned char byte = (unsigned char)text[position];
        for (size_t index = 0u; index < state_count; ++index) {
            if (!current[index]) {
                continue;
            }
            const regex_state_t *state = &regex->states[index];
            if (state->kind != REGEX_STATE_CLASS) {
                continue;
            }
            if ((state->bits[byte >> 3] & (unsigned char)(1u << (byte & 7u))) == 0u) {
                continue;
            }
            if (state->out1 >= 0) {
                next[state->out1] = true;
            }
        }
        /* 用 next 作为下一轮的种子集合，再补 eps 闭包。 */
        regex_closure_t next_closure;
        next_closure.marks = next;
        next_closure.stack = stack;
        next_closure.stack_count = 0u;
        next_closure.state_count = state_count;
        for (size_t index = 0u; index < state_count; ++index) {
            if (next[index]) {
                next_closure.stack[next_closure.stack_count++] = (int)index;
            }
        }
        closure_expand(&next_closure, regex->states, position + 1u, length);
        bool *swap = current;
        current = next;
        next = swap;
    }
    free(current);
    free(next);
    free(stack);
    return matched;
}
