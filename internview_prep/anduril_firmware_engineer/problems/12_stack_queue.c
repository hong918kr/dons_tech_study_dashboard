// 12_stack_queue.c  —  PRACTICE STUB (여기 빈칸을 채우세요)
// 스택 & 큐 (Stacks & Queues)  —  단조 스택 / 단조 덱 / 표현식 평가
// ---------------------------------------------------------------------------
// 빌드: cc -std=c11 -Wall -Wextra 12_stack_queue.c -o /tmp/andb_12 && /tmp/andb_12
// 각 함수의 // TODO 를 구현하고 다시 빌드/실행해 FAIL -> PASS 로 바꾸세요.
// 지금 상태로도 컴파일/실행은 되며(placeholder 반환), 대부분 [FAIL] 로 나옵니다.
//
// 제공된 것(스캐폴딩): 배열 기반 int 스택(istk_*), 문자열 빌더(sb_*),
//                      MinStack/Queue2 의 구조체·init.  이 도구들을 사용하세요.
// 임베디드 힌트:
//   - 단조 스택: "다음 큰 값 / 온도 대기일" 을 전체 O(n) 에.
//   - 단조 덱: 슬라이딩 윈도우 최댓값을 전체 O(n) 에.
//   - RPN·중첩 디코드: 파서/평가기의 기본 뼈대 = 스택.
// ---------------------------------------------------------------------------
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <ctype.h>

// ===========================================================================
// 테스트 하네스 (PASS/FAIL)
// ===========================================================================
static int g_pass = 0;
static int g_fail = 0;
#define T(label, cond) do {                                   \
    if (cond) { printf("  [PASS] %s\n", (label)); g_pass++; } \
    else      { printf("  [FAIL] %s\n", (label)); g_fail++; } \
} while (0)

static bool arr_eq(const int *a, const int *b, int n) {
    for (int i = 0; i < n; ++i) if (a[i] != b[i]) return false;
    return true;
}

// ===========================================================================
// 공용 배열 기반 int 스택 (array-backed stack) — 제공됨. 그대로 사용하세요.
//   top == 현재 원소 수 == 다음 삽입 위치.
// ===========================================================================
typedef struct {
    int *a;
    int  top;
    int  cap;
} istk_t;

static void istk_init(istk_t *s, int cap)   { s->cap = cap > 0 ? cap : 1;
                                               s->a = malloc(sizeof(int) * s->cap);
                                               s->top = 0; }
static void istk_free(istk_t *s)            { free(s->a); s->a = NULL; s->top = 0; s->cap = 0; }
static bool istk_empty(const istk_t *s)     { return s->top == 0; }
static void istk_push(istk_t *s, int v)     { s->a[s->top++] = v; }
static int  istk_pop(istk_t *s)             { return s->a[--s->top]; }
static int  istk_peek(const istk_t *s)      { return s->a[s->top - 1]; }

// (아래 함수들을 위해 unused 경고를 피하려고 참조만 해 둠 — 구현하면 지워도 됨)
static inline void istk_touch(void) {
    (void)istk_init; (void)istk_free; (void)istk_empty;
    (void)istk_push; (void)istk_pop; (void)istk_peek;
}

// ===========================================================================
// P1. valid_parentheses  —  올바른 괄호 문자열인가? (배열 스택)
//   예: "()[]{}" -> true,  "(]" -> false,  "([)]" -> false,  "{[]}" -> true
// ---------------------------------------------------------------------------
bool valid_parentheses(const char *s) {
    (void)s;
    // TODO: 여는 괄호는 push, 닫는 괄호는 top 과 짝이 맞으면 pop.
    //       마지막에 스택이 비어 있어야 유효.
    return false;   // placeholder
}

// ===========================================================================
// P2. MinStack  —  push/pop/top/getMin 이 모두 O(1)
//   힌트: 보조 스택 mn[] 에 "그 시점까지의 최솟값"을 함께 쌓는다.
//   예: push -2,0,-3 -> getMin=-3; pop -> getMin=-2; top=0
// ---------------------------------------------------------------------------
#define MINSTACK_CAP 128
typedef struct {
    int val[MINSTACK_CAP];
    int mn[MINSTACK_CAP];
    int top;
} MinStack;

static void ms_init(MinStack *m)         { m->top = 0; }          // 제공됨
static void ms_push(MinStack *m, int x)  { (void)m; (void)x;
    // TODO: val 에 x 를 넣고, mn 에는 min(x, 직전 최솟값) 을 넣는다.
}
static void ms_pop(MinStack *m)          { (void)m;
    // TODO: top 하나 줄이기 (비어 있으면 무시)
}
static int  ms_top(const MinStack *m)    { (void)m;
    // TODO: 맨 위 값
    return 0;   // placeholder
}
static int  ms_getmin(const MinStack *m) { (void)m;
    // TODO: 현재 최솟값 (mn[top-1])
    return 0;   // placeholder
}

// ===========================================================================
// P3. next_greater_element  —  각 원소 오른쪽의 첫 '더 큰' 값 (단조 스택)
//   없으면 -1. 예: [2 1 2 4 3] -> [4 2 4 -1 -1]
// ---------------------------------------------------------------------------
void next_greater_element(const int *a, int n, int *out) {
    (void)a; (void)n; (void)out;
    // TODO: 인덱스 단조 감소 스택. 현재값이 top 값보다 크면 그 원소의 답 = 현재값.
    //       먼저 out 을 전부 -1 로 초기화.
}

// ===========================================================================
// P4. daily_temperatures  —  더 따뜻한 날까지 며칠? (단조 스택)
//   없으면 0. 예: [73 74 75 71 69 72 76 73] -> [1 1 4 2 1 1 0 0]
// ---------------------------------------------------------------------------
void daily_temperatures(const int *t, int n, int *out) {
    (void)t; (void)n; (void)out;
    // TODO: 인덱스 단조 감소 스택. out[j] = i - j (해소되는 날 차이).
}

// ===========================================================================
// P5. eval_rpn  —  후위(역폴란드) 표기식 평가 (값 스택)
//   주의: 뺄셈/나눗셈은 a (op) b, b 가 먼저 pop.  "-11" 같은 음수는 숫자.
//   예: {"2","1","+","3","*"} -> 9
// ---------------------------------------------------------------------------
int eval_rpn(const char **tok, int n) {
    (void)tok; (void)n;
    // TODO: 피연산자 push, 연산자는 두 개 pop 후 계산해 push.
    return 0;   // placeholder
}

// ===========================================================================
// P6. Queue via two stacks  —  스택 두 개로 FIFO 큐 (분할상환 O(1))
//   enqueue -> in.  dequeue/peek -> out 이 비면 in 을 뒤집어 옮긴 뒤 out 에서.
// ---------------------------------------------------------------------------
typedef struct {
    istk_t in;
    istk_t out;
} Queue2;

static void q2_init(Queue2 *q, int cap) { istk_init(&q->in, cap); istk_init(&q->out, cap); }  // 제공됨
static void q2_free(Queue2 *q)          { istk_free(&q->in); istk_free(&q->out); }            // 제공됨
static void q2_enqueue(Queue2 *q, int x){ (void)q; (void)x;
    // TODO: in 에 push
}
static int  q2_dequeue(Queue2 *q)       { (void)q;
    // TODO: out 이 비면 in 을 통째로 옮긴 뒤 out 에서 pop
    return 0;   // placeholder
}
static int  q2_peek(Queue2 *q)          { (void)q;
    // TODO: dequeue 와 같되 pop 대신 peek
    return 0;   // placeholder
}
static bool q2_empty(const Queue2 *q)   { (void)q;
    // TODO: 두 스택 모두 비면 true
    return true;   // placeholder
}

// ===========================================================================
// P7. sliding_window_max  —  크기 k 창의 최댓값들 (단조 감소 덱)
//   out 길이 = n-k+1. 예: [1 3 -1 -3 5 3 6 7], k=3 -> [3 3 5 5 6 7]
// ---------------------------------------------------------------------------
void sliding_window_max(const int *a, int n, int k, int *out) {
    (void)a; (void)n; (void)k; (void)out;
    // TODO: 인덱스 덱을 값 기준 단조 감소로 유지. head 가 항상 창의 최댓값.
    //       (1) 창 벗어난 앞쪽 제거  (2) 작은 뒤쪽 제거  (3) 인덱스 push
    //       (4) i>=k-1 이면 out 에 a[dq[head]] 기록.
}

// ===========================================================================
// P8. decode_string  —  중첩 인코딩 해제 "3[a2[c]]" -> "accaccacc" (두 스택)
//   반환 문자열은 malloc — 호출자가 free.  예: "3[a]2[bc]" -> "aaabcbc"
// ---------------------------------------------------------------------------
typedef struct { char *buf; int len; int cap; } sb_t;   // 동적 문자열 빌더 — 제공됨
static void sb_init(sb_t *b)             { b->cap = 16; b->len = 0;
                                           b->buf = malloc((size_t)b->cap); b->buf[0] = '\0'; }
static void sb_putc(sb_t *b, char c)     { if (b->len + 1 >= b->cap) {
                                               b->cap *= 2; b->buf = realloc(b->buf, (size_t)b->cap); }
                                           b->buf[b->len++] = c; b->buf[b->len] = '\0'; }
static void sb_puts(sb_t *b, const char *s) { while (*s) sb_putc(b, *s++); }
static inline void sb_touch(void) { (void)sb_init; (void)sb_putc; (void)sb_puts; }

char *decode_string(const char *s) {
    (void)s;
    // TODO: 숫자 스택 + 문자열(빌더) 스택.
    //       '[' 에서 (반복횟수, 바깥문자열) 저장하고 안쪽 새로 시작.
    //       ']' 에서 k 회 반복해 바깥에 이어붙임.
    sb_t out; sb_init(&out);     // placeholder: 빈 문자열 반환 (호출자가 free)
    return out.buf;
}

// ===========================================================================
// main : 모든 케이스 PASS/FAIL
// ===========================================================================
int main(void) {
    istk_touch(); sb_touch();   // (스캐폴딩 unused 경고 방지 — 구현 후 지워도 됨)

    // -------- P1. valid_parentheses --------
    printf("== P1. valid_parentheses ==\n");
    T("\"()\" -> true",        valid_parentheses("()"));
    T("\"()[]{}\" -> true",    valid_parentheses("()[]{}"));
    T("\"(]\" -> false",       !valid_parentheses("(]"));
    T("\"([)]\" -> false",     !valid_parentheses("([)]"));
    T("\"{[]}\" -> true",      valid_parentheses("{[]}"));
    T("\"\" -> true (empty)",  valid_parentheses(""));
    T("\"(\" -> false",        !valid_parentheses("("));
    T("\"]\" -> false",        !valid_parentheses("]"));

    // -------- P2. MinStack --------
    printf("== P2. MinStack (O(1) getMin) ==\n");
    MinStack ms; ms_init(&ms);
    ms_push(&ms, -2); ms_push(&ms, 0); ms_push(&ms, -3);
    T("getMin == -3",          ms_getmin(&ms) == -3);
    ms_pop(&ms);
    T("after pop, top == 0",   ms_top(&ms) == 0);
    T("after pop, getMin -2",  ms_getmin(&ms) == -2);
    ms_push(&ms, -5);
    T("push -5, getMin -5",    ms_getmin(&ms) == -5);
    ms_pop(&ms);
    T("pop, getMin back -2",   ms_getmin(&ms) == -2);

    // -------- P3. next_greater_element --------
    printf("== P3. next_greater_element (monotonic stack) ==\n");
    {
        int a[]  = {2, 1, 2, 4, 3};
        int exp[] = {4, 2, 4, -1, -1};
        int out[5] = {0};
        next_greater_element(a, 5, out);
        T("[2 1 2 4 3] -> [4 2 4 -1 -1]", arr_eq(out, exp, 5));
    }
    {
        int a[]  = {5, 4, 3, 2, 1};
        int exp[] = {-1, -1, -1, -1, -1};
        int out[5] = {0};
        next_greater_element(a, 5, out);
        T("descending -> all -1",         arr_eq(out, exp, 5));
    }
    {
        int a[]  = {1, 2, 3};
        int exp[] = {2, 3, -1};
        int out[3] = {0};
        next_greater_element(a, 3, out);
        T("ascending [1 2 3] -> [2 3 -1]", arr_eq(out, exp, 3));
    }

    // -------- P4. daily_temperatures --------
    printf("== P4. daily_temperatures (monotonic stack) ==\n");
    {
        int t[]  = {73, 74, 75, 71, 69, 72, 76, 73};
        int exp[] = {1, 1, 4, 2, 1, 1, 0, 0};
        int out[8] = {0};
        daily_temperatures(t, 8, out);
        T("classic -> [1 1 4 2 1 1 0 0]", arr_eq(out, exp, 8));
    }
    {
        int t[]  = {30, 40, 50, 60};
        int exp[] = {1, 1, 1, 0};
        int out[4] = {0};
        daily_temperatures(t, 4, out);
        T("increasing -> [1 1 1 0]",      arr_eq(out, exp, 4));
    }
    {
        int t[]  = {60, 50, 40};
        int exp[] = {0, 0, 0};
        int out[3] = {0};
        daily_temperatures(t, 3, out);
        T("decreasing -> [0 0 0]",        arr_eq(out, exp, 3));
    }

    // -------- P5. eval_rpn --------
    printf("== P5. eval_rpn (Reverse Polish) ==\n");
    {
        const char *tk[] = {"2", "1", "+", "3", "*"};
        T("2 1 + 3 * -> 9",        eval_rpn(tk, 5) == 9);
    }
    {
        const char *tk[] = {"4", "13", "5", "/", "+"};
        T("4 13 5 / + -> 6",       eval_rpn(tk, 5) == 6);
    }
    {
        const char *tk[] = {"10", "6", "9", "3", "+", "-11", "*", "/", "*", "17", "+", "5", "+"};
        T("complex w/ -11 -> 22",  eval_rpn(tk, 13) == 22);
    }
    {
        const char *tk[] = {"5", "1", "2", "+", "4", "*", "+", "3", "-"};
        T("nested -> 14",          eval_rpn(tk, 9) == 14);
    }

    // -------- P6. Queue via two stacks --------
    printf("== P6. queue_via_two_stacks ==\n");
    {
        Queue2 q; q2_init(&q, 16);
        T("empty at start",        q2_empty(&q));
        q2_enqueue(&q, 1);
        q2_enqueue(&q, 2);
        T("peek == 1",             q2_peek(&q) == 1);
        T("dequeue == 1",          q2_dequeue(&q) == 1);
        q2_enqueue(&q, 3);
        T("dequeue == 2 (FIFO)",   q2_dequeue(&q) == 2);
        T("dequeue == 3",          q2_dequeue(&q) == 3);
        T("empty at end",          q2_empty(&q));
        q2_free(&q);
    }

    // -------- P7. sliding_window_max --------
    printf("== P7. sliding_window_max (monotonic deque) ==\n");
    {
        int a[]  = {1, 3, -1, -3, 5, 3, 6, 7};
        int exp[] = {3, 3, 5, 5, 6, 7};
        int out[6] = {0};
        sliding_window_max(a, 8, 3, out);
        T("k=3 -> [3 3 5 5 6 7]",  arr_eq(out, exp, 6));
    }
    {
        int a[]  = {9, 8, 7, 6};
        int exp[] = {9, 8, 7, 6};
        int out[4] = {0};
        sliding_window_max(a, 4, 1, out);
        T("k=1 -> identity",       arr_eq(out, exp, 4));
    }
    {
        int a[]  = {1, 2, 3, 4};
        int exp[] = {4};
        int out[1] = {0};
        sliding_window_max(a, 4, 4, out);
        T("k=n -> single max [4]",  arr_eq(out, exp, 1));
    }

    // -------- P8. decode_string --------
    printf("== P8. decode_string (nested) ==\n");
    {
        char *r = decode_string("3[a]2[bc]");
        T("\"3[a]2[bc]\" -> aaabcbc", strcmp(r, "aaabcbc") == 0);
        free(r);
    }
    {
        char *r = decode_string("3[a2[c]]");
        T("\"3[a2[c]]\" -> accaccacc", strcmp(r, "accaccacc") == 0);
        free(r);
    }
    {
        char *r = decode_string("2[abc]3[cd]ef");
        T("\"2[abc]3[cd]ef\" -> abcabccdcdcdef", strcmp(r, "abcabccdcdcdef") == 0);
        free(r);
    }
    {
        char *r = decode_string("10[a]");
        T("\"10[a]\" -> 10x a",     strcmp(r, "aaaaaaaaaa") == 0);
        free(r);
    }
    {
        char *r = decode_string("abc");
        T("\"abc\" (no code) -> abc", strcmp(r, "abc") == 0);
        free(r);
    }

    printf("\n==== %d passed, %d failed ====\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
