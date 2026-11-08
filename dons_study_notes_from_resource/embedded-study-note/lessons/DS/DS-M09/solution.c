/* DS-M09 계층 비트맵: O(1) 최고 우선순위 탐색
 * 우선순위 p = 0..1023. 숫자가 클수록 급하다 (FreeRTOS 관례) → "최고 우선순위" = set 된 가장 큰 p.
 * 2단계: top (32비트 요약) + leaf[32] (32비트 × 32). 비트 p 는 leaf[p >> 5] 의 비트 (p & 31).
 * 불변식: top 의 비트 g == (leaf[g] != 0). malloc 없음 — 구조체 하나 132바이트.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define HB_BITS   1024u
#define HB_GROUPS 32u
#define HB_NONE   (-1)

typedef struct {
    uint32_t top;                        /* 비트 g = 그룹 g (우선순위 32g..32g+31) 에 set 비트가 하나라도 있음 */
    uint32_t leaf[HB_GROUPS];            /* leaf[g] 의 비트 b = 우선순위 32g+b */
} hbm_t;

#define GRP(p) ((uint32_t)(p) >> 5)
#define BIT(p) ((uint32_t)(p) & 31u)

/* x != 0 일 때만 부른다: __builtin_clz(0) / __builtin_ctz(0) 는 UB (DS-B08 참고) */
static uint32_t fls32(uint32_t x) { return 31u - (uint32_t)__builtin_clz(x); }  /* 가장 높은 1비트 위치 */
static uint32_t ffs32(uint32_t x) { return (uint32_t)__builtin_ctz(x); }        /* 가장 낮은 1비트 위치 */

static void hb_init(hbm_t *h) { memset(h, 0, sizeof *h); }

/* ---------------- Part A: set / test / highest (CLZ 두 번) ---------------- */
static void hb_set(hbm_t *h, uint32_t p) {
    h->leaf[GRP(p)] |= 1u << BIT(p);
    h->top |= 1u << GRP(p);
}

static bool hb_test(const hbm_t *h, uint32_t p) {
    return (h->leaf[GRP(p)] >> BIT(p)) & 1u;
}

/* set 된 가장 큰 p, 비었으면 HB_NONE. 루프 없음: CLZ 2번 + 로드 2번 */
static int hb_highest(const hbm_t *h) {
    if (h->top == 0) return HB_NONE;
    uint32_t g = fls32(h->top);
    return (int)(g * 32u + fls32(h->leaf[g]));
}

/* ---------------- Part B: clear (요약 동기화) / lowest / prev ---------------- */
/* 그룹의 마지막 비트를 지우면 요약 비트도 지운다 — 안 하면 hb_highest 가 clz(0) 를 부른다 */
static void hb_clear(hbm_t *h, uint32_t p) {
    uint32_t g = GRP(p);
    h->leaf[g] &= ~(1u << BIT(p));
    if (h->leaf[g] == 0) h->top &= ~(1u << g);
}

/* set 된 가장 작은 p (CTZ 2번), 비었으면 HB_NONE */
static int hb_lowest(const hbm_t *h) {
    if (h->top == 0) return HB_NONE;
    uint32_t g = ffs32(h->top);
    return (int)(g * 32u + ffs32(h->leaf[g]));
}

/* p 보다 엄격히 작은 set 비트 중 가장 큰 것 (내림차순 순회용). p = 0..1024, 없으면 HB_NONE */
static int hb_prev(const hbm_t *h, uint32_t p) {
    if (p == 0) return HB_NONE;
    if (p > HB_BITS) p = HB_BITS;
    uint32_t q = p - 1u, g = GRP(q);
    uint32_t m = h->leaf[g] & (0xFFFFFFFFu >> (31u - BIT(q)));   /* 비트 0..BIT(q) — 시프트는 0..31 */
    if (m) return (int)(g * 32u + fls32(m));
    uint32_t t = h->top & ((1u << g) - 1u);                        /* g 보다 아래 그룹들, g = 0 이면 0 */
    if (t == 0) return HB_NONE;
    g = fls32(t);
    return (int)(g * 32u + fls32(h->leaf[g]));
}

/* ---------------- Part C: O(1) 준비 큐 (RTOS 스케줄러) ---------------- */
/* 최소 침입형 이중 리스트 (DS-M01 에서 자세히) */
typedef struct node { struct node *prev, *next; } node_t;
static void list_init(node_t *s) { s->prev = s->next = s; }
static bool list_empty(const node_t *s) { return s->next == s; }
static void list_add_tail(node_t *s, node_t *n) { n->prev = s->prev; n->next = s; s->prev->next = n; s->prev = n; }
static void list_del(node_t *n) { n->prev->next = n->next; n->next->prev = n->prev; n->prev = n->next = n; }

typedef struct {
    node_t   link;                       /* 반드시 첫 멤버: (tcb_t *)node 캐스트 = container_of 의 특수형 */
    uint16_t id, prio;
    bool     ready;
} tcb_t;

typedef struct {
    hbm_t  map;                          /* 비트 p == (q[p] 가 비어 있지 않음) */
    node_t q[HB_BITS];                   /* 우선순위별 FIFO */
} runq_t;

static void rq_init(runq_t *rq) {
    hb_init(&rq->map);
    for (uint32_t p = 0; p < HB_BITS; p++) list_init(&rq->q[p]);
}

/* 준비 상태로: 같은 우선순위 줄의 맨 뒤에 붙이고 비트를 켠다. 이미 ready 면 무시 */
static void rq_ready(runq_t *rq, tcb_t *t) {
    if (t->ready) return;
    list_add_tail(&rq->q[t->prio], &t->link);
    hb_set(&rq->map, t->prio);
    t->ready = true;
}

/* 블록/삭제: 줄에서 빼고, 줄이 비면 비트를 끈다 (FreeRTOS taskRESET_READY_PRIORITY 와 같은 규칙) */
static void rq_unready(runq_t *rq, tcb_t *t) {
    if (!t->ready) return;
    list_del(&t->link);
    if (list_empty(&rq->q[t->prio])) hb_clear(&rq->map, t->prio);
    t->ready = false;
}

/* 다음에 돌 태스크: 최고 우선순위 줄의 맨 앞. 큐에서 빼지는 않는다. 없으면 NULL */
static tcb_t *rq_pick(const runq_t *rq) {
    int p = hb_highest(&rq->map);
    if (p == HB_NONE) return NULL;
    return (tcb_t *)rq->q[p].next;
}

/* (주어짐) 라운드 로빈: 실행 중 태스크를 같은 줄 맨 뒤로 / 우선순위 변경 */
static void rq_yield(runq_t *rq, tcb_t *t) { if (t->ready) { rq_unready(rq, t); rq_ready(rq, t); } }
static void rq_set_prio(runq_t *rq, tcb_t *t, uint16_t prio) {
    bool was = t->ready;
    rq_unready(rq, t);
    t->prio = prio;
    if (was) rq_ready(rq, t);
}

/* ---------------- 검사 도구 · 참조 모델 (주어짐) ---------------- */
/* 불변식: 요약 비트 g == (leaf[g] != 0) */
static bool hb_check(const hbm_t *h) {
    for (uint32_t g = 0; g < HB_GROUPS; g++)
        if ((((h->top >> g) & 1u) != 0) != (h->leaf[g] != 0)) return false;
    return true;
}

/* 단순 선형 탐색 1: 비트 하나씩, 위에서 아래로 (최악 1024번) */
static int lin_highest_bits(const hbm_t *h) {
    for (uint32_t p = HB_BITS; p-- > 0;)
        if ((h->leaf[GRP(p)] >> BIT(p)) & 1u) return (int)p;
    return HB_NONE;
}
/* 단순 선형 탐색 2: 워드 단위 + CLZ (최악 32워드) — 요약 없이 DS-B08 방식 */
static int lin_highest_words(const hbm_t *h) {
    for (uint32_t g = HB_GROUPS; g-- > 0;)
        if (h->leaf[g]) return (int)(g * 32u + fls32(h->leaf[g]));
    return HB_NONE;
}

static bool g_ref[HB_BITS];              /* 참조 모델: 우선순위별 bool */
static int ref_highest(void) { for (int p = HB_BITS - 1; p >= 0; p--) if (g_ref[p]) return p; return HB_NONE; }
static int ref_lowest(void)  { for (int p = 0; p < (int)HB_BITS; p++) if (g_ref[p]) return p; return HB_NONE; }
static int ref_prev(uint32_t p) { if (p > HB_BITS) p = HB_BITS; while (p-- > 0) if (g_ref[p]) return (int)p; return HB_NONE; }

/* 준비 큐 불변식: 비트 p == (q[p] 비어 있지 않음), 각 줄의 태스크 prio == p, ready 개수 일치 */
static bool rq_check(const runq_t *rq, uint32_t want_ready) {
    if (!hb_check(&rq->map)) return false;
    uint32_t cnt = 0;
    for (uint32_t p = 0; p < HB_BITS; p++) {
        if (hb_test(&rq->map, p) == list_empty(&rq->q[p])) return false;
        for (const node_t *n = rq->q[p].next; n != &rq->q[p]; n = n->next) {
            const tcb_t *t = (const tcb_t *)n;
            if (t->prio != p || !t->ready || n->next->prev != n) return false;
            if (++cnt > want_ready) return false;
        }
    }
    return cnt == want_ready;
}

static uint32_t rng_state = 1u;
static uint32_t xorshift32(void) {
    uint32_t x = rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return rng_state = x;
}

#define NTASK 200u
static hbm_t  g_h;
static runq_t g_rq;
static tcb_t  g_task[NTASK];
static uint32_t g_seq[NTASK];            /* 참조 모델: 줄에 들어간 순번 (FIFO) */

/* 참조 스케줄러: ready 중 prio 최대, 같으면 먼저 들어온(seq 작은) 태스크 */
static int ref_pick(void) {
    int best = -1;
    for (uint32_t i = 0; i < NTASK; i++) {
        if (!g_task[i].ready) continue;
        if (best < 0 || g_task[i].prio > g_task[best].prio ||
            (g_task[i].prio == g_task[best].prio && g_seq[i] < g_seq[best])) best = (int)i;
    }
    return best;
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    hbm_t *h = &g_h;

    /* Part A: 정해진 경우 — 그룹 경계(31/32), 양 끝(0/1023), 중복 set */
    {
        hb_init(h);
        CHECK(hb_highest(h) == HB_NONE && hb_check(h));
        hb_set(h, 5);
        CHECK(hb_highest(h) == 5 && hb_test(h, 5) && !hb_test(h, 4) && h->top == 1u);
        hb_set(h, 31); hb_set(h, 32);
        CHECK(hb_highest(h) == 32 && h->top == 3u && h->leaf[0] == 0x80000020u && h->leaf[1] == 1u);
        hb_set(h, 1023);
        CHECK(hb_highest(h) == 1023 && h->top == 0x80000003u && hb_check(h));
        hb_set(h, 1023);                                     /* 중복 set 은 아무 변화 없음 */
        CHECK(h->leaf[31] == 0x80000000u && hb_check(h));
        hb_init(h); hb_set(h, 0);
        CHECK(hb_highest(h) == 0 && h->top == 1u && h->leaf[0] == 1u);
    }
    /* Part A: 모든 단일 비트에서 highest == p, 두 선형 탐색과 일치 */
    {
        bool ok = true;
        for (uint32_t p = 0; p < HB_BITS && ok; p++) {
            hb_init(h); hb_set(h, p);
            ok = hb_highest(h) == (int)p && lin_highest_bits(h) == (int)p &&
                 lin_highest_words(h) == (int)p && hb_check(h);
        }
        CHECK(ok);
    }

    /* Part B: clear 의 요약 동기화 — 그룹의 마지막 비트를 지우면 top 비트도 꺼진다 */
    {
        hb_init(h);
        hb_set(h, 40); hb_set(h, 63); hb_set(h, 700);
        hb_clear(h, 700);
        CHECK(hb_highest(h) == 63 && h->top == 2u && hb_check(h));
        hb_clear(h, 63);
        CHECK(hb_highest(h) == 40 && h->top == 2u);           /* 그룹 1 에 40 이 아직 남음 */
        hb_clear(h, 40);
        CHECK(h->top == 0 && hb_highest(h) == HB_NONE && hb_check(h));
        hb_clear(h, 40); hb_clear(h, 999);                    /* 없는 비트 clear 는 무해 */
        CHECK(h->top == 0 && hb_check(h));
    }
    /* Part B: lowest / prev 의 경계 */
    {
        hb_init(h);
        CHECK(hb_lowest(h) == HB_NONE && hb_prev(h, 1024) == HB_NONE && hb_prev(h, 0) == HB_NONE);
        hb_set(h, 0); hb_set(h, 31); hb_set(h, 32); hb_set(h, 1023);
        CHECK(hb_lowest(h) == 0 && hb_prev(h, 1024) == 1023 && hb_prev(h, 2000) == 1023);
        CHECK(hb_prev(h, 1023) == 32 && hb_prev(h, 33) == 32 && hb_prev(h, 32) == 31);
        CHECK(hb_prev(h, 31) == 0 && hb_prev(h, 1) == 0 && hb_prev(h, 0) == HB_NONE);
        int seen[8], k = 0;                                    /* 내림차순 순회 */
        for (int p = hb_highest(h); p != HB_NONE && k < 8; p = hb_prev(h, (uint32_t)p)) seen[k++] = p;
        CHECK(k == 4 && seen[0] == 1023 && seen[1] == 32 && seen[2] == 31 && seen[3] == 0);
    }
    /* Part B: 랜덤 set/clear 60000회 — bool 배열 참조 모델과 매 연산 대조 */
    {
        static const uint32_t G[6] = { 0, 1, 7, 15, 30, 31 }, B[4] = { 0, 1, 17, 31 };
        hb_init(h);
        memset(g_ref, 0, sizeof g_ref);
        rng_state = 0xC0FFEEu;
        bool ok = true;
        uint32_t sets = 0, clears = 0, grp_empty = 0, drains = 0;
        for (int op = 0; op < 60000 && ok; op++) {
            if ((op & 4095) == 4095) {                         /* 주기적으로 전부 비우기: 비는 순간까지 검사 */
                for (uint32_t p = 0; p < HB_BITS && ok; p++)
                    if (g_ref[p]) { hb_clear(h, p); g_ref[p] = false; ok = hb_check(h) && hb_highest(h) == ref_highest(); }
                ok = ok && h->top == 0 && hb_highest(h) == HB_NONE && hb_lowest(h) == HB_NONE;
                drains++;
                continue;
            }
            uint32_t r = xorshift32();
            /* 7/8 은 핫스팟 24칸(그룹 경계 비트 0/31 포함) — 그룹이 자주 비었다 찼다 한다. 1/8 은 전 범위 */
            uint32_t p = (r & 7u) ? G[(r >> 8) % 6u] * 32u + B[(r >> 12) & 3u] : (r >> 12) % HB_BITS;
            uint32_t top_before = h->top;
            if ((r >> 4) & 1u) { hb_set(h, p);   g_ref[p] = true;  sets++; }
            else               { hb_clear(h, p); g_ref[p] = false; clears++; }
            if (top_before & ~h->top) grp_empty++;              /* 요약 비트가 꺼진 순간 */
            uint32_t q = (r >> 22) % (HB_BITS + 2u);           /* prev 질의 위치 0..1025 */
            ok = hb_check(h) && hb_highest(h) == ref_highest() && hb_lowest(h) == ref_lowest() &&
                 hb_prev(h, q) == ref_prev(q) && hb_test(h, p) == g_ref[p];
        }
        CHECK(ok);
        printf("[B] random 60000 set/clear vs bool[1024] reference: %s (sets=%u clears=%u, summary-bit clears=%u, full drains=%u)\n",
               ok ? "match" : "MISMATCH", sets, clears, grp_empty, drains);
    }

    /* Part C: 준비 큐 — 우선순위 선점 + 같은 우선순위 FIFO + yield 라운드 로빈 */
    {
        runq_t *rq = &g_rq;
        rq_init(rq);
        tcb_t *t = g_task;
        for (uint16_t i = 0; i < 5; i++) t[i] = (tcb_t){ .id = i, .prio = 0, .ready = false };
        t[0].prio = 0; t[1].prio = 100; t[2].prio = 100; t[3].prio = 900; t[4].prio = 100;
        CHECK(rq_pick(rq) == NULL);
        rq_ready(rq, &t[0]); rq_ready(rq, &t[1]); rq_ready(rq, &t[2]);
        CHECK(rq_pick(rq) == &t[1] && rq_check(rq, 3));
        rq_ready(rq, &t[3]);                                  /* 높은 우선순위 깨어남 → 선점 */
        CHECK(rq_pick(rq) == &t[3]);
        rq_unready(rq, &t[3]);                                /* 블록 → 줄이 비어 비트 900 꺼짐 */
        CHECK(rq_pick(rq) == &t[1] && !hb_test(&rq->map, 900) && rq_check(rq, 3));
        rq_yield(rq, &t[1]);                                  /* 같은 우선순위끼리 순서 교대 */
        CHECK(rq_pick(rq) == &t[2]);
        rq_ready(rq, &t[4]); rq_yield(rq, &t[2]);
        CHECK(rq_pick(rq) == &t[1] && rq_check(rq, 4));       /* 줄: 1, 4, 2 */
        rq_unready(rq, &t[1]); rq_unready(rq, &t[4]); rq_unready(rq, &t[2]);
        CHECK(rq_pick(rq) == &t[0] && !hb_test(&rq->map, 100) && rq_check(rq, 1));
        rq_ready(rq, &t[0]);                                  /* 이미 ready → 중복 삽입 안 됨 */
        CHECK(rq_check(rq, 1));
        rq_unready(rq, &t[0]);
        CHECK(rq_pick(rq) == NULL && rq->map.top == 0 && rq_check(rq, 0));
    }
    /* Part C: 랜덤 20000 연산 — 참조 스케줄러(선형 스캔 + seq)와 매번 대조 */
    {
        runq_t *rq = &g_rq;
        rq_init(rq);
        rng_state = 2024u;
        uint32_t seq = 0, nready = 0;
        for (uint16_t i = 0; i < NTASK; i++) g_task[i] = (tcb_t){ .id = i, .prio = 0, .ready = false };
        bool ok = true;
        for (int op = 0; op < 20000 && ok; op++) {
            uint32_t r = xorshift32();
            tcb_t *t = &g_task[(r >> 8) % NTASK];
            /* 우선순위 32가지만 쓴다 (8 대역 × {0,31,32,127}) → 같은 줄이 길어지고 그룹 경계·1023 도 걸린다 */
            static const uint16_t step[4] = { 0, 31, 32, 127 };
            uint16_t prio = (uint16_t)(((r >> 16) % 8u) * 128u + step[(r >> 20) & 3u]);
            switch (r & 3u) {
            case 0: case 1:                                   /* 깨우기 */
                if (!t->ready) { g_seq[t->id] = seq++; nready++; }
                rq_ready(rq, t);
                break;
            case 2:                                           /* 블록 */
                if (t->ready) nready--;
                rq_unready(rq, t);
                break;
            default: {                                        /* 실행 중 태스크가 yield, 또는 우선순위 변경 */
                tcb_t *cur = rq_pick(rq);
                if (cur && ((r >> 6) & 1u)) { rq_yield(rq, cur); g_seq[cur->id] = seq++; }
                else { if (t->ready) g_seq[t->id] = seq++; rq_set_prio(rq, t, prio); }
            } break;
            }
            int want = ref_pick();
            tcb_t *got = rq_pick(rq);
            ok = rq_check(rq, nready) && (want < 0 ? got == NULL : got == &g_task[want]);
        }
        CHECK(ok);
        printf("[C] random 20000 ready/block/yield/set_prio vs reference scheduler: %s (final ready=%u)\n",
               ok ? "match" : "MISMATCH", nready);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
