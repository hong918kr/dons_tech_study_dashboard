/* ARM-B08 추가 연습 — 한 번에 검증
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c practice.S shared_regcheck.S -o p && ./p
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

uint64_t fact_tail(uint64_t n, uint64_t acc);                           /* 연습 1 */
void fact_tail_end(void);
uint64_t vla_sum(size_t n, void (*cb)(uint64_t *a, size_t n));          /* 연습 2 */
struct node { int64_t val; const struct node *left, *right; };
int64_t  tree_sum(const struct node *t);                                /* 연습 3 */
uint64_t fact_guarded(uint64_t n, uintptr_t limit);                     /* 연습 4 */
uintptr_t get_sp(void);
uint64_t call_preserve_check(uintptr_t fn, uint64_t a, uint64_t b, uint64_t *ret);

/* 훅: 앞에서부터 연속으로 [g_lo, g_hi) 안에 있는 저장된 LR 개수 = asm 프레임 깊이 */
static uintptr_t g_lo, g_hi;
static int g_depth, g_calls;
void base_hook(void);
__attribute__((noinline)) void base_hook(void) {
    g_calls++;
    const uintptr_t *fp = (const uintptr_t *)__builtin_frame_address(0);
    int d = 0;
    while (fp && fp[1] >= g_lo && fp[1] < g_hi && d < 100000) {
        d++;
        fp = (const uintptr_t *)fp[0];
    }
    g_depth = d;
}

static uint64_t fact_ref(uint64_t n) { uint64_t r = 1; for (uint64_t i = 2; i <= n; i++) r *= i; return r; }

static uint64_t *g_vp; static size_t g_vn; static uintptr_t g_vfp;
static void cb_vla(uint64_t *a, size_t n) {
    g_vp = a; g_vn = n;
    g_vfp = ((const uintptr_t *)__builtin_frame_address(0))[0];   /* = vla_sum 의 x29 */
    if (n > 0) a[n - 1] += 1000000;                                /* 다시 읽는지 확인 */
}

static int64_t tree_ref(const struct node *t) { return t ? t->val + tree_ref(t->left) + tree_ref(t->right) : 0; }
static uint64_t xs = 0x9E3779B97F4A7C15u;
static uint64_t xorshift64(void) { xs ^= xs << 13; xs ^= xs >> 7; xs ^= xs << 17; return xs; }

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)

int main(void) {
    /* 연습 1: 꼬리 호출 → base 에서 본 asm 프레임은 항상 1 개 */
    static const uint64_t ns[] = {0, 1, 5, 20, 10000};
    int ok = 1;
    for (size_t i = 0; i < sizeof ns / sizeof ns[0]; i++) {
        g_lo = (uintptr_t)fact_tail; g_hi = (uintptr_t)fact_tail_end; g_depth = g_calls = 0;
        ok &= (fact_tail(ns[i], 1) == fact_ref(ns[i]) && g_calls == 1 && g_depth == 1);
    }
    CHECK(ok);
    uint64_t r = 0;
    CHECK(call_preserve_check((uintptr_t)fact_tail, 20, 1, &r) == 0 && r == fact_ref(20));

    /* 연습 2: VLA — 크기 round_up(8n, 16), 16 정렬, mov sp, x29 로 해제 */
    ok = 1;
    for (size_t n = 0; n <= 40; n++) {
        uint64_t want = (n == 0) ? 0 : (uint64_t)(n - 1) * n * (2 * n - 1) / 6;
        g_vp = NULL;
        uint64_t s = vla_sum(n, cb_vla);
        ok &= (s == want + (n ? 1000000u : 0u));
        ok &= (g_vp != NULL && g_vn == n && ((uintptr_t)g_vp & 15u) == 0);
        ok &= (g_vfp - (uintptr_t)g_vp == ((8u * n + 15u) & ~(uintptr_t)15u));   /* a == x29 - round16(8n) */
    }
    CHECK(ok);
    CHECK(vla_sum(10, NULL) == 285u);
    r = 0;
    CHECK(call_preserve_check((uintptr_t)vla_sum, 33, 0, &r) == 0 && r == 11440u);

    /* 연습 3: 이진 트리 합 — 고정 시드 무작위 트리 vs C 재귀 */
    static struct node pool[64];
    for (int i = 0; i < 64; i++) {
        pool[i].val = (int64_t)(xorshift64() % 2001u) - 1000;
        pool[i].left = pool[i].right = NULL;
    }
    for (int i = 1; i < 64; i++) {                     /* i 를 앞쪽 노드의 빈 자식 자리에 붙인다 */
        for (;;) {
            struct node *p = &pool[xorshift64() % (uint64_t)i];
            if (xorshift64() & 1u) { if (!p->left) { p->left = &pool[i]; break; } }
            else if (!p->right) { p->right = &pool[i]; break; }
        }
    }
    CHECK(tree_sum(NULL) == 0);
    CHECK(tree_sum(&pool[0]) == tree_ref(&pool[0]));
    ok = 1;
    for (int i = 0; i < 64; i++) ok &= (tree_sum(&pool[i]) == tree_ref(&pool[i]));
    CHECK(ok);
    r = 0;
    CHECK(call_preserve_check((uintptr_t)tree_sum, (uint64_t)(uintptr_t)&pool[0], 0, &r) == 0
          && (int64_t)r == tree_ref(&pool[0]));

    /* 연습 4: 스택 한계 — 프레임 5 개까지 허용하면 5! 은 되고 6! 은 거부 */
    CHECK(fact_guarded(20, 0) == fact_ref(20));
    CHECK(fact_guarded(3, UINTPTR_MAX) == UINT64_MAX);
    uintptr_t S = get_sp();                            /* fact_guarded 진입 시 sp 와 같다 (같은 C 프레임) */
    CHECK(fact_guarded(5, S - 32u * 5u) == 120u);
    S = get_sp();
    CHECK(fact_guarded(6, S - 32u * 5u) == UINT64_MAX);
    S = get_sp();
    CHECK(fact_guarded(6, S - 32u * 6u) == 720u);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
