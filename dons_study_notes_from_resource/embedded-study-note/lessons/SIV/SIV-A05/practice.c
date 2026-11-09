/* SIV-A05 추가 연습 — 본문 errata 프레임워크에서 빠진 현실 조각들
 *   1) 비연속 영향 집합: 범위 대신 known-rev 비트마스크 (B0 가 A0 DB 에서 분기된 경우)
 *   2) 혼합 리비전 클러스터 + late CPU (Linux arm64 의 시스템 전역 capability 와 같은 발상)
 *   3) 전원 도메인 복귀 후 재적용: 전역 "already applied" 플래그의 함정
 *   4) 부팅 errata 리포트: APPLIED / MISSING / N/A 와 프로덕션 정책
 *   5) "버그 사라짐" 주장에 필요한 시도 횟수
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else { printf("FAIL %s:%d  %s\n", __func__, __LINE__, #expr); } } while (0)

#define REV(maj, min) ((uint16_t)((maj) << 8 | (min)))
#define REV_A0 REV(0, 0)
#define REV_A1 REV(0, 1)
#define REV_B0 REV(1, 0)
#define REV_B1 REV(1, 1)

/* ---- 연습 1: 영향 집합 비트마스크 ---- */
static const uint16_t KNOWN[] = { REV_A0, REV_A1, REV_B0, REV_B1 };
#define KNOWN_N ((int)(sizeof KNOWN / sizeof KNOWN[0]))

static int rev_slot(uint16_t rev) {
    for (int i = 0; i < KNOWN_N; i++) if (KNOWN[i] == rev) return i;
    return -1;
}
static bool affected_by_mask(uint32_t mask, uint16_t rev) {
    int s = rev_slot(rev);
    return s >= 0 && (mask >> s & 1u);
}
static uint32_t mask_of(const uint16_t *revs, int n) {
    uint32_t m = 0;
    for (int i = 0; i < n; i++) {
        int s = rev_slot(revs[i]);
        if (s >= 0) m |= 1u << s;
    }
    return m;
}
static bool in_range(uint16_t lo, uint16_t hi, uint16_t rev) { return rev >= lo && rev <= hi; }

static void test_p1(void) {
    /* A1 에서 메탈로 고쳤지만 B0 는 A0 DB 에서 분기해 다시 영향받음 */
    static const uint16_t aff[] = { REV_A0, REV_B0 };
    uint32_t m = mask_of(aff, 2);
    CHECK(m == 0x5u);
    CHECK(affected_by_mask(m, REV_A0) && !affected_by_mask(m, REV_A1));
    CHECK(affected_by_mask(m, REV_B0) && !affected_by_mask(m, REV_B1));
    CHECK(!affected_by_mask(m, REV(2, 0)));              /* 미지의 리비전은 표에서 알 수 없다 */
    /* 범위로 쓰면 A1 까지 끌려 들어온다 */
    CHECK(in_range(REV_A0, REV_B0, REV_A1) && !affected_by_mask(m, REV_A1));
}

/* ---- 연습 2: 혼합 리비전 클러스터 ---- */
static uint32_t system_enable(const uint32_t *boot_cpus_need, int n) {
    uint32_t en = 0;
    for (int i = 0; i < n; i++) en |= boot_cpus_need[i];
    return en;
}
static bool late_cpu_allowed(uint32_t system_enabled, uint32_t need) { return (need & ~system_enabled) == 0; }

static void test_p2(void) {
    /* 부팅 CPU 네 개: 두 코어가 서로 다른 스테핑이라 필요한 WA 가 다르다 */
    static const uint32_t boot[4] = { 0x1, 0x1, 0x3, 0x1 };
    uint32_t en = system_enable(boot, 4);
    CHECK(en == 0x3);
    static const struct { uint32_t need; bool ok; } t[] = {
        { 0x0, true }, { 0x1, true }, { 0x3, true }, { 0x4, false }, { 0x5, false },
    };
    int ok = 0;
    for (size_t i = 0; i < sizeof t / sizeof t[0]; i++) ok += late_cpu_allowed(en, t[i].need) == t[i].ok;
    CHECK(ok == 5);
}

/* ---- 연습 3: 전원 도메인 재적용 ---- */
typedef struct { uint32_t reg; uint32_t epoch; } domain_t;
typedef struct { bool done; uint32_t applied_epoch; } wa_state_t;
#define WA_BIT 0x10u
#define EPOCH_NONE 0xFFFFFFFFu

static void domain_power_cycle(domain_t *d) { d->reg = 0; d->epoch++; }
static void apply_naive(domain_t *d, wa_state_t *w) {
    if (w->done) return;
    d->reg |= WA_BIT;
    w->done = true;
}
static void apply_epoch(domain_t *d, wa_state_t *w) {
    if (w->applied_epoch == d->epoch) return;
    d->reg |= WA_BIT;
    w->applied_epoch = d->epoch;
}

static void test_p3(void) {
    domain_t d = { 0, 0 };
    wa_state_t w = { false, EPOCH_NONE };
    apply_naive(&d, &w);
    CHECK(d.reg == WA_BIT);
    domain_power_cycle(&d);
    apply_naive(&d, &w);
    CHECK(d.reg == 0);                   /* 함정: 플래그는 살아남고 하드웨어 상태는 사라졌다 */

    domain_t e = { 0, 0 };
    wa_state_t x = { false, EPOCH_NONE };
    apply_epoch(&e, &x);
    CHECK(e.reg == WA_BIT);
    domain_power_cycle(&e);
    apply_epoch(&e, &x);
    CHECK(e.reg == WA_BIT && x.applied_epoch == 1);
}

/* ---- 연습 4: 부팅 errata 리포트 ---- */
typedef enum { RPT_NA, RPT_APPLIED, RPT_MISSING } rpt_t;
static rpt_t report_state(bool needed, bool built_in) {
    if (!needed) return RPT_NA;
    return built_in ? RPT_APPLIED : RPT_MISSING;
}
static int report_line(char *b, size_t cap, const rpt_t *st, const uint32_t *ids, int n, bool production) {
    int w = snprintf(b, cap, "errata:");
    int missing = 0;
    for (int i = 0; i < n && w < (int)cap; i++) {
        static const char *const NAME[] = { "na", "applied", "MISSING" };
        w += snprintf(b + w, cap - (size_t)w, " E%u=%s", (unsigned)ids[i], NAME[st[i]]);
        missing += st[i] == RPT_MISSING;
    }
    return production && missing ? 1 : 0;
}

static void test_p4(void) {
    CHECK(report_state(false, true) == RPT_NA && report_state(false, false) == RPT_NA);
    CHECK(report_state(true, true) == RPT_APPLIED && report_state(true, false) == RPT_MISSING);
    uint32_t ids[3] = { 1001, 1002, 1003 };
    rpt_t st[3] = { RPT_APPLIED, RPT_MISSING, RPT_NA };
    char line[80];
    CHECK(report_line(line, sizeof line, st, ids, 3, true) == 1);
    CHECK(strcmp(line, "errata: E1001=applied E1002=MISSING E1003=na") == 0);
    CHECK(report_line(line, sizeof line, st, ids, 3, false) == 0);     /* 랩에서는 경고만 */
    st[1] = RPT_APPLIED;
    CHECK(report_line(line, sizeof line, st, ids, 3, true) == 0);
}

/* ---- 연습 5: 시도 횟수 ---- */
static int trials_for(double p, double cl) {
    double miss = 1.0;
    int n = 0;
    while (miss > 1.0 - cl && n < 100000000) { miss *= 1.0 - p; n++; }
    return n;
}

static void test_p5(void) {
    CHECK(trials_for(1.0 / 64, 0.95) == 191);
    CHECK(trials_for(0.01, 0.95) == 299);
    CHECK(trials_for(0.001, 0.95) == 2995);
    CHECK(trials_for(1.0 / 64, 0.99) == 293);
    CHECK(trials_for(0.5, 0.75) == 2);
    /* rule of three 는 작은 p 에서 근사값일 뿐이다 */
    int n = trials_for(0.001, 0.95);
    CHECK(n > 2900 && n < 3100);
}

int main(void) {
    test_p1();
    test_p2();
    test_p3();
    test_p4();
    test_p5();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
