/* ARCH-M07 캐시 친화 코드: 루프 순서와 타일링 — 체크 테스트 (결정적인 것만)
 *
 *   Part A  행렬 곱 4가지 루프: ijk (주어짐) / ikj / jki / 타일링. 행 우선(row-major), 원소 (r,c) = M[r*ld + c],
 *           ld (leading dimension) >= n. 정수값 double 이라 덧셈 순서가 바뀌어도 결과가 비트 단위로 같다.
 *   Part B  축소 N-way LRU 캐시 시뮬레이터 (ARCH-M04/M05 모델을 축소해 넣었다) + 각 루프가 내는 주소 트레이스.
 *           M6 L1D 파라미터 (hw.perflevel0.l1dcachesize 128 KB, hw.cachelinesize 128 B, 8-way 는 가정) 에서
 *           n = 128 미스 수의 순서 jki > ijk > ikj > tiled 를 확인한다.
 *   Part C  Cortex-M7 류 D-cache (16 KB, 4-way, 32 B 라인, float): ld 패딩 (행 간격 = 홀수 개 라인) 과
 *           시뮬레이터로 타일 크기 고르기.
 *
 * 시간은 CHECK 하지 않는다 → demo_bench.c (M6 실측 GFLOP/s), demo_sim.c (큰 시뮬레이션 표).
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; printf("PASS  %s\n", #expr); } \
                         else printf("FAIL  %s  (line %d)\n", #expr, __LINE__); } while (0)

static int imin(int a, int b) { return a < b ? a : b; }

/* ======================= Part A — 루프 순서와 타일링 ======================= */
/* C = A·B (n×n). 모든 함수는 C 의 [0,n)×[0,n) 를 덮어쓰고, 패딩 열 (c >= n) 은 건드리지 않는다. */

/* 주어짐: 교과서 그대로. 안쪽 k 루프가 B 를 "열" 방향 (간격 ld*8 B) 으로 걷는다. */
static void mm_ijk(int n, int ld, const double *restrict A, const double *restrict B, double *restrict C) {
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++) {
            double s = 0.0;
            for (int k = 0; k < n; k++) s += A[i * ld + k] * B[k * ld + j];
            C[i * ld + j] = s;
        }
}

/* TODO: ikj — i 마다 C 의 i 행을 0 으로 지운 뒤, k 마다 r = A[i][k] 를 잡고 j 로 C[i][j] += r * B[k][j].
 *       안쪽 루프가 B 의 k 행과 C 의 i 행을 연속으로 걷는다. */
static void mm_ikj(int n, int ld, const double *restrict A, const double *restrict B, double *restrict C) {
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) C[i * ld + j] = 0.0;
        for (int k = 0; k < n; k++) {
            double r = A[i * ld + k];
            for (int j = 0; j < n; j++) C[i * ld + j] += r * B[k * ld + j];
        }
    }
}

/* TODO: jki — j 마다 C 의 j 열을 0 으로 지운 뒤, k 마다 r = B[k][j] 를 잡고 i 로 C[i][j] += A[i][k] * r.
 *       안쪽 루프가 A 와 C 를 둘 다 열 방향으로 걷는다 (행 우선 배열에서 최악). */
static void mm_jki(int n, int ld, const double *restrict A, const double *restrict B, double *restrict C) {
    for (int j = 0; j < n; j++) {
        for (int i = 0; i < n; i++) C[i * ld + j] = 0.0;
        for (int k = 0; k < n; k++) {
            double r = B[k * ld + j];
            for (int i = 0; i < n; i++) C[i * ld + j] += A[i * ld + k] * r;
        }
    }
}

/* TODO: 타일링 — C 전체를 0 으로 지운 뒤, 블록 루프 ii, jj, kk (간격 T) 안에서 ikj:
 *         for i in [ii, min(ii+T,n)) for k in [kk, min(kk+T,n)) { r = A[i][k]; for j in [jj, min(jj+T,n)) C[i][j] += r*B[k][j]; }
 *       ii → jj → kk 순서라서 C 타일 하나가 kk 루프 내내 캐시에 머문다. T 가 n 의 약수가 아니어도 맞아야 한다. */
static void mm_tiled(int n, int ld, int T, const double *restrict A, const double *restrict B, double *restrict C) {
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++) C[i * ld + j] = 0.0;
    for (int ii = 0; ii < n; ii += T)
        for (int jj = 0; jj < n; jj += T)
            for (int kk = 0; kk < n; kk += T) {
                int ie = imin(ii + T, n), je = imin(jj + T, n), ke = imin(kk + T, n);
                for (int i = ii; i < ie; i++)
                    for (int k = kk; k < ke; k++) {
                        double r = A[i * ld + k];
                        for (int j = jj; j < je; j++) C[i * ld + j] += r * B[k * ld + j];
                    }
            }
}

/* ======================= Part B — 축소 LRU 캐시 시뮬레이터 + 주소 트레이스 ======================= */
/* ARCH-M04/M05 공통 규칙: 바이트 주소 uint32_t, 용량 C·라인 B·연관도 W 모두 2의 거듭제곱, S = C/(B·W),
 * offset = log2 B 비트, index = log2 S 비트, tag = 나머지. 쓰기는 write-allocate 로 읽기와 똑같이 센다
 * (dirty/쓰기 정책은 ARCH-M06 의 몫). */
typedef struct { uint32_t addr; char rw; uint8_t size; } trace_rec_t;

typedef struct {
    uint32_t cap, line, ways, sets;
    unsigned off_bits, idx_bits;
    uint32_t *tag;      /* [sets * ways] */
    uint8_t *valid;     /* [sets * ways] */
    uint64_t *last;     /* [sets * ways] 마지막 사용 시각 (LRU) */
    uint64_t tick, accesses, hits, misses;
} cache_t;

static bool is_pow2(uint32_t x) { return x != 0 && (x & (x - 1)) == 0; }
static unsigned ilog2u(uint32_t x) { unsigned r = 0; while (x > 1) { x >>= 1; r++; } return r; }

/* 주어짐 */
static bool cache_init(cache_t *c, uint32_t cap, uint32_t line, uint32_t ways) {
    memset(c, 0, sizeof *c);
    if (!is_pow2(cap) || !is_pow2(line) || !is_pow2(ways) || cap < line * ways) return false;
    c->cap = cap; c->line = line; c->ways = ways; c->sets = cap / (line * ways);
    c->off_bits = ilog2u(line); c->idx_bits = ilog2u(c->sets);
    size_t nl = (size_t)c->sets * ways;
    c->tag = calloc(nl, sizeof *c->tag);
    c->valid = calloc(nl, 1);
    c->last = calloc(nl, sizeof *c->last);
    return c->tag && c->valid && c->last;
}
static void cache_free(cache_t *c) { free(c->tag); free(c->valid); free(c->last); memset(c, 0, sizeof *c); }

/* TODO: 접근 하나. hit 이면 true.
 *   set = (addr >> off_bits) & (sets-1), tag = addr >> (off_bits + idx_bits).
 *   hit → 그 way 의 last 를 지금 시각으로. miss → 빈 way (가장 낮은 번호) 가 있으면 거기, 없으면 last 가 가장
 *   작은 way 를 내쫓고 채운다. accesses/hits/misses 를 센다. (r->size 는 라인을 넘지 않는다 — 트레이스 규칙) */
static bool cache_access(cache_t *c, const trace_rec_t *r) {
    uint32_t set = (r->addr >> c->off_bits) & (c->sets - 1);
    uint32_t tag = (uint32_t)((uint64_t)r->addr >> (c->off_bits + c->idx_bits));
    size_t base = (size_t)set * c->ways;
    c->accesses++;
    c->tick++;
    size_t victim = base;
    uint64_t oldest = UINT64_MAX;
    for (size_t w = base; w < base + c->ways; w++) {
        if (c->valid[w] && c->tag[w] == tag) {
            c->last[w] = c->tick;
            c->hits++;
            return true;
        }
        uint64_t key = c->valid[w] ? c->last[w] : 0;   /* 빈 way 가 먼저 */
        if (key < oldest) { oldest = key; victim = w; }
    }
    c->misses++;
    c->valid[victim] = 1;
    c->tag[victim] = tag;
    c->last[victim] = c->tick;
    return false;
}

/* 세 행렬의 배치: A, B, C 를 0x1000_0000 부터 붙여 놓는다 (각각 n*ld*elem 바이트). */
typedef struct { uint32_t a, b, c; int n, ld; uint32_t elem; } mm_layout_t;
static mm_layout_t mm_layout(int n, int ld, uint32_t elem) {
    uint32_t bytes = (uint32_t)n * (uint32_t)ld * elem;
    return (mm_layout_t){ 0x10000000u, 0x10000000u + bytes, 0x10000000u + 2 * bytes, n, ld, elem };
}

enum { V_IJK, V_IKJ, V_JKI, V_TILED, V_COUNT };
static const char *const vname[V_COUNT] = { "ijk", "ikj", "jki", "tiled" };

static void emit(cache_t *c, uint32_t base, const mm_layout_t *L, int row, int col, char rw) {
    trace_rec_t r = { base + ((uint32_t)row * (uint32_t)L->ld + (uint32_t)col) * L->elem, rw, (uint8_t)L->elem };
    cache_access(c, &r);
}

/* 각 mm_* 함수가 내는 메모리 접근을 같은 순서로 시뮬레이터에 넣는다 ("컴파일러 모델"):
 *   스칼라 지역 변수 (s, r) 는 레지스터 — 접근 없음. C[i][j] += x 는 읽기 'R' 다음 쓰기 'W'.
 *   0 으로 지우기는 쓰기 'W' 하나. A 는 L->a, B 는 L->b, C 는 L->c. T 는 V_TILED 일 때만 쓴다.
 * ijk 는 주어짐. TODO: ikj / jki / tiled 케이스. */
static void trace_matmul(cache_t *c, const mm_layout_t *L, int variant, int T) {
    int n = L->n;
    switch (variant) {
    case V_IJK:
        for (int i = 0; i < n; i++)
            for (int j = 0; j < n; j++) {
                for (int k = 0; k < n; k++) { emit(c, L->a, L, i, k, 'R'); emit(c, L->b, L, k, j, 'R'); }
                emit(c, L->c, L, i, j, 'W');
            }
        break;
    case V_IKJ:
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < n; j++) emit(c, L->c, L, i, j, 'W');
            for (int k = 0; k < n; k++) {
                emit(c, L->a, L, i, k, 'R');
                for (int j = 0; j < n; j++) {
                    emit(c, L->b, L, k, j, 'R'); emit(c, L->c, L, i, j, 'R'); emit(c, L->c, L, i, j, 'W');
                }
            }
        }
        break;
    case V_JKI:
        for (int j = 0; j < n; j++) {
            for (int i = 0; i < n; i++) emit(c, L->c, L, i, j, 'W');
            for (int k = 0; k < n; k++) {
                emit(c, L->b, L, k, j, 'R');
                for (int i = 0; i < n; i++) {
                    emit(c, L->a, L, i, k, 'R'); emit(c, L->c, L, i, j, 'R'); emit(c, L->c, L, i, j, 'W');
                }
            }
        }
        break;
    case V_TILED:
        for (int i = 0; i < n; i++)
            for (int j = 0; j < n; j++) emit(c, L->c, L, i, j, 'W');
        for (int ii = 0; ii < n; ii += T)
            for (int jj = 0; jj < n; jj += T)
                for (int kk = 0; kk < n; kk += T) {
                    int ie = imin(ii + T, n), je = imin(jj + T, n), ke = imin(kk + T, n);
                    for (int i = ii; i < ie; i++)
                        for (int k = kk; k < ke; k++) {
                            emit(c, L->a, L, i, k, 'R');
                            for (int j = jj; j < je; j++) {
                                emit(c, L->b, L, k, j, 'R'); emit(c, L->c, L, i, j, 'R'); emit(c, L->c, L, i, j, 'W');
                            }
                        }
                }
        break;
    default:
        break;
    }
}

/* 새 캐시 하나에 트레이스를 넣고 (accesses, misses) 를 돌려준다. 주어짐. */
typedef struct { uint64_t accesses, misses; } simres_t;
static simres_t sim_run(uint32_t cap, uint32_t line, uint32_t ways, int n, int ld, uint32_t elem, int variant, int T) {
    cache_t c;
    simres_t res = { 0, 0 };
    if (cache_init(&c, cap, line, ways)) {
        mm_layout_t L = mm_layout(n, ld, elem);
        trace_matmul(&c, &L, variant, T);
        res.accesses = c.accesses;
        res.misses = c.misses;
    }
    cache_free(&c);
    return res;
}

/* ======================= Part C — 펌웨어: ld 패딩과 타일 고르기 ======================= */
/* TODO: n 이상인 가장 작은 ld 로, 행 간격 ld*elem 이 라인 크기의 배수이면서 (행이 라인에 정렬)
 *       행 간격의 라인 수 ld*elem/line 이 홀수인 것. (홀수 → 열을 걸을 때 모든 set 을 돈다) */
static int pad_ld(int n, uint32_t elem, uint32_t line) {
    for (int ld = n;; ld++) {
        uint64_t bytes = (uint64_t)ld * elem;
        if (bytes % line == 0 && (bytes / line) % 2 == 1) return ld;
    }
}

/* TODO: 후보 타일 크기 cand[0..nc) 마다 sim_run(..., V_TILED, T) 의 misses 를 재고 가장 적은 T 를 돌려준다.
 *       같으면 더 큰 T (루프 오버헤드가 적다). misses_out != NULL 이면 후보별 미스 수를 적어 준다. nc < 1 → 0. */
static int best_tile(uint32_t cap, uint32_t line, uint32_t ways, int n, int ld, uint32_t elem,
                     const int *cand, int nc, uint64_t *misses_out) {
    int best = 0;
    uint64_t best_m = UINT64_MAX;
    for (int i = 0; i < nc; i++) {
        uint64_t m = sim_run(cap, line, ways, n, ld, elem, V_TILED, cand[i]).misses;
        if (misses_out) misses_out[i] = m;
        if (m < best_m || (m == best_m && cand[i] > best)) { best_m = m; best = cand[i]; }
    }
    return best;
}

/* ======================= 테스트 ======================= */
static uint32_t g_rng = 2463534242u;
static uint32_t xorshift32(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }

#define SENT 12345.0   /* 패딩 열과 "아직 안 쓴" C 를 표시하는 값 */
#define MAXN 100
#define MAXLD 104

static double gA[MAXN * MAXLD], gB[MAXN * MAXLD], gC[MAXN * MAXLD], gR[MAXN * MAXLD];

static void fill_inputs(int n, int ld) {
    for (int i = 0; i < n * ld; i++) {
        gA[i] = (double)((int)(xorshift32() % 9) - 4);   /* -4..4 정수: 곱·합이 전부 정확 */
        gB[i] = (double)((int)(xorshift32() % 9) - 4);
    }
}
static void fill_c(int n, int ld) { for (int i = 0; i < n * ld; i++) gC[i] = SENT; }
static bool same_as_ref(int n, int ld) {
    for (int i = 0; i < n; i++)
        for (int c = 0; c < ld; c++) {
            double want = c < n ? gR[i * ld + c] : SENT;
            if (gC[i * ld + c] != want) return false;
        }
    return true;
}

/* 하나의 (n, ld) 에서 세 변형 + 여러 T 를 ijk 기준과 비교 */
static bool variants_match(int n, int ld) {
    static const int Ts[] = { 1, 3, 4, 8, 16, 32, 50, 128 };
    fill_inputs(n, ld);
    for (int i = 0; i < n * ld; i++) gR[i] = SENT;
    mm_ijk(n, ld, gA, gB, gR);
    fill_c(n, ld); mm_ikj(n, ld, gA, gB, gC);
    if (!same_as_ref(n, ld)) return false;
    fill_c(n, ld); mm_jki(n, ld, gA, gB, gC);
    if (!same_as_ref(n, ld)) return false;
    for (size_t t = 0; t < sizeof Ts / sizeof Ts[0]; t++) {
        fill_c(n, ld); mm_tiled(n, ld, Ts[t], gA, gB, gC);
        if (!same_as_ref(n, ld)) return false;
    }
    return true;
}

int main(void) {
    printf("-- Part A: loop orders and tiling (exact vs ijk) --\n");
    {
        /* 손 계산: [[1,2],[3,4]]·[[5,6],[7,8]] = [[19,22],[43,50]] */
        double A2[4] = { 1, 2, 3, 4 }, B2[4] = { 5, 6, 7, 8 }, C2[4] = { 0 };
        mm_ikj(2, 2, A2, B2, C2);
        CHECK(C2[0] == 19 && C2[1] == 22 && C2[2] == 43 && C2[3] == 50);
        memset(C2, 0, sizeof C2); mm_jki(2, 2, A2, B2, C2);
        CHECK(C2[0] == 19 && C2[1] == 22 && C2[2] == 43 && C2[3] == 50);
        memset(C2, 0, sizeof C2); mm_tiled(2, 2, 1, A2, B2, C2);
        CHECK(C2[0] == 19 && C2[1] == 22 && C2[2] == 43 && C2[3] == 50);
        /* C 에 쓰레기가 있어도 덮어써야 한다 (누적 버그 잡기) */
        double C3[4] = { 99, 99, 99, 99 };
        mm_ikj(2, 2, A2, B2, C3);
        CHECK(C3[0] == 19 && C3[3] == 50);
        C3[0] = C3[3] = 99; mm_tiled(2, 2, 2, A2, B2, C3);
        CHECK(C3[0] == 19 && C3[3] == 50);
    }
    CHECK(variants_match(1, 1));
    CHECK(variants_match(7, 7));
    CHECK(variants_match(16, 16));
    CHECK(variants_match(33, 33));
    CHECK(variants_match(64, 64));
    CHECK(variants_match(100, 100));
    CHECK(variants_match(33, 40));    /* ld > n: 패딩 열은 그대로 SENT */
    CHECK(variants_match(100, 104));

    printf("-- Part B: compact LRU cache sim + matmul address traces --\n");
    {
        cache_t c;
        CHECK(cache_init(&c, 128 * 1024, 128, 8));
        CHECK(c.sets == 128 && c.off_bits == 7 && c.idx_bits == 7);
        cache_free(&c);
        CHECK(!cache_init(&c, 1000, 64, 2));
        cache_free(&c);

        /* 2 sets × 2 ways × 64 B. 0x000/0x080/0x100 은 모두 set 0, 0x040 은 set 1 */
        CHECK(cache_init(&c, 256, 64, 2));
        static const uint32_t seq[] = { 0x000, 0x080, 0x000, 0x100, 0x080, 0x100, 0x040, 0x07f };
        char pat[16] = { 0 };
        for (int i = 0; i < 8; i++) {
            trace_rec_t r = { seq[i], 'R', 1 };
            pat[i] = cache_access(&c, &r) ? 'H' : 'M';
        }
        printf("      LRU 2-way pattern: %s\n", pat);
        CHECK(strcmp(pat, "MMHMMHMH") == 0);
        CHECK(c.accesses == 8 && c.hits == 3 && c.misses == 5);
        cache_free(&c);
    }
    {
        /* 접근 수 (n=8, T=4): ijk 2n^3+n^2, ikj = jki = 3n^3+2n^2, tiled = 3n^3 + n^2 + n^2*ceil(n/T) */
        uint32_t cap = 128 * 1024, line = 128, ways = 8;
        CHECK(sim_run(cap, line, ways, 8, 8, 8, V_IJK, 0).accesses == 2 * 512 + 64);
        CHECK(sim_run(cap, line, ways, 8, 8, 8, V_IKJ, 0).accesses == 3 * 512 + 2 * 64);
        CHECK(sim_run(cap, line, ways, 8, 8, 8, V_JKI, 0).accesses == 3 * 512 + 2 * 64);
        CHECK(sim_run(cap, line, ways, 8, 8, 8, V_TILED, 4).accesses == 3 * 512 + 64 + 64 * 2);
        CHECK(sim_run(cap, line, ways, 10, 10, 8, V_TILED, 4).accesses == 3 * 1000 + 100 + 100 * 3);
        /* 다 들어가면 compulsory 뿐: n=16 double → 행렬당 2 KB = 16 라인, 셋이면 48 */
        bool all48 = true;
        for (int v = 0; v < V_COUNT; v++) all48 = all48 && sim_run(cap, line, ways, 16, 16, 8, v, 8).misses == 48;
        CHECK(all48);
    }
    {
        /* M6 L1D (Super 코어 hw.perflevel0.l1dcachesize = 128 KB, 128 B 라인, 8-way 가정), n = 128 double.
         * 행렬 하나가 128 KB = L1 전체. */
        uint32_t cap = 128 * 1024, line = 128, ways = 8;
        int n = 128;
        simres_t r[V_COUNT];
        for (int v = 0; v < V_COUNT; v++) {
            r[v] = sim_run(cap, line, ways, n, n, 8, v, 32);
            printf("      M6-L1 n=%d %-5s accesses %9llu misses %8llu  miss rate %6.3f%%  misses/n^3 %.4f\n", n, vname[v],
                   (unsigned long long)r[v].accesses, (unsigned long long)r[v].misses,
                   100.0 * (double)r[v].misses / (double)(r[v].accesses ? r[v].accesses : 1), (double)r[v].misses / ((double)n * n * n));
        }
        CHECK(r[V_JKI].misses > r[V_IJK].misses);
        CHECK(r[V_IJK].misses > r[V_IKJ].misses);
        CHECK(r[V_IKJ].misses > r[V_TILED].misses);
    }

    printf("-- Part C: firmware D-cache (16 KB, 4-way, 32 B, float): ld padding and tile choice --\n");
    {
        CHECK(pad_ld(64, 4, 32) == 72);
        CHECK(pad_ld(128, 8, 128) == 144);
        CHECK(pad_ld(100, 4, 32) == 104);
        CHECK(pad_ld(48, 4, 32) == 56);
        CHECK(pad_ld(1, 8, 128) == 16);
        CHECK(pad_ld(1024, 8, 128) == 1040);

        /* 128×128 float (64 KB/행렬) 를 16 KB D-cache 에서. ld=128 → 행 간격 512 B = 16 라인 (짝수) */
        uint32_t cap = 16 * 1024, line = 32, ways = 4;
        int n = 128, ldp = pad_ld(n, 4, line);
        CHECK(ldp == 136);
        simres_t a = sim_run(cap, line, ways, n, n, 4, V_IJK, 0);
        simres_t b = sim_run(cap, line, ways, n, ldp, 4, V_IJK, 0);
        uint64_t ikj = sim_run(cap, line, ways, n, ldp, 4, V_IKJ, 0).misses;
        printf("      M7 ijk n=%d: ld=%d misses %llu, ld=%d misses %llu (ikj ld=%d: %llu)\n", n, n,
               (unsigned long long)a.misses, ldp, (unsigned long long)b.misses, ldp, (unsigned long long)ikj);
        CHECK(b.misses * 4 < a.misses);          /* 패딩 하나로 ijk 미스가 1/4 미만 */
        CHECK(b.misses == ikj);                  /* 패딩 후 ijk 는 ikj 와 같은 미스 수 (용량 미스만 남는다) */

        static const int cand[] = { 8, 16, 32, 64 };
        uint64_t m[4] = { 0 }, m0[4] = { 0 };
        int bt = best_tile(cap, line, ways, n, ldp, 4, cand, 4, m);
        int bt0 = best_tile(cap, line, ways, n, n, 4, cand, 4, m0);
        printf("      M7 tiled misses ld=%d:", ldp);
        for (int i = 0; i < 4; i++) printf(" T%d=%llu", cand[i], (unsigned long long)m[i]);
        printf("  → best T=%d\n      M7 tiled misses ld=%d:", bt, n);
        for (int i = 0; i < 4; i++) printf(" T%d=%llu", cand[i], (unsigned long long)m0[i]);
        printf("  → best T=%d\n", bt0);
        CHECK(bt == 32);
        CHECK(3u * (uint32_t)bt * (uint32_t)bt * 4u <= cap);   /* A·B·C 타일 셋이 16 KB 안에 */
        CHECK(bt0 == 16);                                     /* 패딩이 없으면 32 타일이 자기 충돌 → 16 이 이긴다 */
        CHECK(m[2] < m0[2]);
        CHECK(m[2] * 10 < ikj);                              /* 타일링이 ikj 보다 10배 넘게 적다 */
        CHECK(best_tile(cap, line, ways, n, ldp, 4, cand, 0, NULL) == 0);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
