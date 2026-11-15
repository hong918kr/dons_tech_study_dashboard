/* SIV-A03 추가 연습 (C 세트) — 전부 PASS, exit 0
 *   P1 Configuration 인코딩과 샘플 주기 계산
 *   P2 에너지 적산: 절사 오차 없는 고정소수점 누산기
 *   P3 Current_LSB 선택: 1-2-5 단계에서 CAL이 맞는 가장 작은 값
 *
 *   cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/SIV-A03_pr && /tmp/SIV-A03_pr
 */
#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

static int g_total, g_pass;
static void check(int cond, const char *fmt, ...)
{
    va_list ap;
    g_total++;
    if (cond) g_pass++;
    printf("%s ", cond ? "PASS" : "FAIL");
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
}

/* ---------- P1 Configuration: [14:12]은 예약(기본 100b), AVG[11:9], VBUSCT[8:6], VSHCT[5:3], MODE[2:0] ---------- */
static const uint32_t CT_US[8] = {140, 204, 332, 588, 1100, 2116, 4156, 8244};
static const uint32_t AVG_N[8] = {1, 4, 16, 64, 128, 256, 512, 1024};

static uint16_t cfg_encode(unsigned avg, unsigned vbusct, unsigned vshct, unsigned mode)
{
    return (uint16_t)(0x4000u | ((avg & 7u) << 9) | ((vbusct & 7u) << 6) | ((vshct & 7u) << 3) | (mode & 7u));
}

/* 연속 모드에서 '평균까지 끝난 샘플 1개'의 주기 [us]. MODE 7=둘 다, 5=shunt만, 6=bus만, 그 외 0 */
static uint32_t cfg_sample_period_us(uint16_t cfg)
{
    unsigned avg = (cfg >> 9) & 7u, vb = (cfg >> 6) & 7u, vs = (cfg >> 3) & 7u, mode = cfg & 7u;
    uint32_t t = 0;
    if (mode == 7) t = CT_US[vb] + CT_US[vs];
    else if (mode == 5) t = CT_US[vs];
    else if (mode == 6) t = CT_US[vb];
    return t * AVG_N[avg];
}

/* ---------- P2 에너지 누산: 샘플마다 P[uW]*dt[us] → uJ, 나머지를 이월해 드리프트 없음 ---------- */
typedef struct { uint64_t uj; uint64_t rem; } energy_t; /* rem 단위 uW*us, 1 uJ = 1e6 uW*us */

static void energy_add(energy_t *e, uint64_t p_uw, uint64_t dt_us)
{
    e->rem += p_uw * dt_us;
    e->uj += e->rem / 1000000u;
    e->rem %= 1000000u;
}

/* ---------- P3 Current_LSB 선택: {1,2,5}x10^k uA 에서 lsb >= Imax/32768 이고 CAL이 1..0x7FFF인 최소값. 없으면 0 ---------- */
static uint32_t pick_lsb_ua(uint64_t imax_ua, uint32_t rshunt_uohm)
{
    static const uint32_t m[3] = {1, 2, 5};
    for (uint32_t dec = 1; dec <= 100000000u; dec *= 10) {
        for (int i = 0; i < 3; i++) {
            uint64_t lsb = (uint64_t)m[i] * dec;
            if (lsb * 32768u < imax_ua) continue;
            uint64_t den = lsb * rshunt_uohm;
            uint64_t cal = (5120000000ULL + den / 2) / den;
            if (cal >= 1 && cal <= 0x7FFFu) return (uint32_t)lsb;
        }
    }
    return 0;
}

int main(void)
{
    puts("--- P1 configuration");
    check(cfg_encode(0, 4, 4, 7) == 0x4127, "POR-equivalent encode = 0x4127 (act 0x%04X)", cfg_encode(0, 4, 4, 7));
    check(cfg_sample_period_us(0x4127) == 2200, "default sample period 2200 us (act %u)", cfg_sample_period_us(0x4127));
    uint16_t c2 = cfg_encode(3, 7, 7, 7); /* AVG 64, 8.244 ms x 2 */
    check(cfg_sample_period_us(c2) == (8244u + 8244u) * 64u, "AVG64 with 8.244 ms conversions = %u us", cfg_sample_period_us(c2));
    check(cfg_sample_period_us(cfg_encode(1, 0, 2, 5)) == 332u * 4u, "shunt-only: 332 us x AVG 4 (act %u)", cfg_sample_period_us(cfg_encode(1, 0, 2, 5)));
    check(cfg_sample_period_us(cfg_encode(0, 0, 0, 0)) == 0, "shutdown mode has no period");

    puts("--- P2 energy accumulator");
    energy_t e = {0, 0};
    uint64_t naive = 0;
    for (int i = 0; i < 1000; i++) {
        energy_add(&e, 1500000u, 1u); /* 1.5 W * 1 us = 1.5 uJ */
        naive += 1500000u * 1u / 1000000u; /* 샘플마다 절사 */
    }
    check(e.uj == 1500 && e.rem == 0, "exact energy 1500 uJ (act %" PRIu64 ")", e.uj);
    check(naive == 1000, "per-sample truncation loses 33%% (naive %" PRIu64 " uJ)", naive);
    energy_t e2 = {0, 0};
    for (int i = 0; i < 7; i++) energy_add(&e2, 333333u, 1000u); /* 0.333333 mJ */
    check(e2.uj == 2333 && e2.rem == 331000, "remainder carried (%" PRIu64 " uJ + %" PRIu64 ")", e2.uj, e2.rem);

    puts("--- P3 current LSB pick");
    static const struct { uint64_t imax_ua; uint32_t r; uint32_t lsb; } t[] = {
        {15000000, 2000, 500}, {5000000, 10000, 200}, {30000000, 500, 1000},
        {1000, 100000, 2},     {40000000, 1000, 2000}};
    for (size_t i = 0; i < sizeof t / sizeof t[0]; i++) {
        uint32_t got = pick_lsb_ua(t[i].imax_ua, t[i].r);
        check(got == t[i].lsb, "imax=%" PRIu64 "uA R=%uuohm -> lsb %u uA (act %u)", t[i].imax_ua, t[i].r, t[i].lsb, got);
    }
    check(pick_lsb_ua(100000000u, 4000000000u) == 0, "impossible combination returns 0 (CAL rounds to 0 for every legal LSB)");

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
