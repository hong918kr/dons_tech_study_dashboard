/* BM-M01 추가 연습 — 한 번에 검증
 *
 * 연습 1  nvic_probe(): ISER 에 1 을 모두 써 보고 읽어 구현된 IRQ 수를, IPR 바이트에 0xFF 를 써 보고 읽어
 *         우선순위 비트 수를 알아낸다. 원래 상태를 정확히 되돌린다.
 * 연습 2  nvic_quiesce(): 부트로더 → 앱 점프 전 정리. 모두 disable + pending 지우기, 그래도 남는 pending
 *         (소스 라인이 아직 high) 을 돌려준다.
 * 연습 3  prio_byte_addr(): CMSIS 식 바이트 주소 (IPR0 + n, SHPR[(n & 0xF) − 4]) 가 워드+레인 계산과 같은지 전수.
 * 연습 4  clear_first_min_work(): "먼저 지우기" 만으로 재진입을 막으려면 지운 뒤 남은 일이 몇 사이클 이상이어야
 *         하나 — 공식과 시뮬레이션 비교.
 *
 * 모델은 solution.c 의 축약판 (BM-B02 mock 축약판 + NVIC 모델 + posted write + TIM2) — 연습 1 을 위해
 * ISER/ICER 는 16 워드 전부(구현 밖은 RAZ/WI), 우선순위 비트 수는 변수.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/BM-M01_pr && /tmp/BM-M01_pr
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define NVIC_ISER0  0xE000E100u
#define NVIC_ICER0  0xE000E180u
#define NVIC_ISPR0  0xE000E200u
#define NVIC_ICPR0  0xE000E280u
#define NVIC_IPR0   0xE000E400u
#define SCB_SHPR1   0xE000ED18u
#define N_WMAX      16              /* ARMv7-M: ISER0..15 → 최대 496 IRQ */
#define N_IPRW      60              /* IPR0..59 (496 / 4 = 124 워드까지 가능하지만 모델은 240 IRQ 까지) */
#define TIM2_SR     0x40000010u
#define TIM2_DIER   0x4000000Cu
#define TIM2_IRQn   28

/* ---------------- 축약 mock: 표 + 훅 + 시간 + posted write ---------------- */
typedef uint32_t (*rd_fn)(uint32_t a);
typedef void (*wr_fn)(uint32_t a, uint32_t v);
static struct { uint32_t base, n; rd_fn r; wr_fn w; } g_map[12];
static int g_nmap, g_unexpected;
static uint64_t g_cycle;
static uint32_t g_lat;                                   /* 주변장치 쓰기 도착 지연 (코어 + 브리지) */
static struct { uint32_t a, v; uint64_t t; } g_post[8];
static int g_npost;
static void tick(void);
static void map_add(uint32_t base, uint32_t nwords, rd_fn r, wr_fn w) { g_map[g_nmap].base = base; g_map[g_nmap].n = nwords; g_map[g_nmap].r = r; g_map[g_nmap].w = w; g_nmap++; }
static int map_find(uint32_t a) {
    for (int i = 0; i < g_nmap; i++) if (a >= g_map[i].base && a < g_map[i].base + 4u * g_map[i].n) return i;
    return -1;
}
static void apply(uint32_t a, uint32_t v) { int i = map_find(a); if (i < 0) g_unexpected++; else g_map[i].w(a, v); }
static void clock_advance(uint32_t n) {
    while (n--) {
        g_cycle++;
        while (g_npost > 0 && g_post[0].t <= g_cycle) {
            uint32_t a = g_post[0].a, v = g_post[0].v;
            memmove(&g_post[0], &g_post[1], (size_t)(g_npost - 1) * sizeof g_post[0]); g_npost--;
            apply(a, v);
        }
        tick();
    }
}
static bool periph(uint32_t a) { return a >= 0x40000000u && a < 0x60000000u; }
static uint32_t REG_READ(uint32_t a) {
    if (periph(a)) while (g_npost > 0) clock_advance(1);
    int i = map_find(a); uint32_t v = 0;
    if (i < 0) g_unexpected++; else v = g_map[i].r(a);
    clock_advance(1);
    return v;
}
static void REG_WRITE(uint32_t a, uint32_t v) {
    if (periph(a)) { g_post[g_npost].a = a; g_post[g_npost].v = v; g_post[g_npost].t = g_cycle + 1u + g_lat; g_npost++; }
    else apply(a, v);
    clock_advance(1);
}

/* ---------------- NVIC 모델 (구현 IRQ 수, 우선순위 비트 수가 변수) ---------------- */
static struct { uint32_t en[N_WMAX], pend[N_WMAX], act[N_WMAX], line[N_WMAX]; uint8_t ipr[4 * N_IPRW]; int n_impl; uint8_t pmask; } g_nv;
static uint32_t impl_mask(uint32_t w) {
    int lo = (int)w * 32;
    if (g_nv.n_impl >= lo + 32) return 0xFFFFFFFFu;
    if (g_nv.n_impl <= lo) return 0u;
    return (1u << (g_nv.n_impl - lo)) - 1u;
}
static uint32_t WORD(uint32_t a) { return (a & 0x7Fu) >> 2; }
static void relatch(uint32_t w) { g_nv.pend[w] |= g_nv.line[w] & ~g_nv.act[w] & impl_mask(w); }
static void line_set(int irq, bool hi) {
    uint32_t w = (uint32_t)irq >> 5, b = 1u << (irq & 31);
    if (hi) g_nv.line[w] |= b; else g_nv.line[w] &= ~b;
    relatch(w);
}
static uint32_t en_r(uint32_t a)            { return g_nv.en[WORD(a)]; }
static void     iser_w(uint32_t a, uint32_t v) { g_nv.en[WORD(a)] |= v & impl_mask(WORD(a)); }
static void     icer_w(uint32_t a, uint32_t v) { g_nv.en[WORD(a)] &= ~v; }
static uint32_t pend_r(uint32_t a)          { return g_nv.pend[WORD(a)]; }
static void     ispr_w(uint32_t a, uint32_t v) { g_nv.pend[WORD(a)] |= v & impl_mask(WORD(a)); }
static void     icpr_w(uint32_t a, uint32_t v) { g_nv.pend[WORD(a)] &= ~v; relatch(WORD(a)); }
static uint32_t ipr_r(uint32_t a) {
    uint32_t w = (a - NVIC_IPR0) >> 2, v = 0;
    for (uint32_t l = 0; l < 4u; l++) v |= (uint32_t)g_nv.ipr[4u * w + l] << (8u * l);
    return v;
}
static void ipr_w(uint32_t a, uint32_t v) {
    uint32_t w = (a - NVIC_IPR0) >> 2;
    for (uint32_t l = 0; l < 4u; l++)
        if ((int)(4u * w + l) < g_nv.n_impl) g_nv.ipr[4u * w + l] = (uint8_t)((v >> (8u * l)) & g_nv.pmask);
}
/* TIM2 (rc_w0 SR, UIE) */
static struct { uint32_t sr, dier, period, phase, updates; } g_tim;
static void tim_line(void) { line_set(TIM2_IRQn, (g_tim.sr & 1u) && (g_tim.dier & 1u)); }
static uint32_t sr_r(uint32_t a)   { return a == TIM2_SR ? g_tim.sr : g_tim.dier; }
static void sr_w(uint32_t a, uint32_t v) { if (a == TIM2_SR) g_tim.sr &= v; else g_tim.dier = v & 1u; tim_line(); }
static void tick(void) {
    if (g_tim.period && g_cycle % g_tim.period == g_tim.phase) { g_tim.sr |= 1u; g_tim.updates++; tim_line(); }
}
static void board(int n_impl, int prio_bits) {
    memset(&g_nv, 0, sizeof g_nv); memset(&g_tim, 0, sizeof g_tim);
    g_nv.n_impl = n_impl; g_nv.pmask = (uint8_t)((0xFFu << (8 - prio_bits)) & 0xFFu);
    g_nmap = 0; g_unexpected = 0; g_cycle = 0; g_lat = 0; g_npost = 0;
    map_add(NVIC_ISER0, N_WMAX, en_r, iser_w);   map_add(NVIC_ICER0, N_WMAX, en_r, icer_w);
    map_add(NVIC_ISPR0, N_WMAX, pend_r, ispr_w); map_add(NVIC_ICPR0, N_WMAX, pend_r, icpr_w);
    map_add(NVIC_IPR0, N_IPRW, ipr_r, ipr_w);
    map_add(0x4000000Cu, 2u, sr_r, sr_w);        /* TIM2 DIER, SR */
}
/* 단일 레벨 디스패처 (solution.c 의 NVIC-lite 와 같은 규칙, 우선순위 비교 생략 — IRQ 하나) */
static void (*g_isr)(void);
static uint32_t g_calls;
static void sim_step(void) {
    clock_advance(1);
    for (int n = 0; n < 16; n++) {
        uint32_t w = TIM2_IRQn >> 5, b = 1u << (TIM2_IRQn & 31);
        if (!(g_nv.en[w] & g_nv.pend[w] & b)) return;
        g_nv.pend[w] &= ~b; g_nv.act[w] |= b; g_calls++;
        g_isr();
        g_nv.act[w] &= ~b; relatch(w);
    }
}

/* ================= 연습 1: 구현된 IRQ 수와 우선순위 비트 수 알아내기 ================= */
static int popcount32(uint32_t x) { int c = 0; while (x) { x &= x - 1u; c++; } return c; }
static void nvic_probe(int *n_irq, int *prio_bits) {
    int n = 0;
    for (uint32_t w = 0; w < N_WMAX; w++) {                     /* 타깃: PRIMASK=1 상태에서 할 것 */
        uint32_t a = NVIC_ISER0 + 4u * w;
        uint32_t before = REG_READ(a);
        REG_WRITE(a, 0xFFFFFFFFu);
        uint32_t got = REG_READ(a);                             /* 구현 안 된 비트는 0 으로 읽힌다 */
        REG_WRITE(NVIC_ICER0 + 4u * w, got & ~before);          /* 내가 켠 것만 끈다 */
        n += popcount32(got);
    }
    uint32_t save = REG_READ(NVIC_IPR0);
    REG_WRITE(NVIC_IPR0, (save & ~0xFFu) | 0xFFu);
    uint32_t got = REG_READ(NVIC_IPR0) & 0xFFu;                 /* 0xF0 → 4비트 */
    REG_WRITE(NVIC_IPR0, save);
    *n_irq = n; *prio_bits = popcount32(got);
}

/* ================= 연습 2: 앱 점프 전 NVIC 정리 ================= */
static int nvic_quiesce(uint32_t still[N_WMAX]) {             /* 남은 pending IRQ 수 */
    int left = 0;
    for (uint32_t w = 0; w < N_WMAX; w++) REG_WRITE(NVIC_ICER0 + 4u * w, 0xFFFFFFFFu);
    for (uint32_t w = 0; w < N_WMAX; w++) REG_WRITE(NVIC_ICPR0 + 4u * w, 0xFFFFFFFFu);
    for (uint32_t w = 0; w < N_WMAX; w++) { still[w] = REG_READ(NVIC_ISPR0 + 4u * w); left += popcount32(still[w]); }
    return left;
}

/* ================= 연습 3: CMSIS 식 바이트 주소 ================= */
static int prio_loc(int irqn, uint32_t *wa, uint32_t *sh) {   /* solution.c 와 같은 워드+레인 계산 */
    if (irqn >= 0 && irqn < 96) { *wa = NVIC_IPR0 + 4u * ((uint32_t)irqn >> 2); *sh = 8u * ((uint32_t)irqn & 3u); return 0; }
    if (!(irqn == -12 || irqn == -11 || irqn == -10 || irqn == -5 || irqn == -4 || irqn == -2 || irqn == -1)) return -1;
    uint32_t exc = (uint32_t)(16 + irqn);
    *wa = SCB_SHPR1 + 4u * ((exc - 4u) >> 2); *sh = 8u * (exc & 3u);
    return 0;
}
static int prio_byte_addr(int irqn, uint32_t *addr) {        /* CMSIS: NVIC->IP[n] / SCB->SHP[(n & 0xF) - 4] */
    if (irqn >= 0 && irqn < 96) { *addr = NVIC_IPR0 + (uint32_t)irqn; return 0; }
    uint32_t idx = ((uint32_t)irqn & 0xFu) - 4u;              /* HardFault(−13): 3 − 4 → 0xFFFFFFFF ! */
    if (irqn < -12 || idx > 11u) return -1;
    if (!(irqn == -12 || irqn == -11 || irqn == -10 || irqn == -5 || irqn == -4 || irqn == -2 || irqn == -1)) return -1;
    *addr = SCB_SHPR1 + idx;
    return 0;
}

/* ================= 연습 4: "먼저 지우기" 에 필요한 최소 남은 일 ================= */
static uint32_t g_work, g_spurious, g_ticks;
static void isr_clear_first(void) {
    if (REG_READ(TIM2_SR) & 1u) { REG_WRITE(TIM2_SR, ~1u); g_ticks++; clock_advance(g_work); }
    else g_spurious++;
}
static uint32_t run_clear_first(uint32_t lat, uint32_t work) {
    board(96, 4);
    g_tim.period = 100u; g_tim.phase = 50u; g_tim.dier = 1u; g_nv.en[0] = 1u << TIM2_IRQn;
    g_lat = lat; g_work = work; g_spurious = g_ticks = 0; g_isr = isr_clear_first; g_calls = 0;
    while (g_cycle < 5000u) sim_step();
    return g_spurious;
}
static uint32_t clear_first_min_work(uint32_t lat) { return lat; }   /* 쓰기 후 1 사이클 + lat 에 도착, 복귀는 1 + W 뒤 */

/* ================= 하네스 ================= */
static int g_pass, g_total;
#define CHECK(e) do { g_total++; if (e) g_pass++; else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #e); } while (0)
static uint32_t g_rng = 0x1234567u;
static uint32_t xs32(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }

int main(void) {
    /* 연습 1 */
    static const int cfg[][2] = { {82, 4}, {91, 4}, {96, 4}, {32, 3}, {240, 8}, {1, 2} };
    for (unsigned k = 0; k < sizeof cfg / sizeof cfg[0]; k++) {
        board(cfg[k][0], cfg[k][1]);
        for (uint32_t w = 0; w < N_WMAX; w++) g_nv.en[w] = xs32() & impl_mask(w);
        for (int i = 0; i < cfg[k][0] && i < 4 * N_IPRW; i++) g_nv.ipr[i] = (uint8_t)(xs32() & g_nv.pmask);
        uint32_t en0[N_WMAX]; uint8_t ipr0[4 * N_IPRW];
        memcpy(en0, g_nv.en, sizeof en0); memcpy(ipr0, g_nv.ipr, sizeof ipr0);
        int n = -1, pb = -1;
        nvic_probe(&n, &pb);
        printf("probe: model %3d IRQs / %d prio bits -> found %3d / %d\n", cfg[k][0], cfg[k][1], n, pb);
        CHECK(n == cfg[k][0] && pb == cfg[k][1]);
        CHECK(memcmp(en0, g_nv.en, sizeof en0) == 0 && memcmp(ipr0, g_nv.ipr, sizeof ipr0) == 0 && g_unexpected == 0);
    }

    /* 연습 2 */
    board(96, 4);
    g_nv.en[0] = 0xFFFF0000u; g_nv.en[1] = 0x00FF00FFu; g_nv.pend[2] = 0x8001u;
    g_tim.sr = 1u; g_tim.dier = 1u; tim_line();                       /* TIM2 소스가 아직 high */
    uint32_t still[N_WMAX];
    int left = nvic_quiesce(still);
    CHECK(left == 1 && still[0] == (1u << TIM2_IRQn) && g_nv.en[0] == 0u && g_nv.en[1] == 0u && g_nv.pend[2] == 0u);
    REG_WRITE(TIM2_DIER, 0u); (void)REG_READ(TIM2_DIER);              /* 소스부터 끄고 (도착 확인) */
    left = nvic_quiesce(still);
    CHECK(left == 0 && g_unexpected == 0);

    /* 연습 3 */
    uint32_t bad = 0, cnt = 0;
    for (int irqn = -16; irqn < 96; irqn++) {
        uint32_t wa, sh, ba;
        int r1 = prio_loc(irqn, &wa, &sh), r2 = prio_byte_addr(irqn, &ba);
        if (r1 != r2) bad++;
        else if (r1 == 0) { cnt++; if (ba != wa + sh / 8u) bad++; }
    }
    uint32_t ba = 0;
    CHECK(bad == 0u && cnt == 103u);
    CHECK(prio_byte_addr(-1, &ba) == 0 && ba == 0xE000ED23u && prio_byte_addr(-13, &ba) == -1);
    printf("cmsis byte addr: %u valid IRQn checked, SysTick -> 0x%08X, HardFault idx=(-13 & 0xF)-4 = 0x%08X\n",
           (unsigned)cnt, (unsigned)0xE000ED23u, (unsigned)((uint32_t)(-13 & 0xF) - 4u));

    /* 연습 4 */
    bool ok4 = true;
    for (uint32_t lat = 0; lat <= 8; lat++) {
        uint32_t wmin = 0;
        while (wmin < 20u && run_clear_first(lat, wmin) != 0u) wmin++;
        printf("lat=%u: smallest work after clear with 0 spurious = %u (formula %u), spurious at W-1 = %u\n",
               (unsigned)lat, (unsigned)wmin, (unsigned)clear_first_min_work(lat),
               (unsigned)(wmin ? run_clear_first(lat, wmin - 1u) : 0u));
        if (wmin != clear_first_min_work(lat)) ok4 = false;
    }
    CHECK(ok4);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
