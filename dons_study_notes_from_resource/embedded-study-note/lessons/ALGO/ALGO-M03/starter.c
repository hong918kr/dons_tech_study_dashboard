/* ALGO-M03 백트래킹: 부분집합·순열·N-Queens
 *
 * 선택-탐색-취소(choose - explore - unchoose) 템플릿 하나로 네 문제를 푼다. 노드 = 재귀 함수 호출 1회(루트 포함).
 *   Part A  부분집합 합: 합이 target 인 모든 부분집합(비트마스크) + 개수. 가지치기 on/off 노드 수 비교
 *   Part B  중복 원소 순열(같은 순열은 한 번만, 사전순) — 개수 = n!/∏k!
 *           N-Queens 비트마스크 — n = 0..12 개수를 OEIS A000170 과 대조, 가지치기 없는 두 기준판과 노드 수 비교
 *   Part C  핀 다중화(pin-mux) 배치: 주변장치마다 리맵 옵션 하나를 골라 핀이 겹치지 않는 모든 배치 수 + 한 개
 *           (mode 1 = 겹치면 바로 가지치기, mode 2 = + MRV 변수 순서 + 전방 검사)
 *
 * 테스트: 무작위(xorshift32 고정 시드) + 작은 크기 전수 + 이름 붙은 사례 + 알려진 값(OEIS, 다항계수).
 *         기준 구현은 테스트 쪽에 따로(2^n 마스크 전수, next_permutation, 직교곱 전수). 동적 할당 없음.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so   (starter.c 도 같은 명령)
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ======================= Part A: 부분집합 합 ======================= */
#define SS_MAXN 24

typedef struct {
    uint32_t w[SS_MAXN];           /* 탐색 순서의 무게 (prune 이면 오름차순 정렬) */
    uint8_t  idx[SS_MAXN];         /* w[k] 의 원래 인덱스 */
    uint64_t suffix[SS_MAXN + 1];  /* suffix[k] = w[k] + ... + w[n-1]  (24 × (2^32-1) < 2^37 → uint64 안전) */
    size_t   n;
    uint64_t target;
    int      prune;
    uint64_t sum;                  /* 선택 상태: choose 에서 더하고 unchoose 에서 뺀다 */
    uint32_t mask;                 /* 선택 상태: 원래 인덱스 기준 비트 */
    uint32_t *out; size_t cap;     /* 결과 마스크 (cap 개까지만 저장, 개수는 끝까지 센다) */
    size_t   count;
    uint64_t nodes;
} ss_ctx_t;

static void ss_emit(ss_ctx_t *c) {
    if (c->count < c->cap) c->out[c->count] = c->mask;
    c->count++;
}

/* k 번째 원소를 넣을지 말지 결정하는 노드. prune == 0 이면 완전 이진 트리(노드 2^(n+1) - 1). */
static void ss_rec(ss_ctx_t *c, size_t k) {
    c->nodes++;
    (void)k; (void)ss_emit;
    /* TODO: prune 이면 (1) sum + suffix[k] < target → return  (2) k == n 또는 sum + w[k] > target → sum == target 이면 emit, return
     *       prune 아니면 k == n 에서 sum == target 이면 emit.
     *       choose(sum += w[k], mask |= bit) → explore(k+1) → unchoose → explore(k+1) */
    return;
}

/* 합이 target 인 부분집합(원래 인덱스 비트마스크)을 out[0..cap) 에 쓰고 전체 개수를 돌려준다.
 * n > SS_MAXN 이면 0. nodes 가 NULL 이 아니면 방문 노드 수. */
size_t subset_sum_all(const uint32_t *w, size_t n, uint64_t target, int prune,
                      uint32_t *out, size_t cap, uint64_t *nodes) {
    ss_ctx_t c;
    if (n > SS_MAXN) return 0;
    memset(&c, 0, sizeof c);
    c.n = n; c.target = target; c.prune = prune; c.out = out; c.cap = out ? cap : 0;
    for (size_t i = 0; i < n; i++) { c.w[i] = w[i]; c.idx[i] = (uint8_t)i; }
    if (prune) {                                   /* 삽입 정렬 (n ≤ 24, 안정) */
        for (size_t i = 1; i < n; i++) {
            uint32_t v = c.w[i]; uint8_t id = c.idx[i]; size_t j = i;
            while (j > 0 && c.w[j - 1] > v) { c.w[j] = c.w[j - 1]; c.idx[j] = c.idx[j - 1]; j--; }
            c.w[j] = v; c.idx[j] = id;
        }
    }
    c.suffix[n] = 0;
    for (size_t i = n; i-- > 0;) c.suffix[i] = c.suffix[i + 1] + c.w[i];
    ss_rec(&c, 0);
    if (nodes) *nodes = c.nodes;
    return c.count;
}

/* ======================= Part B-1: 중복 원소 순열 ======================= */
#define PERM_MAXN 12

typedef struct {
    int32_t  a[PERM_MAXN];         /* 정렬된 입력 */
    size_t   n;
    int      dedup;
    uint8_t  used[PERM_MAXN];
    int32_t  cur[PERM_MAXN];       /* 지금까지 고른 접두사 */
    size_t   depth;
    int32_t *out; size_t cap;      /* out[k*n .. k*n+n) = k 번째 순열 */
    size_t   count;
    uint64_t nodes;
} perm_ctx_t;

static void perm_rec(perm_ctx_t *c) {
    c->nodes++;
    /* TODO: depth == n 이면 cur 를 out 에 복사(count < cap 일 때만), count++.
     *       아니면 i = 0..n-1: used 면 건너뜀, dedup 이고 a[i] == a[i-1] && !used[i-1] 이면 건너뜀,
     *       choose(used[i] = 1, cur[depth++] = a[i]) → explore → unchoose */
    return;
}

/* a 의 순열을 사전순으로 out 에 쓴다 (cap 개까지). dedup 이면 같은 값 순열은 한 번만.
 * 반환 = 전체 개수 (n == 0 이면 빈 순열 1개). n > PERM_MAXN 이면 0. */
size_t perm_all(const int32_t *a, size_t n, int dedup, int32_t *out, size_t cap, uint64_t *nodes) {
    perm_ctx_t c;
    if (n > PERM_MAXN) return 0;
    memset(&c, 0, sizeof c);
    c.n = n; c.dedup = dedup; c.out = out; c.cap = out ? cap : 0;
    for (size_t i = 0; i < n; i++) {               /* 삽입 정렬: 같은 값을 붙이고 사전순 출력 */
        int32_t v = a[i]; size_t j = i;
        while (j > 0 && c.a[j - 1] > v) { c.a[j] = c.a[j - 1]; j--; }
        c.a[j] = v;
    }
    perm_rec(&c);
    if (nodes) *nodes = c.nodes;
    return c.count;
}

/* ======================= Part B-2: N-Queens ======================= */
#define NQ_MAXN 16

/* 기준판 1 (주어짐): 가지치기 없음 — 행마다 n 개 열 아무거나, 다 놓은 뒤에만 검사. 노드 = Σ n^k */
static int nq_valid(const uint8_t *col, unsigned n) {
    for (unsigned r = 0; r < n; r++)
        for (unsigned s = r + 1; s < n; s++) {
            int dc = (int)col[s] - (int)col[r];
            if (dc == 0 || dc == (int)(s - r) || -dc == (int)(s - r)) return 0;
        }
    return 1;
}
static uint64_t nq_naive_rec(uint8_t *col, unsigned r, unsigned n, uint64_t *nodes) {
    (*nodes)++;
    if (r == n) return (uint64_t)nq_valid(col, n);
    uint64_t cnt = 0;
    for (unsigned c = 0; c < n; c++) { col[r] = (uint8_t)c; cnt += nq_naive_rec(col, r + 1, n, nodes); }
    return cnt;
}
/* 기준판 2 (주어짐): 열 중복만 가지치기(= 순열), 대각선은 다 놓은 뒤 검사. 노드 = Σ n!/(n-k)! */
static uint64_t nq_perm_rec(uint8_t *col, unsigned r, unsigned n, uint32_t used, uint64_t *nodes) {
    (*nodes)++;
    if (r == n) return (uint64_t)nq_valid(col, n);
    uint64_t cnt = 0;
    for (unsigned c = 0; c < n; c++) {
        if (used & (1u << c)) continue;
        col[r] = (uint8_t)c;
        cnt += nq_perm_rec(col, r + 1, n, used | (1u << c), nodes);
    }
    return cnt;
}

/* 비트마스크판: cols = 쓰인 열, d1/d2 = 이 행에서 대각선으로 공격받는 열. 상태를 값으로 넘기므로
 * unchoose 는 "함수에서 돌아오는 것" 자체다. */
static uint64_t nq_bits_rec(uint32_t full, uint32_t cols, uint32_t d1, uint32_t d2, uint64_t *nodes) {
    (*nodes)++;
    (void)full; (void)cols; (void)d1; (void)d2;
    /* TODO: cols == full 이면 1. avail = full & ~(cols | d1 | d2) 의 비트마다
     *       cols | bit, (d1 | bit) << 1, (d2 | bit) >> 1 로 다음 행 재귀, 합을 돌려준다 */
    return 0;
}

enum { NQ_NAIVE, NQ_PERM, NQ_BITS };
uint64_t nqueens_count(unsigned n, int mode, uint64_t *nodes) {
    uint8_t col[NQ_MAXN];
    uint64_t nd = 0, cnt;
    if (n > NQ_MAXN) return 0;
    if (mode == NQ_NAIVE)     cnt = nq_naive_rec(col, 0, n, &nd);
    else if (mode == NQ_PERM) cnt = nq_perm_rec(col, 0, n, 0, &nd);
    else                      cnt = nq_bits_rec((n == 0) ? 0u : (uint32_t)((1u << n) - 1u), 0, 0, 0, &nd);
    if (nodes) *nodes = nd;
    return cnt;
}

/* ======================= Part C: 핀 다중화 배치 ======================= */
#define PM_MAXP 12
#define PM_MAXO 4

typedef struct {
    const char *name;
    uint8_t     nopt;
    uint64_t    opt[PM_MAXO];      /* 옵션별 핀 집합: 비트 = port*16 + pin (PA=0, PB=1, PC=2, PD=3) */
    const char *optname[PM_MAXO];
} pm_periph_t;

typedef struct {
    uint64_t count;                /* 유효 배치 전체 수 */
    uint64_t nodes;
    int      found;
    int8_t   pick[PM_MAXP];        /* 처음 찾은 배치: 주변장치별 옵션 번호 */
} pm_result_t;

typedef struct {
    const pm_periph_t *p; size_t np; int mode;
    uint64_t used;                 /* 예약 핀 + 지금까지 고른 핀 */
    int8_t   pick[PM_MAXP];        /* -1 = 아직 안 고름 */
    size_t   assigned;
    pm_result_t *res;
} pm_ctx_t;

static void pm_leaf(pm_ctx_t *c) {
    c->res->count++;
    if (!c->res->found) { c->res->found = 1; memcpy(c->res->pick, c->pick, sizeof c->pick); }
}

/* mode 0 (주어짐): 가지치기 없음 — 순서대로 전부 고른 뒤에만 겹침 검사 */
static void pm_rec0(pm_ctx_t *c, size_t k, uint64_t reserved) {
    c->res->nodes++;
    if (k == c->np) {
        uint64_t acc = reserved;
        for (size_t i = 0; i < c->np; i++) {
            uint64_t m = c->p[i].opt[c->pick[i]];
            if (m & acc) return;
            acc |= m;
        }
        pm_leaf(c);
        return;
    }
    for (int o = 0; o < c->p[k].nopt; o++) { c->pick[k] = (int8_t)o; pm_rec0(c, k + 1, reserved); }
    c->pick[k] = -1;
}

/* mode 1: 주변장치 순서대로, 이미 쓴 핀과 겹치는 옵션은 바로 건너뛴다.
 * mode 2: 매 노드에서 "남은 가능 옵션 수" 가 가장 적은 주변장치를 고르고(MRV), 0 인 것이 있으면 되돌아간다(전방 검사). */
static void pm_rec(pm_ctx_t *c) {
    c->res->nodes++;
    /* TODO: assigned == np 이면 pm_leaf. mode 1: k = assigned (순서대로).
     *       mode 2: 미배정 주변장치마다 (opt & used) == 0 인 옵션 수를 세어 0 이면 return, 최소인 것을 k 로.
     *       k 의 옵션 중 used 와 안 겹치는 것만: choose(used |= m, pick[k] = o, assigned++) → explore → unchoose */
    return;
}

void pinmux_search(const pm_periph_t *p, size_t np, uint64_t reserved, int mode, pm_result_t *res) {
    pm_ctx_t c;
    memset(res, 0, sizeof *res);
    memset(res->pick, -1, sizeof res->pick);
    if (np > PM_MAXP) return;
    memset(&c, 0, sizeof c);
    c.p = p; c.np = np; c.mode = mode; c.used = reserved; c.res = res;
    memset(c.pick, -1, sizeof c.pick);
    if (mode == 0) pm_rec0(&c, 0, reserved);
    else           pm_rec(&c);
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t g_rng = 0x2545F491u;
static uint32_t xorshift32(void) {
    uint32_t x = g_rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return g_rng = x;
}

/* ---- 기준 구현 (테스트 쪽) ---- */
static size_t ref_subsets(const uint32_t *w, size_t n, uint64_t target, uint32_t *out, size_t cap) {
    size_t cnt = 0;
    for (uint32_t m = 0; m < (1u << n); m++) {
        uint64_t s = 0;
        for (size_t i = 0; i < n; i++) if (m >> i & 1u) s += w[i];
        if (s == target) { if (cnt < cap) out[cnt] = m; cnt++; }
    }
    return cnt;
}
static void sort_u32(uint32_t *a, size_t n) {
    for (size_t i = 1; i < n; i++) { uint32_t v = a[i]; size_t j = i; while (j > 0 && a[j - 1] > v) { a[j] = a[j - 1]; j--; } a[j] = v; }
}
/* 사전순 다음 순열 (std::next_permutation 과 같은 반복 알고리즘). 마지막이면 0 */
static int next_perm(int32_t *a, size_t n) {
    if (n < 2) return 0;
    size_t i = n - 1;
    while (i > 0 && a[i - 1] >= a[i]) i--;
    if (i == 0) return 0;
    size_t j = n - 1;
    while (a[j] <= a[i - 1]) j--;
    int32_t t = a[i - 1]; a[i - 1] = a[j]; a[j] = t;
    for (size_t l = i, r = n - 1; l < r; l++, r--) { t = a[l]; a[l] = a[r]; a[r] = t; }
    return 1;
}
static uint64_t factorial(unsigned n) { uint64_t f = 1; for (unsigned i = 2; i <= n; i++) f *= i; return f; }
/* n! / ∏ k_v!  (정렬된 a 에서 같은 값 묶음 크기 k_v) */
static uint64_t multinomial_sorted(const int32_t *a, size_t n) {
    uint64_t r = factorial((unsigned)n);
    for (size_t i = 0; i < n;) { size_t j = i; while (j < n && a[j] == a[i]) j++; r /= factorial((unsigned)(j - i)); i = j; }
    return r;
}
/* Σ_{k=0..n} n!/(n-k)!  = 순열 트리의 노드 수 */
static uint64_t perm_tree_nodes(unsigned n) { uint64_t s = 0, t = 1; for (unsigned k = 0; k <= n; k++) { s += t; t *= (n - k); } return s; }
static uint64_t pow_sum(unsigned n) { uint64_t s = 0, t = 1; for (unsigned k = 0; k <= n; k++) { s += t; t *= n; } return s; }

static uint64_t ref_pinmux(const pm_periph_t *p, size_t np, uint64_t reserved) {   /* 직교곱 전수 (오도미터) */
    int odo[PM_MAXP] = { 0 };
    uint64_t cnt = 0;
    for (size_t i = 0; i < np; i++) if (p[i].nopt == 0) return 0;
    for (;;) {
        uint64_t acc = reserved; int ok = 1;
        for (size_t i = 0; i < np && ok; i++) { uint64_t m = p[i].opt[odo[i]]; if (m & acc) ok = 0; acc |= m; }
        cnt += (uint64_t)ok;
        size_t i = 0;
        while (i < np && ++odo[i] == p[i].nopt) odo[i++] = 0;
        if (i == np) break;
    }
    return cnt;
}
static int pm_pick_valid(const pm_periph_t *p, size_t np, uint64_t reserved, const int8_t *pick) {
    uint64_t acc = reserved;
    for (size_t i = 0; i < np; i++) {
        if (pick[i] < 0 || pick[i] >= p[i].nopt) return 0;
        uint64_t m = p[i].opt[pick[i]];
        if (m & acc) return 0;
        acc |= m;
    }
    return 1;
}

/* STM32F1 AFIO 리맵을 본뜬 표 (핀은 RM0008 확인 권장). 비트 = port*16 + pin */
#define PA(n) (1ull << (n))
#define PB(n) (1ull << (16 + (n)))
#define PC(n) (1ull << (32 + (n)))
#define PD(n) (1ull << (48 + (n)))
static const pm_periph_t g_board[] = {
    { "USART1", 2, { PA(9) | PA(10), PB(6) | PB(7) }, { "PA9/PA10", "PB6/PB7" } },
    { "USART2", 2, { PA(2) | PA(3), PD(5) | PD(6) }, { "PA2/PA3", "PD5/PD6" } },
    { "I2C1",   2, { PB(6) | PB(7), PB(8) | PB(9) }, { "PB6/PB7", "PB8/PB9" } },
    { "SPI1",   2, { PA(4) | PA(5) | PA(6) | PA(7), PA(15) | PB(3) | PB(4) | PB(5) }, { "PA4-PA7", "PA15/PB3/PB4/PB5" } },
    { "CAN1",   3, { PA(11) | PA(12), PB(8) | PB(9), PD(0) | PD(1) }, { "PA11/PA12", "PB8/PB9", "PD0/PD1" } },
    { "TIM3",   3, { PA(6) | PA(7), PB(4) | PB(5), PC(6) | PC(7) }, { "PA6/PA7", "PB4/PB5", "PC6/PC7" } },
    { "ADC1",   1, { PA(0) | PA(1) }, { "PA0/PA1" } },
    { "USB",    1, { PA(11) | PA(12) }, { "PA11/PA12" } },
};
#define SWD_PINS  (PA(13) | PA(14))
#define JTAG_PINS (PA(13) | PA(14) | PA(15) | PB(3) | PB(4))
#define HSE_PINS  (PD(0) | PD(1))
#define LQFP48_MISSING (PC(6) | PC(7) | PD(5) | PD(6))   /* 48핀 패키지에 없는 핀 (확인 권장) */

int main(void) {
    /* ================= Part A: 부분집합 합 ================= */
    {   /* 이름 붙은 사례: {3,34,4,12,5,2}, 9 → {4,5}, {3,4,2} */
        const uint32_t w[6] = { 3, 34, 4, 12, 5, 2 };
        uint32_t got[8], want[2] = { 0x14u /* idx 2,4 */, 0x25u /* idx 0,2,5 */ };
        for (int pr = 0; pr <= 1; pr++) {
            size_t k = subset_sum_all(w, 6, 9, pr, got, 8, NULL);
            sort_u32(got, k < 8 ? k : 8);
            CHECK(k == 2 && got[0] == want[0] && got[1] == want[1]);
        }
        uint64_t nd;
        CHECK(subset_sum_all(NULL, 0, 0, 1, NULL, 0, &nd) == 1 && nd == 1);    /* 빈 집합 하나 */
        CHECK(subset_sum_all(NULL, 0, 5, 0, NULL, 0, NULL) == 0);
        const uint32_t z[3] = { 0, 0, 1 };                                  /* 0 무게: 넣든 빼든 해 */
        CHECK(subset_sum_all(z, 3, 1, 1, NULL, 0, NULL) == 4 && subset_sum_all(z, 3, 0, 1, NULL, 0, NULL) == 4);
        const uint32_t big[4] = { 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu };
        CHECK(subset_sum_all(big, 4, 2ull * 0xFFFFFFFFu, 1, NULL, 0, NULL) == 6);   /* C(4,2), 합이 32비트를 넘는다 */
        uint32_t two[2];
        CHECK(subset_sum_all(z, 3, 0, 0, two, 2, NULL) == 4);              /* cap 이 작아도 개수는 끝까지 */
    }
    {   /* 전수: 무게 ∈ {0..3}^n, n = 0..6, target = 0..3n+1, 두 모드 × 마스크 집합 비교 + 노드 공식 */
        unsigned long cases = 0, bad = 0, bad_nodes = 0;
        uint32_t w[6], got[64], want[64];
        for (size_t n = 0; n <= 6; n++) {
            uint32_t combos = 1u << (2 * n);
            for (uint32_t code = 0; code < combos; code++) {
                for (size_t i = 0; i < n; i++) w[i] = (code >> (2 * i)) & 3u;
                for (uint64_t t = 0; t <= 3 * n + 1; t++) {
                    size_t kw = ref_subsets(w, n, t, want, 64);
                    sort_u32(want, kw);
                    for (int pr = 0; pr <= 1; pr++) {
                        uint64_t nd;
                        size_t kg = subset_sum_all(w, n, t, pr, got, 64, &nd);
                        sort_u32(got, kg < 64 ? kg : 64);
                        cases++;
                        if (kg != kw || memcmp(got, want, kw * sizeof got[0]) != 0) bad++;
                        if (!pr && nd != (2ull << n) - 1) bad_nodes++;
                    }
                }
            }
        }
        printf("Part A exhaustive: %lu cases (w in {0..3}^n, n<=6), mismatches=%lu, nodes!=2^(n+1)-1: %lu\n", cases, bad, bad_nodes);
        CHECK(bad == 0);
        CHECK(bad_nodes == 0);
    }
    {   /* 무작위: n = 16, 무게 1..60, target 무작위, 마스크 집합 비교 */
        static uint32_t got[1 << 16], want[1 << 16];
        uint32_t w[16];
        unsigned long bad = 0, total_sol = 0;
        for (int it = 0; it < 120; it++) {
            uint64_t s = 0;
            for (int i = 0; i < 16; i++) { w[i] = 1 + xorshift32() % 60; s += w[i]; }
            uint64_t t = xorshift32() % (s + 1);
            size_t kw = ref_subsets(w, 16, t, want, 1 << 16);
            size_t kg = subset_sum_all(w, 16, t, 1, got, 1 << 16, NULL);
            sort_u32(want, kw); sort_u32(got, kg);
            total_sol += kw;
            if (kg != kw || memcmp(got, want, kw * sizeof got[0]) != 0) bad++;
        }
        printf("Part A random: 120 instances n=16, total solutions=%lu, mismatches=%lu\n", total_sol, bad);
        CHECK(bad == 0);
    }
    {   /* 노드 수: n = 20, 무게 1..1000, 가지치기 off/on */
        uint32_t w[20];
        uint64_t s = 0;
        for (int i = 0; i < 20; i++) { w[i] = 1 + xorshift32() % 1000; s += w[i]; }
        const uint64_t tg[4] = { 1500, s / 4, s / 2, s - 7 };
        int ok = 1;
        printf("Part A nodes (n=20, sum=%llu):  target | solutions | nodes no-prune | nodes prune\n", (unsigned long long)s);
        for (int t = 0; t < 4; t++) {
            uint64_t n0, n1;
            size_t k0 = subset_sum_all(w, 20, tg[t], 0, NULL, 0, &n0);
            size_t k1 = subset_sum_all(w, 20, tg[t], 1, NULL, 0, &n1);
            printf("    %6llu | %9zu | %14llu | %11llu\n", (unsigned long long)tg[t], k0,
                   (unsigned long long)n0, (unsigned long long)n1);
            if (k0 != k1 || n0 != (2ull << 20) - 1 || n1 >= n0) ok = 0;
        }
        CHECK(ok);
    }

    /* ================= Part B-1: 중복 원소 순열 ================= */
    {
        const int32_t a112[3] = { 2, 1, 1 };
        int32_t out[6 * 3];
        const int32_t want112[9] = { 1, 1, 2, 1, 2, 1, 2, 1, 1 };
        uint64_t nd;
        CHECK(perm_all(a112, 3, 1, out, 6, &nd) == 3 && memcmp(out, want112, sizeof want112) == 0);
        CHECK(perm_all(a112, 3, 0, out, 6, NULL) == 6);                 /* dedup 끄면 3! = 6 (중복 포함) */
        const int32_t a123[3] = { 3, 1, 2 };
        const int32_t want123[18] = { 1,2,3, 1,3,2, 2,1,3, 2,3,1, 3,1,2, 3,2,1 };
        CHECK(perm_all(a123, 3, 1, out, 6, NULL) == 6 && memcmp(out, want123, sizeof want123) == 0);
        CHECK(perm_all(NULL, 0, 1, NULL, 0, &nd) == 1 && nd == 1);       /* 빈 순열 하나 */
        const int32_t seven[5] = { 7, 7, 7, 7, 7 };
        CHECK(perm_all(seven, 5, 1, NULL, 0, &nd) == 1 && nd == 6);      /* 전부 같음: 한 줄 경로, 노드 n+1 */
        /* MISSISSIPPI: 11!/(4!·4!·2!) = 34650,  BANANA: 6!/(3!·2!) = 60 */
        const int32_t miss[11] = { 'M','I','S','S','I','S','S','I','P','P','I' };
        const int32_t ban[6] = { 'B','A','N','A','N','A' };
        CHECK(perm_all(miss, 11, 1, NULL, 0, NULL) == 34650);
        CHECK(perm_all(ban, 6, 1, NULL, 0, NULL) == 60);
        const int32_t ext[4] = { INT32_MAX, INT32_MIN, INT32_MAX, 0 };
        CHECK(perm_all(ext, 4, 1, NULL, 0, NULL) == 12);
    }
    {   /* 전수: 알파벳 {0,1,2} 위 길이 0..7 모든 배열 — next_permutation 사전순 열과 완전히 같은지 */
        static int32_t out[5040 * 7];
        int32_t a[7], ref[7];
        unsigned long arrays = 0, bad_seq = 0, bad_cnt = 0, bad_nodes = 0;
        for (size_t n = 0; n <= 7; n++) {
            uint32_t combos = 1;
            for (size_t i = 0; i < n; i++) combos *= 3;
            for (uint32_t code = 0; code < combos; code++) {
                uint32_t x = code;
                for (size_t i = 0; i < n; i++) { a[i] = (int32_t)(x % 3); x /= 3; }
                arrays++;
                size_t k = perm_all(a, n, 1, out, 5040, NULL);
                memcpy(ref, a, n * sizeof a[0]);
                for (size_t i = 1; i < n; i++) { int32_t v = ref[i]; size_t j = i; while (j > 0 && ref[j - 1] > v) { ref[j] = ref[j - 1]; j--; } ref[j] = v; }
                if (k != multinomial_sorted(ref, n)) bad_cnt++;
                size_t idx = 0; int ok = 1;
                do {
                    if (idx >= k || memcmp(out + idx * n, ref, n * sizeof ref[0]) != 0) { ok = 0; break; }
                    idx++;
                } while (next_perm(ref, n));
                if (!ok || idx != k) bad_seq++;
                uint64_t nd;
                size_t k0 = perm_all(a, n, 0, NULL, 0, &nd);
                if (k0 != factorial((unsigned)n) || nd != perm_tree_nodes((unsigned)n)) bad_nodes++;
            }
        }
        printf("Part B perms exhaustive: %lu arrays over {0,1,2}, len<=7: seq mismatches=%lu, count!=n!/prod k!: %lu, no-dedup nodes/count wrong: %lu\n",
               arrays, bad_seq, bad_cnt, bad_nodes);
        CHECK(bad_seq == 0);
        CHECK(bad_cnt == 0);
        CHECK(bad_nodes == 0);
    }
    {   /* 노드 수 표: [1,1,1,2,2,3,3,3] */
        const int32_t a[8] = { 1, 1, 1, 2, 2, 3, 3, 3 };
        uint64_t n0, n1;
        size_t k0 = perm_all(a, 8, 0, NULL, 0, &n0), k1 = perm_all(a, 8, 1, NULL, 0, &n1);
        printf("Part B perms [1,1,1,2,2,3,3,3]: no-dedup leaves=%zu nodes=%llu | dedup leaves=%zu nodes=%llu\n",
               k0, (unsigned long long)n0, k1, (unsigned long long)n1);
        CHECK(k0 == 40320 && k1 == 560 && n0 == perm_tree_nodes(8));
    }

    /* ================= Part B-2: N-Queens ================= */
    {
        static const uint64_t a000170[13] = { 1, 1, 0, 0, 2, 10, 4, 40, 92, 352, 724, 2680, 14200 };
        int ok = 1;
        for (unsigned n = 0; n <= 12; n++) if (nqueens_count(n, NQ_BITS, NULL) != a000170[n]) { ok = 0; printf("  n=%u got %llu\n", n, (unsigned long long)nqueens_count(n, NQ_BITS, NULL)); }
        printf("Part B N-Queens bitmask n=0..12 vs OEIS A000170: %s\n", ok ? "all equal" : "MISMATCH");
        CHECK(ok);
        printf("Part B N-Queens nodes:  n | solutions | naive (n^n) | perm (cols only) | bitmask (cols+diags)\n");
        int ok2 = 1;
        for (unsigned n = 4; n <= 9; n++) {
            uint64_t nn = 0, np, nb;
            uint64_t cb = nqueens_count(n, NQ_BITS, &nb), cp = nqueens_count(n, NQ_PERM, &np), cn = cb;
            if (n <= 7) cn = nqueens_count(n, NQ_NAIVE, &nn);
            else nn = pow_sum(n);
            printf("    %2u | %9llu | %11llu%s | %16llu | %20llu\n", n, (unsigned long long)cb, (unsigned long long)nn,
                   n <= 7 ? " " : "*", (unsigned long long)np, (unsigned long long)nb);
            if (cn != cb || cp != cb || np != perm_tree_nodes(n) || (n <= 7 && nn != pow_sum(n)) || nb >= np) ok2 = 0;
        }
        printf("    (* = formula sum n^k, not run here; demo_nodes.c runs n=8)\n");
        CHECK(ok2);
        CHECK(nqueens_count(NQ_MAXN + 1, NQ_BITS, NULL) == 0);
    }

    /* ================= Part C: 핀 다중화 ================= */
    {   /* 무작위: 핀 12개, 주변장치 1..7개, 옵션 1..4개(핀 1..3개), 예약 핀 0..2개 — 세 모드 vs 직교곱 전수 */
        pm_periph_t p[PM_MAXP];
        unsigned long inst = 0, bad = 0, bad_pick = 0, feasible = 0;
        for (int it = 0; it < 400; it++) {
            size_t np = 1 + xorshift32() % 7;
            for (size_t i = 0; i < np; i++) {
                p[i].name = "P"; p[i].nopt = (uint8_t)(1 + xorshift32() % 4);
                for (int o = 0; o < p[i].nopt; o++) {
                    uint64_t m = 0; int k = 1 + (int)(xorshift32() % 3);
                    for (int b = 0; b < k; b++) m |= 1ull << (xorshift32() % 12);
                    p[i].opt[o] = m; p[i].optname[o] = "";
                }
            }
            uint64_t reserved = 0;
            for (uint32_t r = xorshift32() % 3; r > 0; r--) reserved |= 1ull << (xorshift32() % 12);
            uint64_t want = ref_pinmux(p, np, reserved);
            inst++; feasible += want > 0;
            for (int mode = 0; mode <= 2; mode++) {
                pm_result_t r;
                pinmux_search(p, np, reserved, mode, &r);
                if (r.count != want) bad++;
                if (r.found != (want > 0) || (r.found && !pm_pick_valid(p, np, reserved, r.pick))) bad_pick++;
            }
        }
        printf("Part C random: %lu instances (%lu feasible), count mismatches=%lu, bad picks=%lu\n", inst, feasible, bad, bad_pick);
        CHECK(bad == 0);
        CHECK(bad_pick == 0);
        pm_result_t r;
        pinmux_search(p, 0, 0, 2, &r);
        CHECK(r.count == 1 && r.found);                                 /* 주변장치 0개: 빈 배치 하나 */
        pm_periph_t none = { "X", 0, { 0 }, { 0 } };
        pinmux_search(&none, 1, 0, 2, &r);
        CHECK(r.count == 0 && !r.found);
    }
    {   /* 보드 시나리오 — 기대값은 손으로 센 것 (5. 풀이 Part C) */
        static const struct { const char *name; size_t np; uint64_t reserved; uint64_t want; } sc[5] = {
            { "S1 LQFP100, SWD only, HSE",        7, SWD_PINS | HSE_PINS,                           32 },
            { "S2 LQFP100, full JTAG, HSE",       7, JTAG_PINS | HSE_PINS,                           8 },
            { "S3 S1 + USB",                      8, SWD_PINS | HSE_PINS,                            8 },
            { "S4 LQFP48, full JTAG, HSE, USB",   8, JTAG_PINS | HSE_PINS | LQFP48_MISSING,          0 },
            { "S5 LQFP48, SWD only, HSE, USB",    8, SWD_PINS | HSE_PINS | LQFP48_MISSING,           2 },
        };
        int ok = 1;
        printf("Part C board:  scenario                         | count | nodes m0 | m1 | m2\n");
        for (int s = 0; s < 5; s++) {
            pm_result_t r[3];
            for (int mode = 0; mode <= 2; mode++) pinmux_search(g_board, sc[s].np, sc[s].reserved, mode, &r[mode]);
            uint64_t ref = ref_pinmux(g_board, sc[s].np, sc[s].reserved);
            printf("    %-34s | %5llu | %8llu | %3llu | %3llu\n", sc[s].name, (unsigned long long)r[2].count,
                   (unsigned long long)r[0].nodes, (unsigned long long)r[1].nodes, (unsigned long long)r[2].nodes);
            for (int mode = 0; mode <= 2; mode++)
                if (r[mode].count != sc[s].want || ref != sc[s].want ||
                    (r[mode].found && !pm_pick_valid(g_board, sc[s].np, sc[s].reserved, r[mode].pick))) ok = 0;
            if (s == 4 && r[2].found) {
                printf("    S5 first solution (MRV):");
                for (size_t i = 0; i < sc[s].np; i++) printf(" %s=%s", g_board[i].name, g_board[i].optname[r[2].pick[i]]);
                printf("\n");
            }
        }
        CHECK(ok);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
