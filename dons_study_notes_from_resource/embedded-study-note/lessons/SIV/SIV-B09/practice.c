/* SIV-B09 추가 연습 — 한 번에 검증
 *  P1 wrap-안전 시간 비교 time_after()     P2 지수 백오프(exponential backoff) 대기
 *  P3 K회 연속 안정(debounce) 대기 — PLL lock glitch
 *  P4 heartbeat: 카운터가 움직이는지 (클럭 게이트된 블록 검출)
 *  P5 여러 단계가 하나의 전체 마감(deadline budget)을 나눠 쓰기
 * 본문의 bus ops + 가짜 시계를 축약해 넣었다. 여기 mock 은 "시간 기반" 이벤트: (off, at_us, val).
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint32_t (*read32)(void *ctx, uint32_t off);
    uint32_t (*now_us)(void *ctx);
    void     (*delay_us)(void *ctx, uint32_t us);
    void     *ctx;
} bus_ops_t;
typedef enum { WAIT_OK, WAIT_TIMEOUT } wait_status_t;
typedef struct { uint32_t polls, last, elapsed_us; } wres_t;

/* ---------------- mock: 시간 기반 이벤트 + free-running 카운터 ---------------- */
typedef struct { uint32_t off, at_us, val; } ev_t;     /* now >= at_us 가 되면 regs[off] = val (at_us 오름차순) */
typedef struct {
    uint32_t regs[64];
    uint32_t now, read_cost_us, reads;
    const ev_t *ev; size_t nev, next;
    uint32_t counter_off; bool counter_stuck;          /* 카운터: 50us 마다 +1, stuck 이면 고정 */
} sim_t;
static sim_t g;
static uint32_t sim_read32(void *ctx, uint32_t off) {
    sim_t *s = ctx;
    s->reads++;
    s->now += s->read_cost_us;
    while (s->next < s->nev && s->ev[s->next].at_us <= s->now) { s->regs[s->ev[s->next].off / 4u] = s->ev[s->next].val; s->next++; }
    if (s->counter_off && off == s->counter_off) return s->counter_stuck ? 0x1234u : s->now / 50u;
    return s->regs[(off / 4u) % 64u];
}
static uint32_t sim_now(void *ctx) { return ((sim_t *)ctx)->now; }
static void sim_delay(void *ctx, uint32_t us) { ((sim_t *)ctx)->now += us; }
static const bus_ops_t B = { sim_read32, sim_now, sim_delay, &g };
static void sim_reset(const ev_t *ev, size_t n) { memset(&g, 0, sizeof g); g.read_cost_us = 1; g.ev = ev; g.nev = n; }
#define N(a) (sizeof(a) / sizeof((a)[0]))

/* 본문 Part B 의 대기 루프 축약판 (마감 후 한 번 더, 남은 시간으로 delay 자르기) */
static wait_status_t wait_ex(const bus_ops_t *b, uint32_t off, uint32_t mask, uint32_t value,
                             uint32_t timeout_us, uint32_t interval_us, wres_t *r) {
    uint32_t start = b->now_us(b->ctx);
    bool last_look = false;
    *r = (wres_t){ 0, 0, 0 };
    for (;;) {
        uint32_t v = b->read32(b->ctx, off);
        r->polls++; r->last = v; r->elapsed_us = b->now_us(b->ctx) - start;
        if ((v & mask) == value) return WAIT_OK;
        if (last_look) return WAIT_TIMEOUT;
        if (r->elapsed_us >= timeout_us) { last_look = true; continue; }
        uint32_t left = timeout_us - r->elapsed_us;
        if (interval_us) b->delay_us(b->ctx, interval_us < left ? interval_us : left);
    }
}

/* ---------------- P1: a 가 b 보다 "나중" 인가 (2^31 us 이내 차이 가정, wrap 안전) ---------------- */
static bool time_after(uint32_t a, uint32_t b) {
    /* (a - b) mod 2^32 가 [1, 2^31-1] 이면 a 가 나중. Linux 는 (long)((b) - (a)) < 0 로 쓴다 (구현 정의 변환 의존) */
    return (uint32_t)(a - b - 1u) < 0x7FFFFFFFu;
}

/* ---------------- P2: 지수 백오프 ---------------- */
static wait_status_t wait_backoff(const bus_ops_t *b, uint32_t off, uint32_t mask, uint32_t value,
                                  uint32_t timeout_us, uint32_t min_us, uint32_t max_us, wres_t *r) {
    uint32_t start = b->now_us(b->ctx), iv = min_us;
    bool last_look = false;
    *r = (wres_t){ 0, 0, 0 };
    for (;;) {
        uint32_t v = b->read32(b->ctx, off);
        r->polls++; r->last = v; r->elapsed_us = b->now_us(b->ctx) - start;
        if ((v & mask) == value) return WAIT_OK;
        if (last_look) return WAIT_TIMEOUT;
        if (r->elapsed_us >= timeout_us) { last_look = true; continue; }
        uint32_t left = timeout_us - r->elapsed_us;
        b->delay_us(b->ctx, iv < left ? iv : left);
        iv = (iv > max_us / 2u) ? max_us : iv * 2u;           /* 오버플로 없이 2배, max 에서 멈춤 */
    }
}

/* ---------------- P3: K회 연속 일치해야 OK ---------------- */
static wait_status_t wait_stable(const bus_ops_t *b, uint32_t off, uint32_t mask, uint32_t value, uint32_t k,
                                 uint32_t timeout_us, uint32_t interval_us, wres_t *r) {
    uint32_t start = b->now_us(b->ctx), run = 0;
    *r = (wres_t){ 0, 0, 0 };
    for (;;) {
        uint32_t v = b->read32(b->ctx, off);
        r->polls++; r->last = v; r->elapsed_us = b->now_us(b->ctx) - start;
        run = ((v & mask) == value) ? run + 1u : 0u;          /* 한 번이라도 깨지면 처음부터 */
        if (run >= k) return WAIT_OK;
        if (r->elapsed_us >= timeout_us) return WAIT_TIMEOUT; /* 안정 대기는 "마감 후 한 번 더" 로 구제하지 않는다 */
        if (interval_us) b->delay_us(b->ctx, interval_us);
    }
}

/* ---------------- P4: heartbeat — 값이 바뀌기만 하면 OK ---------------- */
static wait_status_t wait_counter_advances(const bus_ops_t *b, uint32_t off, uint32_t timeout_us,
                                           uint32_t interval_us, wres_t *r) {
    uint32_t start = b->now_us(b->ctx);
    uint32_t first = b->read32(b->ctx, off);
    *r = (wres_t){ 1, first, 0 };
    for (;;) {
        uint32_t v = b->read32(b->ctx, off);
        r->polls++; r->last = v; r->elapsed_us = b->now_us(b->ctx) - start;
        if (v != first) return WAIT_OK;
        if (r->elapsed_us >= timeout_us) return WAIT_TIMEOUT;
        if (interval_us) b->delay_us(b->ctx, interval_us);
    }
}

/* ---------------- P5: 전체 마감 budget ---------------- */
typedef struct { uint32_t start, budget_us; } deadline_t;
static deadline_t deadline_start(const bus_ops_t *b, uint32_t budget_us) {
    return (deadline_t){ b->now_us(b->ctx), budget_us };
}
static uint32_t deadline_left(const bus_ops_t *b, const deadline_t *d) {
    uint32_t used = b->now_us(b->ctx) - d->start;
    return used >= d->budget_us ? 0u : d->budget_us - used;
}

/* ---------------- 테스트 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

#define ST 0x04u
#define READY 1u

int main(void) {
    wres_t r, r2;

    /* P1 */
    CHECK(time_after(5u, 0xFFFFFFF0u) == true);      /* wrap 을 넘어선 "나중" */
    CHECK(time_after(0xFFFFFFF0u, 5u) == false);
    CHECK(time_after(100u, 100u) == false);
    CHECK(time_after(101u, 100u) == true);
    CHECK((5u > 0xFFFFFFF0u) == false);               /* 순진한 비교는 wrap 에서 틀린다 */

    /* P2: 절대 안 되는 장치 — 고정 10us vs 백오프 10→1000us */
    sim_reset(NULL, 0);
    CHECK(wait_ex(&B, ST, READY, READY, 10000, 10, &r) == WAIT_TIMEOUT);
    sim_reset(NULL, 0);
    CHECK(wait_backoff(&B, ST, READY, READY, 10000, 10, 1000, &r2) == WAIT_TIMEOUT);
    CHECK(r2.polls == 18 && r.polls > 10u * r2.polls);  /* 버스 트래픽 1/50 */
    printf("P2 fixed10: polls=%u  backoff: polls=%u\n", (unsigned)r.polls, (unsigned)r2.polls);
    /* 대가: 반응 지연. t=700 에 READY → 고정은 바로, 백오프는 다음 깨어날 때(t=1278) */
    static const ev_t late[] = { { ST, 700, READY } };
    sim_reset(late, N(late));
    CHECK(wait_ex(&B, ST, READY, READY, 10000, 10, &r) == WAIT_OK && r.elapsed_us - 700u <= 11u);
    sim_reset(late, N(late));
    CHECK(wait_backoff(&B, ST, READY, READY, 10000, 10, 1000, &r2) == WAIT_OK && r2.elapsed_us == 1278);

    /* P3: PLL lock 이 t=20 에 섰다가 t=40 에 풀리고 t=60 부터 안정 */
    static const ev_t pll[] = { { ST, 20, READY }, { ST, 40, 0 }, { ST, 60, READY } };
    sim_reset(pll, N(pll));
    CHECK(wait_ex(&B, ST, READY, READY, 1000, 9, &r) == WAIT_OK && r.polls == 3);      /* 거짓 lock 에 속음 */
    sim_reset(pll, N(pll));
    CHECK(wait_stable(&B, ST, READY, READY, 3, 1000, 9, &r) == WAIT_OK && r.polls == 9 && r.elapsed_us == 81);
    static const ev_t flap[] = { { ST, 20, READY }, { ST, 40, 0 }, { ST, 60, READY }, { ST, 80, 0 },
                                 { ST, 100, READY }, { ST, 120, 0 } };          /* 계속 흔들림 */
    sim_reset(flap, N(flap));
    CHECK(wait_stable(&B, ST, READY, READY, 3, 1000, 9, &r) == WAIT_TIMEOUT && r.last == 0);

    /* P4: 카운터 heartbeat */
    sim_reset(NULL, 0); g.counter_off = 0x10u;
    CHECK(wait_counter_advances(&B, 0x10u, 1000, 10, &r) == WAIT_OK && r.polls <= 7);
    sim_reset(NULL, 0); g.counter_off = 0x10u; g.counter_stuck = true;     /* 클럭 게이트 → 카운터 멈춤 */
    CHECK(wait_counter_advances(&B, 0x10u, 1000, 10, &r) == WAIT_TIMEOUT && r.last == 0x1234u);

    /* P5: PLL(0x00) t=300, RESET_DONE(0x08) t=800, LINK(0x0C) t=5000 — 전체 budget 2000us */
    static const ev_t seq[] = { { 0x00, 300, 1 }, { 0x08, 800, 1 }, { 0x0C, 5000, 1 } };
    sim_reset(seq, N(seq));
    deadline_t dl = deadline_start(&B, 2000);
    CHECK(wait_ex(&B, 0x00, 1, 1, deadline_left(&B, &dl), 50, &r) == WAIT_OK);
    CHECK(wait_ex(&B, 0x08, 1, 1, deadline_left(&B, &dl), 50, &r) == WAIT_OK);
    uint32_t left = deadline_left(&B, &dl);
    CHECK(left > 1000u && left < 1300u);                 /* 앞 단계가 쓴 만큼 줄었다 */
    CHECK(wait_ex(&B, 0x0C, 1, 1, left, 50, &r) == WAIT_TIMEOUT);
    CHECK(g.now - dl.start <= 2000u + 2u && deadline_left(&B, &dl) == 0u);   /* 전체가 2 ms 안에 끝남 */
    printf("P5 left_for_link=%uus total=%uus\n", (unsigned)left, (unsigned)(g.now - dl.start));

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
