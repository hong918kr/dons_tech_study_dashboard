/* BM-M12 추가 연습 — 한 번에 검증
 *
 * 연습 1: 12시간제(FMT=1) TR 패킹 — PM 비트[22] + 시 1..12, 86400 초 전수 왕복
 * 연습 2: 스무스 캘리브레이션 RTC_CALR (CALP, CALM[8:0]) 고르기 — 1024 조합 전수 대비 + 하루 드리프트
 * 연습 3: 웨이크업 타이머(WUT) WUCKSEL/WUTR 고르기 — 2 kHz..16 kHz 분주 / ck_spre / ck_spre+2^16
 * 연습 4: 섀도 없는 16+16 비트 분할 카운터 (STM32F1 RTC_CNTH/CNTL 식) — hi-lo-hi 읽기, 모든 끼어들기 지점
 *
 * 레지스터 필드는 RM0090/RM0008 기준 대표값 — RM 확인 권장. 테스트의 정답은 이 파일의 정의다.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/BM-M12_pr && /tmp/BM-M12_pr
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint8_t bin2bcd(uint8_t v) { return (uint8_t)(((v / 10u) << 4) | (v % 10u)); }
static uint8_t bcd2bin(uint8_t b) { return (uint8_t)((b >> 4) * 10u + (b & 0x0Fu)); }

/* ---------- 연습 1: 12시간제 ---------- */
#define TR_PM (1u << 22)
static uint32_t tr_pack_12h(uint8_t h24, uint8_t m, uint8_t s) {
    uint8_t h12 = (uint8_t)(h24 % 12u == 0u ? 12u : h24 % 12u);      /* 0 시 → 12 AM, 12 시 → 12 PM */
    return (h24 >= 12u ? TR_PM : 0u) | ((uint32_t)bin2bcd(h12) << 16) | ((uint32_t)bin2bcd(m) << 8) | bin2bcd(s);
}
static uint8_t tr_hour24_12h(uint32_t tr) {
    uint8_t h12 = bcd2bin((uint8_t)((tr >> 16) & 0x3Fu));
    return (uint8_t)(h12 % 12u + ((tr & TR_PM) ? 12u : 0u));
}
static void ex1(void) {
    CHECK(tr_pack_12h(0, 0, 0) == 0x00120000u);                        /* 12:00:00 AM */
    CHECK(tr_pack_12h(12, 30, 0) == 0x00523000u);                      /* 12:30:00 PM */
    CHECK(tr_pack_12h(23, 59, 59) == 0x00515959u);                     /* 11:59:59 PM */
    CHECK(tr_pack_12h(1, 5, 9) == 0x00010509u);                        /* 01:05:09 AM */
    int bad = 0;
    for (uint32_t x = 0; x < 86400u; x++) {
        uint8_t h = (uint8_t)(x / 3600u), m = (uint8_t)(x / 60u % 60u), s = (uint8_t)(x % 60u);
        uint32_t tr = tr_pack_12h(h, m, s);
        uint8_t h12 = bcd2bin((uint8_t)((tr >> 16) & 0x3Fu));
        if (tr_hour24_12h(tr) != h || h12 < 1u || h12 > 12u || bcd2bin((uint8_t)((tr >> 8) & 0x7Fu)) != m) bad++;
    }
    CHECK(bad == 0);
}

/* ---------- 연습 2: 스무스 캘리브레이션 ----------
 * f_cal = f_RTCCLK · 2^20 / (2^20 + CALM − 512·CALP)   (RM0090 식 정리형 — 확인 권장)
 * 측정한 오차 x ppm (+ = 빠름) 을 가장 잘 상쇄하는 (CALP, CALM). 동률이면 CALP=0, 작은 CALM. */
typedef struct { uint32_t calp, calm; int64_t resid_ppb; } calib_t;
static void calib_eval(int32_t x_ppm, uint32_t p, uint32_t m, uint64_t *num, uint64_t *den) {
    int64_t D = (1 << 20) + (int64_t)m - 512 * (int64_t)p;
    int64_t N = (1000000 + (int64_t)x_ppm) * (1 << 20) - 1000000 * D;   /* 잔여오차 = N/D ppm */
    *num = (uint64_t)(N < 0 ? -N : N); *den = (uint64_t)D;
}
static calib_t calib_pick(int32_t x_ppm) {
    calib_t best = { 0, 0, 0 };
    uint64_t bn = 0, bd = 1; bool have = false;
    for (uint32_t p = 0; p <= 1u; p++)
        for (uint32_t m = 0; m < 512u; m++) {
            uint64_t n, d;
            calib_eval(x_ppm, p, m, &n, &d);
            if (!have || n * bd < bn * d) { have = true; bn = n; bd = d; best.calp = p; best.calm = m; }
        }
    int64_t D = (1 << 20) + (int64_t)best.calm - 512 * (int64_t)best.calp;
    int64_t N = (1000000 + (int64_t)x_ppm) * (1 << 20) - 1000000 * D;
    best.resid_ppb = (N * 1000 + (N >= 0 ? D / 2 : -D / 2)) / D;         /* ppb 로 반올림 */
    return best;
}
static int64_t drift_ms_per_day(int64_t ppb) { return (ppb * 86400 + (ppb >= 0 ? 500000 : -500000)) / 1000000; }
static void ex2(void) {
    static const struct { int32_t x; uint32_t p, m; } v[] = {
        { 0, 0, 0 }, { 20, 0, 21 }, { -20, 1, 491 }, { 1, 0, 1 }, { -1, 1, 511 },
        { 100, 0, 105 }, { 488, 0, 511 }, { -487, 1, 1 }, { 600, 0, 511 }, { -600, 1, 0 },
    };
    for (size_t i = 0; i < sizeof v / sizeof v[0]; i++) {
        calib_t c = calib_pick(v[i].x);
        CHECK(c.calp == v[i].p && c.calm == v[i].m);
    }
    CHECK(calib_pick(20).resid_ppb == -27 && drift_ms_per_day(20000) == 1728);   /* 20 ppm = 1.728 s/일 */
    CHECK(calib_pick(600).resid_ppb == 112618);                                   /* 범위 밖: 포화 */
    int bad = 0;
    for (int32_t x = -480; x <= 480; x++) {                                        /* 범위 안이면 잔여 < 0.5 ppm */
        calib_t c = calib_pick(x);
        if (c.resid_ppb > 500 || c.resid_ppb < -500) bad++;
    }
    CHECK(bad == 0);
}

/* ---------- 연습 3: 웨이크업 타이머 ----------
 * WUCKSEL: 0 = RTC/16, 1 = RTC/8, 2 = RTC/4, 3 = RTC/2, 4 = ck_spre (1 s), 6 = ck_spre + 2^16 (확인 권장)
 * 주기 = (WUTR + 1) 틱 (모드 6 은 + 65536). 고르는 규칙: 분주 클럭 중 정확히 나누어떨어지는 가장 고운 것 →
 * 없으면 오차 최소(동률이면 고운 쪽) → 32 s 넘으면 ck_spre (정수 초만). */
typedef struct { int wucksel; uint32_t wutr; uint64_t actual_us; } wut_t;
static int wut_pick(uint64_t period_us, uint32_t rtcclk, wut_t *o) {
    if (period_us == 0) return -1;
    static const uint32_t div[4] = { 16, 8, 4, 2 };
    bool have = false; uint64_t best_err = 0;
    for (int i = 3; i >= 0; i--) {                         /* 고운 것(RTC/2)부터 */
        uint64_t num = period_us * rtcclk, den = 1000000ull * div[i];
        uint64_t ticks = (num + den / 2) / den;
        if (ticks < 1 || ticks > 65536u) continue;
        uint64_t act = (ticks * div[i] * 1000000ull + rtcclk / 2) / rtcclk;
        uint64_t err = act > period_us ? act - period_us : period_us - act;
        if (!have || err < best_err) { have = true; best_err = err; o->wucksel = i; o->wutr = (uint32_t)ticks - 1u; o->actual_us = act; }
    }
    if (have) return 0;
    if (period_us % 1000000u) return -1;                  /* 32 s 초과는 1 s 단위만 */
    uint64_t s = period_us / 1000000u;
    if (s <= 65536u) { o->wucksel = 4; o->wutr = (uint32_t)s - 1u; o->actual_us = period_us; return 0; }
    if (s <= 131072u) { o->wucksel = 6; o->wutr = (uint32_t)(s - 65536u - 1u); o->actual_us = period_us; return 0; }
    return -1;
}
static void ex3(void) {
    wut_t w;
    CHECK(wut_pick(1000000u, 32768u, &w) == 0 && w.wucksel == 3 && w.wutr == 16383u && w.actual_us == 1000000u);
    CHECK(wut_pick(10000u, 32768u, &w) == 0 && w.wucksel == 3 && w.wutr == 163u && w.actual_us == 10010u);
    CHECK(wut_pick(32000000u, 32768u, &w) == 0 && w.wucksel == 0 && w.wutr == 65535u);
    CHECK(wut_pick(60000000u, 32768u, &w) == 0 && w.wucksel == 4 && w.wutr == 59u);
    CHECK(wut_pick(129600000000ull, 32768u, &w) == 0 && w.wucksel == 6 && w.wutr == 64063u);   /* 36 시간 */
    CHECK(wut_pick(131073000000ull, 32768u, &w) == -1 && wut_pick(0, 32768u, &w) == -1);
    CHECK(wut_pick(45500000u, 32768u, &w) == -1);                       /* 45.5 s: 분주로는 너무 길고 정수 초 아님 */
}

/* ---------- 연습 4: 섀도 없는 분할 카운터 ----------
 * 32비트 초 카운터가 CNTH(상위 16) / CNTL(하위 16) 두 레지스터로 보인다. 접근 k 번째 직후 카운터 +1 주입. */
static uint32_t g_cnt, g_acc, g_inj_at;
static bool g_inj_armed;
static uint32_t rd(int hi) {
    uint32_t v = hi ? (g_cnt >> 16) : (g_cnt & 0xFFFFu);
    if (g_inj_armed && ++g_acc == g_inj_at) { g_cnt++; g_inj_armed = false; }
    return v;
}
static uint32_t read_naive(void) { uint32_t h = rd(1), l = rd(0); return (h << 16) | l; }
static uint32_t read_hlh(void) {
    uint32_t h1 = rd(1), l = rd(0), h2 = rd(1);
    if (h1 != h2) l = rd(0);                               /* 자리올림이 끼었다 → 새 상위와 짝인 하위를 다시 */
    return (h2 << 16) | l;
}
static int sweep(uint32_t (*f)(void), uint32_t start, int *torn_mask) {
    int n = 0; *torn_mask = 0;
    for (uint32_t k = 0; k <= 4; k++) {
        g_cnt = start; g_acc = 0; g_inj_at = k; g_inj_armed = k > 0;
        if (k == 0) g_cnt++;
        uint32_t v = f();
        if (v != start && v != start + 1u) { *torn_mask |= 1 << k; n++; }
    }
    return n;
}
static void ex4(void) {
    int mask;
    CHECK(sweep(read_naive, 0x0001FFFFu, &mask) == 1 && mask == 0x2);   /* k=1: 0x0001_0000 (−65535 s) */
    CHECK(sweep(read_hlh, 0x0001FFFFu, &mask) == 0);
    uint32_t seed = 2463534242u; int bad = 0;
    for (int i = 0; i < 20000; i++) {
        seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
        uint32_t start = (seed & 3u) ? ((seed & 0xFFFF0000u) | 0xFFFFu) : seed;   /* 3/4 는 자리올림 직전 */
        if (sweep(read_hlh, start, &mask) != 0) bad++;
    }
    CHECK(bad == 0);
}

int main(void) {
    ex1(); printf("ex1 12h format: %d/%d\n", g_pass, g_total);
    ex2(); printf("ex2 smooth calibration: %d/%d\n", g_pass, g_total);
    ex3(); printf("ex3 wakeup timer: %d/%d\n", g_pass, g_total);
    ex4(); printf("ex4 split counter hi-lo-hi: %d/%d\n", g_pass, g_total);
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
