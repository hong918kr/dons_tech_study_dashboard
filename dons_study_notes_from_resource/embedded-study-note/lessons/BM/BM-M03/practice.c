/* BM-M03 추가 연습 — 한 번에 검증
 *
 * P1 적분기(integrator) 디바운서 vs 10 샘플 시프트 레지스터 — 잡음 섞인 누름에서 지연 비교
 * P2 오토 리피트: LONG_PRESS 뒤 200 ms 마다 REPEAT
 * P3 공유 벡터 EXTI15_10 디스패처: 라인 10·13 동시 펜딩, 비트별 W1C (PR |= 버그와 비교)
 * P4 NVIC 로 막았던 라인 다시 허용: EXTI PR + NVIC ICPR 정리 후 ISER (가짜 진입 0)
 *
 * 레지스터 주소는 STM32F4 (RM0090) / ARMv7-M 기준 대표값 — RM/ARM ARM 확인 권장. 정답은 아래 mock 의 정의.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/BM-M03_pr && /tmp/BM-M03_pr
 */
#define UNIT_TEST 1
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static uint32_t t_rng = 0x13579BDFu;
static uint32_t xs32(void) { t_rng ^= t_rng << 13; t_rng ^= t_rng >> 17; t_rng ^= t_rng << 5; return t_rng; }

/* ===================== P1 — 적분기 vs 시프트 레지스터 (1 ms 샘플 단위) ===================== */
#define K 10u
typedef struct { uint8_t cnt, stable; } integ_t;          /* cnt ∈ [0, K]: 눌림 샘플 +1, 놓임 샘플 −1 */
typedef struct { uint16_t hist; uint8_t stable; } shift_t;
enum { NONE = 0, PRESS = 1, RELEASE = 2 };
static int integ_step(integ_t *d, unsigned raw) {
    if (raw) { if (d->cnt < K) d->cnt++; } else if (d->cnt > 0) d->cnt--;
    if (!d->stable && d->cnt == K) { d->stable = 1; return PRESS; }
    if (d->stable && d->cnt == 0) { d->stable = 0; return RELEASE; }
    return NONE;
}
static int shift_step(shift_t *d, unsigned raw) {
    d->hist = (uint16_t)(((uint32_t)d->hist << 1 | raw) & ((1u << K) - 1u));
    if (!d->stable && d->hist == (1u << K) - 1u) { d->stable = 1; return PRESS; }
    if (d->stable && d->hist == 0u) { d->stable = 0; return RELEASE; }
    return NONE;
}
static void p1(void) {
    /* 누름 300 ms 동안 각 샘플이 drop% 확률로 '놓임' 으로 읽힘 (접점 저항이 큰 낡은 스위치) */
    static const unsigned drops[] = { 0, 10, 20, 30 };
    printf("P1  drop%%  integ 지연(ms)  shift 지연(ms)  integ 틀림  shift 틀림   (200회 평균, 지연 = 누름 시작 → PRESS)\n");
    for (unsigned di = 0; di < 4; di++) {
        unsigned bad_i = 0, bad_s = 0; double li = 0, ls = 0;
        for (int it = 0; it < 200; it++) {
            integ_t a = { 0, 0 }; shift_t b = { 0, 0 };
            int pi = -1, ps = -1, ni = 0, ns = 0, ri = 0, rs = 0;
            for (int ms = 0; ms < 600; ms++) {
                unsigned raw = (ms >= 100 && ms < 400) ? ((xs32() % 100u) >= drops[di]) : 0u;
                int ei = integ_step(&a, raw), es = shift_step(&b, raw);
                if (ei == PRESS) { ni++; if (pi < 0) pi = ms; } else if (ei == RELEASE) ri++;
                if (es == PRESS) { ns++; if (ps < 0) ps = ms; } else if (es == RELEASE) rs++;
            }
            if (ni != 1 || ri != 1) bad_i++; else li += pi - 100;
            if (ns != 1 || rs != 1) bad_s++; else ls += ps - 100;
        }
        printf("    %3u     %8.2f       %8.2f       %4u        %4u\n", drops[di],
               200 - bad_i ? li / (200 - bad_i) : 0.0, 200 - bad_s ? ls / (200 - bad_s) : 0.0, bad_i, bad_s);
        if (drops[di] == 0) CHECK(bad_i == 0 && bad_s == 0 && li == 9.0 * 200 && ls == 9.0 * 200);
        if (drops[di] == 20) CHECK(bad_i == 0 && li < ls);       /* 잡음이 있으면 적분기가 더 빨리 확정 */
    }
    /* 짧은 반대 샘플 1개: 시프트는 처음부터 다시, 적분기는 한 칸만 뒤로 */
    integ_t a = { 0, 0 }; shift_t b = { 0, 0 };
    int pi = -1, ps = -1;
    for (int ms = 0; ms < 40; ms++) {
        unsigned raw = (ms != 5);
        if (integ_step(&a, raw) == PRESS && pi < 0) pi = ms;
        if (shift_step(&b, raw) == PRESS && ps < 0) ps = ms;
    }
    CHECK(pi == 11 && ps == 15);
}

/* ===================== P2 — 오토 리피트 ===================== */
#define LONG_MS 800u
#define REP_MS  200u
enum { EV_PRESS = 1, EV_RELEASE, EV_LONG, EV_REPEAT };
typedef struct { uint8_t stable, long_fired; uint32_t press_t, next_rep; } rep_t;
static int rep_ev[64], rep_t_ms[64], rep_n;
static void rep_emit(int e, uint32_t t) { if (rep_n < 64) { rep_ev[rep_n] = e; rep_t_ms[rep_n] = (int)t; rep_n++; } }
/* 1 ms 마다: 확정 레벨(stable_now)을 받아 이벤트를 낸다 */
static void rep_step(rep_t *r, unsigned stable_now, uint32_t now) {
    if (stable_now && !r->stable) { r->stable = 1; r->press_t = now; r->long_fired = 0; rep_emit(EV_PRESS, now); }
    else if (!stable_now && r->stable) { r->stable = 0; rep_emit(EV_RELEASE, now); }
    if (!r->stable) return;
    if (!r->long_fired && now - r->press_t >= LONG_MS) { r->long_fired = 1; r->next_rep = now + REP_MS; rep_emit(EV_LONG, now); }
    else if (r->long_fired && (int32_t)(now - r->next_rep) >= 0) { r->next_rep += REP_MS; rep_emit(EV_REPEAT, now); }
}
static int rep_run(uint32_t hold_ms) {
    rep_t r; memset(&r, 0, sizeof r); rep_n = 0;
    for (uint32_t ms = 0; ms < hold_ms + 300; ms++) rep_step(&r, ms >= 100 && ms < 100 + hold_ms, ms);
    int n = 0; for (int i = 0; i < rep_n; i++) n += rep_ev[i] == EV_REPEAT;
    return n;
}
static void p2(void) {
    CHECK(rep_run(1500) == 3);                                   /* LONG@900, REPEAT@1100,1300,1500 → 뗌 1600 */
    CHECK(rep_n == 6 && rep_ev[1] == EV_LONG && rep_t_ms[1] == 900 && rep_t_ms[2] == 1100 && rep_t_ms[4] == 1500);
    CHECK(rep_run(999) == 0 && rep_n == 3);                      /* LONG 만 */
    CHECK(rep_run(799) == 0 && rep_n == 2);                      /* PRESS, RELEASE */
    CHECK(rep_run(5000) == 20);                                  /* 누름 100..5099: REPEAT 1100, 1300, …, 4900 */
}

/* ===================== P3·P4 공용: EXTI + NVIC 레지스터 mock (BM-B02 mock 축약판) ===================== */
#define EXTI_IMR  0x40013C00u
#define EXTI_PR   0x40013C14u
#define NVIC_ISER1 0xE000E104u                                    /* IRQ 32–63 */
#define NVIC_ICER1 0xE000E184u
#define NVIC_ISPR1 0xE000E204u
#define NVIC_ICPR1 0xE000E284u
#define IRQ_EXTI15_10 40
static uint32_t m_imr, m_pr, m_nv_en1, m_nv_pend1;                /* NVIC 는 32비트 워드 1 (IRQ 32–63) */
static uint32_t m_nacc;
static uint32_t REG_READ(uint32_t a) {
    m_nacc++;
    switch (a) {
    case EXTI_IMR: return m_imr;
    case EXTI_PR: return m_pr;
    case NVIC_ISER1: case NVIC_ICER1: return m_nv_en1;            /* 읽으면 enable 상태 */
    case NVIC_ISPR1: case NVIC_ICPR1: return m_nv_pend1;
    default: return 0;
    }
}
static void REG_WRITE(uint32_t a, uint32_t v) {
    m_nacc++;
    switch (a) {
    case EXTI_IMR: m_imr = v; break;
    case EXTI_PR: m_pr &= ~v; break;                              /* rc_w1 */
    case NVIC_ISER1: m_nv_en1 |= v; break;                        /* 1 쓴 비트만 set, 0 은 무시 */
    case NVIC_ICER1: m_nv_en1 &= ~v; break;
    case NVIC_ISPR1: m_nv_pend1 |= v; break;
    case NVIC_ICPR1: m_nv_pend1 &= ~v; break;
    default: break;
    }
}
static unsigned m_hits[16];
static void on_line(unsigned line) { m_hits[line]++; }

/* P3: EXTI15_10 공유 핸들러 — 펜딩이면서 unmask 된 라인 10–15 를 모두, 비트별로 ack 하고 콜백 */
static void exti15_10_dispatch(void) {
    uint32_t act = REG_READ(EXTI_PR) & REG_READ(EXTI_IMR) & 0xFC00u;
    while (act) {
        unsigned line = (unsigned)__builtin_ctz(act);
        REG_WRITE(EXTI_PR, 1u << line);                           /* 그 비트만 */
        on_line(line);
        act &= act - 1u;
    }
}
static void exti15_10_dispatch_buggy(void) {                       /* 비교용: PR |= + 첫 비트만 */
    uint32_t act = REG_READ(EXTI_PR) & REG_READ(EXTI_IMR) & 0xFC00u;
    if (!act) return;
    unsigned line = (unsigned)__builtin_ctz(act);
    REG_WRITE(EXTI_PR, REG_READ(EXTI_PR) | (1u << line));
    on_line(line);
}
static void p3(void) {
    m_imr = (1u << 10) | (1u << 13); m_pr = (1u << 10) | (1u << 13) | (1u << 12) | (1u << 3);   /* 12 는 마스크됨, 3 은 다른 벡터 */
    memset(m_hits, 0, sizeof m_hits);
    exti15_10_dispatch();
    CHECK(m_hits[10] == 1 && m_hits[13] == 1 && m_hits[12] == 0);
    CHECK(m_pr == ((1u << 12) | (1u << 3)));                      /* 남의 펜딩은 그대로 */
    m_pr = (1u << 10) | (1u << 13) | (1u << 3);
    memset(m_hits, 0, sizeof m_hits);
    exti15_10_dispatch_buggy();
    CHECK(m_hits[10] == 1 && m_hits[13] == 0 && m_pr == 0u);       /* 버그: 13 과 3 의 펜딩까지 사라짐 */
}

/* P4: NVIC disable 로 막았던 EXTI 라인 재허용. 막혀 있던 동안 EXTI PR 과 NVIC 펜딩이 쌓였다. */
static void line_irq_reenable(unsigned line, int irqn) {
    uint32_t nbit = 1u << (irqn & 31);
    REG_WRITE(EXTI_PR, 1u << line);                                /* 1) 주변장치 플래그 */
    REG_WRITE(NVIC_ICPR1, nbit);                                   /* 2) NVIC 펜딩 (irqn >> 5 == 1 → ICPR1) */
    REG_WRITE(NVIC_ISER1, nbit);                                   /* 3) 마지막에 enable */
}
static bool would_enter(int irqn) { uint32_t b = 1u << (irqn & 31); return (m_nv_en1 & b) && (m_nv_pend1 & b); }
static void p4(void) {
    m_imr = 1u << 13; m_pr = 1u << 13; m_nv_en1 = 0; m_nv_pend1 = 1u << (IRQ_EXTI15_10 & 31) | (1u << 3);   /* 다른 IRQ 35 도 펜딩 */
    REG_WRITE(NVIC_ISER1, 1u << (IRQ_EXTI15_10 & 31));             /* 순진하게 enable 만 */
    CHECK(would_enter(IRQ_EXTI15_10));                             /* → 즉시 가짜 진입 */
    m_nv_en1 = 0; m_pr = 1u << 13; m_nv_pend1 = 1u << (IRQ_EXTI15_10 & 31) | (1u << 3);
    m_nacc = 0;
    line_irq_reenable(13, IRQ_EXTI15_10);
    CHECK(!would_enter(IRQ_EXTI15_10) && (m_nv_en1 & (1u << 8)) && m_pr == 0u);
    CHECK(m_nv_pend1 == (1u << 3) && m_nacc == 3u);                /* 다른 IRQ 펜딩은 그대로, 쓰기 3번 */
    CHECK((IRQ_EXTI15_10 >> 5) == 1 && (IRQ_EXTI15_10 & 31) == 8); /* ISER1 bit 8 */
}

int main(void) {
    p1(); p2(); p3(); p4();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
