/* RTOS-A03 WCET 측정과 지터 분석 — 결정적(deterministic) 부분
 *
 * 측정 대상 = 모터 속도 루프 한 스텝 ctrl_step(): 센서 검사(3연속 고장 → 재보정) → 스파이크 필터(5개 중앙값)
 *            → 데드밴드 → 피드포워드 LUT(구간 선형) → PI + 포화 + anti-windup. 입력에 따라 경로가 다르다.
 * 여기서는 시간을 재지 않는다. 각 기본 블록에 "비용 모델 사이클(ops)" 을 붙여 세므로 결과가 매번 같다.
 * 진짜 시간 측정(mach_absolute_time, cold/warm 캐시)은 demo_wcet_host.c, 보드 측정(DWT CYCCNT)은 demo_dwt_cyccnt.c.
 *
 * TODO (8개 함수):
 *  A1 pct_rank / pct_nr     : nearest-rank 백분위 (rank = ceil(q*n/10000), q 는 만분율: p99.9 = 9990)
 *  A2 hist_add / hist_pct_upper : 고정 bin 히스토그램 (under/over), 히스토그램만으로 "보수적" 백분위
 *  A3 summarize             : min/max/HWM 위치/p50/p99/p99.9/지터(max-min)/평균/표준편차(Welford)
 *  B1 cfg_bound             : 제어 흐름 그래프(CFG) 최장 경로 = 정적 WCET 상한. 배타 쌍(flow fact) 1개 지원
 *  B2 craft_worst           : 최악 경로를 실제로 밟는 입력 시퀀스 만들기
 *  C1 cache_access          : 직접 사상(direct-mapped) 캐시 모델 — hit 1, miss 1+MISS_PEN
 *  C2 io_times              : 출력 시각 — 즉시 쓰기 vs 다음 주기 경계에서 래치(타이머 preload)
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }
static int cmp_u32(const void *a, const void *b) {
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return (x > y) - (x < y);
}
static double sqrt_d(double v) {                 /* libm 없이 (Newton). v < 0 → 0 */
    if (!(v > 0)) return 0;
    double r = v > 1 ? v : 1;
    for (int i = 0; i < 200; i++) { double n = 0.5 * (r + v / r); if (n == r) break; r = n; }
    return r;
}

/* ================= Part A — 통계 ================= */
#define Q_P50  5000u
#define Q_P99  9900u
#define Q_P999 9990u
#define Q_MAX  10000u

/* TODO A1: nearest-rank. rank = ceil(q*n/10000), 1 <= rank <= n. n == 0 이면 0 */
static uint32_t pct_rank(uint32_t n, uint32_t q10k) {
    if (n == 0) return 0;
    uint64_t r = ((uint64_t)q10k * n + 9999u) / 10000u;
    if (r < 1) r = 1;
    if (r > n) r = n;
    return (uint32_t)r;
}
static uint32_t pct_nr(const uint32_t *sorted, uint32_t n, uint32_t q10k) {
    if (n == 0) return 0;
    return sorted[pct_rank(n, q10k) - 1];
}

#define MAX_BINS 64
typedef struct {
    uint32_t lo, width, nbins;       /* bin i = [lo + i*width, lo + (i+1)*width) */
    uint32_t bins[MAX_BINS];
    uint32_t under, over, n, maxseen;
} hist_t;
static void hist_init(hist_t *h, uint32_t lo, uint32_t width, uint32_t nbins) {
    memset(h, 0, sizeof *h);
    h->lo = lo; h->width = width ? width : 1; h->nbins = nbins > MAX_BINS ? MAX_BINS : nbins;
}
/* TODO A2: bin 에 넣기. lo + nbins*width 를 uint32 로 계산하면 넘친다 — 차이로 계산할 것 */
static void hist_add(hist_t *h, uint32_t x) {
    h->n++;
    if (x > h->maxseen) h->maxseen = x;
    if (x < h->lo) { h->under++; return; }
    uint64_t i = ((uint64_t)x - h->lo) / h->width;
    if (i >= h->nbins) { h->over++; return; }
    h->bins[i]++;
}
/* TODO A2: 히스토그램만으로 백분위의 "상한" — rank 가 떨어지는 bin 의 위쪽 끝(포함) 값.
 * under 에 떨어지면 lo-1, over 에 떨어지면 maxseen. 진짜 nearest-rank 값 이상이어야 한다 */
static uint32_t hist_pct_upper(const hist_t *h, uint32_t q10k) {
    uint32_t r = pct_rank(h->n, q10k);
    if (r == 0) return 0;
    uint32_t cum = h->under;
    if (cum >= r) return h->lo - 1;
    for (uint32_t i = 0; i < h->nbins; i++) {
        cum += h->bins[i];
        if (cum >= r) {
            uint64_t up = (uint64_t)h->lo + (uint64_t)(i + 1) * h->width - 1;
            if (up > h->maxseen) up = h->maxseen;      /* 마지막 채워진 bin 은 관측 max 로 조인다 */
            return (uint32_t)up;
        }
    }
    return h->maxseen;
}

typedef struct {
    uint32_t n, min, max, hwm_idx, p50, p99, p999, jitter;
    double mean, sd;                 /* 모표준편차 (n 으로 나눔) */
} summary_t;
/* TODO A3: scratch 에 복사해 정렬 → 백분위. hwm_idx = max 가 "처음" 나온 위치.
 * 평균·분산은 Welford (큰 기준값에서도 안 무너진다). n == 0 → -1 */
static int summarize(const uint32_t *x, uint32_t n, uint32_t *scratch, summary_t *s) {
    memset(s, 0, sizeof *s);
    if (n == 0) return -1;
    double mean = 0, m2 = 0;
    s->n = n; s->min = x[0]; s->max = x[0]; s->hwm_idx = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (x[i] < s->min) s->min = x[i];
        if (x[i] > s->max) { s->max = x[i]; s->hwm_idx = i; }
        double d = (double)x[i] - mean;
        mean += d / (double)(i + 1);
        m2 += d * ((double)x[i] - mean);
    }
    s->mean = mean;
    s->sd = sqrt_d(m2 / (double)n);
    s->jitter = s->max - s->min;
    memcpy(scratch, x, (size_t)n * sizeof *x);
    qsort(scratch, n, sizeof *scratch, cmp_u32);
    s->p50 = pct_nr(scratch, n, Q_P50);
    s->p99 = pct_nr(scratch, n, Q_P99);
    s->p999 = pct_nr(scratch, n, Q_P999);
    return 0;
}
/* 비교용(주어진 코드): 교과서 공식 E[x^2] - E[x]^2 — 기준값이 크면 상쇄 오차로 무너진다 */
static double sd_naive(const uint32_t *x, uint32_t n) {
    double s = 0, s2 = 0;
    for (uint32_t i = 0; i < n; i++) { s += x[i]; s2 += (double)x[i] * x[i]; }
    return sqrt_d(s2 / n - (s / n) * (s / n));
}

/* ================= Part C 의 캐시 모델 (ctrl_step 이 사용) ================= */
#define LINE_B    16u
#define NLINES    8u
#define MISS_PEN  10u
typedef struct { uint32_t tag[NLINES]; uint8_t valid[NLINES]; uint32_t hits, misses; } cache_t;
static void cache_flush(cache_t *c) { memset(c->valid, 0, sizeof c->valid); }
/* TODO C1: 직접 사상. line = addr / LINE_B, idx = line % NLINES, tag = line / NLINES.
 * hit → 1 반환, miss → 채우고 1 + MISS_PEN 반환. hits/misses 를 센다 */
static uint32_t cache_access(cache_t *c, uint32_t addr) {
    uint32_t line = addr / LINE_B, idx = line % NLINES, tag = line / NLINES;
    if (c->valid[idx] && c->tag[idx] == tag) { c->hits++; return 1; }
    c->valid[idx] = 1; c->tag[idx] = tag; c->misses++;
    return 1 + MISS_PEN;
}

/* ================= Part B — 측정 대상 함수와 비용 모델 (주어진 코드) ================= */
#define ADC_MAX   4095
#define SPIKE_TH  1500
#define DEADBAND  8
#define OUT_LIM   1000
#define LUT_N     16
#define CAL_N     32
#define FAULT_N   3
#define ADDR_BP   0x20000000u            /* g_bp : 64 B → 캐시 index 0..3 */
#define ADDR_FF   0x20000040u            /* g_ff : 64 B → index 4..7 */
#define ADDR_CAL  0x20000400u            /* g_cal: 128 B → index 0..7 (LUT 와 전부 충돌) */

enum {
    BLK_ENTRY = 1u << 0, BLK_OK = 1u << 1, BLK_FAULT = 1u << 2, BLK_RECAL = 1u << 3, BLK_SPIKE = 1u << 4,
    BLK_PLAIN = 1u << 5, BLK_HOLD = 1u << 6, BLK_LUT = 1u << 7, BLK_EXACT = 1u << 8, BLK_INTERP = 1u << 9,
    BLK_PI = 1u << 10, BLK_SAT = 1u << 11, BLK_INTEG = 1u << 12, BLK_ALL = (1u << 13) - 1
};
static const int32_t g_bp[LUT_N] = {0, 120, 300, 520, 780, 1050, 1350, 1680,
                                    2020, 2380, 2740, 3100, 3440, 3720, 3940, 4095};
static const int32_t g_ff[LUT_N] = {0, 10, 25, 45, 70, 100, 135, 175, 220, 270, 325, 385, 450, 520, 595, 675};
static int32_t g_cal[CAL_N];

typedef struct {
    int32_t kp_q8, ki_q8, integ, out, prev, cal_off;
    int32_t hist[5]; uint32_t hpos, bad_run;
    uint32_t ops, blocks;            /* 마지막 스텝의 비용(모델 사이클)과 지나간 블록 비트맵 */
    cache_t *cache;                  /* NULL = 평평한 메모리 (모든 접근 1 사이클, TCM 같은) */
} ctrl_t;

static int32_t abs_i32(int32_t v) { return v < 0 ? -v : v; }
static void ctrl_init(ctrl_t *c, cache_t *cache) {
    memset(c, 0, sizeof *c);
    c->kp_q8 = 384; c->ki_q8 = 16; c->prev = 2048; c->cache = cache;
    for (int i = 0; i < 5; i++) c->hist[i] = 2048;
    for (int i = 0; i < CAL_N; i++) g_cal[i] = (i * 7) % 11 - 5;
}
static int32_t rd(ctrl_t *c, const int32_t *arr, uint32_t base, int i) {
    c->ops += c->cache ? cache_access(c->cache, base + 4u * (uint32_t)i) : 1u;
    return arr[i];
}
static int32_t ctrl_step(ctrl_t *c, int32_t sp, int32_t meas) {
    c->ops = 10; c->blocks = BLK_ENTRY;
    int32_t m;
    /* 1) 센서 검사: 범위 밖이면 마지막 정상값 유지, 3연속이면 재보정 */
    if (meas < 0 || meas > ADC_MAX) {
        c->ops += 6; c->blocks |= BLK_FAULT;
        m = c->prev;
        if (++c->bad_run >= FAULT_N) {
            c->ops += 8; c->blocks |= BLK_RECAL;
            int32_t acc = 0;
            for (int i = 0; i < CAL_N; i++) { c->ops += 3; acc += rd(c, g_cal, ADDR_CAL, i); }
            c->cal_off = acc / CAL_N;
            c->integ = 0; c->bad_run = 0;
            for (int i = 0; i < 5; i++) c->hist[i] = m;
        }
    } else {
        c->ops += 2; c->blocks |= BLK_OK;
        c->bad_run = 0; m = meas;
    }
    /* 2) 스파이크 필터: 직전 값과 차이가 크면 최근 5개의 중앙값 (삽입 정렬, 이동 1회 = 3) */
    c->hist[c->hpos] = m; c->hpos = (c->hpos + 1) % 5;
    if (abs_i32(m - c->prev) > SPIKE_TH) {
        c->ops += 6; c->blocks |= BLK_SPIKE;
        int32_t s[5]; memcpy(s, c->hist, sizeof s);
        for (int i = 1; i < 5; i++) {
            int32_t v = s[i]; int j = i - 1;
            while (j >= 0 && s[j] > v) { s[j + 1] = s[j]; j--; c->ops += 3; }
            s[j + 1] = v;
        }
        m = s[2];
    } else {
        c->ops += 2; c->blocks |= BLK_PLAIN;
    }
    c->prev = m;
    /* 3) 데드밴드: 오차가 작으면 출력 유지하고 끝 */
    int32_t err = sp - (m - c->cal_off);
    if (abs_i32(err) <= DEADBAND) { c->ops += 3; c->blocks |= BLK_HOLD; return c->out; }
    /* 4) 피드포워드 LUT: 구간을 앞에서부터 선형 탐색 (sp 가 클수록 길다) */
    c->ops += 4; c->blocks |= BLK_LUT;
    int32_t spc = sp < 0 ? 0 : sp > ADC_MAX ? ADC_MAX : sp;
    int i = 0;
    while (i < LUT_N - 2) { c->ops += 2; if (spc < rd(c, g_bp, ADDR_BP, i + 1)) break; i++; }
    int32_t b0 = rd(c, g_bp, ADDR_BP, i), f0 = rd(c, g_ff, ADDR_FF, i), ff;
    if (spc == b0) { c->ops += 2; c->blocks |= BLK_EXACT; ff = f0; }
    else {
        c->ops += 20; c->blocks |= BLK_INTERP;           /* 나눗셈 포함 */
        int32_t b1 = rd(c, g_bp, ADDR_BP, i + 1), f1 = rd(c, g_ff, ADDR_FF, i + 1);
        ff = f0 + (f1 - f0) * (spc - b0) / (b1 - b0);
    }
    /* 5) PI + 포화. 포화되면 적분을 멈춘다 (조건부 적분 anti-windup) */
    c->ops += 15; c->blocks |= BLK_PI;
    int32_t u = (c->kp_q8 * err + c->integ) / 256 + ff;
    if (u > OUT_LIM || u < -OUT_LIM) { c->ops += 8; c->blocks |= BLK_SAT; u = u > 0 ? OUT_LIM : -OUT_LIM; }
    else {
        c->ops += 4; c->blocks |= BLK_INTEG;
        c->integ += c->ki_q8 * err;
        if (c->integ > OUT_LIM * 256) c->integ = OUT_LIM * 256;
        if (c->integ < -OUT_LIM * 256) c->integ = -OUT_LIM * 256;
    }
    c->out = u;
    return u;
}

/* ctrl_step 의 CFG (주어진 데이터). 노드는 위상 순서. 비용(mc) = base_ops + base_mem*mc + loop*(body_ops + body_mem*mc)
 * mc = 메모리 접근 1회 비용 (평평한 메모리 1, "항상 miss" 가정 1+MISS_PEN) */
enum { N_ENTRY, N_OK, N_FAULT, N_RECAL, N_J1, N_SPIKE, N_PLAIN, N_J2, N_HOLD, N_LUT, N_EXACT, N_INTERP,
       N_PI, N_SAT, N_INTEG, N_EXIT, N_COUNT };
typedef struct { const char *name; uint32_t base_ops, base_mem, loop, body_ops, body_mem; int succ[2]; } cfg_node_t;
static const cfg_node_t g_cfg[N_COUNT] = {
    [N_ENTRY]  = {"ENTRY", 10, 0, 0, 0, 0, {N_OK, N_FAULT}},
    [N_OK]     = {"OK", 2, 0, 0, 0, 0, {N_J1, -1}},
    [N_FAULT]  = {"FAULT", 6, 0, 0, 0, 0, {N_RECAL, N_J1}},
    [N_RECAL]  = {"RECAL", 8, 0, CAL_N, 3, 1, {N_J1, -1}},
    [N_J1]     = {"J1", 0, 0, 0, 0, 0, {N_SPIKE, N_PLAIN}},
    [N_SPIKE]  = {"SPIKE", 6, 0, 10, 3, 0, {N_J2, -1}},        /* 원소 5개 삽입 정렬: 이동 최대 10 (흐름 정보) */
    [N_PLAIN]  = {"PLAIN", 2, 0, 0, 0, 0, {N_J2, -1}},
    [N_J2]     = {"J2", 0, 0, 0, 0, 0, {N_HOLD, N_LUT}},
    [N_HOLD]   = {"HOLD", 3, 0, 0, 0, 0, {N_EXIT, -1}},
    [N_LUT]    = {"LUT", 4, 2, LUT_N - 2, 2, 1, {N_EXACT, N_INTERP}},
    [N_EXACT]  = {"EXACT", 2, 0, 0, 0, 0, {N_PI, -1}},
    [N_INTERP] = {"INTERP", 20, 2, 0, 0, 0, {N_PI, -1}},
    [N_PI]     = {"PI", 15, 0, 0, 0, 0, {N_SAT, N_INTEG}},
    [N_SAT]    = {"SAT", 8, 0, 0, 0, 0, {N_EXIT, -1}},
    [N_INTEG]  = {"INTEG", 4, 0, 0, 0, 0, {N_EXIT, -1}},
    [N_EXIT]   = {"EXIT", 0, 0, 0, 0, 0, {-1, -1}},
};
static uint32_t node_cost(const cfg_node_t *v, uint32_t mc) {
    return v->base_ops + v->base_mem * mc + v->loop * (v->body_ops + v->body_mem * mc);
}

/* TODO B1: 최장 경로 (DAG, 노드는 위상 순서, 0 = 진입, n-1 = 출구). 반환 = 상한, path[] 에 경로(진입→출구).
 * excl_a, excl_b >= 0 이면 "두 노드를 같은 경로에서 함께 밟을 수 없다" (흐름 정보) — a 를 뺀 그래프와
 * b 를 뺀 그래프의 최장 경로 중 큰 것. -1 이면 제약 없음 */
static uint32_t longest_skip(const cfg_node_t *g, int n, uint32_t mc, int skip, int *path, int *plen) {
    int64_t dist[N_COUNT]; int par[N_COUNT];
    for (int v = 0; v < n; v++) { dist[v] = -1; par[v] = -1; }
    dist[0] = node_cost(&g[0], mc);
    for (int v = 0; v < n; v++) {
        if (dist[v] < 0 || v == skip) continue;
        for (int k = 0; k < 2; k++) {
            int s = g[v].succ[k];
            if (s < 0 || s == skip) continue;
            int64_t d = dist[v] + node_cost(&g[s], mc);
            if (d > dist[s]) { dist[s] = d; par[s] = v; }
        }
    }
    int tmp[N_COUNT], len = 0;
    for (int v = n - 1; v >= 0; v = par[v]) tmp[len++] = v;
    *plen = len;
    for (int k = 0; k < len; k++) path[k] = tmp[len - 1 - k];
    return dist[n - 1] < 0 ? 0 : (uint32_t)dist[n - 1];
}
static uint32_t cfg_bound(const cfg_node_t *g, int n, uint32_t mc, int excl_a, int excl_b, int *path, int *plen) {
    if (excl_a < 0 || excl_b < 0) return longest_skip(g, n, mc, -1, path, plen);
    int pa[N_COUNT], la, pb[N_COUNT], lb;
    uint32_t a = longest_skip(g, n, mc, excl_a, pa, &la);
    uint32_t b = longest_skip(g, n, mc, excl_b, pb, &lb);
    if (a >= b) { memcpy(path, pa, sizeof(int) * (size_t)la); *plen = la; return a; }
    memcpy(path, pb, sizeof(int) * (size_t)lb); *plen = lb; return b;
}

/* TODO B2: ctrl_init 직후 상태에서 시작해, 마지막 스텝이 최악 경로(재보정 + LUT 최장 + 보간 + 포화)를
 * 밟는 입력 시퀀스를 sp[], meas[] 에 쓰고 길이를 반환 (cap 이하) */
static int craft_worst(int32_t *sp, int32_t *meas, int cap) {
    if (cap < FAULT_N) return 0;
    for (int k = 0; k < FAULT_N; k++) { sp[k] = 4000; meas[k] = 4096 + k; }   /* 범위 밖 3연속 */
    return FAULT_N;
}

/* 무작위 측정 캠페인 (주어진 코드): sp, meas 를 0..4095 에서 균일하게. inject_pm(천분율) 확률로 meas 를 범위 밖으로.
 * flush_each = 매 스텝 전에 캐시를 비움 ("캐시를 다 갈아엎는 태스크에게 선점당한 뒤" 의 최악) */
typedef struct { uint32_t max, argmax, cover, max_misses; } camp_t;
static void campaign(uint32_t seed, uint32_t n, uint32_t inject_pm, cache_t *cache, int flush_each,
                     uint32_t *ops_out, camp_t *r) {
    ctrl_t c; ctrl_init(&c, cache);
    if (cache) { cache_flush(cache); cache->hits = cache->misses = 0; }
    memset(r, 0, sizeof *r);
    uint32_t s = seed;
    for (uint32_t k = 0; k < n; k++) {
        int32_t sp = (int32_t)(xs32(&s) % 4096u), meas = (int32_t)(xs32(&s) % 4096u);
        if (xs32(&s) % 1000u < inject_pm) meas = 4096 + (int32_t)(xs32(&s) % 1000u);
        uint32_t m0 = 0;
        if (cache) { if (flush_each) cache_flush(cache); m0 = cache->misses; }
        ctrl_step(&c, sp, meas);
        if (cache && cache->misses - m0 > r->max_misses) r->max_misses = cache->misses - m0;
        ops_out[k] = c.ops; r->cover |= c.blocks;
        if (c.ops > r->max) { r->max = c.ops; r->argmax = k; }
    }
}

/* ================= Part C — 출력 시각과 I/O 지터 ================= */
enum { IO_IMMEDIATE, IO_LATCHED };
/* TODO C2: 주기 P 마다 k*P 에 출시(release)되는 ISR 형 잡. 시작 = max(k*P, 이전 잡 끝), 끝 = 시작 + exec[k].
 * IMMEDIATE: 끝나는 순간 PWM 레지스터에 씀 → out = 끝.
 * LATCHED  : shadow 레지스터에 쓰고 타이머 갱신 이벤트(주기 경계)에서 반영 → out = ceil(끝 / P) * P.
 * lat[k] = out - k*P (출시 → 출력 지연). 반환 = 오버런 수 (LATCHED: out > (k+1)P, IMMEDIATE: 끝 > (k+1)P) */
static uint32_t io_times(const uint32_t *exec, uint32_t n, uint32_t P, int mode, uint32_t *lat) {
    uint64_t prev_end = 0; uint32_t over = 0;
    for (uint32_t k = 0; k < n; k++) {
        uint64_t rel = (uint64_t)k * P;
        uint64_t start = prev_end > rel ? prev_end : rel;
        uint64_t end = start + exec[k];
        uint64_t out = mode == IO_LATCHED ? (end + P - 1) / P * P : end;
        if (out > rel + P) over++;
        lat[k] = (uint32_t)(out - rel);
        prev_end = end;
    }
    return over;
}

/* ================= 테스트 ================= */
#define NRUN 20000u
static uint32_t g_ops[NRUN], g_scr[NRUN], g_lat[NRUN];

static void print_hist(const hist_t *h) {
    uint32_t mx = 1;
    for (uint32_t i = 0; i < h->nbins; i++) if (h->bins[i] > mx) mx = h->bins[i];
    for (uint32_t i = 0; i < h->nbins; i++) {
        if (!h->bins[i]) continue;
        printf("  [%3u,%3u) %6u |", h->lo + i * h->width, h->lo + (i + 1) * h->width, h->bins[i]);
        for (uint32_t k = 0; k < (h->bins[i] * 40u + mx - 1) / mx; k++) putchar('#');
        putchar('\n');
    }
    printf("  under=%u over=%u\n", h->under, h->over);
}
static void print_path(const int *p, int len) {
    if (len == 0) putchar('\n');
    for (int k = 0; k < len; k++) printf("%s%s", g_cfg[p[k]].name, k + 1 < len ? ">" : "\n");
}

int main(void) {
    /* ---------- A1 nearest-rank ---------- */
    {
        uint32_t a[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
        CHECK(pct_rank(10, Q_P50) == 5 && pct_nr(a, 10, Q_P50) == 5);       /* 짝수 n: 아래 중앙값 (관측값) */
        CHECK(pct_nr(a, 10, Q_P99) == 10 && pct_nr(a, 10, Q_P999) == 10);   /* n=10 이면 p99 = max */
        CHECK(pct_nr(a, 10, 0) == 1 && pct_nr(a, 10, Q_MAX) == 10);
        CHECK(pct_rank(0, Q_P50) == 0 && pct_nr(a, 0, Q_P50) == 0);
        CHECK(pct_rank(1000, Q_P999) == 999 && pct_rank(1000, Q_P99) == 990); /* n=1000: p99.9 는 두 번째로 큰 값 */
        CHECK(pct_rank(1001, Q_P999) == 1000 && pct_rank(100000, Q_P999) == 99900);
        CHECK(pct_rank(4000000000u, Q_P999) == 3996000000u);                  /* q*n 이 32비트를 넘어도 */
    }
    /* ---------- A2 histogram ---------- */
    {
        hist_t h; hist_init(&h, 100, 10, 5);                                  /* [100,150) */
        uint32_t xs[] = {99, 100, 109, 110, 149, 150, 1000, 0};
        for (unsigned i = 0; i < sizeof xs / sizeof *xs; i++) hist_add(&h, xs[i]);
        CHECK(h.n == 8 && h.under == 2 && h.over == 2 && h.maxseen == 1000);
        CHECK(h.bins[0] == 2 && h.bins[1] == 1 && h.bins[4] == 1 && h.bins[2] == 0);
        hist_t w; hist_init(&w, 0xFFFFFF00u, 16, 64);                         /* lo + 64*16 이 32비트를 넘는다 */
        hist_add(&w, 0xFFFFFFFFu); hist_add(&w, 0xFFFFFF00u);
        CHECK(w.over == 0 && w.bins[15] == 1 && w.bins[0] == 1);
        /* 히스토그램 백분위 상한: 정확한 값 이상, bin 폭 미만 차이 */
        uint32_t s = 7, raw[5000], sorted[5000]; hist_t g; hist_init(&g, 0, 25, 40);
        for (int i = 0; i < 5000; i++) { raw[i] = 200 + xs32(&s) % 600u + (i % 97 == 0 ? 700u : 0u); hist_add(&g, raw[i]); }
        memcpy(sorted, raw, sizeof raw); qsort(sorted, 5000, sizeof *sorted, cmp_u32);
        int ok = 1;
        uint32_t qs[] = {Q_P50, Q_P99, Q_P999, Q_MAX, 0, 1234};
        for (unsigned i = 0; i < 6; i++) {
            uint32_t ex = pct_nr(sorted, 5000, qs[i]), up = hist_pct_upper(&g, qs[i]);
            if (!(up >= ex && (up - ex < 25 || up == g.maxseen))) ok = 0;
        }
        CHECK(ok && g.over > 0 && hist_pct_upper(&g, Q_MAX) == sorted[4999]);
        CHECK(hist_pct_upper(&h, 0) == 99 && hist_pct_upper(&h, Q_MAX) == 1000);
    }
    /* ---------- A3 summarize ---------- */
    {
        uint32_t x[] = {4, 2, 9, 4, 5, 4, 7, 9, 5}, scr[16];
        summary_t s;
        CHECK(summarize(x, 0, scr, &s) == -1);
        /* {2,4,4,4,5,5,7,9}: 평균 5, 모표준편차 정확히 2 (교과서 예제) */
        uint32_t y[] = {2, 4, 4, 4, 5, 5, 7, 9};
        CHECK(summarize(y, 8, scr, &s) == 0 && s.mean == 5.0 && s.sd > 1.999999 && s.sd < 2.000001);
        CHECK(summarize(x, 9, scr, &s) == 0 && s.min == 2 && s.max == 9 && s.hwm_idx == 2 && s.jitter == 7);
        CHECK(s.p50 == 5 && s.p99 == 9 && s.p999 == 9);
        uint32_t big[8];
        for (int i = 0; i < 8; i++) big[i] = 1000000000u + y[i];             /* 큰 기준값 (예: 부팅 후 µs) */
        summarize(big, 8, scr, &s);
        double nv = sd_naive(big, 8);
        printf("info: sd welford=%.6f naive=%.6f (baseline 1e9)\n", s.sd, nv);
        CHECK(s.sd > 1.999999 && s.sd < 2.000001 && s.jitter == 7);
        uint32_t one = 42;
        CHECK(summarize(&one, 1, scr, &s) == 0 && s.p999 == 42 && s.jitter == 0 && s.sd == 0);
    }

    /* ---------- B: 경로 비용, 무작위 측정 vs 정적 상한 vs 표적 입력 ---------- */
    int path[N_COUNT] = {0}, plen = 0;
    uint32_t naive = cfg_bound(g_cfg, N_COUNT, 1, -1, -1, path, &plen);
    printf("info: static bound (no flow facts) = %u : ", naive); print_path(path, plen);
    CHECK(naive == 281 && plen == 11 && path[2] == N_RECAL && path[4] == N_SPIKE);
    uint32_t tight = cfg_bound(g_cfg, N_COUNT, 1, N_FAULT, N_SPIKE, path, &plen);
    printf("info: static bound (FAULT x SPIKE)  = %u : ", tight); print_path(path, plen);
    CHECK(tight == 247 && path[2] == N_RECAL && path[4] == N_PLAIN && plen >= 2 && path[plen - 2] == N_SAT);
    {
        cfg_node_t lin[3] = {{"a", 5, 0, 0, 0, 0, {1, -1}}, {"b", 7, 0, 0, 0, 0, {2, -1}}, {"c", 0, 0, 0, 0, 0, {-1, -1}}};
        CHECK(cfg_bound(lin, 3, 1, -1, -1, path, &plen) == 12 && plen == 3);
    }

    camp_t r0, r1;
    campaign(1, NRUN, 0, NULL, 0, g_ops, &r0);
    summary_t s0; summarize(g_ops, NRUN, g_scr, &s0);
    printf("info: random (valid ADC only) n=%u: min=%u p50=%u p99=%u p99.9=%u max=%u (run #%u) jitter=%u "
           "mean=%.1f sd=%.1f cover=0x%04x\n", NRUN, s0.min, s0.p50, s0.p99, s0.p999, s0.max, s0.hwm_idx,
           s0.jitter, s0.mean, s0.sd, r0.cover);
    hist_t h0; hist_init(&h0, 0, 10, 16);
    for (uint32_t k = 0; k < NRUN; k++) hist_add(&h0, g_ops[k]);
    print_hist(&h0);
    CHECK(s0.max == r0.max && s0.hwm_idx == r0.argmax);
    CHECK((r0.cover & (BLK_FAULT | BLK_RECAL)) == 0);                       /* 오류 경로는 한 번도 안 밟음 */
    CHECK(s0.max < tight && s0.max * 120u / 100u < tight);                   /* 측정 max + 20% 도 진짜 WCET 아래 */
    campaign(1, NRUN, 10, NULL, 0, g_ops, &r1);                              /* 1% 고장 주입 */
    printf("info: random (1%% invalid)    max=%u cover=0x%04x  missing=0x%04x\n", r1.max, r1.cover,
           BLK_ALL & ~r1.cover);
    CHECK((r1.cover & BLK_FAULT) && !(r1.cover & BLK_RECAL) && r1.max < tight);

    int32_t sp[16], ms[16];
    int ns = craft_worst(sp, ms, 16);
    {
        ctrl_t c; ctrl_init(&c, NULL);
        uint32_t worst = 0, wblk = 0;
        for (int k = 0; k < ns; k++) { ctrl_step(&c, sp[k], ms[k]); if (c.ops > worst) { worst = c.ops; wblk = c.blocks; } }
        printf("info: crafted %d steps -> worst ops=%u blocks=0x%04x\n", ns, worst, wblk);
        CHECK(ns > 0 && ns <= 16 && worst == tight);                         /* 정적 상한(흐름 정보 포함)이 정확 */
        CHECK((wblk & (BLK_RECAL | BLK_INTERP | BLK_SAT)) == (BLK_RECAL | BLK_INTERP | BLK_SAT) && !(wblk & BLK_SPIKE));
        CHECK(naive > worst);                                                /* 흐름 정보 없는 상한은 비관적 */
    }

    /* ---------- C1 캐시 모델 ---------- */
    cache_t ca; memset(&ca, 0, sizeof ca);
    CHECK(cache_access(&ca, 0x20000000u) == 1 + MISS_PEN && cache_access(&ca, 0x2000000Cu) == 1);   /* 같은 줄 */
    CHECK(cache_access(&ca, 0x20000400u) == 1 + MISS_PEN && cache_access(&ca, 0x20000000u) == 1 + MISS_PEN); /* 충돌 */
    CHECK(cache_access(&ca, 0x20000010u) == 1 + MISS_PEN && ca.hits == 1 && ca.misses == 4);
    cache_flush(&ca);
    CHECK(cache_access(&ca, 0x20000010u) == 1 + MISS_PEN);

    /* ---------- C: cold vs warm, 그리고 다음 호출에 남는 영향 ---------- */
    uint32_t allmiss = cfg_bound(g_cfg, N_COUNT, 1 + MISS_PEN, N_FAULT, N_SPIKE, path, &plen);
    uint32_t cold_worst = 0, warm_worst = 0, after_recal_miss = 0, steady_miss = 0;
    {
        ctrl_t c; ctrl_init(&c, &ca);
        for (int k = 0; k < ns; k++) { cache_flush(&ca); ctrl_step(&c, sp[k], ms[k]); if (c.ops > cold_worst) cold_worst = c.ops; }
        /* 같은 시퀀스를, LUT 전체를 데운 뒤 비우지 않고 (warm) */
        ctrl_init(&c, &ca); cache_flush(&ca);
        for (int32_t v = 100; v < 4096; v += 400) ctrl_step(&c, v, 2048);   /* 워밍업: LUT 8줄을 다 채운다 */
        for (int k = 0; k < ns; k++) { ctrl_step(&c, sp[k], ms[k]); if (c.ops > warm_worst) warm_worst = c.ops; }
        uint32_t m0 = ca.misses; ctrl_step(&c, 1000, 2048); after_recal_miss = ca.misses - m0;  /* 재보정 다음 호출 */
        m0 = ca.misses; ctrl_step(&c, 1000, 2048); steady_miss = ca.misses - m0;
    }
    printf("info: crafted worst: flat=%u warm-cache=%u cold-cache=%u all-miss bound=%u; misses next call after "
           "RECAL=%u, steady=%u\n", tight, warm_worst, cold_worst, allmiss, after_recal_miss, steady_miss);
    CHECK(allmiss == 747 && cold_worst == 377 && warm_worst == cold_worst && tight < cold_worst);  /* 이 경로는 스스로 캐시를 갈아엎는다 */
    CHECK(after_recal_miss > 0 && steady_miss == 0);
    camp_t rw, rc;
    campaign(1, NRUN, 0, &ca, 0, g_ops, &rw);
    summary_t sw; summarize(g_ops, NRUN, g_scr, &sw);
    campaign(1, NRUN, 0, &ca, 1, g_ops, &rc);
    summary_t sc; summarize(g_ops, NRUN, g_scr, &sc);
    printf("info: random warm cache: p50=%u p99=%u max=%u | flush-each (cold): p50=%u p99=%u max=%u max_misses=%u\n",
           sw.p50, sw.p99, sw.max, sc.p50, sc.p99, sc.max, rc.max_misses);
    CHECK(sc.max > sw.max && sc.p50 > sw.p50 && rc.max_misses > rw.max_misses);
    CHECK(sc.max < cold_worst);                                              /* cold 로 재도 무작위는 최악 경로를 못 찾는다 */

    /* ---------- C2 I/O 지터: 즉시 쓰기 vs 주기 경계 래치 ---------- */
    {
        uint32_t ex[4] = {100, 140, 120, 247}, lat[4] = {0};
        CHECK(io_times(ex, 4, 400, IO_IMMEDIATE, lat) == 0 && lat[0] == 100 && lat[3] == 247);
        CHECK(io_times(ex, 4, 400, IO_LATCHED, lat) == 0 && lat[0] == 400 && lat[1] == 400 && lat[3] == 400);
        uint32_t ex2[3] = {100, 377, 100};                                   /* cold 최악이 주기 300 을 넘는다 */
        CHECK(io_times(ex2, 3, 300, IO_LATCHED, lat) == 1 && lat[0] == 300 && lat[1] == 600 && lat[2] == 300);
        CHECK(io_times(ex2, 3, 300, IO_IMMEDIATE, lat) == 1 && lat[1] == 377 && lat[2] == 177);  /* 다음 잡이 밀림 */
        CHECK(io_times(ex, 4, 400, 99, lat) == 0 && lat[2] == 120);         /* 모르는 모드 = 즉시 */
    }
    {
        campaign(1, NRUN, 0, &ca, 0, g_ops, &rw);                            /* warm 측정값으로 */
        summary_t li, ll;
        io_times(g_ops, NRUN, 250, IO_IMMEDIATE, g_lat); summarize(g_lat, NRUN, g_scr, &li);
        uint32_t ov = io_times(g_ops, NRUN, 250, IO_LATCHED, g_lat); summarize(g_lat, NRUN, g_scr, &ll);
        printf("info: I/O latency P=250: immediate min=%u max=%u jitter=%u sd=%.1f | latched min=%u max=%u jitter=%u "
               "overruns=%u\n", li.min, li.max, li.jitter, li.sd, ll.min, ll.max, ll.jitter, ov);
        CHECK(li.jitter == sw.jitter && ll.jitter == 0 && ov == 0 && ll.min == 250);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
