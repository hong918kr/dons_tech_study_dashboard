/* RTOS-M07 Tick 처리와 지연 리스트: 정렬 리스트 vs 타이머 휠
 *
 * 호스트 모델: OS 스레드 1개로 도는 tick 단위 결정적 시뮬레이션. "타이머" = 지연 태스크의 wake 시각,
 * 또는 소프트웨어 타이머의 만료 시각. 두 자료구조가 같은 API 를 구현한다:
 *   start(id, delay) : now + delay (mod 2^32) 에 만료. delay ∈ [1, 2^32-1]. id 는 비활성이어야 한다.
 *   cancel(id)       : 활성이면 떼어내고 1, 아니면 0.                     (주어진 코드, 두 구조 모두 O(1))
 *   tick(out)        : now++ (= kernel_tick), 이번 tick 에 만료된 id 를 out 에.
 *                      만료 시각이 같으면 start 한 순서(FIFO) — FreeRTOS vListInsert 의 "<=" 규칙.
 *   next(&delta)     : 다음 만료까지 남은 tick (tickless idle 이 묻는 값). 비었으면 0 반환.
 *   step(k)          : 만료가 없다고 알려진 k tick 을 처리 없이 건너뛴다 (vTaskStepTick 대용, 주어진 코드).
 * 비용은 ns 가 아니라 연산 수(ops)로 센다 — 결정적이라 테스트에 넣을 수 있다:
 *   상수 작업 1 = 1 op, 리스트 노드 1개 방문 = 1 op, 휠 슬롯 1개 들여다보기 = 1 op.
 * ns 측정은 demo_bench.c 에만 있다.
 *
 * 채울 것 (TODO 1~6):
 *  Part A — FreeRTOS 방식 정렬 리스트: current / overflow 두 리스트 + xNextTaskUnblockTime 캐시
 *    1. sl_start   2. sl_tick   3. sl_next
 *  Part B — hashed timing wheel (Varghese & Lauck 1987 의 Scheme 6: 슬롯마다 정렬 안 한 리스트)
 *    4. hw_start   5. hw_tick   6. hw_next
 *  Part C — 게이트웨이 수신 감시(reset 폭주) vs 배터리 노드(tickless) 시나리오: 주어진 하네스가 ops 를 비교
 * 정답 확인: 64비트 시계를 쓰는 brute-force 참조 구현과 (tick, id) 만료 순서가 완전히 같아야 한다.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXT      10000u          /* 타이머 id 0..MAXT-1 */
#define MAX_SLOTS 4096u
#define TICK_MAX  0xFFFFFFFFu     /* FreeRTOS portMAX_DELAY (32비트 tick) */

static int g_assert_fail;         /* configASSERT 대용: 멈추지 않고 센다 (RTOS-B02 의 교체 가능한 훅) */
#define KERNEL_ASSERT(c) do { if (!(c)) g_assert_fail++; } while (0)

/* ---------------- 주어진 코드: 침입형(intrusive) 원형 이중 연결 리스트 ---------------- */
typedef struct tlist tlist_t;
typedef struct tnode {
    struct tnode *next, *prev;
    tlist_t *owner;               /* 들어 있는 리스트, NULL = 비활성 (FreeRTOS pxContainer) */
    uint32_t expiry;              /* 만료 tick, mod 2^32 (FreeRTOS xItemValue / 미니 커널 wake_tick) */
    uint32_t id;
} tnode_t;
struct tlist { tnode_t end; uint32_t n; };          /* end = 보초(sentinel) (FreeRTOS xListEnd) */

typedef struct { uint64_t start, cancel, tick, next; } ops_t;

static void list_init(tlist_t *l) { l->end.next = l->end.prev = &l->end; l->end.owner = l; l->n = 0; }
static int list_empty(const tlist_t *l) { return l->n == 0; }
static tnode_t *list_head(tlist_t *l) { return l->end.next; }
static void list_insert_before(tlist_t *l, tnode_t *pos, tnode_t *x) {
    x->next = pos; x->prev = pos->prev; pos->prev->next = x; pos->prev = x; x->owner = l; l->n++;
}
static void list_remove(tnode_t *x) {                 /* O(1): 노드가 자기 리스트를 안다 (listREMOVE_ITEM) */
    x->prev->next = x->next; x->next->prev = x->prev; x->owner->n--; x->owner = NULL; x->next = x->prev = NULL;
}

/* ---------------- Part A: 정렬 리스트 (FreeRTOS tasks.c / timers.c 방식) ---------------- */
typedef struct {
    tlist_t list[2];
    tlist_t *cur, *ovf;           /* pxDelayedTaskList / pxOverflowDelayedTaskList */
    uint32_t now;                 /* xTickCount */
    uint32_t next_unblock;        /* xNextTaskUnblockTime: cur 의 가장 이른 만료 이하(lower bound) */
    uint32_t overflows;           /* xNumOfOverflows */
    tnode_t *node;
    ops_t ops;
} slist_t;

static void sl_init(slist_t *s, uint32_t now) {
    memset(s, 0, sizeof *s);
    list_init(&s->list[0]); list_init(&s->list[1]);
    s->cur = &s->list[0]; s->ovf = &s->list[1];
    s->now = now; s->next_unblock = TICK_MAX;
    s->node = calloc(MAXT, sizeof *s->node);
    for (uint32_t i = 0; i < MAXT; i++) s->node[i].id = i;
}
static void sl_free(slist_t *s) { free(s->node); s->node = NULL; }

/* TODO 1: wake = now + delay (넘쳐도 된다). wake < now 면 overflow 리스트, 아니면 current 리스트.
 *   리스트 안에서는 오름차순 정렬, 같은 값은 뒤에(FIFO): "앞 노드의 expiry <= wake 인 동안 지나간다".
 *   ops.start: 1 + 지나간 노드 수.  current 에 넣었고 wake 가 캐시보다 이르면 next_unblock = wake. */
static void sl_start(slist_t *s, uint32_t id, uint32_t delay) {
    tnode_t *x = &s->node[id];
    uint32_t wake = s->now + delay;                    /* tasks.c:8729 — 넘쳐도 된다 */
    tlist_t *l = (wake < s->now) ? s->ovf : s->cur;    /* tasks.c:8734 넘쳤으면 overflow 리스트 */
    x->expiry = wake;
    s->ops.start++;
    tnode_t *it = list_head(l);
    while (it != &l->end && it->expiry <= wake) { s->ops.start++; it = it->next; }   /* list.c:192 "<=" */
    list_insert_before(l, it, x);
    if (l == s->cur && wake < s->next_unblock) s->next_unblock = wake;               /* tasks.c:8751 */
}

/* 주어진 코드: 떼어내기만 한다. next_unblock 은 일부러 안 고친다 — FreeRTOS 도 그렇다 (tasks.c:5532-5542:
 * tickless 가 아니면 "stale" 캐시를 그대로 두고, 그 tick 에 헛걸음 한 번 한 뒤 고친다). 캐시는 lower bound 면 안전. */
static int sl_cancel(slist_t *s, uint32_t id) {
    tnode_t *x = &s->node[id];
    if (!x->owner) return 0;
    list_remove(x); s->ops.cancel++;
    return 1;
}

/* TODO 2: now++. now 가 0 이 되면 두 리스트를 맞바꾼다 (그 순간 current 는 비어 있어야 한다 → KERNEL_ASSERT),
 *   overflows++, 캐시를 새 current 의 머리(비었으면 TICK_MAX)로.
 *   ops.tick += 1 (캐시 비교). now >= next_unblock 일 때만 리스트를 연다: 머리를 볼 때마다 ops.tick += 1,
 *   머리 expiry > now 면 캐시 = 머리 expiry 로 멈춤, 비었으면 캐시 = TICK_MAX, 아니면 떼어내 out 에 담는다.
 *   반환: 만료 개수 (max 보다 많으면 out 에는 max 개까지만). */
static int sl_tick(slist_t *s, uint32_t *out, int max) {
    int n = 0;
    s->now++;                                                          /* tasks.c:4852-4856 */
    if (s->now == 0) {                                                 /* tasks.c:4858 → taskSWITCH_DELAYED_LISTS */
        KERNEL_ASSERT(list_empty(s->cur));                             /* tasks.c:272 */
        tlist_t *t = s->cur; s->cur = s->ovf; s->ovf = t;
        s->overflows++;
        s->next_unblock = list_empty(s->cur) ? TICK_MAX : list_head(s->cur)->expiry;   /* prvResetNextTaskUnblockTime */
    }
    s->ops.tick++;
    if (s->now >= s->next_unblock) {                                   /* tasks.c:4871 — 조기 종료 */
        for (;;) {
            if (list_empty(s->cur)) { s->next_unblock = TICK_MAX; break; }          /* tasks.c:4875-4883 */
            tnode_t *h = list_head(s->cur);
            s->ops.tick++;
            if (s->now < h->expiry) { s->next_unblock = h->expiry; break; }         /* tasks.c:4897-4905 */
            list_remove(h);
            if (n < max) out[n] = h->id;
            n++;
        }
    }
    return n;
}

/* TODO 3: tickless 용 "다음 만료까지 몇 tick?" — current 머리, 없으면 overflow 머리. O(1), ops.next += 1.
 *   비었으면 0 반환. delta = expiry - now (mod 2^32 뺄셈이라 overflow 머리도 맞다). */
static int sl_next(slist_t *s, uint32_t *delta) {
    s->ops.next++;
    if (!list_empty(s->cur)) { *delta = list_head(s->cur)->expiry - s->now; return 1; }
    if (!list_empty(s->ovf)) { *delta = list_head(s->ovf)->expiry - s->now; return 1; }
    return 0;
}

static void sl_step(slist_t *s, uint32_t k) {          /* 주어진 코드: vTaskStepTick 처럼 처리 없이 건너뛴다 */
    KERNEL_ASSERT((uint32_t)(s->now + k) >= s->now);  /* 이 모델은 step 으로 wrap 을 건너지 않는다 */
    s->now += k;
}

/* ---------------- Part B: hashed timing wheel ---------------- */
typedef struct {
    tlist_t *slot;
    uint32_t nslots, mask;        /* nslots = 2^k → slot = expiry & mask. 2^32 의 약수라 wrap 이 보이지 않는다 */
    uint32_t now, count;
    tnode_t *node;
    ops_t ops;
} wheel_t;

static void hw_init(wheel_t *w, uint32_t nslots, uint32_t now) {
    memset(w, 0, sizeof *w);
    w->nslots = nslots; w->mask = nslots - 1; w->now = now;
    w->slot = calloc(nslots, sizeof *w->slot);
    for (uint32_t i = 0; i < nslots; i++) list_init(&w->slot[i]);
    w->node = calloc(MAXT, sizeof *w->node);
    for (uint32_t i = 0; i < MAXT; i++) w->node[i].id = i;
}
static void hw_free(wheel_t *w) { free(w->slot); free(w->node); w->slot = NULL; w->node = NULL; }

/* TODO 4: expiry = now + delay, 슬롯 = expiry & mask, 그 슬롯 리스트 "꼬리" 에 붙인다 (정렬 안 함, FIFO 보존).
 *   count++, ops.start += 1. (Varghese–Lauck 원문 Scheme 6 은 rounds 를 저장 — 여기선 절대 expiry 를 저장. 연습 1) */
static void hw_start(wheel_t *w, uint32_t id, uint32_t delay) {
    tnode_t *x = &w->node[id];
    x->expiry = w->now + delay;
    tlist_t *l = &w->slot[x->expiry & w->mask];
    list_insert_before(l, &l->end, x);
    w->count++; w->ops.start++;
}

static int hw_cancel(wheel_t *w, uint32_t id) {        /* 주어진 코드 */
    tnode_t *x = &w->node[id];
    if (!x->owner) return 0;
    list_remove(x); w->count--; w->ops.cancel++;
    return 1;
}

/* TODO 5: now++, 슬롯 now & mask 를 앞에서부터 끝까지 훑는다 (ops.tick: 슬롯 1 + 방문 노드마다 1).
 *   expiry == now 인 노드만 떼어내 out 에 (리스트 순서 = start 순서), 나머지(다음 바퀴 이후)는 그대로. */
static int hw_tick(wheel_t *w, uint32_t *out, int max) {
    int n = 0;
    w->now++;
    tlist_t *l = &w->slot[w->now & w->mask];
    w->ops.tick++;
    for (tnode_t *it = list_head(l), *nx; it != &l->end; it = nx) {
        nx = it->next;
        w->ops.tick++;
        if (it->expiry == w->now) {
            list_remove(it); w->count--;
            if (n < max) out[n] = it->id;
            n++;
        }
    }
    return n;
}

/* TODO 6: 다음 만료까지 남은 tick. 비었으면 0 반환. ops.next: 호출 1 + 본 슬롯마다 1 + 방문 노드마다 1.
 *   k = 1, 2, ... nslots 순서로 슬롯 (now + k) & mask 를 본다. 그 슬롯에 expiry - now == k 인 노드가 있으면
 *   그게 답 (앞 슬롯들에 더 이른 것이 없었으니까). 한 바퀴 다 돌아도 없으면 모든 노드가 한 바퀴보다 멀다 →
 *   지금까지 본 모든 노드의 (expiry - now) 최솟값이 답 (한 바퀴 동안 모든 노드를 정확히 한 번 봤다). */
static int hw_next(wheel_t *w, uint32_t *delta) {
    w->ops.next++;
    if (w->count == 0) return 0;
    uint32_t best = TICK_MAX;
    for (uint32_t k = 1; k <= w->nslots; k++) {
        tlist_t *l = &w->slot[(w->now + k) & w->mask];
        w->ops.next++;
        for (tnode_t *it = list_head(l); it != &l->end; it = it->next) {
            uint32_t d = it->expiry - w->now;
            w->ops.next++;
            if (d < best) best = d;
        }
        if (best == k) break;
    }
    *delta = best;
    return 1;
}

static void hw_step(wheel_t *w, uint32_t k) { w->now += k; }   /* 주어진 코드: 건너뛴 슬롯에 만료가 없으면 안전 */

/* ======================================================================================
 * 테스트 하네스 (starter.c 와 같다)
 * ====================================================================================== */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t g_rng;
static uint32_t xorshift32(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }
static uint32_t rnd(uint32_t n) { return xorshift32() % n; }

/* 두 구조를 똑같이 모는 얇은 인터페이스 */
typedef struct {
    const char *name; void *self; ops_t *ops;
    void (*start)(void *, uint32_t, uint32_t);
    int  (*cancel)(void *, uint32_t);
    int  (*tick)(void *, uint32_t *, int);
    int  (*next)(void *, uint32_t *);
    void (*step)(void *, uint32_t);
    int  (*check)(void *);        /* 구조 불변식, 1 = OK */
} tq_t;

/* 하네스 보호: 참조는 "비활성" 인데 구조 안에 아직 있으면(= 만료를 놓친 버그) 리스트가 깨지지 않게 먼저 떼어내고 센다 */
static long g_double_start;
static void sl_start_v(void *p, uint32_t i, uint32_t d) {
    slist_t *s = p; if (s->node[i].owner) { g_double_start++; sl_cancel(s, i); } sl_start(s, i, d);
}
static int  sl_cancel_v(void *p, uint32_t i) { return sl_cancel(p, i); }
static int  sl_tick_v(void *p, uint32_t *o, int m) { return sl_tick(p, o, m); }
static int  sl_next_v(void *p, uint32_t *d) { return sl_next(p, d); }
static void sl_step_v(void *p, uint32_t k) { sl_step(p, k); }
static void hw_start_v(void *p, uint32_t i, uint32_t d) {
    wheel_t *w = p; if (w->node[i].owner) { g_double_start++; hw_cancel(w, i); } hw_start(w, i, d);
}
static int  hw_cancel_v(void *p, uint32_t i) { return hw_cancel(p, i); }
static int  hw_tick_v(void *p, uint32_t *o, int m) { return hw_tick(p, o, m); }
static int  hw_next_v(void *p, uint32_t *d) { return hw_next(p, d); }
static void hw_step_v(void *p, uint32_t k) { hw_step(p, k); }

/* 정렬 리스트 불변식: 두 리스트 모두 정렬, current 는 전부 now 보다 뒤, overflow 는 전부 now 보다 앞(다음 바퀴),
 * 캐시 <= current 머리, 개수 일치. */
static int sl_check(void *p) {
    slist_t *s = p;
    for (int li = 0; li < 2; li++) {
        tlist_t *l = li ? s->ovf : s->cur;
        uint32_t cnt = 0, prev = 0;
        for (tnode_t *it = list_head(l); it != &l->end; it = it->next, cnt++) {
            if (cnt && it->expiry < prev) return 0;
            if (li == 0 && !(it->expiry > s->now)) return 0;
            if (li == 1 && !(it->expiry < s->now)) return 0;
            if (it->owner != l) return 0;
            prev = it->expiry;
        }
        if (cnt != l->n) return 0;
    }
    if (!list_empty(s->cur) && s->next_unblock > list_head(s->cur)->expiry) return 0;
    return 1;
}
static int hw_check(void *p) {
    wheel_t *w = p; uint32_t cnt = 0;
    for (uint32_t i = 0; i < w->nslots; i++)
        for (tnode_t *it = list_head(&w->slot[i]); it != &w->slot[i].end; it = it->next, cnt++)
            if ((it->expiry & w->mask) != i || it->expiry == w->now || it->owner != &w->slot[i]) return 0;
    return cnt == w->count;
}

static tq_t make_sl(slist_t *s) {
    return (tq_t){ "sorted", s, &s->ops, sl_start_v, sl_cancel_v, sl_tick_v, sl_next_v, sl_step_v, sl_check };
}
static tq_t make_hw(wheel_t *w) {
    return (tq_t){ "wheel", w, &w->ops, hw_start_v, hw_cancel_v, hw_tick_v, hw_next_v, hw_step_v, hw_check };
}

/* brute-force 참조: 64비트 시계, 매 tick 전부 훑는다. 같은 만료 시각은 start 순서(seq). */
typedef struct { uint64_t now, nseq; uint32_t nid; uint8_t act[MAXT]; uint64_t exp[MAXT], seq[MAXT]; } ref_t;
static void ref_init(ref_t *r, uint32_t now, uint32_t nid) { memset(r, 0, sizeof *r); r->now = now; r->nid = nid; }
static void ref_start(ref_t *r, uint32_t id, uint32_t d) { r->act[id] = 1; r->exp[id] = r->now + d; r->seq[id] = r->nseq++; }
static int ref_cancel(ref_t *r, uint32_t id) { int a = r->act[id]; r->act[id] = 0; return a; }
static int ref_tick(ref_t *r, uint32_t *out) {
    int n = 0;
    r->now++;
    for (uint32_t i = 0; i < r->nid; i++)
        if (r->act[i] && r->exp[i] == r->now) {
            int j = n++;
            while (j > 0 && r->seq[out[j - 1]] > r->seq[i]) { out[j] = out[j - 1]; j--; }
            out[j] = i; r->act[i] = 0;
        }
    return n;
}
static int ref_next(const ref_t *r, uint32_t *delta) {
    uint64_t best = UINT64_MAX;
    for (uint32_t i = 0; i < r->nid; i++) if (r->act[i] && r->exp[i] - r->now < best) best = r->exp[i] - r->now;
    if (best == UINT64_MAX) return 0;
    *delta = (uint32_t)best; return 1;
}

static uint32_t g_out1[MAXT], g_out2[MAXT];
static ref_t g_ref;

/* 무작위 지연: 짧은 것 위주 + 같은 tick 몰림 + 2^31 이상 + 정확히 0 / 0xFFFFFFFF 에 떨어지는 값 */
static uint32_t pick_delay(uint32_t now) {
    uint32_t r = rnd(100), d;
    if (r < 55) d = 1 + rnd(64);
    else if (r < 75) d = 1 + rnd(5000);
    else if (r < 85) d = 1 + rnd(3);
    else if (r < 93) d = 0x80000000u + rnd(0x80000000u);
    else if (r < 95) d = TICK_MAX;
    else if (r < 98) d = 0u - now;                  /* 만료가 정확히 tick 0 */
    else d = TICK_MAX - now;                        /* 만료가 정확히 0xFFFFFFFF */
    return d ? d : 1;
}

typedef struct { long ticks, expired, mism, next_mism, inv_bad; } runstat_t;
static void run_random(tq_t *q, uint32_t seed, uint32_t now0, int nticks, uint32_t nid, runstat_t *st) {
    g_rng = seed;
    ref_init(&g_ref, now0, nid);
    for (int t = 0; t < nticks; t++) {
        uint32_t nact = rnd(4);
        for (uint32_t a = 0; a < nact; a++) {
            uint32_t id = rnd(nid);
            if (g_ref.act[id]) {
                if (rnd(2)) { int c1 = ref_cancel(&g_ref, id), c2 = q->cancel(q->self, id); if (c1 != c2) st->mism++; }
            } else {
                uint32_t d = pick_delay((uint32_t)g_ref.now);
                ref_start(&g_ref, id, d); q->start(q->self, id, d);
            }
        }
        if (rnd(3) == 0 && q->cancel(q->self, nid) != 0) st->mism++;   /* 한 번도 안 쓴 id: 0 이어야 */
        if (t % 7 == 0) {
            uint32_t d1 = 0, d2 = 0;
            int e1 = ref_next(&g_ref, &d1), e2 = q->next(q->self, &d2);
            if (e1 != e2 || (e1 && d1 != d2)) st->next_mism++;
        }
        int n1 = ref_tick(&g_ref, g_out1), n2 = q->tick(q->self, g_out2, (int)MAXT);
        if (n1 != n2 || memcmp(g_out1, g_out2, (size_t)n1 * sizeof g_out1[0]) != 0) st->mism++;
        st->expired += n1; st->ticks++;
        if ((t % 16 == 0 || ((uint32_t)g_ref.now) < 4) && !q->check(q->self)) st->inv_bad++;
    }
}

static void print_list(const char *tag, tlist_t *l, const char *names) {
    printf("  %s=[", tag);
    for (tnode_t *it = list_head(l); it != &l->end; it = it->next)
        printf(" %c:%08x", names[it->id], (unsigned)it->expiry);
    printf(" ]");
}

/* ---- Part C 시나리오 ---- */
/* C1 게이트웨이: 10,000 개 수신 신호, 신호 i 는 P_i tick 마다 도착, 타임아웃 3*P_i. 도착하면 reset(cancel+start).
 * i % 97 == 0 인 신호는 tick 50 이후 끊긴다 → 그 타이머만 만료된다. 시작점은 wrap 100 tick 전. */
#define GW_N     10000u
#define GW_TICKS 200
static uint32_t gw_period(uint32_t i) { return 20 + (i * 37u) % 181u; }
typedef struct { uint64_t resets, expired, seq_hash; ops_t ops; } gwres_t;
static void run_gateway(tq_t *q, gwres_t *res) {
    memset(res, 0, sizeof *res);
    for (uint32_t i = 0; i < GW_N; i++) q->start(q->self, i, 3 * gw_period(i));
    memset(q->ops, 0, sizeof *q->ops);                  /* 초기 적재 비용은 빼고 정상 상태만 센다 */
    uint64_t h = 1469598103934665603ull;
    for (int t = 1; t <= GW_TICKS; t++) {
        for (uint32_t i = 0; i < GW_N; i++) {
            uint32_t p = gw_period(i);
            if ((uint32_t)t % p != i % p) continue;     /* 이번 tick 에 신호 i 도착? */
            if (i % 97 == 0 && t > 50) continue;        /* 끊긴 신호 */
            q->cancel(q->self, i); q->start(q->self, i, 3 * p);
            res->resets++;
        }
        int n = q->tick(q->self, g_out1, (int)MAXT);
        for (int k = 0; k < n; k++) { h = (h ^ (uint64_t)g_out1[k]) * 1099511628211ull; h = (h ^ (uint64_t)t) * 1099511628211ull; }
        res->expired += (uint64_t)n;
    }
    res->seq_hash = h; res->ops = *q->ops;
}
/* C2 배터리 BLE 노드: 주기 타이머 5개 (샘플 100, 광고 250, 배터리 1000, 로그 5000, 동기화 60000 tick),
 * tickless: 깨어날 때마다 next() 로 잘 수 있는 길이를 묻고 step(delta-1) 후 tick 1번. 600,000 tick (1 kHz 10분). */
static const uint32_t bn_period[5] = { 100, 250, 1000, 5000, 60000 };
typedef struct { uint64_t wakeups, fired, seq_hash; ops_t ops; } bnres_t;
static void run_battery(tq_t *q, bnres_t *res) {
    memset(res, 0, sizeof *res);
    for (uint32_t i = 0; i < 5; i++) q->start(q->self, i, bn_period[i]);
    memset(q->ops, 0, sizeof *q->ops);
    uint64_t h = 1469598103934665603ull, now = 0;
    while (now < 600000) {
        uint32_t d;
        if (!q->next(q->self, &d)) break;
        if (d == 0) break;                              /* 이 모델에선 생길 수 없음 (방어) */
        if (d > 1) q->step(q->self, d - 1);
        now += d;
        int n = q->tick(q->self, g_out1, (int)MAXT);
        res->wakeups++;
        for (int k = 0; k < n; k++) {
            h = (h ^ (uint64_t)g_out1[k]) * 1099511628211ull; h = (h ^ now) * 1099511628211ull;
            q->start(q->self, g_out1[k], bn_period[g_out1[k]]);      /* 주기 재장전 (delay_until 과 같음: 정확히 만료 tick 에 깼다) */
        }
        res->fired += (uint64_t)n;
        if (n == 0) break;                              /* next() 가 맞으면 깨어난 tick 에는 반드시 만료가 있다 */
    }
    res->seq_hash = h; res->ops = *q->ops;
}

int main(void) {
    static slist_t S; static wheel_t W;
    tq_t qs, qw;

    /* L0: 주어진 리스트 도우미 자가 검사 (보초, 가운데 떼기, owner) */
    tlist_t L; tnode_t a = { .id = 0 }, b = { .id = 1 }, c = { .id = 2 };
    list_init(&L); list_insert_before(&L, &L.end, &a); list_insert_before(&L, &L.end, &c); list_insert_before(&L, &c, &b);
    list_remove(&b);
    CHECK(L.n == 2 && list_head(&L) == &a && a.next == &c && c.next == &L.end && b.owner == NULL && c.owner == &L);

    /* ---------------- Part A ---------------- */
    /* A1: 0xFFFFFFF0 에서 다섯 개 — wrap 을 사이에 둔 FIFO 와 리스트 교환 */
    const char *nm = "ABCDE";
    sl_init(&S, 0xFFFFFFF0u);
    sl_start(&S, 0, 20); sl_start(&S, 1, 5); sl_start(&S, 2, TICK_MAX); sl_start(&S, 3, 5); sl_start(&S, 4, 16);
    printf("A1 now=%08x", (unsigned)S.now); print_list("cur", S.cur, nm); print_list("ovf", S.ovf, nm);
    printf("  next_unblock=%08x\n", (unsigned)S.next_unblock);
    CHECK(S.cur->n == 2 && S.ovf->n == 3);
    CHECK(S.next_unblock == 0xFFFFFFF5u);
    char seq[64] = ""; int sl_len = 0;
    for (int t = 0; t < 24; t++) {
        int n = sl_tick(&S, g_out1, (int)MAXT);
        for (int k = 0; k < n; k++) sl_len += snprintf(seq + sl_len, sizeof seq - (size_t)sl_len, "%c@%x ", nm[g_out1[k]], (unsigned)S.now);
        if (S.now == 0) { printf("A1 after tick 00000000 (swap):"); print_list("cur", S.cur, nm); print_list("ovf", S.ovf, nm);
                          printf("  next_unblock=%08x overflows=%u\n", (unsigned)S.next_unblock, (unsigned)S.overflows); }
    }
    printf("A1 expired: %s\n", seq);
    CHECK(strcmp(seq, "B@fffffff5 D@fffffff5 E@0 A@4 ") == 0);
    CHECK(S.overflows == 1 && g_assert_fail == 0);
    CHECK(S.cur->n == 1 && list_head(S.cur)->id == 2 && list_head(S.cur)->expiry == 0xFFFFFFEFu);
    uint32_t dd = 0;
    CHECK(sl_next(&S, &dd) == 1 && dd == 0xFFFFFFEFu - 8u);
    sl_free(&S);

    /* A2: 아무것도 만료 안 되는 tick 은 캐시 비교 1번 (xNextTaskUnblockTime 조기 종료) */
    sl_init(&S, 1000);
    for (uint32_t i = 0; i < 3; i++) sl_start(&S, i, 1000 + 7 * i);
    memset(&S.ops, 0, sizeof S.ops);
    for (int t = 0; t < 500; t++) sl_tick(&S, g_out1, (int)MAXT);
    printf("A2 500 idle ticks with 3 delayed: tick ops=%llu\n", (unsigned long long)S.ops.tick);
    CHECK(S.ops.tick == 500);
    sl_free(&S);

    /* A3: 머리를 cancel 하면 캐시가 stale — 그 tick 에 헛걸음 1번(ops 2) 후 고쳐진다 */
    sl_init(&S, 0);
    sl_start(&S, 0, 10); sl_start(&S, 1, 50);
    sl_cancel(&S, 0);
    uint32_t stale = S.next_unblock;
    memset(&S.ops, 0, sizeof S.ops);
    uint64_t at10 = 0;
    for (int t = 1; t <= 50; t++) { uint64_t b = S.ops.tick; int n = sl_tick(&S, g_out1, (int)MAXT); if (t == 10) at10 = S.ops.tick - b; if (t == 10) CHECK(n == 0 && S.next_unblock == 50); }
    printf("A3 cancel head: stale next_unblock=%u, ops at tick 10=%llu, total tick ops for 50 ticks=%llu\n",
           (unsigned)stale, (unsigned long long)at10, (unsigned long long)S.ops.tick);
    CHECK(stale == 10 && at10 == 2);
    CHECK(S.ops.tick == 50 + 1 + 1);                    /* 50 비교 + tick10 헛걸음 1 + tick50 머리 1 (그다음 빈 리스트 확인은 op 아님) */
    sl_free(&S);

    /* A4: 삽입 비용 = 지나간 노드 수. 오름차순 1000개 = 1000 + 0+1+...+999, 내림차순 = 1000 */
    sl_init(&S, 0);
    for (uint32_t i = 0; i < 1000; i++) sl_start(&S, i, 1 + i);
    uint64_t asc = S.ops.start; sl_free(&S);
    sl_init(&S, 0);
    for (uint32_t i = 0; i < 1000; i++) sl_start(&S, i, 1000 - i);
    uint64_t desc = S.ops.start; sl_free(&S);
    printf("A4 insert ops for 1000 timers: ascending delays=%llu  descending=%llu\n", (unsigned long long)asc, (unsigned long long)desc);
    CHECK(asc == 1000 + 999ull * 1000 / 2);
    CHECK(desc == 1000);

    /* A5: 무작위 작업 + 참조 비교 (wrap 포함) */
    runstat_t st = { 0 };
    for (uint32_t sd = 1; sd <= 24; sd++) {
        sl_init(&S, (sd % 4 == 0) ? sd * 977u : 0xFFFFFFFFu - (sd * 131u) % 3000u);
        qs = make_sl(&S);
        run_random(&qs, 0x9E3779B9u * sd, S.now, 4000, 200, &st);
        sl_free(&S);
    }
    printf("A5 sorted vs brute force: %ld ticks, %ld expirations, seq mismatches=%ld, next() mismatches=%ld, invariant bad=%ld, asserts=%d\n",
           st.ticks, st.expired, st.mism, st.next_mism, st.inv_bad, g_assert_fail);
    CHECK(st.expired > 10000 && st.mism == 0);
    CHECK(st.next_mism == 0 && st.inv_bad == 0 && g_assert_fail == 0);

    /* ---------------- Part B ---------------- */
    /* B1: 같은 다섯 개를 8슬롯 휠에서 — 같은 만료 순서, 리스트 교환 없음 */
    hw_init(&W, 8, 0xFFFFFFF0u);
    hw_start(&W, 0, 20); hw_start(&W, 1, 5); hw_start(&W, 2, TICK_MAX); hw_start(&W, 3, 5); hw_start(&W, 4, 16);
    printf("B1 wheel N=8 slots:");
    for (uint32_t i = 0; i < 8; i++) if (!list_empty(&W.slot[i])) {
        printf(" [%u]", (unsigned)i);
        for (tnode_t *it = list_head(&W.slot[i]); it != &W.slot[i].end; it = it->next) printf("%c", nm[it->id]);
    }
    printf("\n");
    seq[0] = 0; sl_len = 0;
    for (int t = 0; t < 24; t++) {
        int n = hw_tick(&W, g_out1, (int)MAXT);
        for (int k = 0; k < n; k++) sl_len += snprintf(seq + sl_len, sizeof seq - (size_t)sl_len, "%c@%x ", nm[g_out1[k]], (unsigned)W.now);
    }
    printf("B1 expired: %s\n", seq);
    CHECK(strcmp(seq, "B@fffffff5 D@fffffff5 E@0 A@4 ") == 0);
    CHECK(W.count == 1 && hw_next(&W, &dd) == 1 && dd == 0xFFFFFFEFu - 8u);
    hw_free(&W);

    /* B2: 한 바퀴(N tick) 동안 tick 비용 = N(슬롯) + n(모든 노드를 정확히 한 번). 삽입은 개당 1 */
    hw_init(&W, 256, 5);
    g_rng = 12345;
    for (uint32_t i = 0; i < 1000; i++) hw_start(&W, i, 300 + rnd(100000));
    uint64_t ins = W.ops.start;
    memset(&W.ops, 0, sizeof W.ops);
    for (int t = 0; t < 256; t++) hw_tick(&W, g_out1, (int)MAXT);
    printf("B2 N=256, 1000 far timers: insert ops=%llu, one lap (256 ticks) tick ops=%llu\n", (unsigned long long)ins, (unsigned long long)W.ops.tick);
    CHECK(ins == 1000);
    CHECK(W.ops.tick == 256 + 1000);
    CHECK(W.count == 1000);
    hw_free(&W);

    /* B3: tickless next() 비용 — 정렬 리스트 1, 휠은 빈 슬롯을 센다 */
    hw_init(&W, 256, 0); sl_init(&S, 0);
    hw_start(&W, 0, 200); sl_start(&S, 0, 200);
    memset(&W.ops, 0, sizeof W.ops); memset(&S.ops, 0, sizeof S.ops);
    uint32_t d1 = 0, d2 = 0;
    CHECK(hw_next(&W, &d1) == 1 && d1 == 200 && sl_next(&S, &d2) == 1 && d2 == 200);
    printf("B3 next() for one timer 200 ticks away: sorted ops=%llu  wheel(N=256) ops=%llu\n",
           (unsigned long long)S.ops.next, (unsigned long long)W.ops.next);
    CHECK(S.ops.next == 1 && W.ops.next == 1 + 200 + 1);
    hw_cancel(&W, 0); hw_start(&W, 0, 5000);
    memset(&W.ops, 0, sizeof W.ops);
    CHECK(hw_next(&W, &d1) == 1 && d1 == 5000 && W.ops.next == 1 + 256 + 1);   /* 한 바퀴 넘게 멀면 전체 스캔 */
    hw_free(&W); sl_free(&S);

    /* B4: 무작위 작업 + 참조 비교, 작은 휠(라운드 많음)부터 큰 휠까지 */
    const uint32_t ns[3] = { 8, 64, 1024 };
    for (int k = 0; k < 3; k++) {
        runstat_t sw = { 0 };
        for (uint32_t sd = 1; sd <= 8; sd++) {
            hw_init(&W, ns[k], (sd % 4 == 0) ? sd * 977u : 0xFFFFFFFFu - (sd * 131u) % 3000u);
            qw = make_hw(&W);
            run_random(&qw, 0x9E3779B9u * sd, W.now, 4000, 200, &sw);
            hw_free(&W);
        }
        printf("B4 wheel N=%-4u vs brute force: %ld ticks, %ld expirations, seq mismatches=%ld, next() mismatches=%ld, invariant bad=%ld\n",
               (unsigned)ns[k], sw.ticks, sw.expired, sw.mism, sw.next_mism, sw.inv_bad);
        CHECK(sw.expired > 3000 && sw.mism == 0 && sw.next_mism == 0 && sw.inv_bad == 0);
    }

    /* ---------------- Part C ---------------- */
    gwres_t gs, gw;
    sl_init(&S, 0xFFFFFFFFu - 99u); qs = make_sl(&S); run_gateway(&qs, &gs); sl_free(&S);
    hw_init(&W, 1024, 0xFFFFFFFFu - 99u); qw = make_hw(&W); run_gateway(&qw, &gw); hw_free(&W);
    printf("C1 gateway 10000 RX-timeout timers, %d ticks across wrap: resets=%llu expired=%llu\n",
           GW_TICKS, (unsigned long long)gs.resets, (unsigned long long)gs.expired);
    printf("   structure      | start ops   per reset | cancel ops | tick ops  per tick | total ops\n");
    printf("   sorted list    | %11llu %9.1f | %10llu | %8llu %9.1f | %llu\n", (unsigned long long)gs.ops.start,
           (double)gs.ops.start / (double)(gs.resets ? gs.resets : 1), (unsigned long long)gs.ops.cancel, (unsigned long long)gs.ops.tick,
           (double)gs.ops.tick / GW_TICKS, (unsigned long long)(gs.ops.start + gs.ops.cancel + gs.ops.tick));
    printf("   wheel N=1024   | %11llu %9.1f | %10llu | %8llu %9.1f | %llu\n", (unsigned long long)gw.ops.start,
           (double)gw.ops.start / (double)(gw.resets ? gw.resets : 1), (unsigned long long)gw.ops.cancel, (unsigned long long)gw.ops.tick,
           (double)gw.ops.tick / GW_TICKS, (unsigned long long)(gw.ops.start + gw.ops.cancel + gw.ops.tick));
    CHECK(gs.resets == gw.resets && gs.expired == gw.expired && gs.seq_hash == gw.seq_hash && gs.expired > 0);
    CHECK(gw.ops.start == gw.resets);
    CHECK((gw.ops.start + gw.ops.cancel + gw.ops.tick) * 100 < gs.ops.start + gs.ops.cancel + gs.ops.tick);

    bnres_t bs, bw;
    sl_init(&S, 0); qs = make_sl(&S); run_battery(&qs, &bs); sl_free(&S);
    hw_init(&W, 256, 0); qw = make_hw(&W); run_battery(&qw, &bw); hw_free(&W);
    printf("C2 battery node 5 periodic timers, tickless over 600000 ticks: wakeups=%llu fired=%llu\n",
           (unsigned long long)bs.wakeups, (unsigned long long)bs.fired);
    printf("   structure      | next ops  per wake | tick ops | start ops | total ops\n");
    printf("   sorted list    | %9llu %8.1f | %8llu | %9llu | %llu\n", (unsigned long long)bs.ops.next, (double)bs.ops.next / (double)(bs.wakeups ? bs.wakeups : 1),
           (unsigned long long)bs.ops.tick, (unsigned long long)bs.ops.start, (unsigned long long)(bs.ops.next + bs.ops.tick + bs.ops.start));
    printf("   wheel N=256    | %9llu %8.1f | %8llu | %9llu | %llu\n", (unsigned long long)bw.ops.next, (double)bw.ops.next / (double)(bw.wakeups ? bw.wakeups : 1),
           (unsigned long long)bw.ops.tick, (unsigned long long)bw.ops.start, (unsigned long long)(bw.ops.next + bw.ops.tick + bw.ops.start));
    CHECK(bs.wakeups == bw.wakeups && bs.fired == bw.fired && bs.seq_hash == bw.seq_hash && bs.wakeups > 6000);
    CHECK(bs.ops.next == bs.wakeups);
    CHECK(bw.ops.next > 20 * bs.ops.next);
    CHECK(g_assert_fail == 0 && g_double_start == 0);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
