/* MEM-B05 리프레시: tREFI, tRFC와 성능 손실
 *
 * Part A: 밀도(2/4/8/16 Gb) · 온도(1x ≤ 85 °C, 2x 85–95 °C)별 리프레시 손실 비율 tRFC / tREFI 를
 *         정수 ps 로 계산해 기약분수 + 밀리퍼센트(4.487 % = 4487)로 돌려준다.
 * Part B: 뱅크 시뮬레이터 — 요청을 뱅크별 FCFS 로 처리하고, all-bank REF(모든 뱅크 tRFC 동안 막힘) /
 *         per-bank · same-bank REF(일부 뱅크만 짧게, 돌아가며)를 넣어 요청 지연 분포(mean/p50/p99/p99.9/max).
 * Part C: 메모리 컨트롤러 레지스터 값(ns → ck: tRFC 는 올림, tREFI 는 내림)과
 *         REF 명령 로그 검사기(최대 8개 연기 · 8개 당김, DDR4 규칙 — 대표값).
 *
 * 이 레슨의 정의 (JESD79-4 / 데이터시트 대표값 — 실제 부품은 데이터시트 확인):
 *   DDR4-3200AA: tCK = 625 ps. tREFI = 7.8 us (≤ 85 °C), 3.9 us (85–95 °C, 2x).
 *   tRFC1: 2 Gb 160 ns, 4 Gb 260 ns, 8 Gb 350 ns, 16 Gb 550 ns.
 *   요청 서비스(close-page, 뱅크 하나): 데이터 반환 tRCD + CL + 4 = 48 ck = 30 ns,
 *   뱅크 점유 tRC = 74 ck = 46.25 ns (ACT → 다음 ACT. 이 시점에 뱅크는 precharge 완료 → REF 가능).
 * 큰 분포 실험은 demo_refresh.c. 여기는 결정적(deterministic) 검사만 (< 2 s).
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c && ./a.out
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

/* ---------------- Part C: 컨트롤러 레지스터 · REF 로그 검사 ---------------- */

typedef struct { uint32_t trefi_ck, trfc_ck; } mc_ref_regs;

/* TODO C1: 컨트롤러에 쓸 값. tRFC 는 "최소 이만큼 기다려라" → 올림, tREFI 는 "평균 이 간격을 넘지 마라" → 내림 */
static int mc_refresh_regs(int density_gb, int temp_c, ps_t tck_ps, mc_ref_regs *out) {
    if (tck_ps <= 0) return RC_EINVAL;
    ps_t rfc = trfc1_ps(density_gb);
    if (rfc < 0) return RC_EINVAL;
    ps_t refi = trefi_ps_for_temp(temp_c);
    if (refi < 0) return (int)refi;
    out->trfc_ck  = (uint32_t)((rfc + tck_ps - 1) / tck_ps);
    out->trefi_ck = (uint32_t)(refi / tck_ps);
    return RC_OK;
}

typedef struct { int worst_postponed, worst_pulled; ps_t first_bad; } ref_report;

/* TODO C2: REF 시각 로그 t[0..n) (오름차순, 0 <= t <= horizon) 를 검사한다.
 *   due(x)    = floor(x / trefi)            (k 번째 REF 의 기한 = k * trefi)
 *   issued(x) = #{ REF : time <= x }
 *   연기(postponed) = due - issued 는 모든 x 에서 <= max_debt  → k*trefi 경계마다 확인
 *   당김(pulled-in) = issued - due 는 모든 x 에서 <= max_debt  → REF 직후마다 확인
 * 위반 개수를 돌려주고 rep 에 최댓값과 첫 위반 시각(-1 = 없음). 정렬/범위 오류는 RC_EINVAL. */
static int ref_trace_check(const ps_t *t, size_t n, ps_t trefi, ps_t horizon, int max_debt, ref_report *rep) {
    if (trefi <= 0 || horizon < 0) return RC_EINVAL;
    for (size_t i = 0; i < n; i++)
        if (t[i] < 0 || t[i] > horizon || (i && t[i] < t[i - 1])) return RC_EINVAL;
    int viol = 0;
    rep->worst_postponed = 0; rep->worst_pulled = 0; rep->first_bad = -1;
    for (size_t i = 0; i < n; i++) {                  /* 당김: REF 직후 */
        int pulled = (int)((int64_t)(i + 1) - t[i] / trefi);
        if (pulled > rep->worst_pulled) rep->worst_pulled = pulled;
        if (pulled > max_debt) { viol++; if (rep->first_bad < 0 || t[i] < rep->first_bad) rep->first_bad = t[i]; }
    }
    size_t j = 0;
    for (int64_t k = 1; k * trefi <= horizon; k++) {  /* 연기: 기한 경계 */
        ps_t x = k * trefi;
        while (j < n && t[j] <= x) j++;
        int owed = (int)(k - (int64_t)j);
        if (owed > rep->worst_postponed) rep->worst_postponed = owed;
        if (owed > max_debt) { viol++; if (rep->first_bad < 0 || x < rep->first_bad) rep->first_bad = x; }
    }
    return viol;
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* 느린 기준 구현: 창 목록을 하나씩 훑는다 (Part B 대조용) */
static ps_t ref_next_free_slow(const ref_cfg *r, int bank, ps_t t, ps_t busy) {
    if (r->dur == 0) return t;
    if (busy > r->period - r->dur) return -1;
    ps_t off = ref_offset(r, bank);
    for (int64_t k = 0;; k++) {
        ps_t ws = off + k * r->period, we = ws + r->dur;
        if (t + busy <= ws) return t;                 /* 이 창 전에 끝난다 */
        if (t < we) t = we;                           /* 겹친다 → 창 끝으로 */
    }
}

#define NR 200000
static req_t g_rq[NR];
static ps_t g_lat0[NR], g_lat1[NR], g_scr[NR];

int main(void) {
    /* ---- Part A ---- */
    frac_t f;
    CHECK(trefi_ps_for_temp(25) == 7800000 && trefi_ps_for_temp(85) == 7800000);
    CHECK(trefi_ps_for_temp(86) == 3900000 && trefi_ps_for_temp(95) == 3900000);
    CHECK(trefi_ps_for_temp(-40) == 7800000);
    CHECK(trefi_ps_for_temp(96) == RC_ETEMP && trefi_ps_for_temp(-41) == RC_ETEMP);
    f = frac_make(350, 7800);   CHECK(f.num == 7 && f.den == 156);
    f = frac_make(0, 7800);     CHECK(f.num == 0 && f.den == 1);
    f = frac_make(5, 0);        CHECK(f.den == 0);
    CHECK(refresh_overhead(8, 25, &f) == RC_OK && f.num == 7 && f.den == 156 && frac_milli_pct(f) == 4487);
    CHECK(refresh_overhead(16, 85, &f) == RC_OK && f.num == 11 && f.den == 156 && frac_milli_pct(f) == 7051);
    CHECK(refresh_overhead(16, 86, &f) == RC_OK && f.num == 11 && f.den == 78 && frac_milli_pct(f) == 14103);
    CHECK(refresh_overhead(4, 90, &f) == RC_OK && f.num == 1 && f.den == 15 && frac_milli_pct(f) == 6667);
    CHECK(refresh_overhead(2, 0, &f) == RC_OK && f.num == 4 && f.den == 195 && frac_milli_pct(f) == 2051);
    CHECK(refresh_overhead(16, 96, &f) == RC_ETEMP);
    CHECK(refresh_overhead(3, 25, &f) == RC_EINVAL);
    CHECK(refresh_overhead(12, 25, &f) == RC_EINVAL);
    f = frac_make(295000, 3900000); CHECK(f.num == 59 && f.den == 780 && frac_milli_pct(f) == 7564); /* DDR5 16Gb REFab */
    f = frac_make(130000, 3900000); CHECK(f.num == 1 && f.den == 30 && frac_milli_pct(f) == 3333);   /* DDR5 REFsb, 뱅크당 */
    CHECK(frac_milli_pct(frac_make(1, 8)) == 12500);
    CHECK(frac_milli_pct(frac_make(2, 3)) == 66667);                  /* 66666.67 → 반올림 */
    CHECK(frac_milli_pct(frac_make(1, 200000)) == 1);                 /* 0.5 → half-up */
    CHECK(frac_milli_pct(frac_make(1, 200001)) == 0);
    {   /* 1x → 2x 는 정확히 두 배, 밀도 2배마다 손실 증가 */
        frac_t a, b; int ok = 1;
        for (int d = 2; d <= 16; d *= 2) {
            ok &= refresh_overhead(d, 25, &a) == RC_OK && refresh_overhead(d, 90, &b) == RC_OK;
            ok &= b.num * a.den == 2 * a.num * b.den;
        }
        CHECK(ok);
    }

    /* ---- Part B: ref_next_free ---- */
    const ref_cfg AB16 = { 7800000, 550000, 1 };      /* DDR4 16 Gb all-bank 1x */
    const ref_cfg SB5  = { 3900000, 130000, 4 };      /* DDR5식 same-bank (4 슬롯, 모델 가정) */
    const ref_cfg NONE = { 7800000, 0, 1 };
    const ps_t BUSY = 46250, LAT = 30000;
    CHECK(ref_next_free(&AB16, 0, 0, BUSY) == 550000);                /* 창 안 → 창 끝 */
    CHECK(ref_next_free(&AB16, 7, 549999, BUSY) == 550000);
    CHECK(ref_next_free(&AB16, 0, 550000, BUSY) == 550000);
    CHECK(ref_next_free(&AB16, 0, 7800000 - BUSY, BUSY) == 7800000 - BUSY);   /* 딱 맞게 끝남 = OK */
    CHECK(ref_next_free(&AB16, 0, 7800000 - BUSY + 1, BUSY) == 8350000);      /* 1 ps 넘침 → 다음 창 뒤 */
    CHECK(ref_next_free(&AB16, 3, 7800000, BUSY) == 8350000);
    CHECK(ref_next_free(&NONE, 0, 123, BUSY) == 123);
    CHECK(ref_next_free(&(ref_cfg){ 100000, 60000, 1 }, 0, 0, BUSY) == -1);   /* busy > period - dur */
    CHECK(ref_next_free(&SB5, 1, 975000, BUSY) == 1105000);           /* 슬롯 1: [975, 1105) ns */
    CHECK(ref_next_free(&SB5, 5, 975000, BUSY) == 1105000);           /* 5 % 4 = 1 → 같은 창 */
    CHECK(ref_next_free(&SB5, 2, 975000, BUSY) == 975000);            /* 슬롯 2 는 1950 ns 부터 */
    CHECK(ref_next_free(&SB5, 0, 0, BUSY) == 130000);
    CHECK(ref_next_free(&SB5, 3, 2925000 - BUSY + 1, BUSY) == 3055000);
    {   /* 무작위 대조: 빠른 O(1) vs 느린 창 훑기 */
        const ref_cfg cf[4] = { AB16, SB5, { 3900000, 550000, 1 }, { 3900000, 130000, 16 } };
        uint32_t s = 12345; int bad = 0;
        for (int i = 0; i < 40000; i++) {
            const ref_cfg *r = &cf[i % 4];
            int bank = (int)(xs32(&s) % 16);
            ps_t t = (ps_t)(xs32(&s) % 40000000u);
            ps_t busy = (i & 1) ? BUSY : (ps_t)(xs32(&s) % 200000u);
            if (ref_next_free(r, bank, t, busy) != ref_next_free_slow(r, bank, t, busy)) bad++;
        }
        printf("ref_next_free vs slow scan: 40000 cases, mismatches=%d\n", bad);
        CHECK(bad == 0);
    }

    /* ---- Part B: sim_run (손으로 계산한 경우) ---- */
    {
        const req_t rq[5] = { { 0, 0 }, { 0, 1 }, { 400000, 0 }, { 400000, 0 }, { 7760000, 2 } };
        ps_t lat[5];
        sim_cfg c = { 16, BUSY, LAT, { 7800000, 350000, 1 } };       /* 8 Gb all-bank */
        CHECK(sim_run(&c, rq, 5, lat) == 3);
        CHECK(lat[0] == 380000 && lat[1] == 380000);                  /* REF 끝 350 ns + 30 ns */
        CHECK(lat[2] == 30000);                                       /* 뱅크 0 은 396.25 ns 에 비었다 */
        CHECK(lat[3] == 76250);                                       /* 앞 요청 뒤에 줄 섬: 46.25 + 30 */
        CHECK(lat[4] == 420000);                                      /* 7806.25 > 7800 → 8150 ns 에 시작 */
        c.ref = NONE;
        CHECK(sim_run(&c, rq, 5, lat) == 0);
        CHECK(lat[0] == 30000 && lat[1] == 30000 && lat[2] == 30000 && lat[3] == 76250 && lat[4] == 30000);
        const req_t badrq[1] = { { 0, 16 } };
        CHECK(sim_run(&c, badrq, 1, lat) == RC_EINVAL);
        c.ref = (ref_cfg){ 100000, 60000, 1 };
        CHECK(sim_run(&c, rq, 5, lat) == RC_EINVAL);
    }

    /* ---- Part B: lat_stats ---- */
    {
        const ps_t a[5] = { 5, 1, 4, 2, 3 }; ps_t scr[5]; lat_stats_t st;
        CHECK(lat_stats(a, 5, scr, &st) == RC_OK && st.mean == 3 && st.p50 == 3 && st.p99 == 5 && st.max == 5);
        CHECK(lat_stats(a, 1, scr, &st) == RC_OK && st.mean == 5 && st.p50 == 5 && st.p999 == 5);
        CHECK(lat_stats(a, 0, scr, &st) == RC_EINVAL);
        static ps_t b[1000], sb[1000];
        uint32_t s = 7;
        for (int i = 0; i < 1000; i++) b[i] = i + 1;
        for (int i = 999; i > 0; i--) { int j = (int)(xs32(&s) % (uint32_t)(i + 1)); ps_t x = b[i]; b[i] = b[j]; b[j] = x; }
        CHECK(lat_stats(b, 1000, sb, &st) == RC_OK && st.mean == 500 && st.p50 == 500 && st.p99 == 990 &&
              st.p999 == 999 && st.max == 1000);
    }

    /* ---- Part B: 무작위 스트림 불변식 (리프레시는 어떤 요청도 빠르게 만들지 못한다) ---- */
    {
        gen_requests(g_rq, NR, 5, 16, 2026);
        sim_cfg c0 = { 16, BUSY, LAT, NONE };
        const ref_cfg modes[4] = { { 7800000, 350000, 1 }, AB16, { 3900000, 295000, 1 }, SB5 };
        long p0 = sim_run(&c0, g_rq, NR, g_lat0);
        CHECK(p0 == 0);
        int mono = 1, floor_ok = 1; ps_t prev_mean = -1; lat_stats_t s0, s1;
        lat_stats(g_lat0, NR, g_scr, &s0);
        for (int m = 0; m < 4; m++) {
            sim_cfg c1 = c0; c1.ref = modes[m];
            long p = sim_run(&c1, g_rq, NR, g_lat1);
            for (int i = 0; i < NR; i++) { mono &= g_lat1[i] >= g_lat0[i]; floor_ok &= g_lat1[i] >= LAT; }
            lat_stats(g_lat1, NR, g_scr, &s1);
            if (m == 0) prev_mean = s1.mean;
            if (m == 1) CHECK(s1.mean > prev_mean);                   /* 16 Gb(550) 가 8 Gb(350) 보다 나쁨 */
            CHECK(p > 0 && s1.mean > s0.mean && s1.max >= s0.max);    /* same-bank 는 max 가 같을 수도 있다 */
        }
        CHECK(mono);
        CHECK(floor_ok);
        CHECK(g_rq[NR - 1].arr > (ps_t)NR * 4000 && g_rq[NR - 1].arr < (ps_t)NR * 6000);  /* 평균 간격 ≈ 5 ns */
    }

    /* ---- Part C: 컨트롤러 레지스터 ---- */
    mc_ref_regs rg;
    CHECK(mc_refresh_regs(8, 25, TCK_PS, &rg) == RC_OK && rg.trefi_ck == 12480 && rg.trfc_ck == 560);
    CHECK(mc_refresh_regs(16, 90, TCK_PS, &rg) == RC_OK && rg.trefi_ck == 6240 && rg.trfc_ck == 880);
    CHECK(mc_refresh_regs(4, 25, TCK_PS, &rg) == RC_OK && rg.trfc_ck == 416);
    CHECK(mc_refresh_regs(2, 25, TCK_PS, &rg) == RC_OK && rg.trfc_ck == 256);
    CHECK(mc_refresh_regs(8, 25, 833, &rg) == RC_OK && rg.trefi_ck == 9363 && rg.trfc_ck == 421);  /* DDR4-2400 */
    CHECK((ps_t)rg.trefi_ck * 833 <= 7800000 && (ps_t)rg.trfc_ck * 833 >= 350000);
    CHECK(mc_refresh_regs(16, 25, 750, &rg) == RC_OK && rg.trefi_ck == 10400 && rg.trfc_ck == 734); /* DDR4-2666 */
    CHECK(mc_refresh_regs(8, 100, TCK_PS, &rg) == RC_ETEMP);
    CHECK(mc_refresh_regs(6, 25, TCK_PS, &rg) == RC_EINVAL);
    CHECK(mc_refresh_regs(8, 25, 0, &rg) == RC_EINVAL);

    /* ---- Part C: REF 로그 검사 ---- */
    {
        const ps_t T = TREFI_1X_PS, RFC = 350000;
        static ps_t tr[256]; size_t n; ref_report rp;
        n = 0; for (int k = 1; k <= 100; k++) tr[n++] = k * T;                 /* 정시 */
        CHECK(ref_trace_check(tr, n, T, 100 * T, 8, &rp) == 0 && rp.worst_postponed == 0 && rp.worst_pulled == 0);
        n = 0; for (int k = 0; k < 100; k++) tr[n++] = k * T;                  /* 0 부터: 항상 1개 앞섬 */
        CHECK(ref_trace_check(tr, n, T, 100 * T, 8, &rp) == 0 && rp.worst_pulled == 1);
        /* 8개 연기: T 다음 10T 까지 비우고, 10T 에 9개 몰아서 (간격 9T = 허용 최대) */
        n = 0; tr[n++] = T;
        for (int j = 0; j < 9; j++) tr[n++] = 10 * T + j * RFC;
        for (int k = 11; k <= 40; k++) tr[n++] = k * T;
        CHECK(ref_trace_check(tr, n, T, 40 * T, 8, &rp) == 0 && rp.worst_postponed == 8);
        /* 9개 연기: 몰아치기가 1 ps 늦다 → 10T 에 빚 9 */
        n = 0; tr[n++] = T;
        for (int j = 0; j < 9; j++) tr[n++] = 10 * T + 1 + j * RFC;
        for (int k = 11; k <= 40; k++) tr[n++] = k * T;
        CHECK(ref_trace_check(tr, n, T, 40 * T, 8, &rp) == 1 && rp.worst_postponed == 9 && rp.first_bad == 10 * T);
        /* 8개 당김: 시작하자마자 8개, 그다음 9T 부터 정시 */
        n = 0; for (int j = 0; j < 8; j++) tr[n++] = j * RFC;
        for (int k = 9; k <= 40; k++) tr[n++] = k * T;
        CHECK(ref_trace_check(tr, n, T, 40 * T, 8, &rp) == 0 && rp.worst_pulled == 8 && rp.worst_postponed == 0);
        /* 9개 당김 → 위반 */
        n = 0; for (int j = 0; j < 9; j++) tr[n++] = j * RFC;
        for (int k = 10; k <= 40; k++) tr[n++] = k * T;
        CHECK(ref_trace_check(tr, n, T, 40 * T, 8, &rp) == 1 && rp.worst_pulled == 9 && rp.first_bad == 8 * RFC);
        /* 8개씩 몰아치기 (9T, 17T, 25T, ... 에 8개 연속): 빚이 8 까지 쌓였다 갚기 — 규칙 안 */
        n = 0; for (int m = 1; m <= 5; m++) for (int j = 0; j < 8; j++) tr[n++] = (8 * m + 1) * T + j * RFC;
        CHECK(ref_trace_check(tr, n, T, 41 * T + 8 * RFC, 8, &rp) == 0 && rp.worst_postponed == 8);
        /* 간격은 늘 1.5T (< 9T) 인데 빚이 쌓인다: 25T 에 빚 9 → 간격만 보는 검사기는 못 잡는다 */
        n = 0; for (int k = 1; k <= 40; k++) tr[n++] = k * T + k * T / 2;
        CHECK(ref_trace_check(tr, n, T, 60 * T, 8, &rp) > 0 && rp.first_bad == 25 * T);
        /* REF 가 아예 없음: 9T 부터 매 경계 위반 → 9T..20T = 12 개 */
        CHECK(ref_trace_check(tr, 0, T, 20 * T, 8, &rp) == 12 && rp.first_bad == 9 * T);
        const ps_t uns[3] = { 3, 2, 5 };
        CHECK(ref_trace_check(uns, 3, T, 10 * T, 8, &rp) == RC_EINVAL);
        CHECK(ref_trace_check(uns, 1, 0, 10 * T, 8, &rp) == RC_EINVAL);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
