/* SIV-B03 추가 연습 — 한 번에 검증
 * 연습 1: strap(부트 핀)으로 리셋값이 정해지는 필드
 * 연습 2: soft reset 후 도메인별 기대값 (CORE 는 리셋값, AON/sticky 는 이전 값 유지)
 * 연습 3: 전원 사이클 여러 번에 걸쳐 흔들리는 비트(flaky) 찾기 → 기술서에 UNDEF 표시가 빠진 비트
 * 연습 4: 같은 IP 의 여러 인스턴스 결과로 원인 분류 (IP RTL/기술서 vs 통합 tie-off)
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

static uint32_t field_mask(unsigned width) { return width >= 32 ? 0xFFFFFFFFu : (1u << width) - 1u; }

/* ---------------- 연습 1: strap 필드 ---------------- */
#define FF_STRAP 0x4u
typedef struct { const char *name; uint8_t lsb, width, flags; uint32_t reset; } pf_t;

static const pf_t bootcfg[] = {          /* BOOTCFG: MODE[1:0]=strap 핀, ECC_EN[4]=1, VER[11:8]=3 */
    { "MODE", 0, 2, FF_STRAP, 0 }, { "ECC_EN", 4, 1, 0, 1 }, { "VER", 8, 4, 0, 3 },
};

/* strap 필드는 기술서 reset 대신 보드가 알려준 strap 값으로 기대값을 만든다 */
static uint32_t expect_with_strap(const pf_t *f, int nf, uint32_t strap) {
    uint32_t e = 0;
    for (int k = 0; k < nf; k++) {
        uint32_t fm = field_mask(f[k].width) << f[k].lsb;
        uint32_t v = (f[k].flags & FF_STRAP) ? strap : f[k].reset;
        e |= (v << f[k].lsb) & fm;
    }
    return e;
}

/* ---------------- 연습 2: soft reset 과 도메인 ---------------- */
enum { DOM_CORE, DOM_AON };
typedef struct { const char *name; int domain; uint32_t reset; } dreg_t;

/* 실패한 레지스터 인덱스를 비트로 돌려준다 (0 이면 통과) */
static uint32_t soft_reset_check(const dreg_t *r, int n, const uint32_t *before, const uint32_t *after) {
    uint32_t bad = 0;
    for (int i = 0; i < n; i++) {
        uint32_t want = (r[i].domain == DOM_AON) ? before[i] : r[i].reset;
        if (after[i] != want) bad |= 1u << i;
    }
    return bad;
}

/* ---------------- 연습 3: flaky 비트 ---------------- */
static uint32_t flaky_mask(const uint32_t *s, int n) {
    uint32_t f = 0;
    for (int i = 1; i < n; i++) f |= s[i] ^ s[0];
    return f;
}
/* 흔들리는데 기술서에 UNDEF/VOLATILE 로 표시되지 않은 비트 */
static uint32_t undeclared_flaky(uint32_t flaky, uint32_t excluded) { return flaky & ~excluded; }

/* ---------------- 연습 4: 인스턴스 비교 ---------------- */
enum { CLS_OK, CLS_ALL_SAME, CLS_INSTANCE };
static int classify(const uint32_t *diff, int n) {
    bool any = false, all_same = true;
    for (int i = 0; i < n; i++) {
        if (diff[i]) any = true;
        if (diff[i] != diff[0]) all_same = false;
    }
    if (!any) return CLS_OK;
    return all_same ? CLS_ALL_SAME : CLS_INSTANCE;   /* 같은 diff 가 모든 인스턴스 → IP/기술서 쪽 */
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    /* 연습 1 */
    CHECK(expect_with_strap(bootcfg, 3, 2) == 0x312u);
    CHECK(expect_with_strap(bootcfg, 3, 3) == 0x313u);
    CHECK(expect_with_strap(bootcfg, 3, 7) == 0x313u);          /* 2비트 필드: 넘치는 strap 값은 잘림 */
    uint32_t silicon = 0x313u;                                   /* 보드 A 는 MODE 핀이 3 */
    CHECK(((silicon ^ expect_with_strap(bootcfg, 3, 2)) & 0xFFFu) == 0x1u);   /* 보드 B 기준으로 돌리면 오탐 */
    CHECK(((silicon ^ expect_with_strap(bootcfg, 3, 3)) & 0xFFFu) == 0);

    /* 연습 2 */
    const dreg_t regs[] = { { "CTRL", DOM_CORE, 0 }, { "BAUD", DOM_CORE, 0x1A }, { "WAKECFG", DOM_AON, 0 },
                            { "BOOTCNT", DOM_AON, 0 } };
    const uint32_t before[] = { 0x7u, 0x68u, 0x3u, 0x5u };
    const uint32_t good[]   = { 0x0u, 0x1Au, 0x3u, 0x5u };
    const uint32_t aon_lost[] = { 0x0u, 0x1Au, 0x0u, 0x5u };    /* WAKECFG 가 리셋돼 버림 */
    const uint32_t core_kept[] = { 0x7u, 0x1Au, 0x3u, 0x5u };   /* CTRL 이 soft reset 에 안 걸림 */
    CHECK(soft_reset_check(regs, 4, before, good) == 0);
    CHECK(soft_reset_check(regs, 4, before, aon_lost) == (1u << 2));
    CHECK(soft_reset_check(regs, 4, before, core_kept) == (1u << 0));

    /* 연습 3 */
    const uint32_t cycles[] = { 0x7C000000u, 0x3C000004u, 0x7C000000u, 0x5C000000u, 0x7D000000u };
    uint32_t fl = flaky_mask(cycles, 5);
    CHECK(fl == 0x61000004u);
    CHECK(undeclared_flaky(fl, 0xFF000000u) == 0x00000004u);    /* TAP[31:24] 은 UNDEF 표시됨, bit2 는 아님 */
    CHECK(flaky_mask(cycles, 1) == 0);

    /* 연습 4 */
    const uint32_t d_ok[]   = { 0, 0, 0, 0 };
    const uint32_t d_all[]  = { 0x1u, 0x1u, 0x1u, 0x1u };
    const uint32_t d_one[]  = { 0, 0, 0x1u, 0 };
    const uint32_t d_mix[]  = { 0x1u, 0x1u, 0x2u, 0x1u };
    CHECK(classify(d_ok, 4) == CLS_OK);
    CHECK(classify(d_all, 4) == CLS_ALL_SAME);
    CHECK(classify(d_one, 4) == CLS_INSTANCE);
    CHECK(classify(d_mix, 4) == CLS_INSTANCE);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
