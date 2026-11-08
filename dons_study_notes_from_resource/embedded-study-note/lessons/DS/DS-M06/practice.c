/* DS-M06 추가 연습 — 한 번에 검증
 * 1) rounds 없는 절대 시각 비교 휠  2) 점유 비트맵 + tw_advance(n) (tickless 따라잡기)
 * 3) 슬롯 250개(2의 거듭제곱 아님)에서 32비트 랩어라운드  4) 단일 패스 tick 의 버그 재현
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct tw_link { struct tw_link *next, *prev; } tw_link_t;
static void link_init(tw_link_t *h) { h->next = h->prev = h; }
static bool link_empty(const tw_link_t *h) { return h->next == h; }
static void link_add_tail(tw_link_t *h, tw_link_t *n) { n->prev = h->prev; n->next = h; h->prev->next = n; h->prev = n; }
static void link_del(tw_link_t *n) { n->prev->next = n->next; n->next->prev = n->prev; n->next = n->prev = n; }
#define container_of(p, T, m) ((T *)(void *)((char *)(p) - offsetof(T, m)))

static uint32_t rng = 1u;
static uint32_t xorshift32(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* ========== 연습 1: rounds 없이 expire == now 로 판정하는 휠 ========== */
#define S1 256u
typedef struct { tw_link_t link; uint32_t expire; bool armed; } t1_t;
typedef struct { tw_link_t slot[S1]; uint32_t now; uint64_t visits, writes; } w1_t;
#define N1 3000u
static w1_t  w1;
static t1_t  tm1[N1];
static uint32_t want1[N1], got1[N1], cnt1[N1];

static void w1_add(t1_t *t, uint32_t delay) {
    if (delay == 0) delay = 1;
    if (t->armed) link_del(&t->link);
    t->expire = w1.now + delay; t->armed = true;
    link_add_tail(&w1.slot[t->expire & (S1 - 1u)], &t->link);
}
static void w1_tick(void) {
    tw_link_t due; link_init(&due);
    w1.now++;
    tw_link_t *head = &w1.slot[w1.now & (S1 - 1u)];
    for (tw_link_t *p = head->next, *nx; p != head; p = nx) {
        nx = p->next; w1.visits++;
        t1_t *t = container_of(p, t1_t, link);
        if (t->expire == w1.now) { link_del(p); link_add_tail(&due, p); }   /* 같음 비교 → 랩어라운드 무관 */
        /* else: 아무것도 안 쓴다 — 나중 바퀴의 타이머는 읽기만 */
    }
    while (!link_empty(&due)) {
        t1_t *t = container_of(due.next, t1_t, link);
        link_del(&t->link); t->armed = false;
        uint32_t id = (uint32_t)(t - tm1);
        got1[id] = w1.now; cnt1[id]++;
    }
}

/* ========== 연습 2: 점유 비트맵 + tw_advance(n) ========== */
#define S2 256u
typedef struct t2 { tw_link_t link; uint32_t expire, rounds; uint16_t id; bool armed; } t2_t;
typedef struct { tw_link_t slot[S2]; uint32_t occ[S2 / 32u]; uint32_t now, slot_visits; } w2_t;
#define N2 1024u
typedef struct { uint32_t tick; uint16_t id; } ev2_t;
#define LOG2 (1u << 16)
static w2_t  wa, wb;
static t2_t  ta[N2], tb[N2];
static ev2_t loga[LOG2], logb[LOG2];
static uint32_t loga_n, logb_n;
static bool g_rearm2 = true;                                    /* 콜백 재등록 규칙 on/off */

static void occ_update(w2_t *w, uint32_t s) {
    if (link_empty(&w->slot[s])) w->occ[s >> 5] &= ~(1u << (s & 31u));
    else                         w->occ[s >> 5] |=  (1u << (s & 31u));
}
static void w2_init(w2_t *w, t2_t *tm, uint32_t start) {
    for (uint32_t i = 0; i < S2; i++) link_init(&w->slot[i]);
    memset(w->occ, 0, sizeof w->occ);
    w->now = start; w->slot_visits = 0;
    for (uint32_t i = 0; i < N2; i++) { link_init(&tm[i].link); tm[i].armed = false; tm[i].id = (uint16_t)i; }
}
static void w2_add(w2_t *w, t2_t *t, uint32_t delay) {
    if (delay == 0) delay = 1;
    if (t->armed) { link_del(&t->link); occ_update(w, t->expire & (S2 - 1u)); }
    t->expire = w->now + delay; t->rounds = (delay - 1u) >> 8; t->armed = true;
    uint32_t s = t->expire & (S2 - 1u);
    link_add_tail(&w->slot[s], &t->link); occ_update(w, s);
}
static void w2_tick(w2_t *w, t2_t *tm, ev2_t *log, uint32_t *log_n) {
    tw_link_t due; link_init(&due);
    w->now++; w->slot_visits++;
    uint32_t s = w->now & (S2 - 1u);
    tw_link_t *head = &w->slot[s];
    for (tw_link_t *p = head->next, *nx; p != head; p = nx) {
        nx = p->next;
        t2_t *t = container_of(p, t2_t, link);
        if (t->rounds == 0) { link_del(p); link_add_tail(&due, p); } else t->rounds--;
    }
    occ_update(w, s);
    while (!link_empty(&due)) {
        t2_t *t = container_of(due.next, t2_t, link);
        link_del(&t->link); t->armed = false;
        if (*log_n < LOG2) log[(*log_n)++] = (ev2_t){ w->now, t->id };
        if (g_rearm2 && ((t->id * 2654435761u) ^ w->now) % 3u == 0)                 /* 결정적 규칙으로 일부 재등록 */
            w2_add(w, &tm[t->id], 1u + (t->id * 7u + w->now) % 700u);
    }
}
/* now 다음의 비어 있지 않은 슬롯까지 거리 1..256, 없으면 0 */
static uint32_t w2_next_nonempty(const w2_t *w) {
    uint32_t start = (w->now + 1u) & (S2 - 1u);
    for (uint32_t k = 0; k <= S2 / 32u; k++) {                  /* 시작 워드부터 한 바퀴 + 1 워드 */
        uint32_t wi = ((start >> 5) + k) % (S2 / 32u);
        uint32_t bits = w->occ[wi];
        if (k == 0) bits &= ~0u << (start & 31u);               /* 시작 비트 앞은 버린다 */
        if (k == S2 / 32u) bits &= (start & 31u) ? ~(~0u << (start & 31u)) : 0u;   /* 시작 워드의 앞부분 */
        if (bits) {
            uint32_t s = wi * 32u + (uint32_t)__builtin_ctz(bits);   /* bits != 0 이므로 ctz 정의됨 */
            uint32_t d = (s - w->now) & (S2 - 1u);
            return d ? d : S2;
        }
    }
    return 0;
}
/* n tick 을 건너뛰되 결과는 w2_tick n 번과 같아야 한다. 빈 슬롯은 줄어들 rounds 도 없다 */
static void w2_advance(w2_t *w, t2_t *tm, uint32_t n, ev2_t *log, uint32_t *log_n) {
    while (n) {
        uint32_t k = w2_next_nonempty(w);
        if (k == 0 || k > n) { w->now += n; return; }
        w->now += k - 1u;
        w2_tick(w, tm, log, log_n);
        n -= k;
    }
}

/* ========== 연습 3: 슬롯 250개 — 절대값 % vs 상대 커서 ========== */
#define S3 250u
#define N3 600u
typedef struct { tw_link_t link; uint32_t rounds, want; uint16_t id; } t3_t;
typedef struct { tw_link_t slot[S3]; uint32_t now, cur; } w3_t;
static w3_t w3;
static t3_t tm3[N3];
static uint32_t got3[N3], cnt3[N3];

/* abs=true : slot = expire % 250, 방문 슬롯 = now % 250 (2^32 이 250 의 배수가 아니라 넘침에서 끊김)
 * abs=false: 커서 cur 를 따로 돌린다. slot = (cur + delay) % 250 — 넘침과 무관 */
static uint32_t run3(bool abs) {
    const uint32_t start = 0xFFFFFF00u;
    for (uint32_t i = 0; i < S3; i++) link_init(&w3.slot[i]);
    w3.now = start; w3.cur = 0;
    for (uint32_t i = 0; i < N3; i++) {
        uint32_t d = i + 1u;                                    /* delay 1..600 */
        t3_t *t = &tm3[i];
        t->id = (uint16_t)i; t->want = start + d; t->rounds = (d - 1u) / S3;
        uint32_t s = abs ? (start + d) % S3 : (w3.cur + d) % S3;
        link_add_tail(&w3.slot[s], &t->link);
        got3[i] = 0; cnt3[i] = 0;
    }
    for (uint32_t k = 0; k < 700u; k++) {
        w3.now++; w3.cur = (w3.cur + 1u) % S3;
        tw_link_t *head = &w3.slot[abs ? w3.now % S3 : w3.cur];
        for (tw_link_t *p = head->next, *nx; p != head; p = nx) {
            nx = p->next;
            t3_t *t = container_of(p, t3_t, link);
            if (t->rounds == 0) { link_del(p); got3[t->id] = w3.now; cnt3[t->id]++; } else t->rounds--;
        }
    }
    uint32_t bad = 0;
    for (uint32_t i = 0; i < N3; i++) bad += cnt3[i] != 1u || got3[i] != tm3[i].want;
    return bad;
}

/* ========== 연습 4: 단일 패스 tick (콜백을 순회 중에 바로 부름) vs 2단계 tick ========== */
typedef struct t4 t4_t;
typedef struct { tw_link_t slot[256]; uint32_t now; } w4_t;
struct t4 { tw_link_t link; uint32_t expire, rounds; void (*cb)(w4_t *, t4_t *); uint32_t fired, last; bool armed; };
static w4_t w4;
static t4_t tm4[4];
static uint32_t g_fires_this_tick;

static void w4_init(void) {
    for (uint32_t i = 0; i < 256u; i++) link_init(&w4.slot[i]);
    w4.now = 0;
    for (uint32_t i = 0; i < 4u; i++) { memset(&tm4[i], 0, sizeof tm4[i]); link_init(&tm4[i].link); }
}
static void w4_add(t4_t *t, uint32_t delay) {
    if (delay == 0) delay = 1;
    if (t->armed) link_del(&t->link);
    t->expire = w4.now + delay; t->rounds = (delay - 1u) >> 8; t->armed = true;
    link_add_tail(&w4.slot[t->expire & 255u], &t->link);
}
static bool w4_cancel(t4_t *t) { if (!t->armed) return false; link_del(&t->link); t->armed = false; return true; }
static void w4_fire(t4_t *t) { t->armed = false; t->fired++; t->last = w4.now; g_fires_this_tick++; if (t->cb) t->cb(&w4, t); }

static void naive_tick(void) {                                  /* 버그 버전: next 만 저장해 두고 바로 콜백 */
    w4.now++; g_fires_this_tick = 0;
    tw_link_t *head = &w4.slot[w4.now & 255u];
    uint32_t guard = 0;
    for (tw_link_t *p = head->next, *nx; p != head && guard < 50u; p = nx, guard++) {
        nx = p->next;
        t4_t *t = container_of(p, t4_t, link);
        if (t->rounds == 0) { link_del(p); w4_fire(t); } else t->rounds--;
    }
}
static void twophase_tick(void) {
    tw_link_t due; link_init(&due);
    w4.now++; g_fires_this_tick = 0;
    tw_link_t *head = &w4.slot[w4.now & 255u];
    for (tw_link_t *p = head->next, *nx; p != head; p = nx) {
        nx = p->next;
        t4_t *t = container_of(p, t4_t, link);
        if (t->rounds == 0) { link_del(p); link_add_tail(&due, p); } else t->rounds--;
    }
    while (!link_empty(&due)) { t4_t *t = container_of(due.next, t4_t, link); link_del(&t->link); w4_fire(t); }
}
static void rearm256_cb(w4_t *w, t4_t *t) { (void)w; w4_add(t, 256); }
static void cancel_next_cb(w4_t *w, t4_t *t) { (void)w; (void)t; w4_cancel(&tm4[1]); }

typedef struct { uint32_t max_per_tick, b_fired; } r4_t;
static r4_t run4(void (*tick)(void)) {
    r4_t r = { 0, 0 };
    w4_init();                                                  /* (a) 콜백이 delay 256 으로 자기 재등록 */
    tm4[0].cb = rearm256_cb; w4_add(&tm4[0], 10);
    w4_add(&tm4[2], 10u + 3u * 256u);                           /* 같은 슬롯, rounds 3 — A 뒤에 있는 이웃 */
    for (uint32_t i = 0; i < 600u; i++) { tick(); if (g_fires_this_tick > r.max_per_tick) r.max_per_tick = g_fires_this_tick; }
    w4_init();                                                  /* (b) A 의 콜백이 같은 tick 의 B 를 취소 */
    tm4[0].cb = cancel_next_cb; w4_add(&tm4[0], 5); w4_add(&tm4[1], 5);
    for (uint32_t i = 0; i < 10u; i++) tick();
    r.b_fired = tm4[1].fired;
    return r;
}

int main(void) {
    /* 연습 1 */
    {
        static const uint32_t dl[] = { 0, 1, 255, 256, 257, 512, 513 };
        for (uint32_t i = 0; i < S1; i++) link_init(&w1.slot[i]);
        w1.now = 0xFFFFFFF0u; w1.visits = 0;
        memset(tm1, 0, sizeof tm1); memset(cnt1, 0, sizeof cnt1);
        for (uint32_t i = 0; i < N1; i++) link_init(&tm1[i].link);
        for (uint32_t i = 0; i < 7u; i++) { w1_add(&tm1[i], dl[i]); want1[i] = w1.now + (dl[i] ? dl[i] : 1u); }
        rng = 31337u;
        uint32_t cancels = 0;
        for (uint32_t step = 0; step < 6000u; step++) {         /* 무작위 등록/재등록/취소 + tick */
            uint32_t r = xorshift32(), id = 7u + (r >> 8) % (N1 - 7u);
            if (r % 4u != 0) { uint32_t d = (r >> 20) % 2000u; w1_add(&tm1[id], d); want1[id] = w1.now + (d ? d : 1u); }
            else if (tm1[id].armed) { link_del(&tm1[id].link); tm1[id].armed = false; want1[id] = 0; cancels++; }
            w1_tick();
        }
        for (uint32_t i = 0; i < 2100u; i++) w1_tick();         /* 남은 것 모두 만료 */
        bool ok = true;
        for (uint32_t i = 0; i < 7u; i++) ok = ok && cnt1[i] == 1u && got1[i] == want1[i];
        CHECK(ok);
        uint32_t fired_ok = 0, bad = 0;
        for (uint32_t i = 7; i < N1; i++) {
            if (want1[i] == 0) continue;
            if (got1[i] == want1[i] && !tm1[i].armed) fired_ok++; else bad++;
        }
        printf("[1] abs-compare wheel: last-arm fired on time %u, bad %u, cancels %u, slot visits %llu, writes to waiting timers 0\n",
               fired_ok, bad, cancels, (unsigned long long)w1.visits);
        CHECK(bad == 0 && fired_ok > 1000u);
    }
    /* 연습 2 */
    {
        w2_init(&wa, ta, 0xFFFF0000u); w2_init(&wb, tb, 0xFFFF0000u);
        loga_n = logb_n = 0;
        rng = 2024u;
        uint32_t ticks = 0;
        bool same_state = true;
        for (uint32_t round = 0; round < 400u; round++) {
            uint32_t nadd = xorshift32() % 6u;
            for (uint32_t k = 0; k < nadd; k++) {
                uint32_t r = xorshift32(), id = (r >> 8) % N2, d = (r >> 18) % 3000u;
                w2_add(&wa, &ta[id], d); w2_add(&wb, &tb[id], d);
            }
            uint32_t n = 1u + xorshift32() % 2000u;             /* 이만큼 "잤다" */
            for (uint32_t i = 0; i < n; i++) w2_tick(&wa, ta, loga, &loga_n);
            w2_advance(&wb, tb, n, logb, &logb_n);
            ticks += n;
            same_state = same_state && wa.now == wb.now && memcmp(wa.occ, wb.occ, sizeof wa.occ) == 0;
        }
        bool same_log = loga_n == logb_n && loga_n > 0 && memcmp(loga, logb, loga_n * sizeof loga[0]) == 0;
        for (uint32_t i = 0; i < N2; i++)
            same_state = same_state && ta[i].armed == tb[i].armed && (!ta[i].armed || (ta[i].expire == tb[i].expire && ta[i].rounds == tb[i].rounds));
        printf("[2] advance vs tick-by-tick: %u ticks, fires %u/%u, slot visits %u vs %u -> %s\n",
               ticks, loga_n, logb_n, wa.slot_visits, wb.slot_visits, same_log && same_state ? "identical" : "DIFFERENT");
        CHECK(same_log && same_state);
        CHECK(wb.slot_visits < wa.slot_visits);
        g_rearm2 = false;
        w2_init(&wb, tb, 7u);                                   /* 빈 휠: 거리 0, advance 는 now 만 이동 */
        CHECK(w2_next_nonempty(&wb) == 0);
        w2_advance(&wb, tb, 1000u, logb, &logb_n);
        CHECK(wb.now == 1007u);
        w2_add(&wb, &tb[0], 256); w2_add(&wb, &tb[1], 3);       /* 256 → 현재 슬롯 = 거리 256 */
        CHECK(w2_next_nonempty(&wb) == 3);
        w2_tick(&wb, tb, logb, &logb_n); w2_tick(&wb, tb, logb, &logb_n); w2_tick(&wb, tb, logb, &logb_n);
        CHECK(w2_next_nonempty(&wb) == 253);
    }
    /* 연습 3 */
    {
        uint32_t bad_abs = run3(true), bad_rel = run3(false);
        printf("[3] 250 slots across 2^32 wrap, 600 timers: misfired abs%%250=%u, relative cursor=%u\n", bad_abs, bad_rel);
        CHECK(bad_abs > 0 && bad_rel == 0);
    }
    /* 연습 4 */
    {
        r4_t nv = run4(naive_tick), tp = run4(twophase_tick);
        printf("[4] single-pass: max fires in one tick %u, cancelled B fired %u | two-phase: %u, %u\n",
               nv.max_per_tick, nv.b_fired, tp.max_per_tick, tp.b_fired);
        CHECK(nv.max_per_tick > 1u && nv.b_fired > 0u);         /* 버그가 실제로 재현되는가 */
        CHECK(tp.max_per_tick == 1u && tp.b_fired == 0u);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
