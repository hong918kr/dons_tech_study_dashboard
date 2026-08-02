// 12_stack_queue.c  —  REFERENCE SOLUTION
// 스택 & 큐 (Stacks & Queues)  —  단조 스택 / 단조 덱 / 표현식 평가
// ---------------------------------------------------------------------------
// 빌드: cc -std=c11 -Wall -Wextra 12_stack_queue.c -o /tmp/andb_12 && /tmp/andb_12
// 임베디드 관점 핵심:
//   - 배열 기반 스택(array-backed stack): 힙 단편화 없이 O(1) push/pop, 결정론적.
//   - 단조 스택(monotonic stack): "다음 큰 값 / 온도 대기일" 을 전체 O(n) 에.
//   - 단조 덱(monotonic deque): 슬라이딩 윈도우 최댓값을 전체 O(n) 에.
//   - 표현식 평가(RPN)·중첩 디코드: 파서/평가기의 기본 뼈대 = 스택.
//   - STL 없음. 모든 스택/덱을 배열로 직접 구현한다.
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
// 공용 배열 기반 int 스택 (array-backed stack)
//   여러 문제(단조 스택, RPN, 두 스택 큐, 디코드)에서 재사용한다.
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

// ===========================================================================
// P1. valid_parentheses  —  올바른 괄호 문자열인가? (배열 스택)
//   여는 괄호는 push, 닫는 괄호는 스택 top 과 짝이 맞아야 pop.
//   예: "()[]{}" -> true,  "(]" -> false,  "([)]" -> false,  "{[]}" -> true
// ---------------------------------------------------------------------------
bool valid_parentheses(const char *s) {
    int n = (int)strlen(s);
    char *st = malloc((size_t)(n > 0 ? n : 1));
    int top = 0;
    for (int i = 0; i < n; ++i) {
        char c = s[i];
        if (c == '(' || c == '[' || c == '{') {
            st[top++] = c;
        } else if (c == ')' || c == ']' || c == '}') {
            if (top == 0) { free(st); return false; }   // 닫는데 열린 게 없음
            char o = st[--top];
            if ((c == ')' && o != '(') ||
                (c == ']' && o != '[') ||
                (c == '}' && o != '{')) { free(st); return false; }
        }
        // 그 외 문자는 무시(괄호만 검사)
    }
    bool ok = (top == 0);   // 남은 여는 괄호 없어야 유효
    free(st);
    return ok;
}

// ===========================================================================
// P2. MinStack  —  push/pop/top/getMin 이 모두 O(1)
//   보조 스택(min)에 "그 시점까지의 최솟값"을 함께 쌓으면 getMin 이 O(1).
//   예: push -2,0,-3 -> getMin=-3; pop -> getMin=-2; top=0
// ---------------------------------------------------------------------------
#define MINSTACK_CAP 128
typedef struct {
    int val[MINSTACK_CAP];   // 값 스택
    int mn[MINSTACK_CAP];    // 대응 접두 최솟값 스택
    int top;
} MinStack;

static void ms_init(MinStack *m)         { m->top = 0; }
static void ms_push(MinStack *m, int x)  {
    m->val[m->top] = x;
    m->mn[m->top]  = (m->top == 0) ? x : (x < m->mn[m->top - 1] ? x : m->mn[m->top - 1]);
    m->top++;
}
static void ms_pop(MinStack *m)          { if (m->top > 0) m->top--; }
static int  ms_top(const MinStack *m)    { return m->val[m->top - 1]; }
static int  ms_getmin(const MinStack *m) { return m->mn[m->top - 1]; }

// ===========================================================================
// P3. next_greater_element  —  각 원소 오른쪽의 첫 '더 큰' 값 (단조 스택)
//   인덱스 단조 감소 스택: 현재 값이 스택 top 값보다 크면 그 원소들의 답이 곧 현재값.
//   없으면 -1. 예: [2 1 2 4 3] -> [4 2 4 -1 -1]
// ---------------------------------------------------------------------------
void next_greater_element(const int *a, int n, int *out) {
    for (int i = 0; i < n; ++i) out[i] = -1;
    istk_t st; istk_init(&st, n);            // 인덱스 저장
    for (int i = 0; i < n; ++i) {
        while (!istk_empty(&st) && a[istk_peek(&st)] < a[i])
            out[istk_pop(&st)] = a[i];       // 현재값이 대기 중 원소들의 next greater
        istk_push(&st, i);
    }
    istk_free(&st);
}

// ===========================================================================
// P4. daily_temperatures  —  더 따뜻한 날까지 며칠? (단조 스택)
//   인덱스 단조 감소 스택. out[i] = 앞으로 더 높은 온도가 나올 때까지의 날 수(없으면 0).
//   예: [73 74 75 71 69 72 76 73] -> [1 1 4 2 1 1 0 0]
// ---------------------------------------------------------------------------
void daily_temperatures(const int *t, int n, int *out) {
    for (int i = 0; i < n; ++i) out[i] = 0;
    istk_t st; istk_init(&st, n);
    for (int i = 0; i < n; ++i) {
        while (!istk_empty(&st) && t[istk_peek(&st)] < t[i]) {
            int j = istk_pop(&st);
            out[j] = i - j;                  // i 일에 j 일의 대기가 해소
        }
        istk_push(&st, i);
    }
    istk_free(&st);
}

// ===========================================================================
// P5. eval_rpn  —  후위(역폴란드) 표기식 평가 (값 스택)
//   피연산자는 push, 연산자는 두 개 pop 해서 계산 후 push. 마지막에 하나 남음.
//   주의: 뺄셈/나눗셈 순서 = a (op) b, 여기서 b 가 먼저 pop.
//   예: {"2","1","+","3","*"} -> 9,  {"4","13","5","/","+"} -> 6
// ---------------------------------------------------------------------------
int eval_rpn(const char **tok, int n) {
    istk_t st; istk_init(&st, n > 0 ? n : 1);
    for (int i = 0; i < n; ++i) {
        const char *t = tok[i];
        // 한 글자이고 연산자인 경우만 연산자로 취급 ("-11" 같은 음수는 숫자)
        if (t[1] == '\0' && (t[0]=='+'||t[0]=='-'||t[0]=='*'||t[0]=='/')) {
            int b = istk_pop(&st);
            int a = istk_pop(&st);
            int r = 0;
            switch (t[0]) {
                case '+': r = a + b; break;
                case '-': r = a - b; break;
                case '*': r = a * b; break;
                case '/': r = a / b; break;
            }
            istk_push(&st, r);
        } else {
            istk_push(&st, atoi(t));
        }
    }
    int res = istk_empty(&st) ? 0 : istk_pop(&st);
    istk_free(&st);
    return res;
}

// ===========================================================================
// P6. Queue via two stacks  —  스택 두 개로 FIFO 큐 (분할상환 O(1))
//   enqueue -> in 스택. dequeue/peek -> out 이 비면 in 을 통째로 뒤집어 옮김.
//   각 원소는 최대 한 번만 옮겨지므로 분할상환(amortized) O(1).
// ---------------------------------------------------------------------------
typedef struct {
    istk_t in;
    istk_t out;
} Queue2;

static void q2_init(Queue2 *q, int cap) { istk_init(&q->in, cap); istk_init(&q->out, cap); }
static void q2_free(Queue2 *q)          { istk_free(&q->in); istk_free(&q->out); }
static void q2_enqueue(Queue2 *q, int x){ istk_push(&q->in, x); }
static void q2_shift(Queue2 *q)         {                    // out 이 비었을 때만 뒤집기
    if (istk_empty(&q->out))
        while (!istk_empty(&q->in)) istk_push(&q->out, istk_pop(&q->in));
}
static int  q2_dequeue(Queue2 *q)       { q2_shift(q); return istk_pop(&q->out); }
static int  q2_peek(Queue2 *q)          { q2_shift(q); return istk_peek(&q->out); }
static bool q2_empty(const Queue2 *q)   { return istk_empty(&q->in) && istk_empty(&q->out); }

// ===========================================================================
// P7. sliding_window_max  —  크기 k 창의 최댓값들 (단조 감소 덱)
//   인덱스 덱을 값 기준 단조 감소로 유지. 앞(head)이 항상 창의 최댓값.
//   out 길이 = n-k+1. 예: [1 3 -1 -3 5 3 6 7], k=3 -> [3 3 5 5 6 7]
// ---------------------------------------------------------------------------
void sliding_window_max(const int *a, int n, int k, int *out) {
    if (n == 0 || k <= 0) return;
    int *dq = malloc(sizeof(int) * (size_t)n);  // 인덱스 저장 덱: [head, tail)
    int head = 0, tail = 0, oi = 0;
    for (int i = 0; i < n; ++i) {
        if (head < tail && dq[head] <= i - k) head++;             // 창 벗어난 앞쪽 제거
        while (head < tail && a[dq[tail - 1]] <= a[i]) tail--;    // 작은 뒤쪽 제거
        dq[tail++] = i;
        if (i >= k - 1) out[oi++] = a[dq[head]];                  // 창이 다 차면 기록
    }
    free(dq);
}

// ===========================================================================
// P8. decode_string  —  중첩 인코딩 해제 "3[a2[c]]" -> "accaccacc" (두 스택)
//   숫자 스택 + 문자열(빌더) 스택. '[' 에서 상태 저장, ']' 에서 k회 반복해 합침.
//   반환 문자열은 malloc — 호출자가 free.
//   예: "3[a]2[bc]" -> "aaabcbc"
// ---------------------------------------------------------------------------
typedef struct { char *buf; int len; int cap; } sb_t;   // 동적 문자열 빌더
static void sb_init(sb_t *b)             { b->cap = 16; b->len = 0;
                                           b->buf = malloc((size_t)b->cap); b->buf[0] = '\0'; }
static void sb_putc(sb_t *b, char c)     { if (b->len + 1 >= b->cap) {
                                               b->cap *= 2; b->buf = realloc(b->buf, (size_t)b->cap); }
                                           b->buf[b->len++] = c; b->buf[b->len] = '\0'; }
static void sb_puts(sb_t *b, const char *s) { while (*s) sb_putc(b, *s++); }

char *decode_string(const char *s) {
    int n = (int)strlen(s);
    istk_t nums; istk_init(&nums, n + 1);         // 반복 횟수 스택
    sb_t  *strs = malloc(sizeof(sb_t) * (size_t)(n + 1));  // 이전 문자열 스택
    int sp = 0;
    sb_t cur; sb_init(&cur);
    int num = 0;
    for (const char *p = s; *p; ++p) {
        char c = *p;
        if (isdigit((unsigned char)c)) {
            num = num * 10 + (c - '0');           // 여러 자리 지원
        } else if (c == '[') {
            istk_push(&nums, num); num = 0;        // 반복 횟수 보관
            strs[sp++] = cur; sb_init(&cur);       // 바깥 문자열 보관, 안쪽 새로 시작
        } else if (c == ']') {
            int k = istk_pop(&nums);
            sb_t prev = strs[--sp];
            for (int i = 0; i < k; ++i) sb_puts(&prev, cur.buf);  // k회 이어붙임
            free(cur.buf);
            cur = prev;
        } else {
            sb_putc(&cur, c);
        }
    }
    istk_free(&nums);
    free(strs);
    return cur.buf;   // 호출자가 free
}

// ===========================================================================
// main : 모든 케이스 PASS/FAIL
// ===========================================================================
int main(void) {
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
        int out[5];
        next_greater_element(a, 5, out);
        T("[2 1 2 4 3] -> [4 2 4 -1 -1]", arr_eq(out, exp, 5));
    }
    {
        int a[]  = {5, 4, 3, 2, 1};
        int exp[] = {-1, -1, -1, -1, -1};
        int out[5];
        next_greater_element(a, 5, out);
        T("descending -> all -1",         arr_eq(out, exp, 5));
    }
    {
        int a[]  = {1, 2, 3};
        int exp[] = {2, 3, -1};
        int out[3];
        next_greater_element(a, 3, out);
        T("ascending [1 2 3] -> [2 3 -1]", arr_eq(out, exp, 3));
    }

    // -------- P4. daily_temperatures --------
    printf("== P4. daily_temperatures (monotonic stack) ==\n");
    {
        int t[]  = {73, 74, 75, 71, 69, 72, 76, 73};
        int exp[] = {1, 1, 4, 2, 1, 1, 0, 0};
        int out[8];
        daily_temperatures(t, 8, out);
        T("classic -> [1 1 4 2 1 1 0 0]", arr_eq(out, exp, 8));
    }
    {
        int t[]  = {30, 40, 50, 60};
        int exp[] = {1, 1, 1, 0};
        int out[4];
        daily_temperatures(t, 4, out);
        T("increasing -> [1 1 1 0]",      arr_eq(out, exp, 4));
    }
    {
        int t[]  = {60, 50, 40};
        int exp[] = {0, 0, 0};
        int out[3];
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
        q2_enqueue(&q, 3);         // in={3}, out={2}
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
        int out[6];
        sliding_window_max(a, 8, 3, out);
        T("k=3 -> [3 3 5 5 6 7]",  arr_eq(out, exp, 6));
    }
    {
        int a[]  = {9, 8, 7, 6};
        int exp[] = {9, 8, 7, 6};
        int out[4];
        sliding_window_max(a, 4, 1, out);
        T("k=1 -> identity",       arr_eq(out, exp, 4));
    }
    {
        int a[]  = {1, 2, 3, 4};
        int exp[] = {4};
        int out[1];
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
