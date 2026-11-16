/* MEM-B05 demo_refresh.c — 리프레시 방식별 요청 지연 분포 (큰 실험)
 *
 * solution.c 의 Part A/B 함수를 그대로 복사해 쓴다 (자체 완결). 시뮬레이션이라 결과는 결정적이다
 * (기계 부하와 무관 — 같은 시드면 같은 숫자).
 * 모델: 16 뱅크(DDR4 x8 4 BG x 4), 뱅크별 FCFS, close-page. 요청마다 뱅크 점유 tRC = 46.25 ns,
 *       데이터 반환 tRCD + CL + 4 = 30 ns. 데이터 버스 경합 · tFAW · 쓰기 · 스케줄링(FR-FCFS)은 무시.
 * 리프레시 파라미터 = 이 레슨의 정의 (JESD79-4 / 데이터시트 대표값). DDR5 행은 같은 16 뱅크 모델에
 *       DDR5식 REF 파라미터만 끼운 것 (실제 DDR5 는 32 뱅크 · 다른 tCK — 모델 가정).
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 demo_refresh.c -o demo_refresh && ./demo_refresh
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef int64_t ps_t;                               /* 시간은 전부 정수 피코초 */
enum { RC_OK = 0, RC_EINVAL = -1, RC_ETEMP = -2 };

#define TCK_PS        625                           /* DDR4-3200 */
#define TREFI_1X_PS   7800000                       /* 7.8 us */
#define TREFI_2X_PS   3900000                       /* 3.9 us */

typedef struct { uint64_t num, den; } frac_t;       /* 항상 기약분수, den > 0 */

static uint64_t gcd_u64(uint64_t a, uint64_t b) { while (b) { uint64_t t = a % b; a = b; b = t; } return a; }

/* 밀도(Gb) → tRFC1 (ps). 표에 없는 밀도는 RC_EINVAL. */
static ps_t trfc1_ps(int density_gb) {
    switch (density_gb) {
    case 2:  return 160000;
    case 4:  return 260000;
    case 8:  return 350000;
    case 16: return 550000;
    default: return RC_EINVAL;
    }
}

/* ---------------- Part A: 손실 비율 ---------------- */

/* TODO A1: 온도(°C) → tREFI (ps). -40..85 → 7.8 us, 86..95 → 3.9 us (2x), 그 밖 → RC_ETEMP */
static ps_t trefi_ps_for_temp(int temp_c) {
    if (temp_c < -40 || temp_c > 95) return RC_ETEMP;
    return temp_c <= 85 ? TREFI_1X_PS : TREFI_2X_PS;
}

/* TODO A2: num/den 을 기약분수로. den == 0 이면 {0, 0} (잘못된 값 표시) */
static frac_t frac_make(uint64_t num, uint64_t den) {
    if (den == 0) return (frac_t){ 0, 0 };
    uint64_t g = gcd_u64(num, den);
    if (g == 0) g = 1;                                /* num == 0 → gcd = den, 0/1 이 된다 */
    return (frac_t){ num / g, den / g };
}

/* TODO A3: 리프레시로 잃는 시간 비율 = tRFC / tREFI (기약분수). 잘못된 밀도/온도면 해당 RC */
static int refresh_overhead(int density_gb, int temp_c, frac_t *out) {
    ps_t rfc = trfc1_ps(density_gb);
    if (rfc < 0) return RC_EINVAL;
    ps_t refi = trefi_ps_for_temp(temp_c);
    if (refi < 0) return (int)refi;
    *out = frac_make((uint64_t)rfc, (uint64_t)refi);
    return RC_OK;
}

/* TODO A4: 분수 → 밀리퍼센트(0.001 %), 반올림(half-up). 7/156 → 4487, 1/200000 → 1 */
static uint64_t frac_milli_pct(frac_t f) {
    if (f.den == 0) return 0;
    return (f.num * 200000u + f.den) / (2u * f.den);
}

/* ---------------- Part B: 뱅크 시뮬레이터 ---------------- */

/* 리프레시 스케줄 (뱅크마다 주기적인 막힘 창 [off + k*period, off + k*period + dur), k >= 0)
 *   all-bank : nslots = 1 → 모든 뱅크 off = 0, 같은 시각에 막힘
 *   per-bank / same-bank : 뱅크 b 는 슬롯 b % nslots, off = slot * (period / nslots) 로 엇갈림
 *   dur = 0 → 리프레시 없음 */
typedef struct { ps_t period, dur; int nslots; } ref_cfg;

static ps_t ref_offset(const ref_cfg *r, int bank) {
    return (ps_t)(bank % r->nslots) * (r->period / r->nslots);
}

/* TODO B1: t 이후, 뱅크를 [s, s + busy) 동안 쓸 수 있는 가장 이른 s (막힘 창과 겹치면 안 된다).
 * busy > period - dur 이면 영원히 못 들어가므로 -1. */
static ps_t ref_next_free(const ref_cfg *r, int bank, ps_t t, ps_t busy) {
    if (r->dur == 0) return t;
    if (busy > r->period - r->dur) return -1;
    ps_t off = ref_offset(r, bank), ws;
    if (t < off) {
        ws = off;                                     /* 아직 첫 창 전 */
    } else {
        ws = off + ((t - off) / r->period) * r->period;
        if (t < ws + r->dur) t = ws + r->dur;         /* 창 안 → 창 끝으로 */
        ws += r->period;                              /* 다음 창 시작 */
    }
    return (t + busy <= ws) ? t : ws + r->dur;        /* 다음 창 전에 끝나면 OK, 아니면 그 창 뒤 */
}

#define MAX_BANKS 64
typedef struct { int nbanks; ps_t busy, lat; ref_cfg ref; } sim_cfg;
typedef struct { ps_t arr; int bank; } req_t;         /* arr 오름차순 */

/* TODO B2: 뱅크별 FCFS. start = ref_next_free(bank, max(arr, bank_free)), lat_out[i] = start + lat - arr,
 * bank_free = start + busy. 리프레시 때문에 start 가 밀린 요청 수를 돌려준다. 잘못된 입력은 RC_EINVAL. */
static long sim_run(const sim_cfg *c, const req_t *rq, size_t n, ps_t *lat_out) {
    if (c->nbanks < 1 || c->nbanks > MAX_BANKS || c->ref.nslots < 1 || c->ref.period <= 0) return RC_EINVAL;
    if (c->ref.dur > 0 && c->busy > c->ref.period - c->ref.dur) return RC_EINVAL;
    ps_t bank_free[MAX_BANKS] = { 0 };
    long pushed = 0;
    for (size_t i = 0; i < n; i++) {
        int b = rq[i].bank;
        if (b < 0 || b >= c->nbanks) return RC_EINVAL;
        ps_t ready = rq[i].arr > bank_free[b] ? rq[i].arr : bank_free[b];
        ps_t start = ref_next_free(&c->ref, b, ready, c->busy);
        if (start != ready) pushed++;
        lat_out[i] = start + c->lat - rq[i].arr;
        bank_free[b] = start + c->busy;
    }
    return pushed;
}

typedef struct { ps_t mean, p50, p99, p999, max; } lat_stats_t;

static int cmp_ps(const void *a, const void *b) {
    ps_t x = *(const ps_t *)a, y = *(const ps_t *)b;
    return (x > y) - (x < y);
}
/* 최근접 순위(nearest-rank) 백분위: 정렬 후 인덱스 ceil(pm * n / 1000) - 1 (pm = 퍼밀) */
static ps_t pct_nearest_rank(const ps_t *sorted, size_t n, unsigned pm) {
    size_t idx = (size_t)(((uint64_t)pm * n + 999) / 1000);
    return sorted[idx ? idx - 1 : 0];
}

/* TODO B3: lat[0..n) 를 scratch 에 복사해 정렬하고 mean(내림) / p50 / p99 / p99.9 / max. n == 0 → RC_EINVAL */
static int lat_stats(const ps_t *lat, size_t n, ps_t *scratch, lat_stats_t *st) {
    if (n == 0) return RC_EINVAL;
    int64_t sum = 0;
    for (size_t i = 0; i < n; i++) { scratch[i] = lat[i]; sum += lat[i]; }
    qsort(scratch, n, sizeof scratch[0], cmp_ps);
    st->mean = sum / (int64_t)n;
    st->p50  = pct_nearest_rank(scratch, n, 500);
    st->p99  = pct_nearest_rank(scratch, n, 990);
    st->p999 = pct_nearest_rank(scratch, n, 999);
    st->max  = scratch[n - 1];
    return RC_OK;
}

/* 결정적 요청 생성기: 1 ns 틱마다 확률 1/gap_ns 로 도착(베르누이 과정 = 이산 포아송), 뱅크는 균등 */
static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }
static void gen_requests(req_t *rq, size_t n, unsigned gap_ns, int nbanks, uint32_t seed) {
    uint32_t s = seed ? seed : 1u;
    uint32_t thr = (uint32_t)(0xFFFFFFFFu / gap_ns);
    ps_t t = 0;
    for (size_t i = 0; i < n; i++) {
        do { t += 1000; } while (xs32(&s) >= thr);
        rq[i].arr = t;
        rq[i].bank = (int)(xs32(&s) % (uint32_t)nbanks);
    }
}


#define N 2000000
static req_t g_rq[N];
static ps_t g_lat[N], g_scr[N];

typedef struct { const char *name; ref_cfg r; } mode_t_;

int main(void) {
    const ps_t BUSY = 46250, LAT = 30000;
    const mode_t_ modes[] = {
        { "no refresh",                 { 7800000, 0, 1 } },
        { "DDR4  8Gb all-bank 1x",      { 7800000, 350000, 1 } },
        { "DDR4 16Gb all-bank 1x",      { 7800000, 550000, 1 } },
        { "DDR4 16Gb all-bank 2x",      { 3900000, 550000, 1 } },
        { "DDR4 16Gb 8-REF batch",      { 8 * 7800000, 8 * 550000, 1 } },
        { "DDR5 16Gb REFab",            { 3900000, 295000, 1 } },
        { "DDR5 16Gb REFsb (4 slots)",  { 3900000, 130000, 4 } },
        { "per-bank 130ns (16 slots)",  { 3900000, 130000, 16 } },
    };
    const int NM = (int)(sizeof modes / sizeof modes[0]);

    /* Part A 표: 밀도 x 온도 → tRFC / tREFI */
    printf("== Part A: refresh overhead tRFC/tREFI (exact) ==\n");
    printf("%-6s %8s | %-16s %9s | %-16s %9s\n", "dens", "tRFC", "1x (7.8us)", "%", "2x (3.9us)", "%");
    for (int d = 2; d <= 16; d *= 2) {
        frac_t a, b;
        refresh_overhead(d, 25, &a); refresh_overhead(d, 90, &b);
        uint64_t ma = frac_milli_pct(a), mb = frac_milli_pct(b);
        char sa[32], sb[32];
        snprintf(sa, sizeof sa, "%llu/%llu", (unsigned long long)a.num, (unsigned long long)a.den);
        snprintf(sb, sizeof sb, "%llu/%llu", (unsigned long long)b.num, (unsigned long long)b.den);
        printf("%2d Gb  %5lld ns | %-16s %3llu.%03llu | %-16s %3llu.%03llu\n", d, (long long)(trfc1_ps(d) / 1000),
               sa, (unsigned long long)(ma / 1000), (unsigned long long)(ma % 1000),
               sb, (unsigned long long)(mb / 1000), (unsigned long long)(mb % 1000));
    }
    const unsigned gaps[2] = { 10, 5 };
    for (int g = 0; g < 2; g++) {
        gen_requests(g_rq, N, gaps[g], 16, 2026);
        double rho = (double)BUSY / (gaps[g] * 1000.0 * 16);
        printf("\n== load: mean gap %u ns, 16 banks, per-bank utilisation rho = %.3f, %d requests, span %.1f ms ==\n",
               gaps[g], rho, N, g_rq[N - 1].arr / 1e9);
        printf("%-27s %7s %7s | %8s %6s %7s %7s %7s | %6s %6s %6s\n", "mode", "blk%", "pushed%",
               "mean", "p50", "p99", "p99.9", "max", ">100ns", ">300ns", ">1us");
        for (int m = 0; m < NM; m++) {
            sim_cfg c = { 16, BUSY, LAT, modes[m].r };
            long pushed = sim_run(&c, g_rq, N, g_lat);
            if (pushed < 0) { printf("%s: invalid\n", modes[m].name); return 1; }
            lat_stats_t st;
            lat_stats(g_lat, N, g_scr, &st);
            long over100 = 0, over300 = 0, over1000 = 0;
            for (int i = 0; i < N; i++) {
                over100 += g_lat[i] > 100000; over300 += g_lat[i] > 300000; over1000 += g_lat[i] > 1000000;
            }
            frac_t f = frac_make((uint64_t)modes[m].r.dur, (uint64_t)modes[m].r.period);
            uint64_t mp = frac_milli_pct(f);
            printf("%-27s %3llu.%03llu %6.3f%% | %8.2f %6.1f %7.1f %7.1f %7.1f | %5.2f%% %5.2f%% %5.3f%%\n",
                   modes[m].name, (unsigned long long)(mp / 1000), (unsigned long long)(mp % 1000),
                   100.0 * pushed / N, st.mean / 1000.0, st.p50 / 1000.0, st.p99 / 1000.0, st.p999 / 1000.0,
                   st.max / 1000.0, 100.0 * over100 / N, 100.0 * over300 / N, 100.0 * over1000 / N);
        }
    }
    return 0;
}
