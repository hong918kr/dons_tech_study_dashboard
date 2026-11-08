/* DS-B02 배열 기반 스택과 괄호·명령 검사
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 <파일>.c && ./a.out
 * ASan: cc -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined <파일>.c && ./a.out
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* ---------------- 공통: 고정 용량 int32 스택 ---------------- */
#define STACK_CAP 8
typedef enum { ST_OK = 0, ST_OVERFLOW, ST_UNDERFLOW } st_status;

typedef struct {
    int32_t items[STACK_CAP];
    size_t  top;        /* 들어 있는 개수 = 다음에 쓸 칸 (empty-ascending) */
    size_t  high_water; /* 지금까지 가장 깊었던 순간의 개수 */
} istack;

static void   st_init(istack *s)        { s->top = 0; s->high_water = 0; }
static size_t st_size(const istack *s)  { return s->top; }
static bool   st_empty(const istack *s) { return s->top == 0; }

/* ---------------- Part A: push / pop / peek + overflow·underflow ---------------- */
static st_status st_push(istack *s, int32_t x) {
    (void)s; (void)x;
    return ST_OK; /* TODO: 가득 차면 ST_OVERFLOW. 아니면 넣고 top++, high_water 갱신 */
}
static st_status st_pop(istack *s, int32_t *out) {
    (void)s; (void)out;
    return ST_UNDERFLOW; /* TODO: 비었으면 ST_UNDERFLOW. 아니면 맨 위를 꺼낸다 */
}
static st_status st_peek(const istack *s, int32_t *out) {
    (void)s; (void)out;
    return ST_UNDERFLOW; /* TODO: 꺼내지 않고 맨 위 값만 본다 */
}

/* ---------------- Part B: 괄호 균형 검사 ---------------- */
typedef enum { BR_OK = 0, BR_MISMATCH, BR_UNCLOSED, BR_EXTRA_CLOSE, BR_TOO_DEEP } br_result;

/* 닫는 괄호에 짝이 되는 여는 괄호. 괄호가 아니면 0 */
static char opener_of(char c) {
    return c == ')' ? '(' : c == ']' ? '[' : c == '}' ? '{' : 0;
}

/* 스택에는 "여는 괄호의 위치"를 넣는다 → 문자도 s[pos] 로 알 수 있고 오류 위치도 보고할 수 있다.
 * err_pos: MISMATCH/EXTRA_CLOSE → 닫는 괄호 위치, UNCLOSED → 가장 안쪽(마지막) 미닫힘 여는 괄호,
 *          TOO_DEEP → 넘친 여는 괄호 위치 */
static br_result check_brackets(const char *str, size_t *err_pos) {
    (void)str; (void)err_pos; (void)opener_of;
    /* TODO: 여는 괄호 → 위치 push (실패 시 TOO_DEEP)
     *       닫는 괄호 → pop (실패 시 EXTRA_CLOSE), 짝이 안 맞으면 MISMATCH
     *       끝까지 갔는데 스택이 남아 있으면 UNCLOSED */
    return BR_OK;
}

/* ---------------- Part C: 후위표기식(RPN) 계산기 ---------------- */
typedef enum {
    RPN_OK = 0, RPN_ERR_EMPTY, RPN_ERR_TOKEN, RPN_ERR_UNDERFLOW,
    RPN_ERR_STACK_FULL, RPN_ERR_DIV0, RPN_ERR_RANGE, RPN_ERR_LEFTOVER
} rpn_status;

/* [제공] 토큰 하나를 정수로: 선택적 '-' + 숫자들. 숫자가 아니면 RPN_ERR_TOKEN, int32 밖이면 RPN_ERR_RANGE */
static rpn_status parse_number(const char *tok, size_t len, int32_t *out) {
    size_t i = 0;
    bool neg = false;
    if (tok[0] == '-') { neg = true; i = 1; }
    if (i == len) return RPN_ERR_TOKEN;
    int64_t v = 0;
    for (; i < len; i++) {
        if (tok[i] < '0' || tok[i] > '9') return RPN_ERR_TOKEN;
        v = v * 10 + (tok[i] - '0');
        if (v > (int64_t)INT32_MAX + 1) return RPN_ERR_RANGE;   /* 더 커지기 전에 멈춘다 */
    }
    if (neg) v = -v;
    if (v > INT32_MAX || v < INT32_MIN) return RPN_ERR_RANGE;
    *out = (int32_t)v;
    return RPN_OK;
}

/* 연산 하나: a op b. int64 로 계산해서 int32 범위를 확인한다 */
static rpn_status apply_op(char op, int32_t a, int32_t b, int32_t *res) {
    (void)op; (void)a; (void)b; (void)res;
    return RPN_ERR_TOKEN; /* TODO: + - * /, 0 나누기 → DIV0, int32 밖 → RANGE */
}

/* 공백으로 구분된 RPN 식을 계산. 성공할 때만 *out 에 쓴다 */
static rpn_status rpn_eval(const char *expr, int32_t *out) {
    (void)expr; (void)out; (void)parse_number; (void)apply_op;
    /* TODO: 공백으로 토큰을 자르고
     *       숫자 → push (가득 차면 STACK_FULL)
     *       연산자 → b = pop, a = pop (모자라면 UNDERFLOW) → apply_op → push
     *       끝: 토큰 0개면 EMPTY, 스택에 1개가 아니면 LEFTOVER, 아니면 *out = pop */
    return RPN_ERR_EMPTY;
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static const char *br_name[] = { "OK", "MISMATCH", "UNCLOSED", "EXTRA_CLOSE", "TOO_DEEP" };
static const char *rpn_name[] = { "OK", "EMPTY", "TOKEN", "UNDERFLOW", "STACK_FULL", "DIV0", "RANGE", "LEFTOVER" };

int main(void) {
    /* Part A: 스택 단위 테스트 */
    istack s;
    int32_t v = 0;
    st_init(&s);
    CHECK(st_empty(&s) && st_pop(&s, &v) == ST_UNDERFLOW && st_peek(&s, &v) == ST_UNDERFLOW);
    CHECK(st_push(&s, 1) == ST_OK && st_push(&s, 2) == ST_OK && st_size(&s) == 2);
    CHECK(st_peek(&s, &v) == ST_OK && v == 2 && st_size(&s) == 2);       /* peek 는 안 꺼낸다 */
    CHECK(st_pop(&s, &v) == ST_OK && v == 2 && st_pop(&s, &v) == ST_OK && v == 1);   /* LIFO */
    for (int32_t k = 0; k < STACK_CAP; k++) (void)st_push(&s, k * 10);
    CHECK(st_size(&s) == STACK_CAP && st_push(&s, 999) == ST_OVERFLOW);
    CHECK(st_peek(&s, &v) == ST_OK && v == 70);                           /* 실패한 push 는 흔적 없음 */
    while (!st_empty(&s)) (void)st_pop(&s, &v);
    CHECK(st_pop(&s, &v) == ST_UNDERFLOW && st_size(&s) == 0);
    CHECK(s.high_water == STACK_CAP);                                     /* 비워도 최고 수위는 남는다 */

    /* Part B: 괄호 검사 10 케이스 */
    static const struct { const char *s; br_result want; size_t pos; } br[] = {
        { "",                    BR_OK,          0 },
        { "()[]{}",              BR_OK,          0 },
        { "{[()()]}",            BR_OK,          0 },
        { "led set(1) [on]",     BR_OK,          0 },   /* 괄호 아닌 글자는 무시 */
        { "(]",                  BR_MISMATCH,    1 },
        { "([)]",                BR_MISMATCH,    2 },
        { "x = (a + b",          BR_UNCLOSED,    4 },
        { "(()",                 BR_UNCLOSED,    0 },
        { "())",                 BR_EXTRA_CLOSE, 2 },
        { "((((((((()))))))))",  BR_TOO_DEEP,    8 },   /* 9단 > STACK_CAP 8 */
    };
    for (size_t k = 0; k < sizeof br / sizeof br[0]; k++) {
        size_t pos = 999;
        br_result r = check_brackets(br[k].s, &pos);
        bool ok = (r == br[k].want) && (r == BR_OK || pos == br[k].pos);
        CHECK(ok);
        if (!ok) printf("   bracket case %zu \"%s\": got %s@%zu, want %s@%zu\n",
                        k, br[k].s, br_name[r], pos, br_name[br[k].want], br[k].pos);
    }

    /* Part C: RPN 계산기 10 케이스 */
    static const struct { const char *e; rpn_status want; int32_t val; } rc[] = {
        { "3 4 +",                     RPN_OK,             7 },
        { "5 1 2 + 4 * + 3 -",         RPN_OK,            14 },
        { "-7 2 /",                    RPN_OK,            -3 },   /* C 나눗셈은 0 쪽으로 자름 */
        { "   ",                       RPN_ERR_EMPTY,      0 },
        { "4 -",                       RPN_ERR_UNDERFLOW,  0 },
        { "1 2",                       RPN_ERR_LEFTOVER,   0 },
        { "2 x +",                     RPN_ERR_TOKEN,      0 },
        { "1 0 /",                     RPN_ERR_DIV0,       0 },
        { "1 2 3 4 5 6 7 8 9",         RPN_ERR_STACK_FULL, 0 },
        { "-2147483648 -1 /",          RPN_ERR_RANGE,      0 },   /* 유일하게 넘치는 나눗셈 */
    };
    for (size_t k = 0; k < sizeof rc / sizeof rc[0]; k++) {
        int32_t out = 12345;                                       /* 실패 시 그대로여야 함 */
        rpn_status r = rpn_eval(rc[k].e, &out);
        bool ok = (r == rc[k].want) && (r == RPN_OK ? out == rc[k].val : out == 12345);
        CHECK(ok);
        if (!ok) printf("   rpn case %zu \"%s\": got %s (%d), want %s (%d)\n",
                        k, rc[k].e, rpn_name[r], (int)out, rpn_name[rc[k].want], (int)rc[k].val);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
