/* DS-M08 추가 연습 — 한 번에 검증
 * 연습 1 점진적 리해싱 / 연습 2 PSL 상한 / 연습 3 정규형(삽입 순서 무관) 배치 / 연습 4 리사이즈 thrash
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAX_CAP 4096u

typedef struct {
    uint32_t key, val;
    uint16_t psl;                        /* 0 = EMPTY */
    uint8_t  dead;                       /* 연습 1: 이주 중인 옛 표에서만 쓰는 "지워짐" 표시 */
} slot_t;

typedef struct { slot_t *slot; uint32_t cap, mask, used, limit; } ht_t;

enum { HT_OK = 0, HT_UPDATED = 1, HT_FULL = -1, HT_PSL = -2 };

static uint64_t g_visits;                /* 슬롯 방문 수 — 연산당 작업량 측정 */

static uint32_t limit_for(uint32_t cap) {
    uint32_t lim = cap - cap / 8u;
    return lim >= cap ? cap - 1u : lim;
}
static void ht_init(ht_t *t, slot_t *buf, uint32_t cap) {
    memset(buf, 0, cap * sizeof *buf);
    t->slot = buf; t->cap = cap; t->mask = cap - 1u; t->used = 0; t->limit = limit_for(cap);
}
static uint32_t home(const ht_t *t, uint32_t key) { return key & t->mask; }

static int32_t rh_locate(const ht_t *t, uint32_t key) {
    uint32_t i = home(t, key);
    for (uint32_t d = 1; d <= t->cap; d++, i = (i + 1u) & t->mask) {
        g_visits++;
        if (t->slot[i].psl < d) return -1;
        if (t->slot[i].key == key) return (int32_t)i;
    }
    return -1;
}

static int rh_put(ht_t *t, uint32_t key, uint32_t val) {
    uint32_t i = home(t, key), n = 0;
    slot_t cur = { key, val, 1, 0 };
    for (; n < t->cap; n++, i = (i + 1u) & t->mask, cur.psl++) {
        g_visits++;
        if (t->slot[i].psl < cur.psl) break;
        if (t->slot[i].key == key) { t->slot[i].val = val; return HT_UPDATED; }
    }
    if (n == t->cap || t->used >= t->limit) return HT_FULL;
    for (;;) {
        slot_t *s = &t->slot[i];
        if (s->psl == 0u) { *s = cur; t->used++; return HT_OK; }
        if (s->psl < cur.psl) { slot_t tmp = *s; *s = cur; cur = tmp; }
        i = (i + 1u) & t->mask; cur.psl++; g_visits++;
    }
}

static bool rh_del(ht_t *t, uint32_t key) {
    int32_t at = rh_locate(t, key);
    if (at < 0) return false;
    uint32_t i = (uint32_t)at, j = (i + 1u) & t->mask;
    while (t->slot[j].psl > 1u) {
        t->slot[i] = t->slot[j]; t->slot[i].psl--;
        i = j; j = (j + 1u) & t->mask; g_visits++;
    }
    t->slot[i] = (slot_t){ 0 };
    t->used--;
    return true;
}

static slot_t *rh_rehash(ht_t *t, slot_t *newbuf, uint32_t newcap) {
    if (newcap < 2u || (newcap & (newcap - 1u)) || newbuf == t->slot || t->used > limit_for(newcap)) return NULL;
    slot_t *old = t->slot; uint32_t oldcap = t->cap;
    ht_init(t, newbuf, newcap);
    for (uint32_t i = 0; i < oldcap; i++) { g_visits++; if (old[i].psl) rh_put(t, old[i].key, old[i].val); }
    return old;
}

static bool rh_check(const ht_t *t) {
    uint32_t full = 0;
    for (uint32_t i = 0; i < t->cap; i++) {
        const slot_t *s = &t->slot[i];
        if (!s->psl) continue;
        full++;
        uint32_t h = home(t, s->key);
        if (s->psl != ((i - h) & t->mask) + 1u) return false;
        if (s->psl > t->slot[(i - 1u) & t->mask].psl + 1u) return false;
        for (uint32_t j = h; j != i; j = (j + 1u) & t->mask)
            if (t->slot[j].key == s->key) return false;
    }
    return full == t->used && t->used <= t->limit && t->limit < t->cap;
}

static uint32_t rng = 1u;
static uint32_t xorshift32(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

/* ---------- 연습 1: 점진적 리해싱 (incremental rehash) ----------
 * 성장 시 옛 표를 "얼리고"(읽기 전용) 새 표를 만든 뒤, 연산마다 옛 표 슬롯 step 칸씩만 옮긴다.
 * 조회: 새 표 → (이주 중이면) 옛 표.  삽입: 항상 새 표.  삭제: 새 표에서 지우고 옛 표 사본엔 dead 표시. */
typedef struct {
    ht_t cur, old;
    bool migrating;
    uint32_t cursor, step, count;
    slot_t *spare;
} imap_t;

static void im_init(imap_t *m, slot_t *a, slot_t *b, uint32_t cap, uint32_t step) {
    memset(m, 0, sizeof *m);
    ht_init(&m->cur, a, cap);
    m->spare = b; m->step = step;
}

static void im_step(imap_t *m) {
    for (uint32_t k = 0; k < m->step && m->migrating; k++) {
        const slot_t *s = &m->old.slot[m->cursor];
        g_visits++;
        if (s->psl && !s->dead && rh_locate(&m->cur, s->key) < 0)   /* 새 표에 더 새 값이 있으면 건너뜀 */
            rh_put(&m->cur, s->key, s->val);
        if (++m->cursor == m->old.cap) { m->migrating = false; m->spare = m->old.slot; }
    }
}

static bool im_find(imap_t *m, uint32_t key, uint32_t *val) {
    int32_t i = rh_locate(&m->cur, key);
    if (i >= 0) { if (val) *val = m->cur.slot[i].val; return true; }
    if (!m->migrating) return false;
    i = rh_locate(&m->old, key);
    if (i < 0 || m->old.slot[i].dead) return false;
    if (val) *val = m->old.slot[i].val;
    return true;
}

static int im_put(imap_t *m, uint32_t key, uint32_t val) {
    im_step(m);
    bool existed = im_find(m, key, NULL);
    int rc = rh_put(&m->cur, key, val);
    if (rc == HT_FULL && !m->migrating && m->cur.cap * 2u <= MAX_CAP) {
        m->old = m->cur;                                  /* 얼린다 — 버퍼는 그대로 */
        ht_init(&m->cur, m->spare, m->old.cap * 2u);
        m->spare = NULL; m->migrating = true; m->cursor = 0;
        rc = rh_put(&m->cur, key, val);
    }
    if (rc == HT_FULL) return HT_FULL;
    if (!existed) m->count++;
    return existed ? HT_UPDATED : HT_OK;
}

static bool im_del(imap_t *m, uint32_t key) {
    im_step(m);
    bool found = rh_del(&m->cur, key);
    if (m->migrating) {
        int32_t i = rh_locate(&m->old, key);
        if (i >= 0 && !m->old.slot[i].dead) { m->old.slot[i].dead = 1; found = true; }
    }
    if (found) m->count--;
    return found;
}

/* ---------- 연습 2: PSL 상한 강제 ---------- */
/* 쓰기 전에 자리 뺏기 사슬을 시뮬레이션해서, 내려놓는 순간의 psl 이 pmax 를 넘으면 거절 */
static int rh_put_bounded(ht_t *t, uint32_t key, uint32_t val, uint32_t pmax) {
    int32_t at = rh_locate(t, key);
    if (at >= 0) { t->slot[at].val = val; return HT_UPDATED; }
    uint32_t i = home(t, key), c = 1;
    for (;;) {
        uint32_t p = t->slot[i].psl;
        if (p == 0u) { if (c > pmax) return HT_PSL; break; }
        if (p < c) { if (c > pmax) return HT_PSL; c = p; }       /* 들고 있던 것을 내려놓고 쫓겨난 것을 든다 */
        i = (i + 1u) & t->mask; c++;
    }
    return rh_put(t, key, val);
}
static int lp_put_bounded(ht_t *t, uint32_t key, uint32_t val, uint32_t pmax) {
    uint32_t i = home(t, key);
    for (uint32_t d = 1; d <= t->cap; d++, i = (i + 1u) & t->mask) {
        slot_t *s = &t->slot[i];
        if (s->psl == 0u) {
            if (d > pmax) return HT_PSL;
            if (t->used >= t->limit) return HT_FULL;
            *s = (slot_t){ key, val, (uint16_t)d, 0 }; t->used++;
            return HT_OK;
        }
        if (s->key == key) { s->val = val; return HT_UPDATED; }
    }
    return HT_FULL;
}
static uint32_t probes_miss_lp(const ht_t *t, uint32_t key) {   /* EMPTY 까지 걷는 선형 탐사 실패 조회 */
    uint32_t i = home(t, key), n = 1;
    while (t->slot[i].psl) { i = (i + 1u) & t->mask; n++; }
    return n;
}
static uint32_t max_psl(const ht_t *t) {
    uint32_t m = 0;
    for (uint32_t i = 0; i < t->cap; i++) if (t->slot[i].psl > m) m = t->slot[i].psl;
    return m;
}

/* ---------- 연습 3: 같은 홈끼리는 키 순서로 — 정규형 Robin Hood ---------- */
static int rh_put_canon(ht_t *t, uint32_t key, uint32_t val) {
    int32_t at = rh_locate(t, key);
    if (at >= 0) { t->slot[at].val = val; return HT_UPDATED; }
    if (t->used >= t->limit) return HT_FULL;
    uint32_t i = home(t, key);
    slot_t cur = { key, val, 1, 0 };
    for (;;) {
        slot_t *s = &t->slot[i];
        if (s->psl == 0u) { *s = cur; t->used++; return HT_OK; }
        if (s->psl < cur.psl || (s->psl == cur.psl && s->key > cur.key)) { slot_t tmp = *s; *s = cur; cur = tmp; }
        i = (i + 1u) & t->mask; cur.psl++;
    }
}
static int lp_put(ht_t *t, uint32_t key, uint32_t val) { return lp_put_bounded(t, key, val, t->cap); }
static bool same_layout(const ht_t *a, const ht_t *b) {
    for (uint32_t i = 0; i < a->cap; i++)
        if (a->slot[i].psl != b->slot[i].psl || (a->slot[i].psl && a->slot[i].key != b->slot[i].key)) return false;
    return true;
}
static bool same_occupancy(const ht_t *a, const ht_t *b) {
    for (uint32_t i = 0; i < a->cap; i++) if ((a->slot[i].psl == 0) != (b->slot[i].psl == 0)) return false;
    return true;
}
static uint64_t psl_sum(const ht_t *t) {
    uint64_t s = 0;
    for (uint32_t i = 0; i < t->cap; i++) s += t->slot[i].psl;
    return s;
}
static void shuffle(uint32_t *a, uint32_t n) {
    for (uint32_t i = n; i > 1; i--) { uint32_t j = xorshift32() % i, x = a[i - 1]; a[i - 1] = a[j]; a[j] = x; }
}

/* ---------- 연습 4: 성장/축소 문턱 — thrash ---------- */
typedef struct { ht_t t; slot_t *spare; uint32_t rehashes; uint64_t moved; } smap_t;
static int sm_put(smap_t *m, uint32_t key) {
    int rc = rh_put(&m->t, key, key);
    if (rc != HT_FULL) return rc;
    m->moved += m->t.used;
    slot_t *old = rh_rehash(&m->t, m->spare, m->t.cap * 2u);
    if (!old) return HT_FULL;
    m->spare = old; m->rehashes++;
    return rh_put(&m->t, key, key);
}
/* shrink_num/shrink_den: used <= cap * num/den 이면 절반으로 */
static bool sm_del(smap_t *m, uint32_t key, uint32_t num, uint32_t den) {
    if (!rh_del(&m->t, key)) return false;
    if (m->t.cap > 16u && (uint64_t)m->t.used * den <= (uint64_t)m->t.cap * num) {
        uint32_t used = m->t.used;
        slot_t *old = rh_rehash(&m->t, m->spare, m->t.cap / 2u);
        if (old) { m->spare = old; m->rehashes++; m->moved += used; }
    }
    return true;
}

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static slot_t g_a[MAX_CAP], g_b[MAX_CAP], g_c[MAX_CAP], g_d[MAX_CAP];
#define POOL 3000u

int main(void) {
    /* 연습 1: 같은 연산 열을 stop-the-world(step=∞) 와 incremental(step=4) 로 — 결과 일치 + 연산당 최대 작업량 */
    {
        static uint32_t pool[POOL], ref_val[POOL];
        static bool ref_has[POOL];
        static imap_t ms[2];
        const uint32_t steps[2] = { UINT32_MAX, 4u };
        uint64_t worst[2] = { 0, 0 }, worst_mig = 0, worst_plain = 0;   /* incremental: 이주 중 / 아닐 때 */
        uint32_t mig_ops = 0;
        bool ok = true;
        for (int w = 0; w < 2 && ok; w++) {
            imap_t *m = &ms[w];
            im_init(m, w ? g_c : g_a, w ? g_d : g_b, 16u, steps[w]);
            memset(ref_has, 0, sizeof ref_has);
            uint32_t ref_n = 0;
            rng = 777u;
            for (uint32_t i = 0; i < POOL; i++) pool[i] = xorshift32();
            for (uint32_t op = 0; op < 20000u && ok; op++) {
                uint32_t r = xorshift32(), idx = r % POOL, key = pool[idx], val = r >> 4, pct = (r >> 20) % 100u, v = 0;
                uint64_t before = g_visits;
                bool was_mig = m->migrating;
                if (pct < 70u) {
                    int rc = im_put(m, key, val);
                    ok = rc == (ref_has[idx] ? HT_UPDATED : HT_OK);
                    if (!ref_has[idx]) ref_n++;
                    ref_has[idx] = true; ref_val[idx] = val;
                } else if (pct < 85u) {
                    ok = im_del(m, key) == ref_has[idx];
                    if (ref_has[idx]) ref_n--;
                    ref_has[idx] = false;
                } else {
                    ok = im_find(m, key, &v) == ref_has[idx] && (!ref_has[idx] || v == ref_val[idx]);
                }
                uint64_t work = g_visits - before;
                if (work > worst[w]) worst[w] = work;
                if (w == 1 && (was_mig || m->migrating)) { mig_ops++; if (work > worst_mig) worst_mig = work; }
                else if (w == 1 && work > worst_plain) worst_plain = work;
                ok = ok && m->count == ref_n && rh_check(&m->cur) && (!m->migrating || rh_check(&m->old));
            }
            for (uint32_t i = 0; i < POOL && ok; i++) {
                uint32_t v = 0;
                ok = im_find(m, pool[i], &v) == ref_has[i] && (!ref_has[i] || v == ref_val[i]);
            }
        }
        CHECK(ok);
        CHECK(ms[1].cur.cap == MAX_CAP && worst_mig * 50u < worst[0] && worst[1] * 10u < worst[0]);
        printf("[1] 20000 ops vs reference: %s | worst slot visits per op: stop-the-world %llu,"
               " incremental(step=4) %llu\n", ok ? "match" : "MISMATCH",
               (unsigned long long)worst[0], (unsigned long long)worst[1]);
        printf("[1] incremental: %u ops while migrating (worst %llu visits), other ops worst %llu visits\n",
               mig_ops, (unsigned long long)worst_mig, (unsigned long long)worst_plain);
    }

    /* 연습 2: psl ≤ 16 을 강제하면 어느 부하율에서 처음 거절되나 (cap 4096, 시드 8개) */
    {
        enum { SEEDS = 8, PMAX = 16 };
        double a_rh = 0, a_lp = 0;
        uint32_t worst_hit = 0, worst_miss = 0, lp_miss_walk = 0;
        ht_t t;
        for (uint32_t s = 0; s < SEEDS; s++) {
            for (int kind = 0; kind < 2; kind++) {
                ht_init(&t, g_a, MAX_CAP); t.limit = MAX_CAP - 1u;
                rng = 4242u + s * 101u;
                for (;;) {
                    uint32_t key = xorshift32();
                    int rc = kind ? rh_put_bounded(&t, key, key, PMAX) : lp_put_bounded(&t, key, key, PMAX);
                    if (rc == HT_PSL || rc == HT_FULL) break;
                }
                double alpha = (double)t.used / MAX_CAP;
                if (kind) {
                    a_rh += alpha / SEEDS;
                    if (max_psl(&t) > worst_hit) worst_hit = max_psl(&t);   /* 성공 조회 탐사 수 = psl */
                    for (uint32_t k = 0; k < 20000u; k++) {
                        uint64_t before = g_visits;
                        rh_locate(&t, xorshift32());
                        if (g_visits - before > worst_miss) worst_miss = (uint32_t)(g_visits - before);
                    }
                } else {
                    a_lp += alpha / SEEDS;
                    for (uint32_t k = 0; k < 20000u; k++) {
                        uint32_t p = probes_miss_lp(&t, xorshift32());
                        if (p > lp_miss_walk) lp_miss_walk = p;
                    }
                }
            }
        }
        CHECK(worst_hit <= PMAX && worst_miss <= PMAX + 1u);
        CHECK(a_rh > a_lp + 0.15);
        printf("[2] psl<=%d enforced: first reject at alpha linear %.3f | robin hood %.3f (avg of %d seeds)\n",
               PMAX, a_lp, a_rh, SEEDS);
        printf("[2] robin hood worst hit %u, worst miss %u probes | linear miss walking to EMPTY: worst %u\n",
               worst_hit, worst_miss, lp_miss_walk);
    }

    /* 연습 3: 정규형 Robin Hood 는 삽입 순서와 무관하게 배치가 같다. 선형 탐사는 다르지만 Σpsl 은 같다 */
    {
        enum { N = 700, CAP = 1024, ORDERS = 30 };
        static uint32_t keys[N];
        ht_t ref, t, lref, l;
        bool canon_same = true, lp_occ_same = true, lp_sum_same = true, lp_any_diff = false;
        rng = 99u;
        for (uint32_t i = 0; i < N; i++) {
            uint32_t k;
            do { k = xorshift32(); } while (k == 0);
            keys[i] = k;
        }
        ht_init(&ref, g_a, CAP); ht_init(&lref, g_b, CAP);
        ref.limit = lref.limit = CAP - 1u;
        for (uint32_t i = 0; i < N; i++) { rh_put_canon(&ref, keys[i], keys[i]); lp_put(&lref, keys[i], keys[i]); }
        for (int o = 0; o < ORDERS; o++) {
            shuffle(keys, N);
            ht_init(&t, g_c, CAP); ht_init(&l, g_d, CAP);
            t.limit = l.limit = CAP - 1u;
            for (uint32_t i = 0; i < N; i++) { rh_put_canon(&t, keys[i], keys[i]); lp_put(&l, keys[i], keys[i]); }
            canon_same = canon_same && same_layout(&ref, &t) && rh_check(&t);
            lp_occ_same = lp_occ_same && same_occupancy(&lref, &l) && same_occupancy(&ref, &l);
            lp_sum_same = lp_sum_same && psl_sum(&l) == psl_sum(&lref) && psl_sum(&l) == psl_sum(&ref);
            lp_any_diff = lp_any_diff || !same_layout(&lref, &l);
        }
        /* backward-shift 삭제도 정규형을 지킨다: 700개 넣고 300개 지운 표 == 남은 400개만 넣은 표 */
        shuffle(keys, N);
        for (uint32_t i = 0; i < 300u; i++) rh_del(&t, keys[i]);
        ht_init(&ref, g_a, CAP); ref.limit = CAP - 1u;
        for (uint32_t i = 300u; i < N; i++) rh_put_canon(&ref, keys[i], keys[i]);
        bool del_canon = same_layout(&ref, &t) && rh_check(&t);
        CHECK(canon_same && del_canon);
        CHECK(lp_occ_same && lp_sum_same && lp_any_diff);
        printf("[3] %d orders: canonical RH layouts identical=%s, after 300 deletes=%s | linear layouts differ=%s,"
               " occupancy & sum(psl) identical=%s\n", ORDERS, canon_same ? "yes" : "no", del_canon ? "yes" : "no",
               lp_any_diff ? "yes" : "no", (lp_occ_same && lp_sum_same) ? "yes" : "no");
    }

    /* 연습 4: 성장 7/8 고정, 축소 문턱 7/16(=성장/2) vs 1/4 — 문턱에서 put/del 을 1000번 번갈아 */
    {
        static const uint32_t num[2] = { 7u, 1u }, den[2] = { 16u, 4u };
        uint32_t reh[2]; uint64_t moved[2];
        for (int p = 0; p < 2; p++) {
            smap_t m = { .spare = g_b };
            ht_init(&m.t, g_a, 1024);
            for (uint32_t k = 0; k < 896u; k++) sm_put(&m, k);            /* 정확히 7/8 */
            m.rehashes = 0; m.moved = 0;
            for (uint32_t i = 0; i < 500u; i++) { sm_put(&m, 100000u + i); sm_del(&m, 100000u + i, num[p], den[p]); }
            reh[p] = m.rehashes; moved[p] = m.moved;
            CHECK(rh_check(&m.t) && m.t.used == 896u);
        }
        CHECK(reh[0] == 1000u && reh[1] == 1u);
        printf("[4] 500 x (put,del) at the 7/8 boundary: shrink<=7/16 -> %u rehashes (%llu entries moved) |"
               " shrink<=1/4 -> %u rehash (%llu moved)\n", reh[0], (unsigned long long)moved[0],
               reh[1], (unsigned long long)moved[1]);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
