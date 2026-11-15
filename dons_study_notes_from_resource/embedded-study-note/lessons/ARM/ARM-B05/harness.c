/* ARM-B05 조건 플래그와 분기: CMP, B.cond, CSEL — 테스트 하네스
 * starter.S / solution.S (+ shared_bugs.S) 와 함께 빌드한다:
 *   cc -std=c11 -Wall -Wextra -Werror -O2 harness.c solution.S shared_bugs.S -o /tmp/ARM-B05_solution
 * 규칙: C 에서 UB 가 되는 값(abs(INT64_MIN), signed overflow)은 C 로 계산하지 않고 기대값을 손으로 쓴다.
 */
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* ---- asm 함수 (solution.S / starter.S) ---- */
int64_t  smax_br(int64_t a, int64_t b);
int64_t  smax_sel(int64_t a, int64_t b);
int64_t  smin_br(int64_t a, int64_t b);
int64_t  smin_sel(int64_t a, int64_t b);
uint64_t umax_br(uint64_t a, uint64_t b);
uint64_t umax_sel(uint64_t a, uint64_t b);
uint64_t umin_br(uint64_t a, uint64_t b);
uint64_t umin_sel(uint64_t a, uint64_t b);
int64_t  sabs_br(int64_t x);
int64_t  sabs_sel(int64_t x);
int64_t  sign_br(int64_t x);
int64_t  sign_sel(int64_t x);
int64_t  clamp_br(int64_t x, int64_t lo, int64_t hi);
int64_t  clamp_sel(int64_t x, int64_t lo, int64_t hi);
int64_t  sat_add_s64(int64_t a, int64_t b);
int      tick_reached(uint32_t now, uint32_t deadline);
int64_t  find_fault(const uint32_t *st, size_t n);
/* ---- 일부러 버그를 심은 함수 (shared_bugs.S) ---- */
uint64_t umax_bug_gt(uint64_t a, uint64_t b);
int64_t  smax_bug_hi(int64_t a, int64_t b);
int      tick_naive_hs(uint32_t now, uint32_t deadline);
int      tick_bug_ge(uint32_t now, uint32_t deadline);

/* ---- 테스트 도구 ---- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) g_pass++; \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint64_t g_rng = 0x9E3779B97F4A7C15ull;               /* 고정 시드 xorshift64 */
static uint64_t xs(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 7; g_rng ^= g_rng << 17; return g_rng; }
static int64_t rnd_s(void) {                                 /* 전 범위 + 작은 값 + 양 끝 근처 */
    switch (xs() & 3) {
    case 0:  return (int64_t)xs();
    case 1:  return (int64_t)(xs() % 17) - 8;
    case 2:  return INT64_MAX - (int64_t)(xs() % 8);
    default: return INT64_MIN + (int64_t)(xs() % 8);
    }
}
#define NRAND 100000

/* ---- C 참조 모델 (UB 없음) ---- */
static int64_t  ref_smax(int64_t a, int64_t b)   { return a > b ? a : b; }
static int64_t  ref_smin(int64_t a, int64_t b)   { return a < b ? a : b; }
static uint64_t ref_umax(uint64_t a, uint64_t b) { return a > b ? a : b; }
static uint64_t ref_umin(uint64_t a, uint64_t b) { return a < b ? a : b; }
static int64_t  ref_abs(int64_t x)  { return x == INT64_MIN ? INT64_MIN : (x < 0 ? -x : x); } /* MIN 은 손으로 */
static int64_t  ref_sign(int64_t x) { return (x > 0) - (x < 0); }
static int64_t  ref_clamp(int64_t x, int64_t lo, int64_t hi) { int64_t t = x < lo ? lo : x; return t > hi ? hi : t; }
static int64_t  ref_sat_add(int64_t a, int64_t b) {
    if (b > 0 && a > INT64_MAX - b) return INT64_MAX;      /* 넘치기 전에 판정 → UB 없음 */
    if (b < 0 && a < INT64_MIN - b) return INT64_MIN;
    return a + b;
}
static int ref_tick(uint32_t now, uint32_t dl) { return (uint32_t)(now - dl) < 0x80000000u; }

static const int64_t EDGE[] = {
    0, 1, -1, 2, -2, 7, -7, INT64_MAX, INT64_MAX - 1, INT64_MIN, INT64_MIN + 1,
    0x7FFFFFFF, 0x80000000LL, 0xFFFFFFFFLL, 0x100000000LL, -0x80000000LL,
};
#define NEDGE (sizeof EDGE / sizeof EDGE[0])
static const uint32_t EDGE32[] = {
    0, 1, 2, 5, 100, 0x7FFFFFF0u, 0x7FFFFFFFu, 0x80000000u, 0x80000001u, 0x80000010u,
    0xFFFFFFF0u, 0xFFFFFFFEu, 0xFFFFFFFFu,
};
#define NEDGE32 (sizeof EDGE32 / sizeof EDGE32[0])

/* ---- 비교 도우미: 틀린 개수를 세고 첫 불일치를 tag 와 함께 출력 ---- */
typedef int64_t  (*s1_fn)(int64_t);
typedef int64_t  (*s2_fn)(int64_t, int64_t);
typedef uint64_t (*u2_fn)(uint64_t, uint64_t);
typedef int64_t  (*s3_fn)(int64_t, int64_t, int64_t);
typedef int      (*t_fn)(uint32_t, uint32_t);

static int s2_cmp(const char *tag, const char *nm, s2_fn f, s2_fn ref, int64_t a, int64_t b, int bad) {
    int64_t got = f(a, b), want = ref(a, b);
    if (got == want) return bad;
    if (!bad) printf("  [%s] %s(%" PRId64 ", %" PRId64 ") = %" PRId64 ", want %" PRId64 "\n", tag, nm, a, b, got, want);
    return bad + 1;
}
static int s2_edge(const char *tag, const char *nm, s2_fn f, s2_fn ref) {
    int bad = 0;
    for (size_t i = 0; i < NEDGE; i++) for (size_t j = 0; j < NEDGE; j++) bad = s2_cmp(tag, nm, f, ref, EDGE[i], EDGE[j], bad);
    return bad;
}
static int s2_rand(const char *nm, s2_fn f, s2_fn ref) {
    int bad = 0;
    for (int k = 0; k < NRAND; k++) { int64_t a = rnd_s(), b = rnd_s(); bad = s2_cmp("rand", nm, f, ref, a, b, bad); }
    return bad;
}
static int u2_cmp(const char *tag, const char *nm, u2_fn f, u2_fn ref, uint64_t a, uint64_t b, int bad) {
    uint64_t got = f(a, b), want = ref(a, b);
    if (got == want) return bad;
    if (!bad) printf("  [%s] %s(0x%" PRIx64 ", 0x%" PRIx64 ") = 0x%" PRIx64 ", want 0x%" PRIx64 "\n", tag, nm, a, b, got, want);
    return bad + 1;
}
static int u2_edge(const char *tag, const char *nm, u2_fn f, u2_fn ref) {
    int bad = 0;
    for (size_t i = 0; i < NEDGE; i++) for (size_t j = 0; j < NEDGE; j++)
        bad = u2_cmp(tag, nm, f, ref, (uint64_t)EDGE[i], (uint64_t)EDGE[j], bad);
    return bad;
}
static int u2_rand(const char *nm, u2_fn f, u2_fn ref) {
    int bad = 0;
    for (int k = 0; k < NRAND; k++) { uint64_t a = (uint64_t)rnd_s(), b = (uint64_t)rnd_s(); bad = u2_cmp("rand", nm, f, ref, a, b, bad); }
    return bad;
}
static int s1_all(const char *nm, s1_fn f, s1_fn ref) {
    int bad = 0;
    for (int k = 0; k < (int)NEDGE + NRAND; k++) {
        int64_t x = k < (int)NEDGE ? EDGE[k] : rnd_s(), got = f(x), want = ref(x);
        if (got != want && !bad++) printf("  [mismatch] %s(%" PRId64 ") = %" PRId64 ", want %" PRId64 "\n", nm, x, got, want);
    }
    return bad;
}
static int s3_all(const char *nm, s3_fn f, s3_fn ref) {
    int bad = 0;
    for (size_t i = 0; i < NEDGE; i++) for (size_t j = 0; j < NEDGE; j++) for (size_t k = 0; k < NEDGE + NRAND / 100; k++) {
        int64_t x = k < NEDGE ? EDGE[k] : rnd_s(), lo = EDGE[i], hi = EDGE[j], got = f(x, lo, hi), want = ref(x, lo, hi);
        if (got != want && !bad++) printf("  [mismatch] %s(%" PRId64 ", %" PRId64 ", %" PRId64 ") = %" PRId64 ", want %" PRId64 "\n",
                                          nm, x, lo, hi, got, want);
    }
    return bad;
}
static int t_all(const char *tag, const char *nm, t_fn f) {
    int bad = 0;
    for (int k = 0; k < (int)(NEDGE32 * NEDGE32) + NRAND; k++) {
        uint32_t now, dl;
        if (k < (int)(NEDGE32 * NEDGE32)) { now = EDGE32[k / NEDGE32]; dl = EDGE32[k % NEDGE32]; }
        else { now = (uint32_t)xs(); dl = (uint32_t)xs(); }
        int got = f(now, dl), want = ref_tick(now, dl);
        if (got != want && !bad++) printf("  [%s] %s(0x%08" PRIx32 ", 0x%08" PRIx32 ") = %d, want %d\n", tag, nm, now, dl, got, want);
    }
    return bad;
}

int main(void) {
    /* ===== Part A: max / min, 분기판 = branchless판 = C 참조 ===== */
    printf("-- Part A: max/min --\n");
    CHECK(smax_br(-1, 1) == 1 && smax_sel(-1, 1) == 1 && smin_br(-1, 1) == -1 && smin_sel(-1, 1) == -1);
    CHECK(umax_br(UINT64_MAX, 1) == UINT64_MAX && umax_sel(UINT64_MAX, 1) == UINT64_MAX);
    CHECK(umin_br(UINT64_MAX, 1) == 1 && umin_sel(UINT64_MAX, 1) == 1);
    CHECK(s2_edge("mismatch", "smax_br", smax_br, ref_smax) == 0 && s2_rand("smax_br", smax_br, ref_smax) == 0);
    CHECK(s2_edge("mismatch", "smax_sel", smax_sel, ref_smax) == 0 && s2_rand("smax_sel", smax_sel, ref_smax) == 0);
    CHECK(s2_edge("mismatch", "smin_br", smin_br, ref_smin) == 0 && s2_rand("smin_br", smin_br, ref_smin) == 0);
    CHECK(s2_edge("mismatch", "smin_sel", smin_sel, ref_smin) == 0 && s2_rand("smin_sel", smin_sel, ref_smin) == 0);
    CHECK(u2_edge("mismatch", "umax_br", umax_br, ref_umax) == 0 && u2_rand("umax_br", umax_br, ref_umax) == 0);
    CHECK(u2_edge("mismatch", "umax_sel", umax_sel, ref_umax) == 0 && u2_rand("umax_sel", umax_sel, ref_umax) == 0);
    CHECK(u2_edge("mismatch", "umin_br", umin_br, ref_umin) == 0 && u2_rand("umin_br", umin_br, ref_umin) == 0);
    CHECK(u2_edge("mismatch", "umin_sel", umin_sel, ref_umin) == 0 && u2_rand("umin_sel", umin_sel, ref_umin) == 0);

    /* ===== Part B: abs / sign / clamp 경계값 ===== */
    printf("-- Part B: abs/sign/clamp --\n");
    /* abs(INT64_MIN): C 에선 -INT64_MIN 이 UB → 기대값을 손으로: ISA 처럼 wrap 해서 INT64_MIN */
    CHECK(sabs_br(INT64_MIN) == INT64_MIN && sabs_sel(INT64_MIN) == INT64_MIN);
    CHECK(sabs_br(INT64_MIN + 1) == INT64_MAX && sabs_sel(INT64_MIN + 1) == INT64_MAX);
    CHECK(sabs_br(-1) == 1 && sabs_sel(-1) == 1 && sabs_br(0) == 0 && sabs_sel(0) == 0);
    CHECK(s1_all("sabs_br", sabs_br, ref_abs) == 0);
    CHECK(s1_all("sabs_sel", sabs_sel, ref_abs) == 0);
    CHECK(sign_br(INT64_MIN) == -1 && sign_br(0) == 0 && sign_br(INT64_MAX) == 1);
    CHECK(sign_sel(INT64_MIN) == -1 && sign_sel(0) == 0 && sign_sel(INT64_MAX) == 1);
    CHECK(s1_all("sign_br", sign_br, ref_sign) == 0);
    CHECK(s1_all("sign_sel", sign_sel, ref_sign) == 0);
    CHECK(clamp_br(-5, 0, 10) == 0 && clamp_br(15, 0, 10) == 10 && clamp_br(7, 0, 10) == 7);
    CHECK(clamp_sel(-5, 0, 10) == 0 && clamp_sel(15, 0, 10) == 10 && clamp_sel(7, 0, 10) == 7);
    CHECK(clamp_br(5, 10, 0) == 0 && clamp_sel(5, 10, 0) == 0);          /* lo > hi: min(max) 정의상 hi */
    CHECK(s3_all("clamp_br", clamp_br, ref_clamp) == 0);
    CHECK(s3_all("clamp_sel", clamp_sel, ref_clamp) == 0);

    /* --- 주입 버그: GT vs HI. "내 테스트가 버그를 잡는가?" --- */
    printf("-- Part B: injected bugs (GT vs HI) --\n");
    CHECK(umax_bug_gt(UINT64_MAX, 1) == 1 && smax_bug_hi(-1, 1) == -1);  /* 버그 재현: 틀린 답이 나온다 */
    CHECK(u2_edge("caught", "umax_bug_gt", umax_bug_gt, ref_umax) > 0);   /* 경계 표는 잡는다 */
    CHECK(s2_edge("caught", "smax_bug_hi", smax_bug_hi, ref_smax) > 0);
    {
        int small = 0, full = 0;
        for (int k = 0; k < NRAND; k++) {                              /* 둘 다 < 2^63 인 "작은 양수" 무작위 */
            uint64_t a = xs() >> 1, b = xs() >> 1;
            small += umax_bug_gt(a, b) != ref_umax(a, b);
        }
        for (int k = 0; k < NRAND; k++) {                              /* 전 범위 무작위 */
            uint64_t a = xs(), b = xs();
            full += umax_bug_gt(a, b) != ref_umax(a, b);
        }
        printf("  umax_bug_gt caught by random: small-positive %d/%d, full-range %d/%d\n", small, NRAND, full, NRAND);
        CHECK(small == 0);                                              /* 작은 양수만으론 절대 못 잡는다 */
        CHECK(full > 0);
    }

    /* ===== Part C: 펌웨어 ===== */
    printf("-- Part C: firmware --\n");
    CHECK(sat_add_s64(INT64_MAX, 1) == INT64_MAX && sat_add_s64(INT64_MIN, -1) == INT64_MIN);
    CHECK(sat_add_s64(INT64_MIN, INT64_MAX) == -1 && sat_add_s64(INT64_MAX, INT64_MAX) == INT64_MAX);
    CHECK(s2_edge("mismatch", "sat_add_s64", sat_add_s64, ref_sat_add) == 0 && s2_rand("sat_add_s64", sat_add_s64, ref_sat_add) == 0);
    CHECK(tick_reached(5, 0xFFFFFFF0u) == 1 && tick_reached(0xFFFFFFF0u, 5) == 0);   /* wrap 넘어간 deadline */
    CHECK(tick_reached(100, 100) == 1 && tick_reached(99, 100) == 0);
    CHECK(tick_reached(0x7FFFFFF0u, 0x80000010u) == 0);                 /* 0x7FFF…/0x8000… 경계 */
    CHECK(t_all("mismatch", "tick_reached", tick_reached) == 0);
    CHECK(tick_naive_hs(5, 0xFFFFFFF0u) == 0 && tick_bug_ge(0x7FFFFFF0u, 0x80000010u) == 1); /* 버그 재현 */
    CHECK(t_all("caught", "tick_naive_hs", tick_naive_hs) > 0);          /* 주입 버그 2종도 잡히나 */
    CHECK(t_all("caught", "tick_bug_ge", tick_bug_ge) > 0);
    {
        static uint32_t st[1000];
        const uint32_t clean[3] = {0x00, 0x10, 0x80};                  /* bit4, bit7 은 fault 아님 */
        const uint32_t a[3] = {0x01, 0x20, 0x40};                      /* ERR 는 index 1 */
        const uint32_t b[2] = {0x40, 0x20};                            /* OVR 만으로도 fault */
        st[999] = 0x60;
        CHECK(find_fault(NULL, 0) == -1);                               /* n == 0: 메모리 안 읽음 */
        CHECK(find_fault(clean, 3) == -1);
        CHECK(find_fault(a, 3) == 1 && find_fault(a, 1) == -1 && find_fault(b, 2) == 0);
        CHECK(find_fault(st, 1000) == 999 && find_fault(st, 999) == -1);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
