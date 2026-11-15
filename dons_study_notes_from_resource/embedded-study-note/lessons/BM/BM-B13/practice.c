/* BM-B13 추가 연습 — 한 번에 검증
 *
 * 연습 1: 64비트 tick (hi:lo) — ISR 이 lo 를 올리고 넘치면 hi 를 올린다. hi→lo→hi 재읽기 (C-M14 참고)
 * 연습 2: micros() = g_ticks·1000 + SysTick CVR 로 tick 안 위치. (ticks, CVR) 두 번 읽기 일관성
 * 연습 3: 구현 정의 캐스트 없이 wrap-safe 비교: (uint32_t)(now - deadline) < 0x80000000u
 * 연습 4: 32.768 kHz 에서 1024 Hz tick — ms ↔ tick 변환(올림/내림), +1 규칙, wrap 주기
 * SysTick 모델은 solution.c 와 같은 규칙의 축약판 (BM-B02 mock 축약판 위).
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#define UNIT_TEST 1
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static uint32_t xs = 0x2468ACE1u;
static uint32_t rnd(void) { xs ^= xs << 13; xs ^= xs >> 17; xs ^= xs << 5; return xs; }

/* ======================= 연습 1: 64비트 tick ======================= */
static volatile uint32_t t_lo, t_hi;
static int g_isr_after_read = -1, g_reads;      /* n 번째 읽기 직후 ISR 주입 (-1 = 없음) */
static void tick_isr(void) { if (++t_lo == 0u) t_hi++; }
static uint32_t rd(volatile uint32_t *p) {
    uint32_t v = *p;
    if (g_reads++ == g_isr_after_read) tick_isr();
    return v;
}
static uint64_t ticks64_naive(void) {           /* hi 다음 lo — 사이에 wrap 이 끼면 2^32 뒤로 간다 */
    uint32_t hi = rd(&t_hi), lo = rd(&t_lo);
    return ((uint64_t)hi << 32) | lo;
}
static uint64_t ticks64(void) {                 /* 답: hi 를 앞뒤로 읽어 같을 때만 믿는다 */
    uint32_t h1, lo, h2;
    do { h1 = rd(&t_hi); lo = rd(&t_lo); h2 = rd(&t_hi); } while (h1 != h2);
    return ((uint64_t)h1 << 32) | lo;
}
static void test_ticks64(void) {
    int naive_bad = 0, bad = 0;
    for (int inj = -1; inj < 4; inj++) {
        for (uint32_t lo0 = 0xFFFFFFFDu; lo0 != 2u; lo0++) {   /* wrap 근처 시작점 */
            t_hi = 7u; t_lo = lo0; g_reads = 0; g_isr_after_read = inj;
            uint64_t before = ((uint64_t)7u << 32) | lo0;
            uint64_t v = ticks64_naive();
            uint64_t after = ((uint64_t)t_hi << 32) | t_lo;
            if (v < before || v > after) naive_bad++;
            t_hi = 7u; t_lo = lo0; g_reads = 0; g_isr_after_read = inj;
            v = ticks64();
            after = ((uint64_t)t_hi << 32) | t_lo;
            if (v < before || v > after) bad++;
        }
    }
    CHECK(naive_bad == 1);                      /* hi 읽은 직후, lo 가 0xFFFFFFFF 일 때 단 한 경우 */
    CHECK(bad == 0);
    t_hi = 7u; t_lo = 0xFFFFFFFFu; g_reads = 0; g_isr_after_read = 0;
    CHECK(ticks64_naive() == ((uint64_t)7u << 32));             /* 7:00000000 — 2^32 - 1 tick 과거 */
    t_hi = 7u; t_lo = 0xFFFFFFFFu; g_reads = 0; g_isr_after_read = 0;
    CHECK(ticks64() == ((uint64_t)8u << 32) && g_reads == 6);   /* 한 번 재시도 */
}

/* ======================= 연습 2: micros() — mock SysTick 축약판 ======================= */
#define SYST_CSR 0xE000E010u
#define SYST_RVR 0xE000E014u
#define SYST_CVR 0xE000E018u
static uint32_t m_csr, m_rvr, m_cvr, m_stall; static uint64_t m_cycles; static int m_unexpected;
static volatile uint32_t g_ticks;
static void SysTick_Handler(void) { g_ticks++; }
static void mock_cycles(uint64_t n) {           /* CLKSOURCE=1 가정: 코어 클럭 = SysTick 클럭 */
    while (n--) {
        m_cycles++;
        if (!(m_csr & 1u)) continue;
        if (m_cvr == 0u) { m_cvr = m_rvr; continue; }
        if (--m_cvr == 0u) { m_csr |= 1u << 16; if (m_csr & 2u) SysTick_Handler(); }
    }
}
static uint32_t mock_read32(uint32_t addr) {
    if (addr == SYST_CVR) {                     /* 주입: CVR 를 읽기 직전에 m_stall 클럭이 흐른다 (선점·버스 지연) */
        uint32_t k = m_stall; m_stall = 0; mock_cycles(k);
        return m_cvr;
    }
    if (addr == SYST_RVR) return m_rvr;
    if (addr == SYST_CSR) { uint32_t v = m_csr; m_csr &= ~(1u << 16); return v; }
    m_unexpected++; return 0u;
}
#define REG_READ(addr) mock_read32(addr)
static void systick_start_1mhz(void) {         /* 1 MHz 코어, 1 kHz tick → RVR = 999, 1 클럭 = 1 µs */
    m_csr = 7u; m_rvr = 999u; m_cvr = 0u; m_cycles = 0; m_stall = 0; g_ticks = 0;
}
static uint32_t tick_offset(uint32_t rvr, uint32_t cvr) {   /* 마지막 tick 경계 이후 지난 클럭 */
    return (rvr - cvr + 1u) % (rvr + 1u);
}
static uint64_t micros_naive(void) {            /* 틀림: tick 과 CVR 이 서로 다른 순간의 값일 수 있다 */
    uint32_t t = g_ticks, cvr = REG_READ(SYST_CVR), rvr = REG_READ(SYST_RVR);
    return (uint64_t)t * 1000u + (uint64_t)tick_offset(rvr, cvr) * 1000u / (rvr + 1u);
}
static uint64_t micros(void) {                  /* 답: tick → CVR → tick, tick 이 그대로일 때만 믿는다 */
    uint32_t t1, t2, cvr, rvr = REG_READ(SYST_RVR);
    do { t1 = g_ticks; cvr = REG_READ(SYST_CVR); t2 = g_ticks; } while (t1 != t2);
    return (uint64_t)t1 * 1000u + (uint64_t)tick_offset(rvr, cvr) * 1000u / (rvr + 1u);
}
static void test_micros(void) {
    int naive_back = 0, bad = 0, exact = 0;
    for (uint32_t phase = 0; phase < 3000u; phase++) {
        systick_start_1mhz(); mock_cycles(phase);
        if (micros() == m_cycles) exact++;                       /* 정지 상태: 1 클럭 = 1 µs 정확 */
        uint64_t t0 = m_cycles; m_stall = 3u;
        uint64_t v = micros_naive();
        if (v < t0) naive_back++;
        systick_start_1mhz(); mock_cycles(phase);
        t0 = m_cycles; m_stall = 3u;
        v = micros();
        if (v < t0 || v > m_cycles) bad++;
    }
    CHECK(exact == 3000);
    CHECK(naive_back == 9);                     /* 경계 3번 × stall 3클럭 위상 = 9 — 1 ms 뒤로 감 */
    CHECK(bad == 0);
    systick_start_1mhz(); mock_cycles(2999); m_stall = 3u;
    CHECK(micros_naive() == 2002u);             /* 진짜는 2999~3002 µs → 약 1 ms 과거 */
    systick_start_1mhz(); mock_cycles(2999); m_stall = 3u;
    CHECK(micros() == 3002u);
    CHECK(m_unexpected == 0);
}

/* ======================= 연습 3: 구현 정의 없는 wrap-safe 비교 ======================= */
static bool tae_cast(uint32_t now, uint32_t dl) { return (int32_t)(now - dl) >= 0; }   /* C11 6.3.1.3p3: 구현 정의 */
static bool tae_unsigned(uint32_t now, uint32_t dl) { return (uint32_t)(now - dl) < 0x80000000u; }
static void test_unsigned_compare(void) {
    int diff = 0;
    for (uint64_t d = 0; d <= 0xFFFFFFFFull; d += 65521u) {            /* 거리 전 구간 표본 */
        uint32_t now = rnd();
        if (tae_cast(now, now - (uint32_t)d) != tae_unsigned(now, now - (uint32_t)d)) diff++;
    }
    for (int64_t e = -300; e <= 300; e++) {                            /* 0 과 2^31 경계 근처 전부 */
        uint32_t now = rnd();
        uint32_t d0 = (uint32_t)e, d1 = 0x80000000u + (uint32_t)e;
        if (tae_cast(now, now - d0) != tae_unsigned(now, now - d0)) diff++;
        if (tae_cast(now, now - d1) != tae_unsigned(now, now - d1)) diff++;
    }
    for (int i = 0; i < 1000000; i++) { uint32_t a = rnd(), b = rnd(); if (tae_cast(a, b) != tae_unsigned(a, b)) diff++; }
    CHECK(diff == 0);
    CHECK(tae_unsigned(0x00000005u, 0xFFFFFFF0u) == true);
    CHECK(tae_unsigned(0x80000000u, 0x00000000u) == false);            /* 2^31: 같은 한계 */
}

/* ======================= 연습 4: 1024 Hz tick (32.768 kHz) ======================= */
#define TICK_HZ 1024u
static uint32_t ms_to_ticks_ceil(uint32_t ms) {          /* 최소 ms 를 덮는 tick 수 (올림, 포화) */
    uint64_t t = ((uint64_t)ms * TICK_HZ + 999u) / 1000u; /* 64비트 중간값: ms·1024 는 32비트를 넘는다 */
    return t > UINT32_MAX ? UINT32_MAX : (uint32_t)t;
}
static uint32_t delay_wait_ticks_1024(uint32_t ms) {     /* +1 규칙은 그대로: 위상 때문에 경계 하나 더 */
    if (ms == 0u) return 0u;
    uint64_t t = (uint64_t)ms_to_ticks_ceil(ms) + 1u;
    return t > UINT32_MAX ? UINT32_MAX : (uint32_t)t;
}
static uint64_t ticks_to_ms(uint64_t ticks) { return ticks * 1000u / TICK_HZ; }   /* 내림 */
static void test_1024(void) {
    CHECK(32768u % TICK_HZ == 0u && 32768u / TICK_HZ - 1u == 31u);    /* reload 31: 정확 */
    CHECK(ms_to_ticks_ceil(0u) == 0u);
    CHECK(ms_to_ticks_ceil(1u) == 2u);          /* 1.024 tick → 2 */
    CHECK(ms_to_ticks_ceil(125u) == 128u);      /* 125 ms = 정확히 128 tick */
    CHECK(ms_to_ticks_ceil(1000u) == 1024u);
    CHECK(ms_to_ticks_ceil(4194303999u) == UINT32_MAX);                /* 4294967294.976 → 올림 = 2^32 - 1 */
    CHECK(ms_to_ticks_ceil(UINT32_MAX) == UINT32_MAX);                 /* 4398046510.08 → 포화 (잘리면 103079215) */
    CHECK(delay_wait_ticks_1024(1u) == 3u);
    CHECK(delay_wait_ticks_1024(4194303998u) == UINT32_MAX);           /* 4294967294 + 1 */
    CHECK(delay_wait_ticks_1024(4194303999u) == UINT32_MAX);           /* 포화 */
    CHECK(ticks_to_ms(1024u) == 1000u && ticks_to_ms(1u) == 0u && ticks_to_ms(2u) == 1u);
    {   /* 성질: tick 경계 위상과 무관하게 delay 는 최소 ms */
        int bad = 0;
        for (uint32_t ms = 1; ms <= 3000u; ms++) {
            uint32_t w = delay_wait_ticks_1024(ms);
            /* 최악 위상: 첫 경계가 즉시 → 실제 대기 = (w - 1) tick */
            if ((uint64_t)(w - 1u) * 1000000u < (uint64_t)ms * 1000u * TICK_HZ) bad++;
        }
        CHECK(bad == 0);
    }
    /* wrap 주기: 2^32 / tick_hz 초 */
    CHECK((1ull << 32) / 1000u == 4294967u);    /* 1 kHz: 4 294 967 s = 49.71 일 */
    CHECK((1ull << 32) / TICK_HZ == 4194304u);  /* 1024 Hz: 4 194 304 s = 48.5 일 */
    CHECK((1ull << 32) / 1000000u == 4294u);    /* 1 MHz (µs 카운터): 4294 s = 71.6 분 */
    CHECK(0x7FFFFFFFu / 1000u / 86400u == 24u); /* deadline 형식 한계: 2^31 - 1 ms = 24.8 일 */
}

int main(void) {
    test_ticks64();
    test_micros();
    test_unsigned_compare();
    test_1024();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
