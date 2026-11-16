/* MEM-B04 DDR4 vs DDR5 vs LPDDR4/5 구조 비교와 대역폭 계산
 *
 * Part A: 기본 계산기 — 피크 대역폭(MB/s, 정수로 정확), burst 하나가 나르는 바이트(access granularity),
 *         캐시라인(64 B / 128 B)에 필요한 burst 수, burst 하나의 시간(ps)
 * Part B: 세대 표(DDR4 / DDR5 / LPDDR4·4X / LPDDR5·5X + HBM2E / HBM3 미리보기) + 설정 검증(잘못된 설정 거부),
 *         라인 효율(over-fetch), LPDDR5 CK/WCK 클럭 계산
 * Part C: "시스템" 계산기 — 데스크톱 2×DDR5-5600, 노트북 128-bit LPDDR5X-8533, 4-stack HBM3 가속기,
 *         8채널 DDR4-3200 서버: 채널 수, 피크, 라인당 burst, Little 법칙으로 피크를 채우는 in-flight 라인 수
 *
 * 모든 세대 수치 = 이 레슨의 정의 (JESD79-4 / JESD79-5 / JESD209-4 / JESD209-5 / JESD235 / JESD238 대표값 —
 * 실제 부품은 데이터시트 확인). MB/s 의 M = 10^6 (십진). 힙 없음, 결정적, < 1 s.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c && ./a.out
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* ---------------- Part A: 기본 계산 ---------------- */

/* 피크 대역폭 [MB/s] = MT/s × (bus_bits / 8) × channels.
 * 잘못된 입력(0, bus_bits % 8 != 0) 이나 uint64 overflow → 0 */
static uint64_t peak_mbps(uint32_t mts, uint32_t bus_bits, uint32_t channels) {
    (void)mts; (void)bus_bits; (void)channels;
    return 0; /* TODO: 입력 검사 → MT/s × (bus_bits/8) × channels, uint64 overflow 면 0 */
}

/* burst 하나가 나르는 바이트 (= access granularity) = 채널 폭(바이트) × BL. 잘못되면 0 */
static uint32_t burst_bytes(uint32_t ch_bits, uint32_t bl) {
    (void)ch_bits; (void)bl;
    return 0; /* TODO: (ch_bits/8) × BL, 잘못된 입력이면 0 */
}

/* line_bytes 를 옮기는 데 필요한 burst 수 = ceil(line / granularity). 잘못되면 -1 */
static int bursts_per_line(uint32_t ch_bits, uint32_t bl, uint32_t line_bytes) {
    (void)ch_bits; (void)bl; (void)line_bytes;
    return 0; /* TODO: ceil(line / burst_bytes), 잘못되면 -1 */
}

/* burst 하나의 시간 [ps] = BL beat × (10^6 / MT/s) ps, 가장 가까운 정수로 반올림. 잘못되면 0 */
static uint32_t burst_ps(uint32_t mts, uint32_t bl) {
    (void)mts; (void)bl;
    return 0; /* TODO: BL × 10^6 / MT/s ps, 반올림 */
}

/* ---------------- Part B: 세대 표와 설정 검증 ---------------- */

typedef enum { GEN_DDR4, GEN_DDR5, GEN_LPDDR4, GEN_LPDDR5, GEN_HBM2E, GEN_HBM3, GEN_COUNT } mem_gen;

typedef struct {
    const char *name;
    uint16_t ch_bits;      /* 독립 채널 하나의 데이터 폭 (DDR5 = 서브채널, HBM = pseudo-channel), ECC 제외 */
    uint16_t ch_per_unit;  /* 단위 하나에 든 독립 채널 수 */
    const char *unit;      /* DIMM(채널 하나에 DIMM 하나 가정) / x16 채널 / stack */
    uint8_t bl[2];         /* 허용 burst length (0 = 없음). DDR4 의 4 = BC4(burst chop) */
    uint16_t min_mts, max_mts;  /* 이 레슨의 검증 범위 (JEDEC 속도 등급 대표값, XMP/EXPO 오버클럭 제외) */
    uint8_t ecc_bits;      /* ECC DIMM 이 채널당 더하는 side-band 비트 (0 = 표준 side-band ECC 없음) */
} gen_info;

static const gen_info GEN[GEN_COUNT] = {
    { "DDR4",      64,  1, "DIMM",   {  8,  4 }, 1600, 3200, 8 },  /* 64b(+8 ECC), BL8 / BC4, 8n prefetch  */
    { "DDR5",      32,  2, "DIMM",   { 16, 32 }, 3200, 8800, 8 },  /* 2×32b(+8 ECC 각각), BL16 (BL32 = x4 선택) */
    { "LPDDR4/4X", 16,  1, "x16 ch", { 16, 32 }, 1600, 4266, 0 },  /* x16 채널, BL16/BL32, 16n prefetch      */
    { "LPDDR5/5X", 16,  1, "x16 ch", { 16, 32 }, 1600, 9600, 0 },  /* x16 채널, BL16/BL32, WCK:CK 2:1 / 4:1   */
    { "HBM2E",     64, 16, "stack",  {  4,  0 }, 2000, 3600, 0 },  /* 8 ch × 2 PC × 64b = 1024b, BL4 (PC 모드) */
    { "HBM3",      32, 32, "stack",  {  8,  0 }, 4000, 6400, 0 },  /* 16 ch × 2 PC × 32b = 1024b, BL8        */
};

typedef struct { mem_gen gen; uint32_t mts; uint32_t bl; uint32_t units; } mem_cfg;
enum { CFG_OK = 0, CFG_BAD_GEN = -1, CFG_BAD_RATE = -2, CFG_BAD_BL = -3, CFG_BAD_UNITS = -4 };
#define MAX_UNITS 64u

/* 설정 검증: 세대 → 속도 범위 → BL → 단위 수 순서로 첫 오류를 돌려준다 */
static int cfg_check(const mem_cfg *c) {
    (void)c; (void)GEN;
    return CFG_OK; /* TODO: 세대 → 속도 범위 → BL → 단위 수 (MAX_UNITS) 순서로 검사 */
}

/* 독립 채널 수 = 단위당 채널 × 단위 수 (잘못된 설정이면 0) */
static uint32_t cfg_channels(const mem_cfg *c) {
    (void)c;
    return 0; /* TODO: ch_per_unit × units (잘못된 설정이면 0) */
}

/* 설정의 피크 대역폭 [MB/s] (잘못된 설정이면 0) */
static uint64_t cfg_peak_mbps(const mem_cfg *c) {
    (void)c;
    return 0; /* TODO: peak_mbps(mts, ch_bits, cfg_channels) */
}

/* 라인 효율 [‰] = 쓸모 있는 바이트 / 실제로 옮긴 바이트 (burst 단위 over-fetch). 잘못되면 -1 */
static int line_efficiency_permille(uint32_t ch_bits, uint32_t bl, uint32_t line_bytes) {
    (void)ch_bits; (void)bl; (void)line_bytes;
    return 0; /* TODO: line × 1000 / (burst 수 × granularity) */
}

/* LPDDR5: 데이터는 WCK 의 양 에지(DDR) → WCK = MT/s / 2, CK = WCK / ratio. 결과는 kHz (정수로 정확).
 * ratio: 4 (4:1) 또는 2 (2:1, 이 레슨 규칙: ≤ 3200 MT/s 에서만 — 대표값, JESD209-5 확인). 잘못되면 -1 */
static int lp5_clocks(uint32_t mts, uint32_t ratio, uint32_t *wck_khz, uint32_t *ck_khz) {
    (void)mts; (void)ratio; (void)wck_khz; (void)ck_khz;
    return -1; /* TODO: WCK = MT/s/2, CK = WCK/ratio (kHz), ratio 4, 또는 2 (≤ 3200 MT/s) */
}

/* ---------------- Part C: 시스템 계산기 ---------------- */

typedef struct {
    uint32_t channels;        /* 독립 채널 수 (컨트롤러가 따로 스케줄하는 단위) */
    uint64_t peak_mbps;
    uint32_t gran_bytes;      /* burst 하나 = access granularity */
    int      bursts_per_line;
    uint32_t burst_ps;
    uint64_t inflight_bytes;  /* Little 법칙: 피크 × 지연 = 날아다니고 있어야 하는 바이트 */
    uint64_t inflight_lines;  /* ceil(inflight_bytes / line) = 필요한 미해결 요청(MLP) 수 */
} sys_report;

/* 잘못된 설정/라인이면 음수 (cfg_check 코드 또는 -5), 성공 0. latency_ns 는 가정값으로 받는다 */
static int sys_calc(const mem_cfg *c, uint32_t line_bytes, uint32_t latency_ns, sys_report *r) {
    (void)c; (void)line_bytes; (void)latency_ns; (void)r;
    return -1; /* TODO: cfg_check → 보고서 채우기 (Little: inflight = peak × latency / 1000, 올림) */
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

static void print_gen_table(void) {   /* BL 이 둘이면 "첫째/둘째" 로 같이 찍는다 */
    printf("\n%-10s %4s %4s %-7s %-6s %-7s %-6s %-6s %8s %10s %4s\n", "gen", "ch_b", "ch/u", "unit", "BL",
           "gran B", "n/64B", "n/128B", "MT/s max", "MB/s/unit", "ECC");
    for (int i = 0; i < GEN_COUNT; i++) {
        const gen_info *g = &GEN[i];
        char bl[16], gr[16], n64[16], n128[16];
        int k = g->bl[1] ? 1 : 0;
        snprintf(bl, sizeof bl, k ? "%u/%u" : "%u", g->bl[0], g->bl[k]);
        snprintf(gr, sizeof gr, k ? "%u/%u" : "%u", burst_bytes(g->ch_bits, g->bl[0]), burst_bytes(g->ch_bits, g->bl[k]));
        snprintf(n64, sizeof n64, k ? "%d/%d" : "%d", bursts_per_line(g->ch_bits, g->bl[0], 64),
                 bursts_per_line(g->ch_bits, g->bl[k], 64));
        snprintf(n128, sizeof n128, k ? "%d/%d" : "%d", bursts_per_line(g->ch_bits, g->bl[0], 128),
                 bursts_per_line(g->ch_bits, g->bl[k], 128));
        printf("%-10s %4u %4u %-7s %-6s %-7s %-6s %-6s %8u %10llu %4u\n", g->name, g->ch_bits, g->ch_per_unit,
               g->unit, bl, gr, n64, n128, g->max_mts,
               (unsigned long long)peak_mbps(g->max_mts, g->ch_bits, g->ch_per_unit), g->ecc_bits);
    }
}

int main(void) {
    /* ---- Part A: 알려진 값 ---- */
    CHECK(peak_mbps(3200, 64, 1) == 25600u);        /* DDR4-3200, 64b 채널 하나 */
    CHECK(peak_mbps(4800, 32, 2) == 38400u);        /* DDR5-4800 DIMM = 2 × 32b 서브채널 */
    CHECK(peak_mbps(6400, 16, 1) == 12800u);        /* LPDDR5-6400 x16 채널 */
    CHECK(peak_mbps(3200, 64, 8) == 204800u);       /* 8채널 DDR4-3200 서버 */
    CHECK(peak_mbps(6400, 1024, 1) == 819200u);     /* HBM3-6400 stack 하나 (1024b) */
    CHECK(peak_mbps(0, 64, 1) == 0 && peak_mbps(3200, 60, 1) == 0 && peak_mbps(3200, 64, 0) == 0);
    CHECK(peak_mbps(3200, 0, 1) == 0);
    CHECK(peak_mbps(UINT32_MAX, 64, 1) == 34359738360ull);          /* 크지만 유효: 32비트를 넘는다 */
    CHECK(peak_mbps(UINT32_MAX, 1024, UINT32_MAX) == 0);             /* uint64 overflow → 거부 */

    CHECK(burst_bytes(64, 8) == 64 && burst_bytes(32, 16) == 64);    /* DDR4 BL8, DDR5 서브채널 BL16 */
    CHECK(burst_bytes(16, 16) == 32 && burst_bytes(16, 32) == 64);   /* LPDDR x16 BL16 / BL32 */
    CHECK(burst_bytes(64, 4) == 32 && burst_bytes(32, 8) == 32);     /* DDR4 BC4, HBM3 PC BL8 */
    CHECK(burst_bytes(64, 16) == 128);                               /* 가상의 "64b DDR5" */
    CHECK(burst_bytes(12, 8) == 0 && burst_bytes(64, 0) == 0);

    CHECK(bursts_per_line(64, 8, 64) == 1 && bursts_per_line(32, 16, 64) == 1);
    CHECK(bursts_per_line(16, 16, 64) == 2 && bursts_per_line(16, 32, 64) == 1);
    CHECK(bursts_per_line(32, 8, 64) == 2 && bursts_per_line(64, 4, 64) == 2);
    CHECK(bursts_per_line(64, 8, 128) == 2 && bursts_per_line(16, 32, 128) == 2);   /* M6 의 128 B 라인 */
    CHECK(bursts_per_line(64, 16, 64) == 1 && bursts_per_line(64, 8, 65) == 2);
    CHECK(bursts_per_line(64, 8, 0) == -1 && bursts_per_line(12, 8, 64) == -1);

    CHECK(burst_ps(3200, 8) == 2500 && burst_ps(4800, 16) == 3333);  /* 4 tCK × 625 ps / 16 × 208.3 ps */
    CHECK(burst_ps(6400, 16) == 2500 && burst_ps(6400, 32) == 5000);
    CHECK(burst_ps(6400, 8) == 1250 && burst_ps(5600, 16) == 2857);
    CHECK(burst_ps(0, 8) == 0 && burst_ps(3200, 0) == 0);

    {   /* 성질 테스트: 고정 시드 2000 회 */
        uint32_t s = 0xB04B04u; int bad = 0;
        for (int t = 0; t < 2000; t++) {
            uint32_t mts = 1 + xs32(&s) % 10000u, bits = 8u * (1 + xs32(&s) % 128u), ch = 1 + xs32(&s) % 64u;
            uint32_t bl = 1u << (xs32(&s) % 6u), line = 1 + xs32(&s) % 512u;
            if (peak_mbps(mts, bits, ch) != (uint64_t)ch * peak_mbps(mts, bits, 1)) bad++;
            if (peak_mbps(mts, bits, ch) != (uint64_t)mts * bits / 8u * ch) bad++;
            int n = bursts_per_line(bits, bl, line);
            uint32_t g = burst_bytes(bits, bl);
            if (n < 1 || (uint64_t)(n - 1) * g >= line || (uint64_t)n * g < line) bad++;
        }
        CHECK(bad == 0);
    }

    /* ---- Part B: 설정 검증, 효율, LPDDR5 클럭 ---- */
    mem_cfg ddr4 = { GEN_DDR4, 3200, 8, 1 }, ddr5 = { GEN_DDR5, 4800, 16, 1 }, lp5 = { GEN_LPDDR5, 6400, 32, 1 };
    CHECK(cfg_check(&ddr4) == CFG_OK && cfg_check(&ddr5) == CFG_OK && cfg_check(&lp5) == CFG_OK);
    CHECK(cfg_peak_mbps(&ddr4) == 25600u && cfg_peak_mbps(&ddr5) == 38400u && cfg_peak_mbps(&lp5) == 12800u);
    CHECK(cfg_channels(&ddr5) == 2 && cfg_channels(&ddr4) == 1);
    mem_cfg xmp = { GEN_DDR4, 3600, 8, 1 };              /* XMP 오버클럭 키트: JEDEC 범위 밖 */
    mem_cfg d5bl8 = { GEN_DDR5, 4800, 8, 1 };            /* DDR5 에 BL8 은 없다 */
    mem_cfg lpbl8 = { GEN_LPDDR5, 6400, 8, 1 };
    mem_cfg h3bl4 = { GEN_HBM3, 6400, 4, 1 };
    mem_cfg nogen = { (mem_gen)99, 3200, 8, 1 };
    mem_cfg zero = { GEN_DDR4, 3200, 8, 0 }, many = { GEN_DDR5, 4800, 16, 65 };
    mem_cfg d5slow = { GEN_DDR5, 3000, 16, 1 }, bl0 = { GEN_HBM2E, 3200, 0, 1 };
    CHECK(cfg_check(&xmp) == CFG_BAD_RATE && cfg_check(&d5slow) == CFG_BAD_RATE);
    CHECK(cfg_check(&d5bl8) == CFG_BAD_BL && cfg_check(&lpbl8) == CFG_BAD_BL);
    CHECK(cfg_check(&h3bl4) == CFG_BAD_BL && cfg_check(&bl0) == CFG_BAD_BL);
    CHECK(cfg_check(&nogen) == CFG_BAD_GEN && cfg_check(NULL) == CFG_BAD_GEN);
    CHECK(cfg_check(&zero) == CFG_BAD_UNITS && cfg_check(&many) == CFG_BAD_UNITS);
    CHECK(cfg_peak_mbps(&xmp) == 0 && cfg_channels(&nogen) == 0);

    CHECK(line_efficiency_permille(64, 8, 64) == 1000);    /* DDR4: 라인 = burst */
    CHECK(line_efficiency_permille(64, 16, 64) == 500);    /* 64b 채널에 BL16 이었다면: 절반 낭비 */
    CHECK(line_efficiency_permille(32, 16, 32) == 500);    /* 32 B 섹터(GPU) 를 DDR5 에서 */
    CHECK(line_efficiency_permille(32, 8, 32) == 1000);    /* HBM3 PC: 32 B 섹터에 딱 */
    CHECK(line_efficiency_permille(16, 32, 128) == 1000 && line_efficiency_permille(64, 8, 96) == 750);
    CHECK(line_efficiency_permille(0, 8, 64) == -1);

    uint32_t wck = 0, ck = 0;
    CHECK(lp5_clocks(6400, 4, &wck, &ck) == 0 && wck == 3200000u && ck == 800000u);
    CHECK(lp5_clocks(3200, 2, &wck, &ck) == 0 && wck == 1600000u && ck == 800000u);
    CHECK(lp5_clocks(8533, 4, &wck, &ck) == 0 && wck == 4266500u && ck == 1066625u);
    CHECK(lp5_clocks(6400, 2, &wck, &ck) == -1 && lp5_clocks(6400, 3, &wck, &ck) == -1);
    CHECK(lp5_clocks(6400, 4, NULL, &ck) == -1);

    print_gen_table();

    /* ---- Part C: 시스템 계산기 (지연 100 ns = 가정값) ---- */
    struct { const char *name; mem_cfg c; } sys[] = {
        { "desktop 2x DDR5-5600 DIMM",   { GEN_DDR5,   5600, 16, 2 } },
        { "laptop 128b LPDDR5X-8533",    { GEN_LPDDR5, 8533, 32, 8 } },
        { "server 8ch DDR4-3200",        { GEN_DDR4,   3200,  8, 8 } },
        { "accel 4-stack HBM3-6400",     { GEN_HBM3,   6400,  8, 4 } },
    };
    sys_report r[4] = { { 0 } };
    printf("\n%-28s %4s %9s %5s %6s %6s %9s %8s\n", "system (lat 100 ns)", "ch", "MB/s", "gran",
           "b/64B", "bst_ps", "inflightB", "lines64");
    for (int i = 0; i < 4; i++) {
        CHECK(sys_calc(&sys[i].c, 64, 100, &r[i]) == 0);
        printf("%-28s %4u %9llu %4uB %6d %6u %9llu %8llu\n", sys[i].name, r[i].channels,
               (unsigned long long)r[i].peak_mbps, r[i].gran_bytes, r[i].bursts_per_line, r[i].burst_ps,
               (unsigned long long)r[i].inflight_bytes, (unsigned long long)r[i].inflight_lines);
    }
    CHECK(r[0].channels == 4 && r[0].peak_mbps == 89600u && r[0].gran_bytes == 64 && r[0].bursts_per_line == 1);
    CHECK(r[0].burst_ps == 2857 && r[0].inflight_bytes == 8960u && r[0].inflight_lines == 140u);
    CHECK(r[1].channels == 8 && r[1].peak_mbps == 136528u && r[1].gran_bytes == 64 && r[1].burst_ps == 3750);
    CHECK(r[1].inflight_bytes == 13653u && r[1].inflight_lines == 214u);    /* 13652.8 → 올림 */
    CHECK(r[2].channels == 8 && r[2].peak_mbps == 204800u && r[2].inflight_lines == 320u);
    CHECK(r[3].channels == 128 && r[3].peak_mbps == 3276800u && r[3].gran_bytes == 32);
    CHECK(r[3].bursts_per_line == 2 && r[3].burst_ps == 1250 && r[3].inflight_lines == 5120u);
    sys_report z = { 0 };
    CHECK(sys_calc(&xmp, 64, 100, &z) == CFG_BAD_RATE && z.peak_mbps == 0);   /* 실패 시 보고서 안 건드림 */
    CHECK(sys_calc(&ddr4, 0, 100, &z) == -5 && sys_calc(&ddr4, 64, 100, NULL) == -5);
    CHECK(sys_calc(&ddr4, 64, 0, &z) == 0 && z.inflight_bytes == 0 && z.peak_mbps == 25600u);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
