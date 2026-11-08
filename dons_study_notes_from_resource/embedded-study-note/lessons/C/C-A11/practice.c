/* C-A11 추가 연습 — 한 번에 검증
 *
 * 연습 1  잠들기 직전 경쟁: check-then-WFI          (idle_buggy 재현 + idle_fixed)
 * 연습 2  임계 구역 대신 C11 원자 연산으로 이벤트 플래그 (fetch_or / exchange / fetch_and)
 * 연습 3  부호 안전 디코더: 구현 정의 시프트 없이 N비트 부호 확장
 * 연습 4  스니펫 6 리뷰: SPI 송신 루프 (volatile 캐스트 제거, off-by-one, 타임아웃 없음)
 */
#include <limits.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* ---------------- 연습 1: check-then-WFI ---------------- */
static volatile uint32_t p_events;
static uint32_t p_primask;
static int p_pending, p_nacc, p_fire_at = -1, p_isr_runs, p_slept_with_event, p_woke_by_pending;

static void p_isr(void) { p_events = p_events | 1u; p_isr_runs++; }
static void p_irq_line(void) { if (p_primask) p_pending = 1; else p_isr(); }
static uint32_t p_rd(const volatile uint32_t *r) {
    uint32_t v = *r;
    if (++p_nacc == p_fire_at) { p_fire_at = -1; p_irq_line(); }
    return v;
}
static uint32_t p_save(void) { uint32_t o = p_primask; p_primask = 1u; return o; }
static void p_restore(uint32_t o) {
    p_primask = o;
    if (!p_primask && p_pending) { p_pending = 0; p_isr(); }
}
/* Cortex-M WFI 모델: 펜딩 인터럽트가 있으면 (PRIMASK=1 이어도) 바로 깬다.
 * 없으면 다음 인터럽트까지 잠든다 — 이때 처리 안 된 이벤트가 있으면 "늦은 처리" 로 센다. */
static void wfi(void) {
    if (p_pending) { p_woke_by_pending++; return; }
    if (p_events) p_slept_with_event++;
}
static void idle_buggy(void) {                 /* 리뷰 대상 원본 */
    if (p_rd(&p_events) == 0u) wfi();          /* ← 검사와 WFI 사이에 ISR 이 오면? */
}
static void idle_fixed(void) {
    uint32_t pm = p_save();                    /* 막은 채로 검사하고 잔다 */
    if (p_rd(&p_events) == 0u) wfi();          /* 사이에 온 IRQ 는 펜딩 → WFI 가 즉시 깬다 */
    p_restore(pm);                             /* 여기서 ISR 실행 */
}
static void p_reset(int fire_at) {
    p_events = 0u; p_primask = 0u; p_pending = 0; p_nacc = 0; p_fire_at = fire_at;
    p_isr_runs = 0; p_slept_with_event = 0; p_woke_by_pending = 0;
}

/* ---------------- 연습 2: 원자 연산 이벤트 플래그 ---------------- */
static _Atomic uint32_t a_events;
static void a_isr_set(uint32_t bits) {         /* ISR: 비트를 세운다 */
    atomic_fetch_or_explicit(&a_events, bits, memory_order_release);
}
static uint32_t a_take(void) {                 /* main: 전부 가져가며 0 으로 — 한 번의 RMW */
    return atomic_exchange_explicit(&a_events, 0u, memory_order_acquire);
}
static int a_take_one(uint32_t bit) {          /* main: 한 비트만 가져간다. 그 비트가 서 있었으면 1 */
    return (atomic_fetch_and_explicit(&a_events, ~bit, memory_order_acquire) & bit) != 0u;
}

/* ---------------- 연습 3: N비트 2의 보수 → int32_t (1 <= bits <= 32) ---------------- */
static int32_t sext(uint32_t v, unsigned bits) {
    uint32_t mask = (bits == 32u) ? 0xFFFFFFFFu : ((1u << bits) - 1u);
    uint32_t x = v & mask;
    uint32_t sign = 1u << (bits - 1u);
    if ((x & sign) == 0u) return (int32_t)x;               /* 양수: 그대로 (범위 안) */
    return -(int32_t)(~x & mask) - 1;                      /* 음수: -(보수) - 1, 오버플로 없음 */
}
static int64_t sext_ref(uint32_t v, unsigned bits) {      /* 기준값: 64비트 정수 산술 */
    uint64_t m = (bits == 32u) ? 0xFFFFFFFFull : ((1ull << bits) - 1u);
    uint64_t x = v & m;
    return (x >> (bits - 1u)) ? (int64_t)x - (int64_t)(1ull << bits) : (int64_t)x;
}
static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

/* ---------------- 연습 4: SPI 송신 ---------------- */
#define SPI_SR_TXE (1u << 1)
typedef struct { volatile uint32_t SR, DR; } spi_regs_t;
static spi_regs_t SPI1_m;
static int s_ready_after;                      /* DR 를 쓴 뒤 SR 을 몇 번 읽어야 TXE 가 서나 (-1 = 영원히 안 섬) */
static int s_reads_since_write;
static uint8_t s_sent[16];
static size_t s_nsent;
static uint32_t spi_sr(void) {                 /* = READ_REG(SPI1->SR) */
    if (s_ready_after >= 0 && ++s_reads_since_write >= s_ready_after) SPI1_m.SR |= SPI_SR_TXE;
    return SPI1_m.SR;
}
static void spi_dr(uint8_t b) {                /* = WRITE_REG(SPI1->DR, b): TXE 가 내려간다 */
    if (s_nsent < sizeof s_sent) s_sent[s_nsent++] = b;
    SPI1_m.SR &= ~SPI_SR_TXE;
    s_reads_since_write = 0;
}
/* 고친 판: n 바이트 보내고 n 반환. 한 바이트라도 timeout(SR 읽기 횟수) 안에 TXE 가 안 서면 -1. */
static int spi_send(const uint8_t *b, size_t n, uint32_t timeout) {
    for (size_t i = 0; i < n; i++) {
        uint32_t t = timeout;
        while ((spi_sr() & SPI_SR_TXE) == 0u) {
            if (t-- == 0u) return -1;          /* 버스가 멈춰도 펌웨어는 멈추지 않는다 */
        }
        spi_dr(b[i]);
    }
    return (int)n;
}
static void s_reset(int ready_after) {
    SPI1_m.SR = SPI_SR_TXE; s_ready_after = ready_after; s_reads_since_write = 0; s_nsent = 0;
    memset(s_sent, 0, sizeof s_sent);
}

int main(void) {
    /* 연습 1 */
    p_reset(1);                                 /* 검사 읽기 직후 IRQ */
    idle_buggy();
    CHECK(p_isr_runs == 1 && p_slept_with_event == 1);     /* 원본: 이벤트를 두고 잠든다 (재현) */
    p_reset(1);
    idle_fixed();
    CHECK(p_isr_runs == 1 && p_slept_with_event == 0 && p_woke_by_pending == 1);
    CHECK(p_events == 1u && p_primask == 0u);
    p_reset(-1);
    idle_fixed();                               /* 이벤트 없음 → 그냥 잔다 (깰 이유 없음) */
    CHECK(p_isr_runs == 0 && p_slept_with_event == 0 && p_woke_by_pending == 0);
    p_reset(-1);
    p_events = 1u;
    idle_fixed();                               /* 이벤트가 이미 있으면 자지 않는다 */
    CHECK(p_slept_with_event == 0 && p_woke_by_pending == 0);

    /* 연습 2 */
    CHECK(ATOMIC_INT_LOCK_FREE == 2 && atomic_is_lock_free(&a_events));
    atomic_store(&a_events, 0u);
    a_isr_set(1u << 0);
    a_isr_set(1u << 3);
    CHECK(a_take() == ((1u << 0) | (1u << 3)) && atomic_load(&a_events) == 0u);
    a_isr_set(1u << 1);
    a_isr_set(1u << 2);
    CHECK(a_take_one(1u << 1) == 1 && atomic_load(&a_events) == (1u << 2));   /* 다른 비트는 남는다 */
    CHECK(a_take_one(1u << 1) == 0);
    CHECK(a_take() == (1u << 2) && a_take() == 0u);

    /* 연습 3 */
    CHECK(sext(0x800000u, 24) == -8388608 && sext(0xFFFFFFu, 24) == -1 && sext(0x7FFFFFu, 24) == 8388607);
    CHECK(sext(0x800u, 12) == -2048 && sext(0xFFFu, 12) == -1 && sext(0x7FFu, 12) == 2047);
    CHECK(sext(0x80000000u, 32) == INT32_MIN && sext(0xFFFFFFFFu, 32) == -1 && sext(1u, 1) == -1);
    CHECK(sext(0xFF123456u, 24) == 0x123456);                  /* 상위 쓰레기 비트는 무시 */
    uint32_t seed = 0xC0FFEEu;
    int ok = 1;
    for (int i = 0; i < 100000; i++) {
        uint32_t v = xs32(&seed);
        unsigned bits = 1u + xs32(&seed) % 32u;
        ok &= (int64_t)sext(v, bits) == sext_ref(v, bits);
    }
    CHECK(ok);

    /* 연습 4 */
    const uint8_t msg[3] = { 0x9Fu, 0x00u, 0x42u };
    s_reset(2);
    CHECK(spi_send(msg, 3, 10u) == 3 && s_nsent == 3 && memcmp(s_sent, msg, 3) == 0);   /* 정확히 n 바이트 */
    s_reset(-1);
    SPI1_m.SR = 0u;                                            /* TXE 가 영원히 안 선다 */
    CHECK(spi_send(msg, 3, 1000u) == -1 && s_nsent == 0);      /* 멈추지 않고 에러 */
    s_reset(5);
    CHECK(spi_send(msg, 3, 3u) == -1 && s_nsent == 1);         /* 첫 바이트 뒤 타임아웃 */
    s_reset(2);
    CHECK(spi_send(msg, 0, 10u) == 0 && s_nsent == 0);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
