/* MEM-B05 추가 연습 — 한 번에 검증
 *
 * 연습 1: 고립된 요청 하나의 최악 지연과 "리프레시에 걸리는 도착 시각" 개수 (한 주기 전수, 1 ps 단위)
 * 연습 2: DDR4 FGR(fine granularity refresh) 1x/2x/4x 손실 비율 (대표값)
 * 연습 3: 리프레시로 잃는 대역폭 MB/s (정수, 내림)
 * 연습 4: 리텐션 산수 — 64 ms / 8192 REF, REF 한 번에 뱅크당 몇 행, 잘못 쓴 tREFI 레지스터 검출
 * 연습 5: 온도 히스테리시스로 1x ↔ 2x 리프레시 전환 (센서 떨림에도 안 흔들리게)
 *
 * 이 레슨의 정의 (JESD79-4 / 데이터시트 대표값 — 실제 부품은 데이터시트 확인)
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c && ./a.out
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef int64_t ps_t;
enum { RC_OK = 0, RC_EINVAL = -1 };

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

typedef struct { uint64_t num, den; } frac_t;
static uint64_t gcd_u64(uint64_t a, uint64_t b) { while (b) { uint64_t t = a % b; a = b; b = t; } return a; }
static frac_t frac_make(uint64_t num, uint64_t den) {
    if (den == 0) return (frac_t){ 0, 0 };
    uint64_t g = gcd_u64(num, den);
    return (frac_t){ num / g, den / g };
}
static uint64_t frac_milli_pct(frac_t f) { return f.den ? (f.num * 200000u + f.den) / (2u * f.den) : 0; }

/* (solution.c Part B 축약판) 주기적 막힘 창과 ref_next_free */
typedef struct { ps_t period, dur; int nslots; } ref_cfg;
static ps_t ref_offset(const ref_cfg *r, int bank) { return (ps_t)(bank % r->nslots) * (r->period / r->nslots); }
static ps_t ref_next_free(const ref_cfg *r, int bank, ps_t t, ps_t busy) {
    if (r->dur == 0) return t;
    if (busy > r->period - r->dur) return -1;
    ps_t off = ref_offset(r, bank), ws;
    if (t < off) ws = off;
    else {
        ws = off + ((t - off) / r->period) * r->period;
        if (t < ws + r->dur) t = ws + r->dur;
        ws += r->period;
    }
    return (t + busy <= ws) ? t : ws + r->dur;
}

/* ---- 연습 1 ---- */
typedef struct { ps_t worst_lat, worst_arr; int64_t n_delayed; } iso_result;
/* 한가한 뱅크에 요청 하나: [0, period) 의 모든 도착 시각(1 ps)을 전수 */
static iso_result isolated_scan(const ref_cfg *r, int bank, ps_t busy, ps_t lat) {
    iso_result o = { 0, 0, 0 };
    for (ps_t t = 0; t < r->period; t++) {
        ps_t s = ref_next_free(r, bank, t, busy);
        if (s != t) o.n_delayed++;
        if (s + lat - t > o.worst_lat) { o.worst_lat = s + lat - t; o.worst_arr = t; }
    }
    return o;
}

/* ---- 연습 2 ---- (DDR4 FGR: 모드 1/2/4 → tREFI/모드, tRFC1/2/4 대표값) */
static int fgr_overhead(int density_gb, int mode, frac_t *out) {
    static const struct { int d; ps_t rfc[3]; } T[] = {
        { 4,  { 260000, 160000, 110000 } },
        { 8,  { 350000, 260000, 160000 } },
        { 16, { 550000, 350000, 260000 } },
    };
    int mi = mode == 1 ? 0 : mode == 2 ? 1 : mode == 4 ? 2 : -1;
    if (mi < 0) return RC_EINVAL;
    for (unsigned i = 0; i < sizeof T / sizeof T[0]; i++)
        if (T[i].d == density_gb) { *out = frac_make((uint64_t)T[i].rfc[mi], (uint64_t)(7800000 / mode)); return RC_OK; }
    return RC_EINVAL;
}

/* ---- 연습 3 ---- */
static uint64_t lost_mbps(uint64_t peak_mbps, frac_t f) { return f.den ? peak_mbps * f.num / f.den : 0; }

/* ---- 연습 4 ---- */
static ps_t trefi_from_retention(ps_t retention_ps, int nref) { return nref > 0 ? retention_ps / nref : RC_EINVAL; }
static int rows_per_ref(int rows_per_bank, int nref) { return (nref > 0 && rows_per_bank % nref == 0) ? rows_per_bank / nref : RC_EINVAL; }
/* 컨트롤러가 실제로 쓰는 tREFI_ck 로 한 행이 다시 리프레시될 때까지 걸리는 시간 (8192 REF 한 바퀴) */
static ps_t row_interval_ps(uint32_t trefi_ck, ps_t tck_ps, int nref) { return (ps_t)trefi_ck * tck_ps * nref; }

/* ---- 연습 5 ---- */
typedef enum { REF_1X, REF_2X, REF_OVERTEMP } ref_rate;
/* 1x → 2x : T >= 83 (85 °C 스펙 경계 앞 2 °C 가드 밴드)   2x → 1x : T <= 78
 * T > 95 → OVERTEMP (2x 유지 + 경보, 쓰로틀 요청)          OVERTEMP → 2x : T <= 93 */
static ref_rate ref_rate_update(ref_rate cur, int temp_c) {
    if (temp_c > 95) return REF_OVERTEMP;
    switch (cur) {
    case REF_1X:       return temp_c >= 83 ? REF_2X : REF_1X;
    case REF_2X:       return temp_c <= 78 ? REF_1X : REF_2X;
    case REF_OVERTEMP: return temp_c <= 93 ? REF_2X : REF_OVERTEMP;
    }
    return REF_2X;
}
static ref_rate ref_rate_naive(int temp_c) { return temp_c > 95 ? REF_OVERTEMP : temp_c > 85 ? REF_2X : REF_1X; }
static bool rate_is_safe(ref_rate r, int temp_c) { return temp_c <= 85 || r != REF_1X; }

int main(void) {
    const ps_t BUSY = 46250, LAT = 30000;

    /* 연습 1 */
    {
        const ref_cfg AB16 = { 7800000, 550000, 1 }, SB5 = { 3900000, 130000, 4 };
        iso_result a = isolated_scan(&AB16, 0, BUSY, LAT);
        printf("ex1 all-bank 16Gb: delayed arrivals=%lld / %lld ps, worst latency=%lld ps at t=%lld\n",
               (long long)a.n_delayed, (long long)AB16.period, (long long)a.worst_lat, (long long)a.worst_arr);
        CHECK(a.n_delayed == 550000 + BUSY - 1);                     /* D + busy - 1 */
        CHECK(a.worst_lat == 550000 + BUSY - 1 + LAT);               /* 626249 ps */
        CHECK(a.worst_arr == 7800000 - BUSY + 1);
        iso_result b = isolated_scan(&SB5, 1, BUSY, LAT);
        printf("ex1 same-bank DDR5: delayed arrivals=%lld / %lld ps, worst latency=%lld ps\n",
               (long long)b.n_delayed, (long long)SB5.period, (long long)b.worst_lat);
        CHECK(b.n_delayed == 130000 + BUSY - 1 && b.worst_lat == 130000 + BUSY - 1 + LAT);
    }

    /* 연습 2 */
    {
        frac_t f;
        CHECK(fgr_overhead(8, 1, &f) == RC_OK && frac_milli_pct(f) == 4487);
        CHECK(fgr_overhead(8, 2, &f) == RC_OK && f.num == 1 && f.den == 15 && frac_milli_pct(f) == 6667);
        CHECK(fgr_overhead(8, 4, &f) == RC_OK && f.num == 16 && f.den == 195 && frac_milli_pct(f) == 8205);
        CHECK(fgr_overhead(16, 4, &f) == RC_OK && f.num == 2 && f.den == 15 && frac_milli_pct(f) == 13333);
        CHECK(fgr_overhead(16, 3, &f) == RC_EINVAL && fgr_overhead(2, 1, &f) == RC_EINVAL);
        int ok = 1;                                                  /* 모드가 커질수록 총 손실은 늘어난다 */
        for (int d = 4; d <= 16; d *= 2) {
            frac_t a, b, c; fgr_overhead(d, 1, &a); fgr_overhead(d, 2, &b); fgr_overhead(d, 4, &c);
            ok &= frac_milli_pct(a) < frac_milli_pct(b) && frac_milli_pct(b) < frac_milli_pct(c);
        }
        CHECK(ok);
    }

    /* 연습 3 */
    CHECK(lost_mbps(25600, frac_make(350, 7800)) == 1148);           /* DDR4-3200 x64, 8 Gb 1x */
    CHECK(lost_mbps(25600, frac_make(550, 3900)) == 3610);           /* 16 Gb 2x */
    CHECK(lost_mbps(38400, frac_make(295, 3900)) == 2904);           /* DDR5-4800 DIMM, REFab 16 Gb */

    /* 연습 4 */
    CHECK(trefi_from_retention(64000000000LL, 8192) == 7812500);     /* 7.8125 us — 스펙은 7.8 us 로 표기 */
    CHECK(trefi_from_retention(32000000000LL, 8192) == 3906250);
    CHECK(trefi_from_retention(64000000000LL, 0) == RC_EINVAL);
    CHECK(rows_per_ref(32768, 8192) == 4 && rows_per_ref(65536, 8192) == 8 && rows_per_ref(131072, 8192) == 16);
    CHECK(rows_per_ref(1000, 8192) == RC_EINVAL);
    CHECK(row_interval_ps(12480, 625, 8192) == 63897600000LL);       /* 63.8976 ms ≤ 64 ms OK */
    CHECK(row_interval_ps(12480, 625, 8192) <= 64000000000LL);
    CHECK(row_interval_ps(14400, 625, 8192) == 73728000000LL);       /* 9 us 로 잘못 씀 → 73.7 ms */
    CHECK(row_interval_ps(14400, 625, 8192) > 64000000000LL);
    CHECK(row_interval_ps(12480, 625, 8192) > 32000000000LL);        /* 고온에서 1x 그대로면 32 ms 위반 */

    /* 연습 5: 84 ↔ 86 으로 떨리는 센서 */
    {
        int temps[200]; int n = 0;
        for (int i = 0; i < 20; i++) temps[n++] = 60 + 2 * i;        /* 60 → 98 상승 */
        for (int i = 0; i < 100; i++) temps[n++] = (i & 1) ? 86 : 84;
        for (int i = 0; i < 30; i++) temps[n++] = 86 - i;            /* 86 → 57 하강 */
        ref_rate h = REF_1X, nv = REF_1X; int sw_h = 0, sw_n = 0, safe = 1;
        for (int i = 0; i < n; i++) {
            ref_rate h2 = ref_rate_update(h, temps[i]), n2 = ref_rate_naive(temps[i]);
            sw_h += h2 != h; sw_n += n2 != nv; h = h2; nv = n2;
            safe &= rate_is_safe(h, temps[i]);
        }
        printf("ex5 mode switches: hysteresis=%d naive=%d\n", sw_h, sw_n);
        CHECK(safe);
        CHECK(sw_h == 4);                                            /* 1x→2x, →OVERTEMP, →2x, →1x */
        CHECK(sw_n > 50);
        CHECK(ref_rate_update(REF_1X, 96) == REF_OVERTEMP && ref_rate_update(REF_OVERTEMP, 94) == REF_OVERTEMP);
        CHECK(ref_rate_update(REF_2X, 79) == REF_2X && ref_rate_update(REF_2X, 78) == REF_1X);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
