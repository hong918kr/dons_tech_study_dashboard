/* DS-B02 추가 연습 — 한 번에 검증 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---- 본문 스택 (그대로) ---- */
#define STACK_CAP 8
typedef enum { ST_OK = 0, ST_OVERFLOW, ST_UNDERFLOW } st_status;
typedef struct { int32_t items[STACK_CAP]; size_t top; size_t high_water; } istack;
static void   st_init(istack *s)        { s->top = 0; s->high_water = 0; }
static size_t st_size(const istack *s)  { return s->top; }
static bool   st_empty(const istack *s) { return s->top == 0; }
static st_status st_push(istack *s, int32_t x) {
    if (s->top == STACK_CAP) return ST_OVERFLOW;
    s->items[s->top++] = x;
    if (s->top > s->high_water) s->high_water = s->top;
    return ST_OK;
}
static st_status st_pop(istack *s, int32_t *out) {
    if (s->top == 0) return ST_UNDERFLOW;
    *out = s->items[--s->top];
    return ST_OK;
}
static st_status st_peek(const istack *s, int32_t *out) {
    if (s->top == 0) return ST_UNDERFLOW;
    *out = s->items[s->top - 1];
    return ST_OK;
}

/* ================= 연습 1: min-stack — get_min 을 O(1) 로 ================= */
typedef struct { istack vals, mins; } minstack;   /* mins[k] = vals[0..k] 의 최솟값 */
static void ms_init(minstack *m) { st_init(&m->vals); st_init(&m->mins); }
static st_status ms_push(minstack *m, int32_t x) {
    int32_t cur;
    int32_t mn = (st_peek(&m->mins, &cur) == ST_OK && cur < x) ? cur : x;
    if (st_push(&m->vals, x) != ST_OK) return ST_OVERFLOW;
    (void)st_push(&m->mins, mn);                    /* 두 스택 크기는 항상 같다 */
    return ST_OK;
}
static st_status ms_pop(minstack *m, int32_t *out) {
    int32_t dummy;
    if (st_pop(&m->vals, out) != ST_OK) return ST_UNDERFLOW;
    (void)st_pop(&m->mins, &dummy);
    return ST_OK;
}
static st_status ms_min(const minstack *m, int32_t *out) { return st_peek(&m->mins, out); }

/* ================= 연습 2: 배열 하나에 스택 두 개 (양 끝에서 마주 보며 자람) ================= */
#define DUAL_CAP 8
typedef struct { int32_t buf[DUAL_CAP]; size_t lo; size_t hi; } dualstack;  /* 왼쪽 [0,lo), 오른쪽 [hi,CAP) */
static void ds_init(dualstack *d) { d->lo = 0; d->hi = DUAL_CAP; }
static st_status ds_push(dualstack *d, int side, int32_t x) {
    if (d->lo == d->hi) return ST_OVERFLOW;         /* 두 꼭대기가 만났다 = 공유 공간 소진 */
    if (side == 0) d->buf[d->lo++] = x; else d->buf[--d->hi] = x;
    return ST_OK;
}
static st_status ds_pop(dualstack *d, int side, int32_t *out) {
    if (side == 0) { if (d->lo == 0) return ST_UNDERFLOW; *out = d->buf[--d->lo]; }
    else           { if (d->hi == DUAL_CAP) return ST_UNDERFLOW; *out = d->buf[d->hi++]; }
    return ST_OK;
}

/* ================= 연습 3: 따옴표를 아는 명령줄 괄호 검사 ================= */
typedef enum { BR_OK = 0, BR_MISMATCH, BR_UNCLOSED, BR_EXTRA_CLOSE, BR_TOO_DEEP, BR_OPEN_QUOTE } br_result;
static char opener_of(char c) { return c == ')' ? '(' : c == ']' ? '[' : c == '}' ? '{' : 0; }
static br_result check_cmd(const char *str, size_t *err_pos) {
    istack st;
    st_init(&st);
    int32_t pos = 0;
    bool in_q = false;
    size_t q_pos = 0;
    for (size_t i = 0; str[i] != '\0'; i++) {
        char c = str[i];
        if (in_q) {                                    /* 따옴표 안: \" 이스케이프와 닫는 " 만 본다 */
            if (c == '\\' && str[i + 1] != '\0') i++;
            else if (c == '"') in_q = false;
            continue;
        }
        if (c == '"') { in_q = true; q_pos = i; }
        else if (c == '(' || c == '[' || c == '{') {
            if (st_push(&st, (int32_t)i) != ST_OK) { *err_pos = i; return BR_TOO_DEEP; }
        } else if (opener_of(c)) {
            if (st_pop(&st, &pos) != ST_OK) { *err_pos = i; return BR_EXTRA_CLOSE; }
            if (str[pos] != opener_of(c))   { *err_pos = i; return BR_MISMATCH; }
        }
    }
    if (in_q) { *err_pos = q_pos; return BR_OPEN_QUOTE; }
    if (st_peek(&st, &pos) == ST_OK) { *err_pos = (size_t)pos; return BR_UNCLOSED; }
    return BR_OK;
}

/* ================= 연습 4: shunting-yard (중위 → 후위) + 본문 RPN 계산기 ================= */
typedef enum {
    RPN_OK = 0, RPN_ERR_EMPTY, RPN_ERR_TOKEN, RPN_ERR_UNDERFLOW,
    RPN_ERR_STACK_FULL, RPN_ERR_DIV0, RPN_ERR_RANGE, RPN_ERR_LEFTOVER
} rpn_status;
static rpn_status parse_number(const char *tok, size_t len, int32_t *out) {
    size_t i = 0;
    bool neg = false;
    if (tok[0] == '-') { neg = true; i = 1; }
    if (i == len) return RPN_ERR_TOKEN;
    int64_t v = 0;
    for (; i < len; i++) {
        if (tok[i] < '0' || tok[i] > '9') return RPN_ERR_TOKEN;
        v = v * 10 + (tok[i] - '0');
        if (v > (int64_t)INT32_MAX + 1) return RPN_ERR_RANGE;
    }
    if (neg) v = -v;
    if (v > INT32_MAX || v < INT32_MIN) return RPN_ERR_RANGE;
    *out = (int32_t)v;
    return RPN_OK;
}
static rpn_status apply_op(char op, int32_t a, int32_t b, int32_t *res) {
    int64_t r;
    switch (op) {
    case '+': r = (int64_t)a + b; break;
    case '-': r = (int64_t)a - b; break;
    case '*': r = (int64_t)a * b; break;
    case '/': if (b == 0) return RPN_ERR_DIV0; r = (int64_t)a / b; break;
    default: return RPN_ERR_TOKEN;
    }
    if (r > INT32_MAX || r < INT32_MIN) return RPN_ERR_RANGE;
    *res = (int32_t)r;
    return RPN_OK;
}
static rpn_status rpn_eval(const char *expr, int32_t *out) {
    istack st;
    st_init(&st);
    size_t ntok = 0;
    const char *p = expr;
    for (;;) {
        while (*p == ' ') p++;
        if (*p == '\0') break;
        const char *tok = p;
        while (*p != ' ' && *p != '\0') p++;
        size_t len = (size_t)(p - tok);
        ntok++;
        if (len == 1 && (*tok == '+' || *tok == '-' || *tok == '*' || *tok == '/')) {
            int32_t a, b, r;
            if (st_pop(&st, &b) != ST_OK || st_pop(&st, &a) != ST_OK) return RPN_ERR_UNDERFLOW;
            rpn_status e = apply_op(*tok, a, b, &r);
            if (e != RPN_OK) return e;
            (void)st_push(&st, r);
        } else {
            int32_t v;
            rpn_status e = parse_number(tok, len, &v);
            if (e != RPN_OK) return e;
            if (st_push(&st, v) != ST_OK) return RPN_ERR_STACK_FULL;
        }
    }
    if (ntok == 0) return RPN_ERR_EMPTY;
    if (st_size(&st) != 1) return RPN_ERR_LEFTOVER;
    return st_pop(&st, out) == ST_OK ? RPN_OK : RPN_ERR_UNDERFLOW;
}

static int prec(int32_t op) { return (op == '*' || op == '/') ? 2 : (op == '+' || op == '-') ? 1 : 0; }

/* 출력 버퍼에 한 글자 덧붙이기 (넘치면 false) */
static bool out_put(char *buf, size_t cap, size_t *n, char c) {
    if (*n + 1 >= cap) return false;               /* '\0' 자리 남겨둔다 */
    buf[(*n)++] = c;
    buf[*n] = '\0';
    return true;
}
static bool out_tok(char *buf, size_t cap, size_t *n, char c) {   /* 토큰 앞에 공백 */
    return (*n == 0 || out_put(buf, cap, n, ' ')) && out_put(buf, cap, n, c);
}

/* 음이 아닌 정수, + - * /, 괄호만. 실패(괄호 짝, 모르는 글자, 버퍼/스택 부족)면 false */
static bool infix_to_rpn(const char *in, char *out, size_t cap) {
    istack ops;
    st_init(&ops);
    size_t n = 0;
    int32_t top;
    out[0] = '\0';
    for (size_t i = 0; in[i] != '\0'; i++) {
        char c = in[i];
        if (c == ' ') continue;
        if (c >= '0' && c <= '9') {
            if (!out_tok(out, cap, &n, c)) return false;
            while (in[i + 1] >= '0' && in[i + 1] <= '9')       /* 여러 자리 숫자는 붙여 쓴다 */
                if (!out_put(out, cap, &n, in[++i])) return false;
        } else if (prec(c)) {
            /* 왼쪽 결합: 꼭대기 연산자의 우선순위가 같거나 높으면 먼저 내보낸다 */
            while (st_peek(&ops, &top) == ST_OK && prec(top) >= prec(c)) {
                (void)st_pop(&ops, &top);
                if (!out_tok(out, cap, &n, (char)top)) return false;
            }
            if (st_push(&ops, c) != ST_OK) return false;
        } else if (c == '(') {
            if (st_push(&ops, c) != ST_OK) return false;
        } else if (c == ')') {
            for (;;) {
                if (st_pop(&ops, &top) != ST_OK) return false;  /* '(' 없이 바닥 → 짝 안 맞음 */
                if (top == '(') break;
                if (!out_tok(out, cap, &n, (char)top)) return false;
            }
        } else {
            return false;
        }
    }
    while (st_pop(&ops, &top) == ST_OK) {
        if (top == '(') return false;                            /* 닫히지 않은 '(' */
        if (!out_tok(out, cap, &n, (char)top)) return false;
    }
    return true;
}

/* ---------------- 테스트 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    int32_t v = 0;

    /* 연습 1 */
    minstack m;
    ms_init(&m);
    const int32_t in[] = { 5, 3, 7, 3, 1 };
    for (size_t k = 0; k < 5; k++) (void)ms_push(&m, in[k]);
    CHECK(ms_min(&m, &v) == ST_OK && v == 1);
    (void)ms_pop(&m, &v);  CHECK(ms_min(&m, &v) == ST_OK && v == 3);
    (void)ms_pop(&m, &v);  CHECK(ms_min(&m, &v) == ST_OK && v == 3);    /* 중복 최솟값 3 이 아직 남음 */
    (void)ms_pop(&m, &v);  (void)ms_pop(&m, &v);
    CHECK(ms_min(&m, &v) == ST_OK && v == 5);
    (void)ms_pop(&m, &v);  CHECK(ms_min(&m, &v) == ST_UNDERFLOW && st_empty(&m.vals));

    /* 연습 2 */
    dualstack d;
    ds_init(&d);
    for (int32_t k = 0; k < 5; k++) (void)ds_push(&d, 0, k);          /* 왼쪽 5개 */
    for (int32_t k = 100; k < 103; k++) (void)ds_push(&d, 1, k);      /* 오른쪽 3개 → 꽉 참 */
    CHECK(ds_push(&d, 0, 9) == ST_OVERFLOW && ds_push(&d, 1, 9) == ST_OVERFLOW);
    CHECK(ds_pop(&d, 1, &v) == ST_OK && v == 102 && ds_pop(&d, 0, &v) == ST_OK && v == 4);
    CHECK(ds_push(&d, 1, 200) == ST_OK && ds_push(&d, 1, 201) == ST_OK && ds_push(&d, 0, 1) == ST_OVERFLOW);
    for (int k = 0; k < 4; k++) (void)ds_pop(&d, 1, &v);
    CHECK(ds_pop(&d, 1, &v) == ST_UNDERFLOW && d.lo == 4);

    /* 연습 3 */
    size_t pos = 999;
    CHECK(check_cmd("echo \"(\" (ok)", &pos) == BR_OK);
    CHECK(check_cmd("set name \"a)b\" (x", &pos) == BR_UNCLOSED && pos == 15);
    CHECK(check_cmd("say \"hi", &pos) == BR_OPEN_QUOTE && pos == 4);
    CHECK(check_cmd("p \"\\\"(\" ]", &pos) == BR_EXTRA_CLOSE && pos == 8);   /* p "\"(" ] */

    /* 연습 4 */
    char buf[64];
    CHECK(infix_to_rpn("3 + 4 * 2", buf, sizeof buf) && strcmp(buf, "3 4 2 * +") == 0
          && rpn_eval(buf, &v) == RPN_OK && v == 11);
    CHECK(infix_to_rpn("(1 + 2) * (3 + 4)", buf, sizeof buf) && strcmp(buf, "1 2 + 3 4 + *") == 0
          && rpn_eval(buf, &v) == RPN_OK && v == 21);
    CHECK(infix_to_rpn("10 - 4 - 3", buf, sizeof buf) && strcmp(buf, "10 4 - 3 -") == 0
          && rpn_eval(buf, &v) == RPN_OK && v == 3);                       /* 왼쪽 결합 */
    CHECK(infix_to_rpn("100 / (4 - 2) / 5", buf, sizeof buf) && strcmp(buf, "100 4 2 - / 5 /") == 0
          && rpn_eval(buf, &v) == RPN_OK && v == 10);
    CHECK(!infix_to_rpn("(1 + 2", buf, sizeof buf) && !infix_to_rpn("1 + 2)", buf, sizeof buf));
    CHECK(!infix_to_rpn("1 + 2 * 3", buf, 6));                             /* 출력 버퍼 부족 */

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
