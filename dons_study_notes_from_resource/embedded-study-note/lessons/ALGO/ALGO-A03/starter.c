/* ALGO-A03 비트마스크 DP: 테스트 포인트 방문 순서(TSP)
 *
 * 플라잉 프로브(flying probe)가 홈(park) 위치 H 에서 출발해 보드의 테스트 포인트 n <= 16 개를 모두 찍는
 * 최소 이동 비용 순서를 Held-Karp 비트마스크 DP 로 구한다.
 *   Part A  dp[mask][last] 채우기 · 답 고르기(open/closed) · 부모 표 역추적, 순열 브루트포스(n <= 9) 대조
 *   Part B  open vs closed, 선행 제약(pre[j] ⊆ mask), 막힌 이동(COST_INF) 과 포화 덧셈, n = 0/1, 비대칭 비용
 *   Part C  거리 함수(유클리드 반올림 / 맨해튼 / 체비쇼프), 16점 보드, 최근접 이웃 + 2-opt 휴리스틱과 최적해 비교
 * 메모리: 정적 표 dp 4 MiB + par 1 MiB (MAXN = 16). 동적 할당 없음. 모든 테스트는 결정적(xorshift32 고정 시드).
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o st && ./st   (solution.c 도 같은 명령)
 */
#include <math.h>   /* 테스트 쪽 기준(sqrt) 에만 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAXN 16
#define FULLMASK(n) ((1u << (n)) - 1u)
typedef uint32_t cost_t;
#define COST_INF UINT32_MAX     /* "갈 수 없음". 합이 2^32-1 이상이 되어도 INF 로 포화 = 오류로 보고 */
#define PAR_HOME 0xFFu          /* 부모 = 홈 H (경로의 첫 점) */

typedef struct {
    int n;                       /* 테스트 포인트 수 0..MAXN. 노드 번호 n = 홈 H */
    cost_t d[MAXN + 1][MAXN + 1];/* d[i][j] = i→j 이동 비용 (비대칭 허용). COST_INF = 금지된 이동 */
    uint16_t pre[MAXN];          /* pre[j] = j 보다 먼저 찍어야 하는 점들의 마스크 (0 = 제약 없음) */
} tsp_t;

/* 포화 덧셈: 어느 쪽이 INF 이거나 합이 INF 이상이면 INF. (오버플로로 작은 값이 되는 일을 막는다) */
static inline cost_t cadd(cost_t a, cost_t b) { return (a >= COST_INF - b) ? COST_INF : a + b; }

/* DP 표 (정적, BSS). dp[mask][j] = H 에서 출발해 mask 의 점을 정확히 한 번씩 찍고 j 에서 끝나는 최소 비용.
 * par[mask][j] = 그 최적 경로에서 j 바로 앞 점 (PAR_HOME = 홈에서 바로 옴). */
static cost_t  g_dp[1u << MAXN][MAXN];
static uint8_t g_par[1u << MAXN][MAXN];
static uint64_t g_trans;         /* 살펴본 전이 (i → j) 후보 수 — 복잡도 n(n-1)2^(n-2) 검사용 */
static cost_t   hk_dp(uint32_t mask, int j)  { return g_dp[mask][j]; }    /* 테스트·demo 용 읽기 */
static unsigned hk_par(uint32_t mask, int j) { return g_par[mask][j]; }

/* ======================= Part A — Held-Karp ======================= */
/* 상태: (mask, j), j ∈ mask.  기저: dp[{j}][j] = d[H][j] (pre[j] == 0 일 때만).
 * 전이: dp[mask][j] = min_{i ∈ mask\{j}} dp[mask\{j}][i] + d[i][j]   (Part B: pre[j] ⊆ mask\{j} 일 때만)
 * 순서: mask 를 숫자 오름차순으로 — mask\{j} < mask 이므로 필요한 값은 항상 먼저 채워져 있다.
 * 동점이면 가장 작은 i (엄격한 <). 후보 (i, j) 하나마다 g_trans++. */
static void hk_fill(const tsp_t *t) {
    (void)t;
    g_trans = 0;
    /* TODO: 표 초기화(INF, PAR_HOME) → 기저 dp[{j}][j] → mask 오름차순으로 전이 (pre[j] ⊆ mask\{j} 검사, g_trans++) */
}

/* 답: open = min_j dp[full][j],  closed = min_j dp[full][j] + d[j][H].  n == 0 이면 0 (홈에 그대로).
 * *last_out = 최적 경로의 마지막 점 (동점이면 가장 작은 j, n == 0 이거나 불가능하면 -1). */
static cost_t hk_answer(const tsp_t *t, int closed, int *last_out) {
    (void)t; (void)closed;
    *last_out = -1;
    return COST_INF; /* TODO: n == 0 → 0. 아니면 min_j dp[full][j] (closed 면 + d[j][H], 포화 덧셈) */
}

/* 부모 표 역추적: (full, last) 에서 par 를 따라 홈까지. order[0..n-1] = 방문 순서 (홈 제외).
 * 반환: n (성공) / -1 (표가 이상함: 범위 밖, mask 에 없는 점, 홈에 너무 일찍/늦게 도착). */
static int hk_backtrack(const tsp_t *t, int last, uint8_t *order) {
    (void)t; (void)last; (void)order;
    return -1; /* TODO: (full, last) 에서 par 를 따라가며 order 를 뒤에서부터 채운다 */
}

/* 묶음: 표 채우기 → 답 → 역추적. 불가능하면 COST_INF (order 는 건드리지 않음). */
static cost_t tsp_solve(const tsp_t *t, int closed, uint8_t *order) {
    hk_fill(t);
    int last;
    cost_t c = hk_answer(t, closed, &last);
    if (c == COST_INF) return COST_INF;
    if (t->n > 0 && hk_backtrack(t, last, order) != t->n) return COST_INF;
    return c;
}

/* 주어진 순서의 비용 (홈 → order[0] → … → order[n-1] (→ 홈)). 포화 덧셈. */
static cost_t tour_cost(const tsp_t *t, int closed, const uint8_t *order) {
    int n = t->n, cur = n;
    cost_t c = 0;
    for (int k = 0; k < n; k++) { c = cadd(c, t->d[cur][order[k]]); cur = order[k]; }
    if (closed) c = cadd(c, t->d[cur][n]);
    return c;
}

/* order 가 0..n-1 의 순열이고 선행 제약을 지키는가 */
static bool tour_valid(const tsp_t *t, const uint8_t *order) {
    uint32_t seen = 0;
    for (int k = 0; k < t->n; k++) {
        int j = order[k];
        if (j >= t->n || ((seen >> j) & 1u)) return false;
        if ((uint32_t)t->pre[j] & ~seen) return false;
        seen |= 1u << j;
    }
    return seen == FULLMASK(t->n);
}

/* ======================= Part C — 거리 함수와 휴리스틱 ======================= */
typedef struct { int32_t x, y; } pt_t;   /* 좌표 단위 10 µm, |x|,|y| <= 2^20 가정 */
typedef enum { MET_EUCLID = 0, MET_MANHATTAN = 1, MET_CHEBYSHEV = 2 } metric_t;
static const char *const metric_name[] = {"euclid", "manhattan", "chebyshev"};

/* floor(sqrt(v)) — 비트 단위 정수 제곱근 (ALGO-M15 에서 자세히) */
static uint64_t isqrt_u64(uint64_t v) {
    uint64_t r = 0, bit = 1ull << 62;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    return r;
}

/* 세 거리. 유클리드는 정수로 반올림: round(sqrt(s)) — 부동소수점 없이.
 *   맨해튼  |dx| + |dy|          (축을 하나씩 움직이는 갠트리)
 *   체비쇼프 max(|dx|, |dy|)      (X/Y 모터가 같은 속도로 동시에 움직이는 갠트리 = 이 레슨의 기본)
 *   유클리드 round(sqrt(dx²+dy²)) (보간 직선 이동, 경로 속도 제한) */
static cost_t metric_dist(pt_t a, pt_t b, metric_t m) {
    (void)a; (void)b; (void)m;
    return 0; /* TODO: |dx|,|dy| 를 uint64 로 → 맨해튼 / 체비쇼프 / 반올림 유클리드 (isqrt_u64 + 반올림 규칙) */
}

/* 점 n 개 + 홈 → 비용 행렬 (pre 는 0 으로) */
static void build_board(tsp_t *t, const pt_t *p, int n, pt_t home, metric_t m) {
    memset(t, 0, sizeof *t);
    t->n = n;
    for (int i = 0; i <= n; i++)
        for (int j = 0; j <= n; j++)
            t->d[i][j] = metric_dist(i == n ? home : p[i], j == n ? home : p[j], m);
}

/* 최근접 이웃(nearest neighbour): 홈에서 시작해 아직 안 찍은 점 중 가장 가까운 점으로 (동점 = 작은 번호).
 * 선행 제약은 무시한다 (Part C 휴리스틱 비교는 제약 없는 보드에서만). 반환 = tour_cost. */
static cost_t nn_tour(const tsp_t *t, int closed, uint8_t *order) {
    (void)closed;
    for (int k = 0; k < t->n; k++) order[k] = (uint8_t)k;   /* TODO: 최근접 이웃 (지금은 번호 순서) */
    return 0;
}

/* 2-opt: order[i..j] 를 뒤집어 비용이 엄격히 줄면 적용, 더 이상 줄지 않을 때까지.
 * 대칭 비용 가정 (뒤집힌 구간 안쪽 간선 비용이 그대로). 바뀌는 간선은 두 개:
 *   (a=앞, b=order[i]) + (c=order[j], e=뒤)  →  (a, c) + (b, e)
 *   a = i==0 ? H : order[i-1],   e = j==n-1 ? (closed ? H : 없음) : order[j+1]
 * 정수 비용이 매번 엄격히 줄므로 반드시 끝난다. 반환 = tour_cost. */
static cost_t two_opt(const tsp_t *t, int closed, uint8_t *order) {
    (void)t; (void)closed; (void)order;
    return 0; /* TODO: 개선되는 뒤집기가 없을 때까지 order[i..j] 뒤집기 (바뀌는 간선 두 개만 비교) */
}

#ifndef ALGO_A03_NO_MAIN
/* ======================= 테스트 하네스 (starter.c 와 동일) ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

/* 기준(reference): 가능한 순열을 전부 (n! 개) — 합은 uint64 로, INF 간선은 그 가지를 버린다 (포화 덧셈과 독립). */
static uint64_t bf_open, bf_closed;
static void bf_rec(const tsp_t *t, uint32_t used, int last, int depth, uint64_t acc) {
    if (depth == t->n) {
        if (acc < bf_open) bf_open = acc;
        uint64_t back = (last == t->n) ? 0 : t->d[last][t->n];          /* n == 0: 홈에서 안 움직임 */
        if (back != COST_INF && acc + back < bf_closed) bf_closed = acc + back;
        return;
    }
    for (int j = 0; j < t->n; j++) {
        if ((used >> j) & 1u) continue;
        if ((uint32_t)t->pre[j] & ~used) continue;
        if (t->d[last][j] == COST_INF) continue;
        bf_rec(t, used | (1u << j), j, depth + 1, acc + t->d[last][j]);
    }
}
static void brute(const tsp_t *t) { bf_open = bf_closed = UINT64_MAX; bf_rec(t, 0, t->n, 0, 0); }
/* DP 결과(uint32, INF = 불가능/포화) 와 기준(uint64) 이 같은가 */
static bool same_cost(cost_t dp, uint64_t bf) {
    if (bf >= COST_INF) return dp == COST_INF;
    return dp == (cost_t)bf;
}
/* 어떤 구간을 뒤집어도 비용이 줄지 않는가 (2-opt 국소 최적) — tour_cost 를 통째로 다시 계산해서 독립 확인 */
static bool is_2opt_local_opt(const tsp_t *t, int closed, const uint8_t *order) {
    cost_t base = tour_cost(t, closed, order);
    uint8_t tmp[MAXN];
    for (int i = 0; i < t->n - 1; i++)
        for (int j = i + 1; j < t->n; j++) {
            memcpy(tmp, order, (size_t)t->n);
            for (int l = i, r = j; l < r; l++, r--) { uint8_t x = tmp[l]; tmp[l] = tmp[r]; tmp[r] = x; }
            if (tour_cost(t, closed, tmp) < base) return false;
        }
    return true;
}
static void print_order(const char *label, const uint8_t *order, int n, const char *const *names) {
    printf("%s H", label);
    for (int k = 0; k < n; k++) printf(" %s", names ? names[order[k]] : (char[4]){(char)('A' + order[k]), 0});
    printf("\n");
}

/* Part C 보드: 100 x 80 mm, 좌표 단위 10 µm. 홈 = 왼쪽 아래 모서리 바깥 (-5 mm, -5 mm) */
static const pt_t BOARD[16] = {
    { 2550, 2900}, { 5300, 1900}, { 2300, 3950}, { 4350, 5050},   /* VBAT 3V3 1V8 VCORE        */
    { 1200, 1050}, {  900, 3000}, { 6650, 6450}, { 9250, 7550},   /* SWDIO SWCLK nRST UART_TX  */
    { 6300,  900}, { 1650, 4300}, { 6100, 7650}, { 9450, 6550},   /* UART_RX CAN_H CAN_L XTAL  */
    { 8500, 4450}, { 3800, 3250}, { 2450, 6050}, { 1800, 4100},   /* GND ADC_IN PWM_A I2C_SDA  */
};
static const char *const TP[16] = {"VBAT", "3V3", "1V8", "VCORE", "SWDIO", "SWCLK", "nRST", "UART_TX",
                                   "UART_RX", "CAN_H", "CAN_L", "XTAL", "GND", "ADC_IN", "PWM_A", "I2C_SDA"};
static const pt_t HOME = {-500, -500};

int main(void) {
    /* ---------- Part A: 손 계산 예 (체비쇼프, 홈 (0,0)) ---------- */
    {
        const pt_t p[4] = {{6, 4}, {4, 2}, {7, 0}, {5, 5}};   /* A B C D */
        tsp_t t; uint8_t o[MAXN] = {0};
        build_board(&t, p, 4, (pt_t){0, 0}, MET_CHEBYSHEV);
        cost_t open = tsp_solve(&t, 0, o);
        CHECK(open == 11);                                   /* H→D→A→B→C = 5+1+2+3 */
        CHECK(o[0] == 3 && o[1] == 0 && o[2] == 1 && o[3] == 2);
        CHECK(g_trans == 48);                                /* n(n-1)2^(n-2) = 4·3·4 */
        CHECK(hk_dp(0x9u, 0) == 6 && hk_dp(0xFu, 2) == 11 && hk_par(0xFu, 2) == 1);   /* {A,D}끝A = H→D→A, C 의 앞 = B */
        print_order("hand open  =11:", o, 4, NULL);
        cost_t closed = tsp_solve(&t, 1, o);
        CHECK(closed == 17);                                 /* B C A D 또는 그 역순 */
        CHECK(tour_cost(&t, 1, o) == 17 && tour_valid(&t, o));
        print_order("hand closed=17:", o, 4, NULL);
        cost_t nn = nn_tour(&t, 0, o);
        CHECK(nn == 12);                                     /* 탐욕: H→B 4 → A 2 → D 1 → C 5 */
        print_order("hand NN    =12:", o, 4, NULL);
    }
    /* ---------- Part A: 무작위 보드 vs 순열 브루트포스 (n = 0..9, 세 거리, open/closed) ---------- */
    {
        uint32_t s = 0xA03u;
        int cases = 0, bad_cost = 0, bad_tour = 0, bad_trans = 0;
        for (int n = 0; n <= 9; n++) {
            int reps = n <= 6 ? 40 : (n == 7 ? 16 : (n == 8 ? 6 : 3));
            for (int r = 0; r < reps; r++)
                for (int m = 0; m < 3; m++) {
                    pt_t p[MAXN]; tsp_t t; uint8_t o[MAXN] = {0};
                    int32_t span = (r & 1) ? 20 : 10000;           /* 작은 격자 → 동점·겹친 점이 자주 나온다 */
                    for (int i = 0; i < n; i++) { p[i].x = (int32_t)(xs32(&s) % (uint32_t)span); p[i].y = (int32_t)(xs32(&s) % (uint32_t)span); }
                    build_board(&t, p, n, (pt_t){0, 0}, (metric_t)m);
                    brute(&t);
                    for (int closed = 0; closed <= 1; closed++) {
                        cost_t c = tsp_solve(&t, closed, o);
                        cases++;
                        if (!same_cost(c, closed ? bf_closed : bf_open)) bad_cost++;
                        if (n > 0 && (!tour_valid(&t, o) || tour_cost(&t, closed, o) != c)) bad_tour++;
                        if (n >= 2 && g_trans != (uint64_t)n * (uint64_t)(n - 1) << (n - 2)) bad_trans++;
                    }
                }
        }
        printf("random boards: %d cases (n=0..9, 3 metrics, open+closed): cost mismatches=%d bad tours=%d bad transition counts=%d\n",
               cases, bad_cost, bad_tour, bad_trans);
        CHECK(bad_cost == 0);
        CHECK(bad_tour == 0);
        CHECK(bad_trans == 0);
    }
    /* ---------- Part A: 비대칭 비용 행렬 (DP 는 대칭을 쓰지 않는다) ---------- */
    {
        uint32_t s = 77u;
        int bad = 0, asym = 0;
        for (int r = 0; r < 60; r++) {
            tsp_t t; uint8_t o[MAXN] = {0};
            memset(&t, 0, sizeof t);
            t.n = 1 + r % 8;
            for (int i = 0; i <= t.n; i++)
                for (int j = 0; j <= t.n; j++) t.d[i][j] = (i == j) ? 0 : xs32(&s) % 1000u;
            if (t.d[0][1] != t.d[1][0]) asym++;
            brute(&t);
            for (int closed = 0; closed <= 1; closed++) {
                cost_t c = tsp_solve(&t, closed, o);
                if (!same_cost(c, closed ? bf_closed : bf_open) || !tour_valid(&t, o) || tour_cost(&t, closed, o) != c) bad++;
            }
        }
        printf("asymmetric matrices: 60 instances (%d with d[0][1] != d[1][0]), mismatches=%d\n", asym, bad);
        CHECK(bad == 0 && asym > 40);
    }
    /* ---------- Part B: 경계 — n = 0, n = 1, open <= closed ---------- */
    {
        tsp_t t; uint8_t o[MAXN] = {0};
        memset(&t, 0, sizeof t);
        CHECK(tsp_solve(&t, 0, o) == 0 && tsp_solve(&t, 1, o) == 0);   /* 찍을 점 없음 = 이동 0 */
        t.n = 1; t.d[1][0] = 7; t.d[0][1] = 11;                          /* H→P0 = 7, P0→H = 11 */
        CHECK(tsp_solve(&t, 0, o) == 7 && o[0] == 0);
        CHECK(tsp_solve(&t, 1, o) == 18);
        uint32_t s = 4242u;
        int viol = 0;
        for (int r = 0; r < 200; r++) {
            pt_t p[MAXN];
            int n = 1 + r % 10;
            for (int i = 0; i < n; i++) { p[i].x = (int32_t)(xs32(&s) % 5000u); p[i].y = (int32_t)(xs32(&s) % 5000u); }
            build_board(&t, p, n, (pt_t){0, 0}, MET_CHEBYSHEV);
            cost_t op = tsp_solve(&t, 0, o);
            int lastop = o[n - 1];
            cost_t cl = tsp_solve(&t, 1, o);
            cost_t minback = COST_INF;
            for (int j = 0; j < n; j++) if (t.d[j][n] < minback) minback = t.d[j][n];
            /* open + min_j d[j][H] <= closed <= open + d[open 의 마지막][H] */
            if (op == COST_INF || !(op + minback <= cl && cl <= op + t.d[lastop][n])) viol++;
        }
        CHECK(viol == 0);
    }
    /* ---------- Part B: 선행 제약 vs 브루트포스, 순환 제약 = 불가능 ---------- */
    {
        uint32_t s = 909u;
        int bad = 0, viol = 0, infeasible = 0;
        for (int r = 0; r < 120; r++) {
            pt_t p[MAXN]; tsp_t t; uint8_t o[MAXN] = {0};
            int n = 2 + r % 7;
            for (int i = 0; i < n; i++) { p[i].x = (int32_t)(xs32(&s) % 3000u); p[i].y = (int32_t)(xs32(&s) % 3000u); }
            build_board(&t, p, n, (pt_t){0, 0}, MET_EUCLID);
            for (int k = 0; k < 2; k++) {                               /* 제약 2개: a 보다 b 가 먼저 */
                int a = (int)(xs32(&s) % (uint32_t)n), b = (int)(xs32(&s) % (uint32_t)n);
                if (a != b) t.pre[a] |= (uint16_t)(1u << b);
            }
            brute(&t);
            for (int closed = 0; closed <= 1; closed++) {
                cost_t c = tsp_solve(&t, closed, o);
                if (!same_cost(c, closed ? bf_closed : bf_open)) bad++;
                if (c == COST_INF) infeasible++;
                else if (!tour_valid(&t, o) || tour_cost(&t, closed, o) != c) viol++;
            }
        }
        printf("precedence: 120 instances, mismatches=%d invalid tours=%d infeasible (cyclic constraints)=%d\n", bad, viol, infeasible);
        CHECK(bad == 0);
        CHECK(viol == 0 && infeasible > 0 && infeasible < 60);
        tsp_t t; uint8_t o[MAXN] = {0};
        const pt_t p[3] = {{1, 0}, {2, 0}, {3, 0}};
        build_board(&t, p, 3, (pt_t){0, 0}, MET_MANHATTAN);
        t.pre[0] = 1u << 2;                                             /* P0 은 P2 다음에만 */
        CHECK(tsp_solve(&t, 0, o) == 5 && tour_valid(&t, o));   /* 제약 없으면 3 (P0 P1 P2) — 제약이 2 를 더한다 */
        t.pre[2] = 1u << 0;                                             /* P2 는 P0 다음에만 → 순환 */
        CHECK(tsp_solve(&t, 0, o) == COST_INF);
    }
    /* ---------- Part B: 막힌 이동(INF) 과 큰 비용 — 포화 덧셈 vs uint64 기준 ---------- */
    {
        uint32_t s = 31337u;
        int bad = 0, infeasible = 0;
        for (int r = 0; r < 120; r++) {
            tsp_t t; uint8_t o[MAXN] = {0};
            memset(&t, 0, sizeof t);
            t.n = 1 + r % 8;
            for (int i = 0; i <= t.n; i++)
                for (int j = 0; j <= t.n; j++) {
                    uint32_t x = xs32(&s);
                    t.d[i][j] = (i == j) ? 0 : (x % 5u == 0 ? COST_INF : (x >> 4) % (1u << 27));   /* 20% 금지, 나머지 < 2^27 */
                }
            brute(&t);
            for (int closed = 0; closed <= 1; closed++) {
                cost_t c = tsp_solve(&t, closed, o);
                if (!same_cost(c, closed ? bf_closed : bf_open)) bad++;
                else if (c != COST_INF && (!tour_valid(&t, o) || tour_cost(&t, closed, o) != c)) bad++;
                if (c == COST_INF) infeasible++;
            }
        }
        printf("blocked moves + costs < 2^27: 240 cases, mismatches=%d, infeasible=%d\n", bad, infeasible);
        CHECK(bad == 0 && infeasible > 0);
        tsp_t t; uint8_t o[MAXN] = {0};
        memset(&t, 0, sizeof t);
        t.n = 2;
        for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) t.d[i][j] = i == j ? 0 : 0x80000000u;
        CHECK(tsp_solve(&t, 0, o) == COST_INF);   /* 참값 2^32 > uint32 → 작은 값으로 감기지 않고 INF */
    }
    /* ---------- Part C: 거리 함수 ---------- */
    {
        int sq_bad = 0;                                      /* 주어진 isqrt_u64 확인 (k², k²-1 경계) */
        for (uint64_t k = 1; k < 3000000; k += 997) sq_bad += isqrt_u64(k * k) != k || isqrt_u64(k * k - 1) != k - 1;
        CHECK(sq_bad == 0 && isqrt_u64(0) == 0 && isqrt_u64(UINT64_MAX) == 0xFFFFFFFFu);
        pt_t o0 = {0, 0};
        CHECK(metric_dist(o0, (pt_t){3, 4}, MET_EUCLID) == 5 && metric_dist(o0, (pt_t){3, 4}, MET_MANHATTAN) == 7 &&
              metric_dist(o0, (pt_t){3, 4}, MET_CHEBYSHEV) == 4);
        CHECK(metric_dist(o0, (pt_t){2, 1}, MET_EUCLID) == 2 && metric_dist(o0, (pt_t){7, 1}, MET_EUCLID) == 7 &&
              metric_dist(o0, (pt_t){-5, 7}, MET_CHEBYSHEV) == 7 && metric_dist((pt_t){-3, -3}, (pt_t){4, -1}, MET_MANHATTAN) == 9);
        /* 반올림한 유클리드는 삼각 부등식을 깬다: d(0,(2,2)) = 3 > d(0,(1,1)) + d((1,1),(2,2)) = 1 + 1 */
        CHECK(metric_dist(o0, (pt_t){2, 2}, MET_EUCLID) == 3 &&
              metric_dist(o0, (pt_t){2, 2}, MET_EUCLID) >
              metric_dist(o0, (pt_t){1, 1}, MET_EUCLID) + metric_dist((pt_t){1, 1}, (pt_t){2, 2}, MET_EUCLID));
        uint32_t s = 2024u;
        int bad = 0;
        for (int r = 0; r < 200000; r++) {   /* 기준: floor(sqrt(double) + 0.5) — s < 2^43 에서 정확 (md 5. 풀이) */
            pt_t a = {(int32_t)(xs32(&s) % (1u << 21)) - (1 << 20), (int32_t)(xs32(&s) % (1u << 21)) - (1 << 20)};
            pt_t b = {(int32_t)(xs32(&s) % (1u << 21)) - (1 << 20), (int32_t)(xs32(&s) % (1u << 21)) - (1 << 20)};
            if (r & 1) { b.x = a.x + (int32_t)(xs32(&s) % 64u); b.y = a.y + (int32_t)(xs32(&s) % 64u); }
            double dx = (double)a.x - b.x, dy = (double)a.y - b.y;
            cost_t ref = (cost_t)floor(sqrt(dx * dx + dy * dy) + 0.5);
            if (metric_dist(a, b, MET_EUCLID) != ref || metric_dist(b, a, MET_EUCLID) != ref) bad++;
        }
        CHECK(bad == 0);
    }
    /* ---------- Part C: 16점 보드 (체비쇼프 = X/Y 동시 이동 갠트리) ---------- */
    {
        tsp_t t; uint8_t o[MAXN] = {0}, h[MAXN] = {0};
        build_board(&t, BOARD, 16, HOME, MET_CHEBYSHEV);
        cost_t open = tsp_solve(&t, 0, o);
        CHECK(tour_valid(&t, o) && tour_cost(&t, 0, o) == open);
        CHECK(g_trans == 16ull * 15ull << 14);                   /* 3 932 160 */
        CHECK(is_2opt_local_opt(&t, 0, o));                      /* 최적이면 어떤 뒤집기도 못 줄인다 (필요조건) */
        cost_t nn = nn_tour(&t, 0, h), nn2 = two_opt(&t, 0, h);
        CHECK(open <= nn2 && nn2 <= nn && is_2opt_local_opt(&t, 0, h));
        printf("board 16 TP chebyshev: open optimal %.2f mm, NN %.2f mm, NN+2opt %.2f mm\n", open / 100.0, nn / 100.0, nn2 / 100.0);
        print_order("  optimal open:", o, 16, TP);
        cost_t closed = tsp_solve(&t, 1, o);
        CHECK(tour_valid(&t, o) && tour_cost(&t, 1, o) == closed && closed >= open);
        printf("  closed (return home) optimal %.2f mm\n", closed / 100.0);
        /* 선행 제약: 전원 레일 4개(VBAT 3V3 1V8 VCORE)를 다른 모든 점보다 먼저 */
        for (int j = 4; j < 16; j++) t.pre[j] = 0x000F;
        cost_t pc = tsp_solve(&t, 0, o);
        CHECK(pc != COST_INF && tour_valid(&t, o) && tour_cost(&t, 0, o) == pc && pc >= open);
        printf("  rails-first open optimal %.2f mm (+%.2f mm for the constraint)\n", pc / 100.0, (pc - open) / 100.0);
        print_order("  rails-first:", o, 16, TP);
    }
    /* ---------- Part C: 거리 함수가 바뀌면 최적 순서도 바뀐다 (보드, 교차 평가) ---------- */
    {
        tsp_t tm[3]; uint8_t ord[3][MAXN] = {{0}};
        cost_t opt[3], x[3][3];
        for (int m = 0; m < 3; m++) { build_board(&tm[m], BOARD, 16, HOME, (metric_t)m); opt[m] = tsp_solve(&tm[m], 0, ord[m]); }
        int ok = 1;
        printf("  order optimized for \\ evaluated under:  euclid  manhattan  chebyshev  (mm)\n");
        for (int a = 0; a < 3; a++) {
            printf("  %-10s", metric_name[a]);
            for (int b = 0; b < 3; b++) {
                x[a][b] = tour_cost(&tm[b], 0, ord[a]);
                if (x[a][b] < opt[b]) ok = 0;                 /* 다른 거리로 최적화한 순서가 더 좋을 수 없다 */
                printf(" %10.2f", x[a][b] / 100.0);
            }
            printf("\n");
        }
        CHECK(ok && x[0][0] == opt[0] && x[1][1] == opt[1] && x[2][2] == opt[2]);
    }
    /* ---------- Part C: 휴리스틱 vs 최적 — 무작위 보드 300장 (n = 10, 체비쇼프, open) ---------- */
    {
        uint32_t s = 555u;
        int viol = 0, nn_opt = 0, two_opt_opt = 0;
        double nn_sum = 0, tw_sum = 0, nn_max = 0, tw_max = 0;
        for (int r = 0; r < 300; r++) {
            pt_t p[MAXN]; tsp_t t; uint8_t o[MAXN] = {0}, h[MAXN] = {0};
            for (int i = 0; i < 10; i++) { p[i].x = (int32_t)(xs32(&s) % 10001u); p[i].y = (int32_t)(xs32(&s) % 8001u); }
            build_board(&t, p, 10, HOME, MET_CHEBYSHEV);
            cost_t op = tsp_solve(&t, 0, o), nn = nn_tour(&t, 0, h);
            int nnvalid = tour_valid(&t, h);
            cost_t tw = two_opt(&t, 0, h);
            if (!nnvalid || !tour_valid(&t, h) || !(op <= tw && tw <= nn) || !is_2opt_local_opt(&t, 0, h)) viol++;
            double g1 = 100.0 * (nn - op) / op, g2 = 100.0 * (tw - op) / op;
            nn_sum += g1; tw_sum += g2;
            if (g1 > nn_max) nn_max = g1;
            if (g2 > tw_max) tw_max = g2;
            nn_opt += (nn == op); two_opt_opt += (tw == op);
        }
        printf("heuristics, 300 boards n=10: NN gap mean %.2f%% max %.2f%% (optimal %d/300); NN+2opt gap mean %.2f%% max %.2f%% (optimal %d/300)\n",
               nn_sum / 300, nn_max, nn_opt, tw_sum / 300, tw_max, two_opt_opt);
        CHECK(viol == 0);
    }
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
#endif /* ALGO_A03_NO_MAIN */
