/* RTOS-A02 추가 연습 — tickless idle 응용 4문제 (호스트 C, 시뮬레이션 시간, 결정적)
 *   연습 1  클럭 설정: N 과 최대 억제 tick, 나누어떨어지지 않는 tick 주파수의 드리프트와 분수 누산기
 *   연습 2  "타이머가 멈춰 있는 시간" 보정 (ulStoppedTimerCompensation): 한 번 잘 때마다의 위상 오차
 *   연습 3  자유 계수 RTC + compare 방식 tickless: 정지가 없으니 보정도 없다 (24-bit wrap 포함)
 *   연습 4  configEXPECTED_IDLE_TIME_BEFORE_SLEEP 손익분기
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

static int g_pass, g_total;
#define CHECK(e) do { g_total++; if (e) { g_pass++; printf("PASS  %s\n", #e); } \
                      else printf("FAIL  %s   (line %d)\n", #e, __LINE__); } while (0)
#define NEVER UINT64_MAX

/* ======================= 연습 1 ======================= */
static void tick_cfg(uint32_t timer_hz, uint32_t tick_hz, unsigned width_bits, uint32_t *n, uint32_t *max_supp) {
    *n = timer_hz / tick_hz;                                    /* ulTimerCountsForOneTick */
    *max_supp = (uint32_t)(((1ull << width_bits) - 1u) / *n);   /* xMaximumPossibleSuppressedTicks */
}
/* tick 마다 몇 count 를 셀지: 몫 q 에 나머지를 누산해 넘칠 때 +1 (Bresenham). 누적 오차 < 1 count. */
typedef struct { uint32_t q, r, den, acc; } frac_t;
static frac_t frac_init(uint32_t timer_hz, uint32_t tick_hz) { frac_t f = { timer_hz / tick_hz, timer_hz % tick_hz, tick_hz, 0 }; return f; }
static uint32_t frac_next(frac_t *f) {
    f->acc += f->r;
    if (f->acc >= f->den) { f->acc -= f->den; return f->q + 1u; }
    return f->q;
}

static void ex1(void) {
    printf("\n== 연습 1: 클럭 설정 ==\n");
    uint32_t n, m;
    tick_cfg(64000000u, 1000u, 24, &n, &m); CHECK(n == 64000u && m == 262u);    /* SysTick 64 MHz */
    tick_cfg(48000000u, 1000u, 24, &n, &m); CHECK(n == 48000u && m == 349u);    /* 본문 */
    tick_cfg(16000000u, 1000u, 24, &n, &m); CHECK(n == 16000u && m == 1048u);
    tick_cfg(32768u, 1024u, 16, &n, &m);    CHECK(n == 32u && m == 2047u);      /* 16-bit LPTIM, LSE */
    /* 32768 Hz 에 1000 Hz tick: N = 32 로 잘림 → tick 이 0.9765625 ms. 진짜 1 시간 동안 커널이 세는 tick */
    tick_cfg(32768u, 1000u, 16, &n, &m);
    uint64_t ticks_hour = 3600ull * 32768u / n;
    printf("  32768/1000 → N=%u, 1 시간에 %llu tick (+%llu, %.2f %%)\n", n, (unsigned long long)ticks_hour,
           (unsigned long long)(ticks_hour - 3600000u), (ticks_hour - 3600000.0) / 36000.0);
    CHECK(n == 32u && ticks_hour == 3686400u);
    /* 분수 누산기: 1000 tick = 정확히 32768 count, 경계 오차 항상 1 count 미만 */
    frac_t f = frac_init(32768u, 1000u);
    uint64_t sum = 0; bool within = true;
    for (uint64_t k = 1; k <= 3600000u; k++) {
        sum += frac_next(&f);
        uint64_t ideal_floor = k * 32768u / 1000u;             /* 이상적 경계 = k·32.768 */
        if (!(sum == ideal_floor || sum == ideal_floor + 1u)) within = false;
        if (k == 1000u) CHECK(sum == 32768u);
    }
    CHECK(within && sum == 3600ull * 32768u);                 /* 1 시간 = 3,600,000 tick, 드리프트 0 */
}

/* ======================= 연습 2 ======================= */
/* 본문과 같은 SysTick 모델 (start 즉시 적재, 1→0 에서 IRQ, 0 다음 클럭 재적재) 을 한 번의 잠에만 축소해 쓴다.
 * 시각 0 에 VAL = v0 (다음 tick 경계 = v0, 기준 경계 T = v0 − N). 타이머가 멈춘 구간 s_pre, s_post 동안
 * 진짜 시간은 흐르지만 카운터는 안 센다. comp 는 reload 에서 빼는 보정값. ext_at = 다른 IRQ 시각 (NEVER = 없음).
 * 반환: 다음 tick IRQ 시각 − 그 tick 의 이상적 경계 (count). *counted = 이번 잠으로 커널이 센 tick. */
static int64_t one_sleep(uint32_t N, uint32_t v0, uint32_t E, uint64_t ext_at,
                         uint32_t s_pre, uint32_t s_post, uint32_t comp, uint32_t *counted) {
    uint64_t t = 0;                                   /* 정지 순간 */
    uint32_t left = v0 ? v0 : N;
    t += s_pre;                                       /* 계산하는 동안 카운터 정지 */
    uint32_t reload = left + N * (E - 1u);
    if (reload > comp) reload -= comp;                /* port.c:648-651 */
    uint64_t t_irq = t + reload;                      /* 즉시 적재 → reload 클럭 뒤 IRQ */
    uint32_t remain;
    if (ext_at < t_irq) {                             /* 조기 기상 */
        uint32_t val = (uint32_t)(t_irq - ext_at);
        uint32_t done = E * N - val, c = done / N;
        remain = (c + 1u) * N - done; *counted = c; t = ext_at;
    } else {                                          /* 만료: d = 0 → 남은 조각 N */
        remain = N; *counted = E; t = t_irq;
    }
    t += s_post;                                      /* 다시 정지 구간 */
    uint64_t next_irq = t + remain;
    int64_t ideal = (int64_t)v0 - (int64_t)N + (int64_t)(*counted + 1u) * N;
    return (int64_t)next_irq - ideal;
}

static void ex2(void) {
    printf("\n== 연습 2: 정지 구간 보정 ==\n");
    const uint32_t N = 48000;
    uint32_t c;
    /* 정지 0, 보정 0 → 오차 0 (본문) */
    CHECK(one_sleep(N, 30000, 100, NEVER, 0, 0, 0, &c) == 0 && c == 100u);
    CHECK(one_sleep(N, 30000, 100, 37u * N + 5000u, 0, 0, 0, &c) == 0 && c == 37u);
    /* 오차 = s_pre + s_post − comp, 경로와 무관 (조기/만료, v0 여러 값) */
    bool law = true;
    uint32_t v0s[3] = { 1, 30000, 48000 }; uint32_t pre[3] = { 0, 40, 47 }, post[3] = { 0, 50, 47 }, cm[3] = { 0, 94, 90 };
    for (int a = 0; a < 3; a++) for (int b = 0; b < 3; b++) for (int k = 0; k < 3; k++) {
        int64_t want = (int64_t)pre[b] + post[b] - cm[k];
        if (one_sleep(N, v0s[a], 200, NEVER, pre[b], post[b], cm[k], &c) != want) law = false;
        if (one_sleep(N, v0s[a], 200, 77u * N + 20000u, pre[b], post[b], cm[k], &c) != want) law = false;
    }
    CHECK(law);
    /* 10 분 동안 3,360 번 잤다면 (본문 Part C 의 횟수) */
    int64_t e_none = one_sleep(N, 30000, 100, NEVER, 40, 50, 0, &c) * 3360;
    int64_t e_94   = one_sleep(N, 30000, 100, NEVER, 40, 50, 94, &c) * 3360;
    printf("  정지 40+50 count: 보정 없음 %lld count = %.3f ms 느려짐 / 보정 94 → %lld count = %.3f ms 빨라짐 (10 분)\n",
           (long long)e_none, e_none / 48000.0, (long long)e_94, e_94 / 48000.0);
    CHECK(e_none == 302400 && e_94 == -13440);
}

/* ======================= 연습 3 ======================= */
/* 자유 계수 24-bit RTC (32768 Hz, 멈추지 않음) + compare 레지스터 하나. 커널은 "마지막으로 알린 tick 경계의 카운터 값"
 * last 만 기억한다. 잘 때 CC = last + E·N, 깨면 경과 = (cnt − last) mod 2^24 를 N 으로 나눠 온전한 tick 만 알린다. */
#define RTC_MASK 0xFFFFFFu
typedef struct { uint32_t last; uint64_t ticks; uint32_t N, max_ticks; } rtc_tl_t;
static uint32_t rtc_set_timeout(rtc_tl_t *k, uint32_t E) {        /* CC 값 */
    if (E > k->max_ticks) E = k->max_ticks;
    return (k->last + E * k->N) & RTC_MASK;
}
static uint32_t rtc_announce(rtc_tl_t *k, uint32_t cnt) {         /* 깨어난 순간의 카운터 → 알린 tick 수 */
    uint32_t elapsed = (cnt - k->last) & RTC_MASK;
    uint32_t dt = elapsed / k->N;
    k->ticks += dt;
    k->last = (k->last + dt * k->N) & RTC_MASK;
    return dt;
}
static uint32_t g_rng = 2463534242u;
static uint32_t xorshift32(void) { uint32_t x = g_rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g_rng = x; }

static void ex3(void) {
    printf("\n== 연습 3: 자유 계수 RTC + compare ==\n");
    rtc_tl_t k = { 0, 0, 32, (RTC_MASK / 2u) / 32u };            /* 32768 Hz / 1024 Hz, 반 바퀴까지만 (모호성 방지) */
    CHECK(k.max_ticks == 262143u);
    uint64_t now = 0; bool exact = true; uint32_t wraps = 0;
    for (int i = 0; i < 200000; i++) {
        uint32_t E = 2u + xorshift32() % 300000u;                 /* 상한보다 큰 요청도 섞는다 */
        uint32_t cc = rtc_set_timeout(&k, E);
        uint64_t until_cc = (cc - (uint32_t)(now & RTC_MASK)) & RTC_MASK;
        uint64_t wake = (xorshift32() & 1u) ? until_cc : xorshift32() % (until_cc + 1u);   /* 만료 또는 조기 */
        uint64_t before = now >> 24;
        now += wake;
        if ((now >> 24) != before) wraps++;
        rtc_announce(&k, (uint32_t)(now & RTC_MASK));
        if (k.ticks != now / k.N) exact = false;                  /* 커널 tick = 진짜 시간 / N, 항상 */
    }
    printf("  200000 회 잠, 카운터 wrap %u 번, 커널 tick=%llu, 진짜 시간/N=%llu\n", wraps,
           (unsigned long long)k.ticks, (unsigned long long)(now / k.N));
    CHECK(exact && wraps > 100);
}

/* ======================= 연습 4 ======================= */
/* 같은 Sleep 모드에서 k tick 동안 쉴 때: ticked = 매 tick 깨어남 k 번 × wake_clk,
 * tickless = 진입·복귀 계산 ovh_clk + 깨어남 1 번. tickless 가 엄격히 이득인 최소 k. */
static uint32_t breakeven_ticks(uint32_t ovh_clk, uint32_t wake_clk) {
    uint32_t k = 1;
    while ((uint64_t)k * wake_clk <= (uint64_t)ovh_clk + wake_clk) k++;
    return k;
}
static void ex4(void) {
    printf("\n== 연습 4: 손익분기 ==\n");
    CHECK(breakeven_ticks(0, 480) == 2u);          /* 공짜로 진입해도 1 tick 쉬는 건 이득 없음 → 최소 2 */
    CHECK(breakeven_ticks(960, 480) == 4u);        /* 20 µs 계산 @48 MHz */
    CHECK(breakeven_ticks(4800, 480) == 12u);      /* Stop 복귀 + PLL 재설정 100 µs 라면 */
    printf("  ovh 0/960/4800 clk → 최소 %u / %u / %u tick\n", breakeven_ticks(0, 480), breakeven_ticks(960, 480),
           breakeven_ticks(4800, 480));
}

int main(void) {
    ex1(); ex2(); ex3(); ex4();
    printf("\n%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
