/* DS-M06 해시 타이머 휠
 * 슬롯 256개 단일 레벨 + 라운드 카운터. 슬롯 = 침입형 원형 이중 연결 리스트(sentinel). malloc 없음.
 * 참조 구현: (expire, seq) min-heap + lazy 삭제. 같은 tick 에 만료되는 타이머는 등록 순서(seq, FIFO)로 실행.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TW_BITS      8u
#define TW_SLOTS     (1u << TW_BITS)          /* 256 */
#define TW_MASK      (TW_SLOTS - 1u)
#define TW_MAX_DELAY 0x7FFFFFFFu              /* 2^31-1: 랩어라운드 안전 비교가 성립하는 한계 */

/* ---- 최소 침입형 리스트 (DS-B06 sentinel 리스트, DS-M01 에서 자세히) ---- */
typedef struct tw_link { struct tw_link *next, *prev; } tw_link_t;
static void link_init(tw_link_t *h) { h->next = h->prev = h; }
static bool link_empty(const tw_link_t *h) { return h->next == h; }
static void link_add_tail(tw_link_t *h, tw_link_t *n) {
    n->prev = h->prev; n->next = h; h->prev->next = n; h->prev = n;
}
static void link_del(tw_link_t *n) {                 /* 떼고 자기 고리로 — 다시 del 해도 무해 */
    n->prev->next = n->next; n->next->prev = n->prev; n->next = n->prev = n;
}
#define container_of(p, T, m) ((T *)(void *)((char *)(p) - offsetof(T, m)))

typedef struct tw_wheel tw_wheel_t;
typedef struct tw_timer tw_timer_t;
typedef void (*tw_cb_t)(tw_wheel_t *w, tw_timer_t *t);

struct tw_timer {
    tw_link_t link;       /* 슬롯 리스트(또는 tick 중 만료 리스트)에 걸리는 고리 — 객체에 내장 */
    uint32_t  expire;     /* 절대 만료 tick (검사·next_expiry 용) */
    uint32_t  rounds;     /* 커서가 이 슬롯을 앞으로 몇 번 "그냥 지나쳐야" 하나 */
    uint32_t  seq;        /* 등록 순번 — 같은 tick 만료의 순서 = FIFO */
    tw_cb_t   cb;
    uint16_t  id;
    bool      armed;
};

struct tw_wheel {
    tw_link_t slot[TW_SLOTS];
    uint32_t  now;        /* 마지막으로 처리한 tick */
    uint32_t  seq;
    uint32_t  count;      /* armed 타이머 수 */
};

static void tw_init(tw_wheel_t *w, uint32_t start) {
    for (uint32_t i = 0; i < TW_SLOTS; i++) link_init(&w->slot[i]);
    w->now = start; w->seq = 0; w->count = 0;
}
static void tw_timer_init(tw_timer_t *t, uint16_t id, tw_cb_t cb) {
    link_init(&t->link);
    t->expire = t->rounds = t->seq = 0;
    t->cb = cb; t->id = id; t->armed = false;
}

/* ---------------- Part A: 등록 (슬롯 + 라운드) ---------------- */
/* now + delay 에 만료되도록 건다. delay 0 은 1 로 올린다(다음 tick — add 안에서 콜백을 부르지 않는다).
 * 이미 armed 면 옮겨 건다(mod_timer). delay > TW_MAX_DELAY 면 false, 상태 변화 없음. */
static bool tw_add(tw_wheel_t *w, tw_timer_t *t, uint32_t delay) {
    if (delay > TW_MAX_DELAY) return false;
    if (delay == 0) delay = 1;
    if (t->armed) { link_del(&t->link); w->count--; }
    t->expire = w->now + delay;                     /* unsigned 랩어라운드는 정의된 동작 */
    t->rounds = (delay - 1u) >> TW_BITS;            /* 256 → 0, 257 → 1, 512 → 1 */
    t->seq    = w->seq++;
    t->armed  = true;
    link_add_tail(&w->slot[t->expire & TW_MASK], &t->link);   /* 꼬리 = seq 오름차순 유지 */
    w->count++;
    return true;
}

/* ---------------- Part B: 취소 ---------------- */
static bool tw_cancel(tw_wheel_t *w, tw_timer_t *t) {
    if (!t->armed) return false;                    /* 이미 만료/취소 — 콜백 안의 자기 취소 포함 */
    link_del(&t->link);
    t->armed = false;
    w->count--;
    return true;
}

/* ---------------- Part A+B: tick 처리 (콜백 재진입 안전) ---------------- */
static void tw_tick(tw_wheel_t *w) {
    tw_link_t due;                                  /* 이번 tick 만료 목록 — 스택 위 지역 sentinel */
    link_init(&due);
    w->now++;
    tw_link_t *head = &w->slot[w->now & TW_MASK];
    /* 1단계: 콜백 없이 슬롯만 훑는다 → _safe 순회로 충분. rounds==0 은 due 로, 나머지는 rounds-- */
    for (tw_link_t *p = head->next, *nx; p != head; p = nx) {
        nx = p->next;
        tw_timer_t *t = container_of(p, tw_timer_t, link);
        if (t->rounds == 0) { link_del(p); link_add_tail(&due, p); }
        else t->rounds--;
    }
    /* 2단계: due 의 머리를 하나씩 떼어 실행. 콜백이 due 안의 다른 타이머를 취소/재등록해도
     * 그 노드는 due 에서 빠질 뿐이고, 새로 거는 타이머는 모두 now+1 이후라 due 에 안 들어온다 */
    while (!link_empty(&due)) {
        tw_timer_t *t = container_of(due.next, tw_timer_t, link);
        link_del(&t->link);
        t->armed = false;
        w->count--;
        if (t->cb) t->cb(w, t);
    }
}

/* ---------------- Part C: 다음 만료까지 남은 tick (tickless idle) ---------------- */
/* 1 이상. 타이머가 없으면 0. 슬롯을 now+1 부터 차례로 보며 rounds==0 을 처음 만나면 바로 답 */
static uint32_t tw_next_expiry(tw_wheel_t *w) {
    uint32_t best = 0;
    for (uint32_t k = 1; k <= TW_SLOTS; k++) {
        tw_link_t *head = &w->slot[(w->now + k) & TW_MASK];
        for (tw_link_t *p = head->next; p != head; p = p->next) {
            uint32_t d = container_of(p, tw_timer_t, link)->rounds * TW_SLOTS + k;
            if (best == 0 || d < best) best = d;
        }
        if (best != 0 && best <= k) return best;    /* 뒤 슬롯은 모두 > k */
    }
    return best;
}

/* ---------------- 검사 도구 (주어짐) ---------------- */
#define NT 4096u                                    /* 타이머 객체 수 */

/* 불변식: 링크 일관성, 슬롯 == expire & MASK, 1 <= expire-now <= MAX, rounds == (expire-now-1)>>8,
 * 슬롯 안 seq 오름차순, 전체 개수 == count */
static bool tw_check(const tw_wheel_t *w) {
    uint32_t total = 0;
    for (uint32_t s = 0; s < TW_SLOTS; s++) {
        const tw_link_t *head = &w->slot[s];
        uint32_t last_seq = 0, n = 0;
        for (const tw_link_t *p = head->next; p != head; p = p->next) {
            if (p->next->prev != p || p->prev->next != p || ++n > NT) return false;
            const tw_timer_t *t = (const tw_timer_t *)(const void *)((const char *)p - offsetof(tw_timer_t, link));
            uint32_t d = t->expire - w->now;
            if (!t->armed || (t->expire & TW_MASK) != s || d == 0 || d > TW_MAX_DELAY) return false;
            if (t->rounds != (d - 1u) >> TW_BITS) return false;
            if (n > 1 && t->seq <= last_seq) return false;
            last_seq = t->seq;
        }
        total += n;
    }
    return total == w->count;
}

static uint32_t rng_state = 1u;
static uint32_t xorshift32(void) {
    uint32_t x = rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return rng_state = x;
}
/* a 가 b 보다 이른가 (랩어라운드 안전, 차이 < 2^31 가정). (int32_t) 캐스트 없이 — 구현 정의 동작 회피 */
static bool tick_before(uint32_t a, uint32_t b) { return a != b && (uint32_t)(a - b) > 0x7FFFFFFFu; }

/* ---- 참조 구현: (expire, seq) min-heap, 취소는 lazy (seq 불일치/armed 아님 → 버림) ---- */
#define REF_CAP (1u << 16)
typedef struct { uint32_t expire, seq; uint16_t id; } ref_ev_t;
static ref_ev_t ref_heap[REF_CAP];
static uint32_t ref_n, ref_now, ref_seq;
static struct { uint32_t expire, seq; bool armed; } ref_tm[NT];
static bool ref_overflow;

static bool ref_before(ref_ev_t a, ref_ev_t b) {
    return a.expire != b.expire ? tick_before(a.expire, b.expire) : a.seq < b.seq;
}
static void ref_push(ref_ev_t e) {
    if (ref_n == REF_CAP) { ref_overflow = true; return; }
    uint32_t i = ref_n++;
    while (i > 0 && ref_before(e, ref_heap[(i - 1u) / 2u])) { ref_heap[i] = ref_heap[(i - 1u) / 2u]; i = (i - 1u) / 2u; }
    ref_heap[i] = e;
}
static ref_ev_t ref_pop(void) {
    ref_ev_t top = ref_heap[0], x = ref_heap[--ref_n];
    uint32_t i = 0;
    for (;;) {
        uint32_t l = 2u * i + 1u, m = l;
        if (l >= ref_n) break;
        if (l + 1u < ref_n && ref_before(ref_heap[l + 1u], ref_heap[l])) m = l + 1u;
        if (!ref_before(ref_heap[m], x)) break;
        ref_heap[i] = ref_heap[m]; i = m;
    }
    if (ref_n) ref_heap[i] = x;
    return top;
}

/* ---- 두 구현을 같은 스크립트로 돌리기 위한 얇은 인터페이스 ---- */
typedef struct {
    const char *name;
    bool     (*add)(uint16_t id, uint32_t delay);
    bool     (*cancel)(uint16_t id);
    void     (*tick)(void);
    bool     (*armed)(uint16_t id);
    uint32_t (*expire)(uint16_t id);
    uint32_t (*now)(void);
} model_t;

typedef struct { uint32_t tick; uint16_t id; } fire_t;
#define LOG_CAP (1u << 17)
static fire_t   g_log[2][LOG_CAP];
static uint32_t g_log_n[2];
static int      g_log_sel;                          /* 0 = wheel, 1 = ref */
static struct { uint32_t fires, rearm, cb_cancel, cb_cancel_same_tick, cb_mod_same_tick, peak; } g_st;

static uint32_t pick_delay(uint32_t r) {
    uint32_t k = 1u + (r >> 3) % 4u;
    switch (r & 7u) {
    case 0:  return 0;                              /* delay 0 → 다음 tick */
    case 1:  return 256u * k;                       /* 정확히 256 의 배수 */
    case 2:  return 256u * k - 1u;
    case 3:  return 256u * k + 1u;
    case 4: case 5: return 1u + (r >> 3) % 64u;     /* 짧은 타임아웃 */
    default: return 1u + (r >> 3) % 3000u;          /* 여러 바퀴 */
    }
}

/* 만료 콜백 스크립트: 자기 재등록(주기 타이머) / 짝 취소 / 짝 재등록 / 아무것도 안 함 */
static void on_fire(const model_t *m, uint16_t id) {
    if (g_log_n[g_log_sel] < LOG_CAP) g_log[g_log_sel][g_log_n[g_log_sel]++] = (fire_t){ m->now(), id };
    g_st.fires++;
    uint32_t r = xorshift32();
    uint16_t mate = (uint16_t)(id ^ 1u);
    bool same = m->armed(mate) && m->expire(mate) == m->now();   /* 짝이 이번 tick 만료 대기 중 */
    switch (r % 8u) {
    case 0: case 1: m->add(id, pick_delay(r >> 3)); g_st.rearm++; break;
    case 2: if (m->cancel(mate)) { g_st.cb_cancel++; g_st.cb_cancel_same_tick += same; } break;
    case 3: m->add(mate, pick_delay(r >> 3)); g_st.cb_mod_same_tick += same; break;
    case 4: m->cancel(id); break;                   /* 자기 취소 — 이미 armed 아님 → 무해해야 */
    default: break;
    }
}

/* wheel 쪽 */
static tw_wheel_t g_w;
static tw_timer_t g_tm[NT];
static void wheel_cb(tw_wheel_t *w, tw_timer_t *t);
static bool     w_add(uint16_t id, uint32_t d) { return tw_add(&g_w, &g_tm[id], d); }
static bool     w_cancel(uint16_t id) { return tw_cancel(&g_w, &g_tm[id]); }
static void     w_tick(void) { tw_tick(&g_w); }
static bool     w_armed(uint16_t id) { return g_tm[id].armed; }
static uint32_t w_expire(uint16_t id) { return g_tm[id].expire; }
static uint32_t w_now(void) { return g_w.now; }
static const model_t WHEEL = { "wheel", w_add, w_cancel, w_tick, w_armed, w_expire, w_now };
static void wheel_cb(tw_wheel_t *w, tw_timer_t *t) { (void)w; on_fire(&WHEEL, t->id); }

/* ref 쪽 */
static const model_t REF;
static bool r_add(uint16_t id, uint32_t d) {
    if (d > TW_MAX_DELAY) return false;
    if (d == 0) d = 1;
    ref_tm[id].expire = ref_now + d; ref_tm[id].seq = ref_seq++; ref_tm[id].armed = true;
    ref_push((ref_ev_t){ ref_tm[id].expire, ref_tm[id].seq, id });
    return true;
}
static bool r_cancel(uint16_t id) { bool was = ref_tm[id].armed; ref_tm[id].armed = false; return was; }
static void r_tick(void) {
    ref_now++;
    while (ref_n && !tick_before(ref_now, ref_heap[0].expire)) {   /* expire <= now */
        ref_ev_t e = ref_pop();
        if (!ref_tm[e.id].armed || ref_tm[e.id].seq != e.seq) continue;   /* 취소/재등록된 옛 항목 */
        ref_tm[e.id].armed = false;
        on_fire(&REF, e.id);
    }
}
static bool     r_armed(uint16_t id) { return ref_tm[id].armed; }
static uint32_t r_expire(uint16_t id) { return ref_tm[id].expire; }
static uint32_t r_now(void) { return ref_now; }
static const model_t REF = { "heap", r_add, r_cancel, r_tick, r_armed, r_expire, r_now };

static uint32_t brute_next(void) {                  /* armed 중 최소 (expire - now), 없으면 0 */
    uint32_t best = 0;
    for (uint32_t i = 0; i < NT; i++)
        if (g_tm[i].armed) { uint32_t d = g_tm[i].expire - g_w.now; if (best == 0 || d < best) best = d; }
    return best;
}

/* 같은 시드로 한 모델을 돌린다. wheel 이면 매 연산 후 tw_check, 매 step next_expiry 대조 */
static bool run_script(const model_t *m, int sel, uint32_t start, uint32_t steps, uint32_t seed) {
    bool ok = true;
    g_log_sel = sel; g_log_n[sel] = 0;
    memset(&g_st, 0, sizeof g_st);
    tw_init(&g_w, start);
    for (uint32_t i = 0; i < NT; i++) tw_timer_init(&g_tm[i], (uint16_t)i, wheel_cb);
    ref_n = 0; ref_now = start; ref_seq = 0; ref_overflow = false;
    memset(ref_tm, 0, sizeof ref_tm);
    rng_state = seed;
    for (uint32_t step = 0; step < steps && ok; step++) {
        uint32_t nops = xorshift32() % 7u;
        for (uint32_t k = 0; k < nops && ok; k++) {
            uint32_t r = xorshift32();
            uint16_t id = (uint16_t)((r >> 8) % NT);
            switch (r % 10u) {
            case 0: case 1: case 2: case 3: m->add(id, pick_delay(xorshift32())); break;
            case 4: case 5: {                       /* 짝 (2i, 2i+1) 을 같은 delay 로 → 같은 tick 만료 */
                uint32_t d = pick_delay(xorshift32());
                m->add((uint16_t)(id & ~1u), d); m->add((uint16_t)(id | 1u), d); break;
            }
            case 6: case 7: m->cancel(id); break;
            case 8: m->add(id, 0); break;
            default: ok = !m->add(id, TW_MAX_DELAY + 1u); break;   /* 범위 밖 거절 */
            }
            if (sel == 0) ok = ok && tw_check(&g_w);
        }
        if (sel == 0) ok = ok && tw_next_expiry(&g_w) == brute_next();
        if (g_w.count > g_st.peak) g_st.peak = g_w.count;
        m->tick();
        if (sel == 0) ok = ok && tw_check(&g_w);
    }
    return ok && !ref_overflow;
}

/* ---- 고정 시나리오용 콜백 ---- */
static uint32_t g_fire_tick[NT], g_fire_cnt[NT];
static uint16_t g_order[64];
static uint32_t g_order_n;
static void rec_cb(tw_wheel_t *w, tw_timer_t *t) {
    g_fire_tick[t->id] = w->now; g_fire_cnt[t->id]++;
    if (g_order_n < 64) g_order[g_order_n++] = t->id;
}
static uint32_t g_cb_ret;                           /* 콜백 안 API 반환값 기록 */
static void cancel_mate_cb(tw_wheel_t *w, tw_timer_t *t) { rec_cb(w, t); g_cb_ret = tw_cancel(w, &g_tm[t->id ^ 1u]); }
static void cancel_self_cb(tw_wheel_t *w, tw_timer_t *t) { rec_cb(w, t); g_cb_ret = tw_cancel(w, t); }
static void periodic256_cb(tw_wheel_t *w, tw_timer_t *t) { rec_cb(w, t); if (g_fire_cnt[t->id] < 3) tw_add(w, t, 256); }
static void add_zero_cb(tw_wheel_t *w, tw_timer_t *t) { rec_cb(w, t); if (g_fire_cnt[t->id] < 3) tw_add(w, t, 0); }
static void mod_mate_cb(tw_wheel_t *w, tw_timer_t *t) { rec_cb(w, t); tw_add(w, &g_tm[t->id ^ 1u], 10); }

static void reset_fixed(uint32_t start) {
    tw_init(&g_w, start);
    for (uint32_t i = 0; i < NT; i++) tw_timer_init(&g_tm[i], (uint16_t)i, rec_cb);
    memset(g_fire_tick, 0, sizeof g_fire_tick); memset(g_fire_cnt, 0, sizeof g_fire_cnt);
    g_order_n = 0;
}
static bool tick_n(uint32_t n) {                    /* n tick 진행, 매 tick 불변식 검사 */
    bool ok = true;
    for (uint32_t i = 0; i < n; i++) { tw_tick(&g_w); ok = ok && tw_check(&g_w); }
    return ok;
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    tw_wheel_t *w = &g_w;

    /* Part A: 경계 delay 들이 정확히 start + delay 에 한 번만 만료 */
    {
        static const uint32_t dl[] = { 1, 2, 255, 256, 257, 511, 512, 513, 768, 1000, 70000 };
        const uint32_t start = 1000, nd = sizeof dl / sizeof dl[0];
        reset_fixed(start);
        bool ok = true;
        for (uint32_t i = 0; i < nd; i++) ok = ok && tw_add(w, &g_tm[i], dl[i]) && tw_check(w);
        CHECK(ok && w->count == nd);
        CHECK(g_tm[3].rounds == 0 && g_tm[4].rounds == 1 && g_tm[6].rounds == 1);   /* 256→0, 257→1, 512→1 */
        CHECK(tick_n(70000));
        ok = w->count == 0;
        for (uint32_t i = 0; i < nd; i++) ok = ok && g_fire_cnt[i] == 1 && g_fire_tick[i] == start + dl[i];
        CHECK(ok);
    }
    /* Part A: delay 0 → add 안에서 실행 안 됨, 다음 tick 에 실행. 범위 밖 delay 거절 */
    {
        reset_fixed(50);
        CHECK(tw_add(w, &g_tm[0], 0) && g_fire_cnt[0] == 0 && g_tm[0].expire == 51);
        CHECK(tick_n(1) && g_fire_cnt[0] == 1 && g_fire_tick[0] == 51);
        CHECK(!tw_add(w, &g_tm[1], TW_MAX_DELAY + 1u) && !g_tm[1].armed && w->count == 0);
    }
    /* Part A: 같은 tick 만료 = 등록 순서(FIFO). rounds 가 다른 타이머가 같은 슬롯에 섞여 있어도 */
    {
        reset_fixed(0);
        tw_add(w, &g_tm[5], 300);                   /* expire 300, rounds 1 */
        tick_n(44);
        tw_add(w, &g_tm[3], 256);                   /* expire 300, rounds 0 */
        tw_add(w, &g_tm[9], 256);
        tw_add(w, &g_tm[7], 44);                    /* expire 88 — 슬롯 다름 */
        CHECK(tw_check(w) && tick_n(256) && g_order_n == 4);
        CHECK(g_order[0] == 7 && g_order[1] == 5 && g_order[2] == 3 && g_order[3] == 9);
    }

    /* Part B: 취소 — 만료 안 됨, 두 번째 취소는 false, 재등록(mod)은 새 시각에 한 번만 */
    {
        reset_fixed(0xFFFFFF00u);                   /* 32비트 랩어라운드를 건너는 구간 */
        tw_add(w, &g_tm[0], 356); tw_add(w, &g_tm[1], 300); tw_add(w, &g_tm[2], 300);
        CHECK(tw_cancel(w, &g_tm[1]) && !tw_cancel(w, &g_tm[1]) && !tw_cancel(w, &g_tm[9]) && tw_check(w));
        CHECK(tw_add(w, &g_tm[2], 50) && w->count == 2 && tw_check(w));     /* armed 상태에서 옮겨 걸기 */
        CHECK(tick_n(400) && g_fire_cnt[1] == 0 && g_fire_cnt[2] == 1 && g_fire_tick[2] == 0xFFFFFF00u + 50u);
        CHECK(g_fire_cnt[0] == 1 && g_fire_tick[0] == 0x64u && w->count == 0);  /* 0xFFFFFF00 + 356 = 0x64 (넘침) */
    }
    /* Part B: 콜백이 같은 tick 에 만료 대기 중인 짝을 취소 → 짝은 실행 안 됨 */
    {
        reset_fixed(0);
        g_tm[4].cb = cancel_mate_cb;
        tw_add(w, &g_tm[4], 20); tw_add(w, &g_tm[5], 20);
        CHECK(tick_n(20) && g_fire_cnt[4] == 1 && g_fire_cnt[5] == 0 && g_cb_ret == 1 && w->count == 0);
    }
    /* Part B: 콜백 안 자기 취소 → false (이미 armed 아님), 무해 */
    {
        reset_fixed(0);
        g_tm[6].cb = cancel_self_cb;
        tw_add(w, &g_tm[6], 3);
        g_cb_ret = 7;
        CHECK(tick_n(5) && g_fire_cnt[6] == 1 && g_cb_ret == 0 && w->count == 0);
    }
    /* Part B: 콜백 안 delay 256 재등록 → 같은 슬롯이지만 이번 tick 엔 다시 안 돈다: T, T+256, T+512 */
    {
        reset_fixed(0);
        g_tm[8].cb = periodic256_cb;
        tw_add(w, &g_tm[8], 256);
        bool ok = tick_n(256) && g_fire_cnt[8] == 1;
        ok = ok && tick_n(256) && g_fire_cnt[8] == 2 && g_fire_tick[8] == 512;
        ok = ok && tick_n(256) && g_fire_cnt[8] == 3 && g_fire_tick[8] == 768 && !g_tm[8].armed;
        CHECK(ok);
    }
    /* Part B: 콜백 안 delay 0 등록 → 다음 tick (무한 루프 아님) */
    {
        reset_fixed(0);
        g_tm[2].cb = add_zero_cb;
        tw_add(w, &g_tm[2], 5);
        CHECK(tick_n(5) && g_fire_cnt[2] == 1 && g_tm[2].armed && g_tm[2].expire == 6);
        CHECK(tick_n(2) && g_fire_cnt[2] == 3 && g_fire_tick[2] == 7 && !g_tm[2].armed);
    }
    /* Part B: 콜백이 같은 tick 만료 대기 중인 짝을 옮겨 검 → 짝은 새 시각에만 실행 */
    {
        reset_fixed(0);
        g_tm[10].cb = mod_mate_cb;
        tw_add(w, &g_tm[10], 30); tw_add(w, &g_tm[11], 30);
        CHECK(tick_n(30) && g_fire_cnt[11] == 0 && g_tm[11].armed && g_tm[11].expire == 40);
        CHECK(tick_n(10) && g_fire_cnt[11] == 1 && g_fire_tick[11] == 40);
    }

    /* Part C: next_expiry — 비었으면 0, rounds 반영, tick 진행 후 갱신 */
    {
        reset_fixed(0xFFFFFFF0u);
        CHECK(tw_next_expiry(w) == 0);
        tw_add(w, &g_tm[0], 1000); tw_add(w, &g_tm[1], 300);
        CHECK(tw_next_expiry(w) == 300);
        tw_add(w, &g_tm[2], 5);
        CHECK(tw_next_expiry(w) == 5 && tick_n(4) && g_fire_cnt[2] == 0 && tw_next_expiry(w) == 1);
        CHECK(tick_n(1) && g_fire_cnt[2] == 1 && tw_next_expiry(w) == 295);
    }
    /* Part C: 타이머 4096개, 20000 tick — 같은 스크립트를 wheel 과 min-heap 참조에 돌려 만료 로그 대조 */
    {
        const uint32_t start = 0xFFFFE000u, steps = 20000u, seed = 0xBEEFu;
        bool okw = run_script(&WHEEL, 0, start, steps, seed);
        uint32_t rearm = g_st.rearm, cbc = g_st.cb_cancel, cbs = g_st.cb_cancel_same_tick,
                 cbm = g_st.cb_mod_same_tick, peak = g_st.peak, adds = g_w.seq;
        static uint32_t ex_w[NT]; static bool ar_w[NT];
        for (uint32_t i = 0; i < NT; i++) { ar_w[i] = g_tm[i].armed; ex_w[i] = g_tm[i].expire; }
        bool okr = run_script(&REF, 1, start, steps, seed);
        bool same = g_log_n[0] == g_log_n[1] && g_log_n[0] > 0 && g_log_n[0] < LOG_CAP;
        uint32_t first_diff = UINT32_MAX;
        for (uint32_t i = 0; same && i < g_log_n[0]; i++)
            if (g_log[0][i].tick != g_log[1][i].tick || g_log[0][i].id != g_log[1][i].id) { same = false; first_diff = i; }
        bool state = true;
        for (uint32_t i = 0; i < NT; i++)
            state = state && ar_w[i] == ref_tm[i].armed && (!ar_w[i] || ex_w[i] == ref_tm[i].expire);
        printf("[C] wheel vs heap: %u steps from now=0x%08X (wraps), fires=%u (wheel) / %u (heap) -> %s\n",
               steps, start, g_log_n[0], g_log_n[1], same ? "identical order" : "MISMATCH");
        if (first_diff != UINT32_MAX) printf("[C] first diff at #%u\n", first_diff);
        printf("[C] cb: re-arm self %u, cancel mate %u (same-tick %u), mod mate same-tick %u\n",
               rearm, cbc, cbs, cbm);
        printf("[C] registrations %u, peak live timers %u\n", adds, peak);
        CHECK(okw && okr);
        CHECK(same && state);
        CHECK(cbs > 0 && cbm > 0);                  /* 까다로운 경우가 실제로 일어났는가 */
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
