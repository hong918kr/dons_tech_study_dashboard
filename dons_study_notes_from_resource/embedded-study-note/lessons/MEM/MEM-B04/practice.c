/* MEM-B04 추가 연습 — 한 번에 검증
 *
 * 연습 1: 속도 등급 이름(2666, 8533)은 반올림된 숫자 → 정확한 유리수 MT/s, 정확한 피크, "채널 몇 개?" 함정
 * 연습 2: ECC 오버헤드 — DDR4 ECC DIMM 72/64, DDR5 ECC DIMM 2×40, DDR5 on-die ECC 136/128
 * 연습 3: burst 길이를 CK 사이클로 — DDR4 BL8 = 4 tCK, LPDDR5 BL16 @ WCK:CK 4:1 = 2 CK
 * 연습 4: 라인 크기 32 / 64 / 128 B (GPU 섹터 / x86 / Apple M6) × 세대 → burst 수와 효율
 * 연습 5: 1 TB/s 를 내려면 데이터 핀이 몇 개? DDR5-6400 vs LPDDR5X-8533 vs HBM3-6400
 *
 * 수치 = 이 레슨의 정의 (JEDEC 대표값 — 데이터시트 확인). 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint64_t gcd64(uint64_t a, uint64_t b) { while (b) { uint64_t t = a % b; a = b; b = t; } return a; }

/* (MEM-B04 solution.c 축약판) */
static uint32_t burst_bytes(uint32_t ch_bits, uint32_t bl) {
    if (ch_bits == 0 || ch_bits % 8u != 0 || bl == 0) return 0;
    return (ch_bits / 8u) * bl;
}
static int bursts_per_line(uint32_t ch_bits, uint32_t bl, uint32_t line) {
    uint32_t g = burst_bytes(ch_bits, bl);
    if (g == 0 || line == 0) return -1;
    return (int)((line + g - 1u) / g);
}
static int line_eff_permille(uint32_t ch_bits, uint32_t bl, uint32_t line) {
    int n = bursts_per_line(ch_bits, bl, line);
    return n < 0 ? -1 : (int)((uint64_t)line * 1000u / ((uint64_t)n * burst_bytes(ch_bits, bl)));
}

/* ---- 연습 1: 정확한 데이터율 ----
 * 관찰 규칙(이 레슨): JEDEC 속도 등급의 실제 데이터율은 100/3 MT/s 의 배수이고 (2666.67 = 8000/3,
 * 8533.33 = 25600/3), 이름은 그 값을 정수로 반올림/버림한 것. → 3 × 이름을 100 단위로 반올림해 / 3.
 * 이름과 1 MT/s 이상 차이 나면 이 격자 위의 등급이 아니므로 -1. */
static int grade_exact(uint32_t nominal, uint64_t *num, uint64_t *den) {
    if (nominal == 0 || num == NULL || den == NULL) return -1;
    uint64_t n = ((uint64_t)nominal * 3u + 50u) / 100u * 100u, d = 3;
    uint64_t diff = n > 3u * nominal ? n - 3u * nominal : 3u * nominal - n;
    if (diff >= 3) return -1;                       /* |n/3 − nominal| ≥ 1 */
    uint64_t g = gcd64(n, d);
    *num = n / g; *den = d / g;
    return 0;
}
/* 정확한 피크 [MB/s] = (num/den) × bytes × ch, 기약분수로 */
static void peak_exact(uint64_t num, uint64_t den, uint32_t bus_bits, uint32_t ch, uint64_t *pn, uint64_t *pd) {
    uint64_t n = num * (bus_bits / 8u) * ch, g = gcd64(n, den);
    *pn = n / g; *pd = den / g;
}
/* target MB/s 를 채우는 데 필요한 채널 수 = ceil(target / 채널당 피크), 채널당 피크 = (num/den)·bytes */
static uint64_t channels_needed(uint64_t target_mbps, uint64_t num, uint64_t den, uint32_t ch_bits) {
    uint64_t per_num = num * (ch_bits / 8u);                /* 채널당 = per_num / den */
    return (target_mbps * den + per_num - 1u) / per_num;
}

/* ---- 연습 2: ECC 오버헤드 [‱ = 1/10000] ---- */
static uint32_t ecc_overhead_bp(uint32_t data_bits, uint32_t ecc_bits) {
    return data_bits ? (uint32_t)((uint64_t)ecc_bits * 10000u / data_bits) : 0;
}
/* DIMM 전체 데이터 버스 폭 (ECC 포함) */
static uint32_t dimm_bus_bits(bool ddr5, bool ecc) {
    return ddr5 ? 2u * (32u + (ecc ? 8u : 0u)) : 64u + (ecc ? 8u : 0u);
}

/* ---- 연습 3: burst = 몇 CK? ratio 1 = 데이터가 CK 양 에지(DDR4/5), 2·4 = LPDDR5 WCK:CK.
 * 한 CK 주기에 2 × ratio beat → BL / (2·ratio). 나누어떨어지지 않으면 -1 */
static int burst_ck(uint32_t bl, uint32_t ratio) {
    if (bl == 0 || (ratio != 1 && ratio != 2 && ratio != 4)) return -1;
    return bl % (2u * ratio) ? -1 : (int)(bl / (2u * ratio));
}

int main(void) {
    /* 연습 1 */
    uint64_t n = 0, d = 0, pn = 0, pd = 0;
    CHECK(grade_exact(2666, &n, &d) == 0 && n == 8000 && d == 3);
    CHECK(grade_exact(8533, &n, &d) == 0 && n == 25600 && d == 3);
    CHECK(grade_exact(7467, &n, &d) == 0 && n == 22400 && d == 3);   /* 7466.67 → 이름은 반올림 */
    CHECK(grade_exact(3200, &n, &d) == 0 && n == 3200 && d == 1);
    CHECK(grade_exact(3250, &n, &d) == -1);
    grade_exact(2666, &n, &d);
    peak_exact(n, d, 64, 1, &pn, &pd);
    CHECK(pn == 64000 && pd == 3);                                   /* 21333.33 MB/s (이름으로 계산: 21328) */
    grade_exact(8533, &n, &d);
    CHECK(channels_needed(307200, 8533, 1, 16) == 19);               /* 이름 그대로: 17066 × 18 = 307188 < 307200 */
    CHECK(channels_needed(307200, n, d, 16) == 18);                  /* 정확히: 51200/3 × 18 = 307200 */

    /* 연습 2 */
    CHECK(ecc_overhead_bp(64, 8) == 1250 && ecc_overhead_bp(32, 8) == 2500 && ecc_overhead_bp(128, 8) == 625);
    CHECK(dimm_bus_bits(false, true) == 72 && dimm_bus_bits(true, true) == 80 && dimm_bus_bits(true, false) == 64);

    /* 연습 3 */
    CHECK(burst_ck(8, 1) == 4 && burst_ck(16, 1) == 8);              /* DDR4 BL8 = 4 tCK, DDR5 BL16 = 8 tCK */
    CHECK(burst_ck(16, 4) == 2 && burst_ck(32, 4) == 4 && burst_ck(16, 2) == 4);
    CHECK(burst_ck(4, 4) == -1 && burst_ck(16, 3) == -1);

    /* 연습 4 */
    static const struct { const char *name; uint32_t bits, bl; } C[] = {
        { "DDR4 BL8", 64, 8 }, { "DDR5 BL16", 32, 16 }, { "LPDDR5 BL16", 16, 16 },
        { "LPDDR5 BL32", 16, 32 }, { "HBM3 BL8", 32, 8 },
    };
    static const uint32_t LINES[3] = { 32, 64, 128 };
    static const int EFF[3][5] = { { 500, 500, 1000, 500, 1000 }, { 1000, 1000, 1000, 1000, 1000 },
                                   { 1000, 1000, 1000, 1000, 1000 } };
    static const int NB[3][5] = { { 1, 1, 1, 1, 1 }, { 1, 1, 2, 1, 2 }, { 2, 2, 4, 2, 4 } };
    int bad = 0;
    printf("%-12s %14s %14s %14s\n", "config", "32B n/eff", "64B n/eff", "128B n/eff");
    for (int i = 0; i < 5; i++) {
        printf("%-12s", C[i].name);
        for (int j = 0; j < 3; j++) {
            int nb = bursts_per_line(C[i].bits, C[i].bl, LINES[j]), e = line_eff_permille(C[i].bits, C[i].bl, LINES[j]);
            printf(" %6d/%4d‰  ", nb, e);
            if (nb != NB[j][i] || e != EFF[j][i]) bad++;
        }
        printf("\n");
    }
    CHECK(bad == 0);

    /* 연습 5: 1 TB/s = 1 000 000 MB/s */
    uint64_t ch5 = channels_needed(1000000, 6400, 1, 32);            /* DDR5-6400 서브채널 */
    grade_exact(8533, &n, &d);
    uint64_t chl = channels_needed(1000000, n, d, 16);               /* LPDDR5X-8533 x16 */
    uint64_t pcs = channels_needed(1000000, 6400, 1, 32);            /* HBM3-6400 PC (32b) */
    uint64_t stacks = (pcs + 31u) / 32u;                             /* stack 단위로만 산다 */
    printf("1 TB/s: DDR5-6400 %llu subch = %llu pins (%llu DIMM), LPDDR5X-8533 %llu ch = %llu pins, "
           "HBM3-6400 %llu PC -> %llu stacks = %llu pins\n",
           (unsigned long long)ch5, (unsigned long long)(ch5 * 32), (unsigned long long)((ch5 + 1) / 2),
           (unsigned long long)chl, (unsigned long long)(chl * 16), (unsigned long long)pcs,
           (unsigned long long)stacks, (unsigned long long)(stacks * 1024));
    CHECK(ch5 == 40 && ch5 * 32 == 1280);
    CHECK(chl == 59 && chl * 16 == 944);
    CHECK(pcs == 40 && stacks == 2 && stacks * 1024 == 2048);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
