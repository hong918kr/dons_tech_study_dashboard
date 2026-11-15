/* RTOS-M07 추가 연습 — 한 번에 검증
 *  연습 1. Varghese–Lauck Scheme 6 원문대로: 슬롯 노드에 rounds(남은 바퀴 수)를 저장하고 방문 때마다 감소
 *  연습 2. delta list (각 노드가 "앞 노드보다 몇 tick 뒤" 만 저장): tick 은 머리 1개만 감소 — wrap 이 없다
 *  연습 3. 휠 + 점유 비트맵(uint64_t): next() 가 빈 슬롯을 건너뛴다 (RTOS-B04 의 비트맵 ready list 와 같은 생각)
 *  연습 4. 2단 계층 휠(64 x 64, cascade): 4032 tick 까지를 128 슬롯으로
 * 모두 64비트 brute-force 참조와 (tick, id) 만료 순서가 같아야 한다 (시작점은 wrap 직전).
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXT 512u

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t g_rng;
static uint32_t xorshift32(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }
static uint32_t rnd(uint32_t n) { return xorshift32() % n; }

/* ---- 리스트 도우미 (solution.c 와 같은 침입형 원형 리스트) ---- */
typedef struct tlist tlist_t;
typedef struct tnode { struct tnode *next, *prev; tlist_t *owner; uint32_t expiry, aux, id; } tnode_t;
struct tlist { tnode_t end; uint32_t n; };
static void list_init(tlist_t *l) { l->end.next = l->end.prev = &l->end; l->end.owner = l; l->n = 0; }
static tnode_t *list_head(tlist_t *l) { return l->end.next; }
static void list_insert_before(tlist_t *l, tnode_t *pos, tnode_t *x) {
    x->next = pos; x->prev = pos->prev; pos->prev->next = x; pos->prev = x; x->owner = l; l->n++;
}
static void list_remove(tnode_t *x) { x->prev->next = x->next; x->next->prev = x->prev; x->owner->n--; x->owner = NULL; }

/* ---- 공통 인터페이스 + brute-force 참조 ---- */
typedef struct {
    void *self;
    void (*start)(void *, uint32_t, uint32_t);
    int  (*cancel)(void *, uint32_t);
    int  (*tick)(void *, uint32_t *);
    int  (*next)(void *, uint32_t *);     /* NULL = 이 구조는 next 를 검사하지 않음 */
} tq_t;
typedef struct { uint64_t now, nseq; uint8_t act[MAXT]; uint64_t exp[MAXT], seq[MAXT]; } ref_t;
static ref_t R;
static void ref_start(uint32_t id, uint32_t d) { R.act[id] = 1; R.exp[id] = R.now + d; R.seq[id] = R.nseq++; }
static int ref_tick(uint32_t *out) {
    int n = 0; R.now++;
    for (uint32_t i = 0; i < MAXT; i++)
        if (R.act[i] && R.exp[i] == R.now) {
            int j = n++;
            while (j > 0 && R.seq[out[j - 1]] > R.seq[i]) { out[j] = out[j - 1]; j--; }
            out[j] = i; R.act[i] = 0;
        }
    return n;
}
static int ref_next(uint32_t *d) {
    uint64_t b = UINT64_MAX;
    for (uint32_t i = 0; i < MAXT; i++) if (R.act[i] && R.exp[i] - R.now < b) b = R.exp[i] - R.now;
    if (b == UINT64_MAX) return 0;
    *d = (uint32_t)b; return 1;
}
static uint32_t o1[MAXT], o2[MAXT];
/* maxd: 지연 상한 (계층 휠은 4032). 반환: 불일치 수 */
static long compare_random(tq_t *q, uint32_t seed, uint32_t now0, int nticks, uint32_t maxd, long *expired) {
    long bad = 0;
    g_rng = seed; memset(&R, 0, sizeof R); R.now = now0;
    for (int t = 0; t < nticks; t++) {
        for (uint32_t a = rnd(4); a > 0; a--) {
            uint32_t id = rnd(MAXT);
            if (R.act[id]) { if (rnd(2)) { R.act[id] = 0; if (q->cancel(q->self, id) != 1) bad++; } }
            else {
                uint32_t r = rnd(10), d = r < 6 ? 1 + rnd(64) : r < 8 ? 1 + rnd(3) : 1 + rnd(maxd);
                if (q->cancel(q->self, id) != 0) bad++;      /* 참조는 비활성인데 구조엔 남아 있음 = 만료 누락. 리스트 보호 */
                ref_start(id, d); q->start(q->self, id, d);
            }
        }
        if (q->next && t % 5 == 0) {
            uint32_t d1 = 0, d2 = 0; int e1 = ref_next(&d1), e2 = q->next(q->self, &d2);
            if (e1 != e2 || (e1 && d1 != d2)) bad++;
        }
        int n1 = ref_tick(o1), n2 = q->tick(q->self, o2);
        if (n1 != n2 || memcmp(o1, o2, (size_t)n1 * sizeof o1[0])) bad++;
        *expired += n1;
    }
    return bad;
}

/* ================= 연습 1: Scheme 6 원문 — rounds 저장 ================= */
typedef struct { tlist_t slot[64]; uint32_t mask, bits, now; tnode_t node[MAXT]; uint64_t writes, visits; } rw_t;
static void rw_init(rw_t *w, uint32_t bits, uint32_t now) {
    memset(w, 0, sizeof *w); w->bits = bits; w->mask = (1u << bits) - 1; w->now = now;
    for (uint32_t i = 0; i <= w->mask; i++) list_init(&w->slot[i]);
    for (uint32_t i = 0; i < MAXT; i++) w->node[i].id = i;
}
static void rw_start(void *p, uint32_t id, uint32_t d) {
    rw_t *w = p; tnode_t *x = &w->node[id];
    x->aux = (d - 1) >> w->bits;                       /* 첫 방문(1 ~ N tick 뒤) 이후 더 돌아야 할 바퀴 수 */
    tlist_t *l = &w->slot[(w->now + d) & w->mask];
    list_insert_before(l, &l->end, x);
}
static int rw_cancel(void *p, uint32_t id) { rw_t *w = p; if (!w->node[id].owner) return 0; list_remove(&w->node[id]); return 1; }
static int rw_tick(void *p, uint32_t *out) {
    rw_t *w = p; int n = 0; w->now++;
    tlist_t *l = &w->slot[w->now & w->mask];
    for (tnode_t *it = list_head(l), *nx; it != &l->end; it = nx) {
        nx = it->next; w->visits++;
        if (it->aux == 0) { list_remove(it); out[n++] = it->id; }
        else { it->aux--; w->writes++; }                /* 방문마다 쓰기 1번 — 절대 expiry 저장 방식엔 없다 */
    }
    return n;
}

/* ================= 연습 2: delta list ================= */
typedef struct { tlist_t l; tnode_t node[MAXT]; uint64_t ins_visits; } dl_t;
static void dl_init(dl_t *q) { memset(q, 0, sizeof *q); list_init(&q->l); for (uint32_t i = 0; i < MAXT; i++) q->node[i].id = i; }
static void dl_start(void *p, uint32_t id, uint32_t d) {
    dl_t *q = p; tnode_t *it = list_head(&q->l);
    while (it != &q->l.end && d >= it->aux) { d -= it->aux; it = it->next; q->ins_visits++; }   /* >= : 같은 시각은 뒤로(FIFO) */
    tnode_t *x = &q->node[id]; x->aux = d;
    list_insert_before(&q->l, it, x);
    if (it != &q->l.end) it->aux -= d;                  /* 뒤 노드의 delta 를 내 몫만큼 줄인다 */
}
static int dl_cancel(void *p, uint32_t id) {
    dl_t *q = p; tnode_t *x = &q->node[id];
    if (!x->owner) return 0;
    if (x->next != &q->l.end) x->next->aux += x->aux;   /* 내 delta 를 뒤 노드에 돌려준다 — 빠뜨리면 뒤가 전부 당겨진다 */
    list_remove(x); return 1;
}
static int dl_tick(void *p, uint32_t *out) {
    dl_t *q = p; int n = 0;
    if (q->l.n == 0) return 0;
    list_head(&q->l)->aux--;                            /* tick 당 머리 하나만 감소 */
    while (q->l.n && list_head(&q->l)->aux == 0) { tnode_t *h = list_head(&q->l); list_remove(h); out[n++] = h->id; }
    return n;
}
static int dl_next(void *p, uint32_t *d) { dl_t *q = p; if (!q->l.n) return 0; *d = list_head(&q->l)->aux; return 1; }

/* ================= 연습 3: 64슬롯 휠 + 점유 비트맵 ================= */
static int ctz64(uint64_t v) {                          /* v != 0. de Bruijn — 내장 함수 없이 */
    static const int tab[64] = {
        0, 1, 48, 2, 57, 49, 28, 3, 61, 58, 50, 42, 38, 29, 17, 4, 62, 55, 59, 36, 53, 51, 43, 22, 45, 39, 33, 30, 24, 18, 12, 5,
        63, 47, 56, 27, 60, 41, 37, 16, 54, 35, 52, 21, 44, 32, 23, 11, 46, 26, 40, 15, 34, 20, 31, 10, 25, 14, 19, 9, 13, 8, 7, 6 };
    return tab[((v & (0 - v)) * 0x03F79D71B4CB0A89ull) >> 58];
}
typedef struct { tlist_t slot[64]; uint64_t occ; uint32_t now, count; tnode_t node[MAXT]; uint64_t next_ops; int use_bitmap; } bw_t;
static void bw_init(bw_t *w, uint32_t now, int use_bitmap) {
    memset(w, 0, sizeof *w); w->now = now; w->use_bitmap = use_bitmap;
    for (int i = 0; i < 64; i++) list_init(&w->slot[i]);
    for (uint32_t i = 0; i < MAXT; i++) w->node[i].id = i;
}
static void bw_start(void *p, uint32_t id, uint32_t d) {
    bw_t *w = p; tnode_t *x = &w->node[id]; x->expiry = w->now + d;
    uint32_t s = x->expiry & 63; list_insert_before(&w->slot[s], &w->slot[s].end, x);
    w->occ |= 1ull << s; w->count++;
}
static int bw_cancel(void *p, uint32_t id) {
    bw_t *w = p; tnode_t *x = &w->node[id]; if (!x->owner) return 0;
    uint32_t s = x->expiry & 63; list_remove(x); w->count--;
    if (w->slot[s].n == 0) w->occ &= ~(1ull << s);
    return 1;
}
static int bw_tick(void *p, uint32_t *out) {
    bw_t *w = p; int n = 0; w->now++;
    uint32_t s = w->now & 63; tlist_t *l = &w->slot[s];
    for (tnode_t *it = list_head(l), *nx; it != &l->end; it = nx) {
        nx = it->next;
        if (it->expiry == w->now) { list_remove(it); w->count--; out[n++] = it->id; }
    }
    if (l->n == 0) w->occ &= ~(1ull << s);
    return n;
}
static int bw_next(void *p, uint32_t *delta) {
    bw_t *w = p; w->next_ops++;
    if (!w->count) return 0;
    uint32_t best = 0xFFFFFFFFu;
    if (!w->use_bitmap) {                                /* solution.c 의 hw_next 와 같은 선형 스캔 */
        for (uint32_t k = 1; k <= 64; k++) {
            tlist_t *l = &w->slot[(w->now + k) & 63]; w->next_ops++;
            for (tnode_t *it = list_head(l); it != &l->end; it = it->next) { uint32_t d = it->expiry - w->now; w->next_ops++; if (d < best) best = d; }
            if (best == k) break;
        }
    } else {
        uint32_t base = (w->now + 1) & 63;               /* 비트 0 = 슬롯 now+1 이 되게 회전 */
        uint64_t m = base ? (w->occ >> base) | (w->occ << (64 - base)) : w->occ;
        while (m) {
            uint32_t k = (uint32_t)ctz64(m) + 1; m &= m - 1;
            tlist_t *l = &w->slot[(w->now + k) & 63]; w->next_ops++;
            for (tnode_t *it = list_head(l); it != &l->end; it = it->next) { uint32_t d = it->expiry - w->now; w->next_ops++; if (d < best) best = d; }
            if (best == k) break;
        }
    }
    *delta = best; return 1;
}

/* ================= 연습 4: 2단 계층 휠 64 x 64 ================= */
typedef struct { tlist_t l0[64], l1[64]; uint32_t now; tnode_t node[MAXT]; uint64_t moved, tick_ops; } hh_t;
static void hh_init(hh_t *h, uint32_t now) {
    memset(h, 0, sizeof *h); h->now = now;
    for (int i = 0; i < 64; i++) { list_init(&h->l0[i]); list_init(&h->l1[i]); }
    for (uint32_t i = 0; i < MAXT; i++) h->node[i].id = i;
}
static void hh_start(void *p, uint32_t id, uint32_t d) {        /* d <= 4032 */
    hh_t *h = p; tnode_t *x = &h->node[id]; x->expiry = h->now + d;
    tlist_t *l = ((x->expiry ^ h->now) >> 6) == 0 ? &h->l0[x->expiry & 63]       /* 같은 64-tick 블록 → 1단 */
                                                   : &h->l1[(x->expiry >> 6) & 63]; /* 다른 블록 → 2단 (블록 번호로) */
    list_insert_before(l, &l->end, x);
}
static int hh_cancel(void *p, uint32_t id) { hh_t *h = p; if (!h->node[id].owner) return 0; list_remove(&h->node[id]); return 1; }
static int hh_tick(void *p, uint32_t *out) {
    hh_t *h = p; int n = 0; h->now++; h->tick_ops++;
    if ((h->now & 63) == 0) {                                   /* 새 블록 시작: 2단 슬롯 하나를 1단으로 내린다(cascade) */
        tlist_t *src = &h->l1[(h->now >> 6) & 63];
        while (src->n) {
            tnode_t *x = list_head(src); list_remove(x);
            list_insert_before(&h->l0[x->expiry & 63], &h->l0[x->expiry & 63].end, x);
            h->moved++; h->tick_ops++;
        }
    }
    tlist_t *l = &h->l0[h->now & 63];                           /* 1단 슬롯의 노드는 전부 지금 만료 */
    while (l->n) { tnode_t *x = list_head(l); list_remove(x); out[n++] = x->id; h->tick_ops++; }
    return n;
}

int main(void) {
    static rw_t RW; static dl_t DL; static bw_t BW; static hh_t HH;
    long exp1 = 0, bad;

    /* 연습 1 */
    bad = 0; uint64_t writes = 0, visits = 0;
    for (uint32_t sd = 1; sd <= 6; sd++) {
        rw_init(&RW, 3, 0xFFFFFFFFu - sd * 300u);
        tq_t q = { &RW, rw_start, rw_cancel, rw_tick, NULL };
        bad += compare_random(&q, 0xA5A5u + sd, RW.now, 3000, 5000, &exp1);
        writes += RW.writes; visits += RW.visits;
    }
    printf("ex1 rounds wheel N=8: %ld expirations, mismatches=%ld, slot visits=%llu, rounds writes=%llu (%.0f%% of visits)\n",
           exp1, bad, (unsigned long long)visits, (unsigned long long)writes, 100.0 * (double)writes / (double)visits);
    CHECK(bad == 0 && exp1 > 1000);
    CHECK(writes > 0 && writes < visits);
    rw_init(&RW, 3, 0);                                        /* 손 계산: d = 5, 8, 9, 17 */
    rw_start(&RW, 0, 5); rw_start(&RW, 1, 8); rw_start(&RW, 2, 9); rw_start(&RW, 3, 17);
    CHECK(RW.node[0].aux == 0 && RW.node[1].aux == 0 && RW.node[2].aux == 1 && RW.node[3].aux == 2);

    /* 연습 2 */
    long exp2 = 0; bad = 0;
    for (uint32_t sd = 1; sd <= 6; sd++) {
        dl_init(&DL);
        tq_t q = { &DL, dl_start, dl_cancel, dl_tick, dl_next };
        bad += compare_random(&q, 0xBEEFu + sd, 0xFFFFFFFFu - sd * 500u, 3000, 0xFFFFFFF0u, &exp2);
    }
    printf("ex2 delta list: %ld expirations, mismatches (order + next) =%ld\n", exp2, bad);
    CHECK(bad == 0 && exp2 > 1000);
    dl_init(&DL);                                              /* 손 계산: 10, 4, 10, 15 → delta 4,6,0,5 */
    dl_start(&DL, 0, 10); dl_start(&DL, 1, 4); dl_start(&DL, 2, 10); dl_start(&DL, 3, 15);
    char buf[64]; int len = 0;
    for (tnode_t *it = list_head(&DL.l); it != &DL.l.end; it = it->next) len += snprintf(buf + len, sizeof buf - (size_t)len, "%u:%u ", (unsigned)it->id, (unsigned)it->aux);
    printf("ex2 deltas after start(10,4,10,15): %s\n", buf);
    CHECK(strcmp(buf, "1:4 0:6 2:0 3:5 ") == 0);
    dl_cancel(&DL, 0);
    len = 0; for (tnode_t *it = list_head(&DL.l); it != &DL.l.end; it = it->next) len += snprintf(buf + len, sizeof buf - (size_t)len, "%u:%u ", (unsigned)it->id, (unsigned)it->aux);
    printf("ex2 after cancel(id0)        : %s\n", buf);
    CHECK(strcmp(buf, "1:4 2:6 3:5 ") == 0);

    /* 연습 3 */
    long exp3 = 0; bad = 0;
    for (int bm = 0; bm < 2; bm++)
        for (uint32_t sd = 1; sd <= 4; sd++) {
            bw_init(&BW, 0xFFFFFFFFu - sd * 200u, bm);
            tq_t q = { &BW, bw_start, bw_cancel, bw_tick, bw_next };
            bad += compare_random(&q, 0xC0DEu + sd, BW.now, 3000, 100000, &exp3);
        }
    printf("ex3 64-slot wheel +/- bitmap: %ld expirations, mismatches=%ld\n", exp3, bad);
    CHECK(bad == 0);
    uint64_t ops_plain = 0, ops_bm = 0;
    for (int bm = 0; bm < 2; bm++) {                           /* 배터리 노드 축소판: 주기 50/60/1000 의 타이머 3개, tickless */
        static const uint32_t per[3] = { 50, 60, 1000 };
        bw_init(&BW, 0, bm);
        for (uint32_t i = 0; i < 3; i++) bw_start(&BW, i, per[i]);
        BW.next_ops = 0;
        for (int wake = 0; wake < 500; wake++) {
            uint32_t d; if (!bw_next(&BW, &d)) break;
            BW.now += d - 1;
            int n = bw_tick(&BW, o1);
            for (int k = 0; k < n; k++) bw_start(&BW, o1[k], per[o1[k]]);
        }
        if (bm) ops_bm = BW.next_ops; else ops_plain = BW.next_ops;
    }
    printf("ex3 tickless 500 wakeups: next() ops linear scan=%llu  bitmap=%llu\n", (unsigned long long)ops_plain, (unsigned long long)ops_bm);
    CHECK(ops_bm * 5 < ops_plain);
    CHECK(ctz64(1) == 0 && ctz64(0x8000000000000000ull) == 63 && ctz64(0x50) == 4);

    /* 연습 4 */
    long exp4 = 0; bad = 0; uint64_t moved = 0, tops = 0, ticks = 0;
    for (uint32_t sd = 1; sd <= 6; sd++) {
        hh_init(&HH, 0xFFFFFFFFu - sd * 700u);
        tq_t q = { &HH, hh_start, hh_cancel, hh_tick, NULL };
        bad += compare_random(&q, 0xD00Du + sd, HH.now, 6000, 4032, &exp4);
        moved += HH.moved; tops += HH.tick_ops; ticks += 6000;
    }
    printf("ex4 2-level wheel 64x64: %ld expirations, mismatches=%ld, cascaded nodes=%llu, tick ops/tick=%.2f\n",
           exp4, bad, (unsigned long long)moved, (double)tops / (double)ticks);
    CHECK(bad == 0 && exp4 > 1000 && moved > 0);
    hh_init(&HH, 60);                                          /* 손 계산: now=60, d=3 → 63 (1단), d=4 → 64 (2단 슬롯 1) */
    hh_start(&HH, 0, 3); hh_start(&HH, 1, 4); hh_start(&HH, 2, 4032);
    CHECK(HH.node[0].owner == &HH.l0[63] && HH.node[1].owner == &HH.l1[1] && HH.node[2].owner == &HH.l1[(4092u >> 6) & 63]);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
