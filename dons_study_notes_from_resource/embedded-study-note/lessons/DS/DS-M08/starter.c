/* DS-M08 해시 테이블 리사이즈와 Robin Hood 해싱
 * DS-B11 의 선형 탐사 표 위에 Robin Hood 삽입 + 조기 종료 조회 + backward-shift 삭제,
 * 그리고 부하율 기반 리사이즈(호출자가 준 두 번째 버퍼로 rehash)를 얹는다. malloc 없음.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MAX_CAP  4096u                   /* Part B 맵이 커질 수 있는 최대 용량 (2의 거듭제곱) */
#define MIN_CAP  16u
#define MEAS_CAP 16384u                  /* Part C 측정용 표 크기 */

typedef struct {
    uint32_t key;
    uint32_t val;
    uint16_t psl;                        /* probe sequence length: 0 = EMPTY, 1 = 홈 슬롯, d = 홈에서 d-1칸 뒤 */
} slot_t;                                /* 12바이트 (2바이트 패딩) */

typedef struct {
    slot_t  *slot;                       /* 호출자가 준 버퍼 — 표는 메모리를 소유하지 않는다 */
    uint32_t cap, mask;                  /* cap = 2^k, mask = cap - 1 */
    uint32_t used;                       /* 차 있는 슬롯 수 */
    uint32_t limit;                      /* used 상한 (기본 7/8·cap, 항상 < cap → EMPTY 가 최소 1칸) */
    uint32_t last_probes;                /* 직전 연산이 들여다본 슬롯 수 */
} ht_t;

enum { HT_OK = 0, HT_UPDATED = 1, HT_FULL = -1 };

static uint32_t limit_for(uint32_t cap) {
    uint32_t lim = cap - cap / 8u;       /* 부하율 상한 7/8 = 0.875 */
    return lim >= cap ? cap - 1u : lim;  /* cap < 8 이어도 EMPTY 1칸은 남긴다 */
}

static void ht_init(ht_t *t, slot_t *buf, uint32_t cap) {
    memset(buf, 0, cap * sizeof *buf);   /* psl = 0 → 전부 EMPTY */
    t->slot = buf;
    t->cap = cap;
    t->mask = cap - 1u;
    t->used = 0;
    t->limit = limit_for(cap);
    t->last_probes = 0;
}

/* 홈 슬롯 = 키의 하위 비트 (DS-B11 과 같음 — 손으로 추적하기 쉽게. 실전은 Fibonacci 해싱) */
static uint32_t ht_home(const ht_t *t, uint32_t key) { return key & t->mask; }

/* ---------------- Part A: Robin Hood 조회 / 삽입 ---------------- */
/* key 가 있는 슬롯 번호, 없으면 -1.
 * 홈부터 d = 1, 2, ... 로 걸으며 슬롯의 psl 이 d 보다 작으면(EMPTY 포함) 즉시 "없음":
 * 내 키가 그 뒤에 있었다면 삽입 때 이 "부자" 항목의 자리를 뺏었을 것이기 때문. */
static int32_t rh_locate(ht_t *t, uint32_t key) {
    (void)t; (void)key;
    /* TODO: 홈부터 d = 1,2,... 로 걷는다. last_probes 를 0으로 두고 한 칸 볼 때마다 ++.
     *       slot.psl < d 면 -1 (EMPTY 도 psl 0 이라 같이 처리됨), key 같으면 그 인덱스. 최대 cap 칸 */
    return -1;
}

static bool rh_find(ht_t *t, uint32_t key, uint32_t *val) {
    int32_t i = rh_locate(t, key);
    if (i < 0) return false;
    if (val) *val = t->slot[i].val;
    return true;
}

/* 있으면 갱신(HT_UPDATED), 없으면 삽입(HT_OK), used >= limit 이면 HT_FULL (리사이즈 신호).
 * 1단계: 조회와 같은 길을 걸으며 같은 키를 찾는다 (자리를 뺏기 전에만 확인하면 충분).
 * 2단계: 들고 있는 항목(cur)보다 psl 이 작은 "부자" 를 만나면 자리를 바꾸고, 쫓겨난 항목을 들고 계속. */
static int rh_put(ht_t *t, uint32_t key, uint32_t val) {
    (void)t; (void)key; (void)val;
    /* TODO: 1단계 — cur = {key,val,psl=1} 을 들고 걷다가 같은 키면 갱신(HT_UPDATED),
     *              slot.psl < cur.psl 이면 멈춤. 새 키인데 used >= limit 이면 아무것도 바꾸지 말고 HT_FULL.
     *       2단계 — EMPTY 면 내려놓고 used++ (HT_OK). slot.psl < cur.psl 이면 swap 후 쫓겨난 항목을 들고 계속 */
    return HT_FULL;
}

/* ---------------- Part B: backward-shift 삭제 / rehash ---------------- */
/* 묘비 없이 삭제: 지운 자리 뒤의 항목들을 psl 이 1(홈) 이거나 EMPTY 인 칸을 만날 때까지
 * 한 칸씩 앞으로 당긴다 (당긴 항목은 psl - 1). */
static bool rh_del(ht_t *t, uint32_t key) {
    (void)t; (void)key;
    /* TODO: rh_locate 로 찾고, 다음 칸의 psl > 1 인 동안 그 항목을 한 칸 앞으로 당긴다(psl--).
     *       마지막 자리를 EMPTY 로, used-- */
    return false;
}

/* 표 전체를 newbuf(newcap 칸)로 다시 해싱하고, 이제 안 쓰는 옛 버퍼를 돌려준다.
 * newcap 이 2의 거듭제곱이 아니거나, 항목이 newcap 의 limit 을 넘거나, 같은 버퍼면 NULL (표는 그대로). */
static slot_t *rh_rehash(ht_t *t, slot_t *newbuf, uint32_t newcap) {
    (void)t; (void)newbuf; (void)newcap;
    /* TODO: newcap 검사(2의 거듭제곱, used <= limit_for(newcap), newbuf != 현재 버퍼) → 실패면 NULL.
     *       옛 버퍼/cap 을 기억하고 ht_init(newbuf) 후 옛 항목을 전부 rh_put. 옛 버퍼를 돌려준다 */
    return NULL;
}

/* 불변식 검사 (주어짐):
 * (1) used 가 실제 차 있는 슬롯 수와 같고 used <= limit < cap
 * (2) 모든 항목의 psl == (자리 - 홈) mod cap + 1
 * (3) Robin Hood 불변식: psl[j] <= psl[j-1] + 1  (EMPTY 는 psl 0) — 조기 종료 조회가 맞을 조건
 * (4) 중복 키 없음 (홈 → 자기 자리 사이에 같은 키가 또 있으면 안 됨) */
static bool rh_check(const ht_t *t) {
    uint32_t full = 0;
    for (uint32_t i = 0; i < t->cap; i++) {
        const slot_t *s = &t->slot[i];
        if (s->psl == 0u) continue;
        full++;
        uint32_t home = ht_home(t, s->key);
        if (s->psl != ((i - home) & t->mask) + 1u) return false;
        if (s->psl > t->slot[(i - 1u) & t->mask].psl + 1u) return false;
        for (uint32_t j = home; j != i; j = (j + 1u) & t->mask)
            if (t->slot[j].key == s->key) return false;
    }
    return full == t->used && t->used <= t->limit && t->limit < t->cap;
}

/* 슬롯 lo..hi 를 한 줄로: [슬롯:키/psl], '.' = EMPTY */
static void ht_dump(const ht_t *t, const char *tag, uint32_t lo, uint32_t hi) {
    printf("%-22s", tag);
    for (uint32_t i = lo; i <= hi; i++) {
        const slot_t *s = &t->slot[i];
        if (s->psl) printf(" [%2u:%2u/%u]", i, s->key, s->psl);
        else printf(" [%2u:  .  ]", i);
    }
    printf("\n");
}

/* ---------------- Part C: 선형 탐사(DS-B11, 삭제 없음) 대조군 — 주어짐 ---------------- */
static int lp_put(ht_t *t, uint32_t key, uint32_t val) {      /* 처음 만난 EMPTY 에 넣는다 */
    uint32_t i = ht_home(t, key);
    for (uint16_t d = 1; d <= t->cap; d++, i = (i + 1u) & t->mask) {
        slot_t *s = &t->slot[i];
        if (s->psl == 0u) {
            if (t->used >= t->limit) return HT_FULL;
            *s = (slot_t){ key, val, d };
            t->used++;
            return HT_OK;
        }
        if (s->key == key) { s->val = val; return HT_UPDATED; }
    }
    return HT_FULL;
}
static bool lp_find(ht_t *t, uint32_t key) {                   /* EMPTY 를 만날 때까지 */
    uint32_t i = ht_home(t, key);
    t->last_probes = 0;
    for (uint32_t n = 0; n < t->cap; n++, i = (i + 1u) & t->mask) {
        t->last_probes++;
        if (t->slot[i].psl == 0u) return false;
        if (t->slot[i].key == key) return true;
    }
    return false;
}

typedef struct {
    uint32_t n;                          /* 항목 수 */
    uint32_t max;                        /* 최대 psl */
    uint64_t sum, sumsq;                 /* Σpsl, Σpsl² */
} psl_stats_t;

/* 표의 모든 항목에 대해 psl 통계를 모은다. 성공 조회의 탐사 수 = 그 키의 psl 이므로
 * 평균 psl = 평균 성공 탐사 수, 최대 psl = 최악 성공 탐사 수. */
static void psl_stats(const ht_t *t, psl_stats_t *st) {
    (void)t;
    memset(st, 0, sizeof *st);
    /* TODO: psl != 0 인 슬롯마다 n++, sum += psl, sumsq += psl², max 갱신 */
}

static uint32_t rng_state = 1u;
static uint32_t xorshift32(void) {
    uint32_t x = rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return rng_state = x;
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* Part B 맵: 버퍼 두 개를 핑퐁. 꽉 차면(7/8) 두 배로, 1/4 밑이면 절반으로 rehash. */
static slot_t g_buf_a[MAX_CAP], g_buf_b[MAX_CAP];
typedef struct { ht_t t; slot_t *spare; uint32_t grows, shrinks, max_moved; } map_t;

static int map_put(map_t *m, uint32_t key, uint32_t val) {
    int rc = rh_put(&m->t, key, val);
    if (rc != HT_FULL || m->t.cap * 2u > MAX_CAP) return rc;
    uint32_t moved = m->t.used;
    slot_t *old = rh_rehash(&m->t, m->spare, m->t.cap * 2u);
    if (!old) return HT_FULL;
    m->spare = old;
    m->grows++;
    if (moved > m->max_moved) m->max_moved = moved;
    return rh_put(&m->t, key, val);
}

static bool map_del(map_t *m, uint32_t key) {
    if (!rh_del(&m->t, key)) return false;
    if (m->t.cap > MIN_CAP && m->t.used * 4u < m->t.cap) {        /* 1/4 미만 → 절반 */
        slot_t *old = rh_rehash(&m->t, m->spare, m->t.cap / 2u);
        if (old) { m->spare = old; m->shrinks++; }
    }
    return true;
}

static slot_t g_meas_lp[MEAS_CAP], g_meas_rh[MEAS_CAP];
#define POOL 3000u

int main(void) {
    static slot_t small[16], small2[32];
    ht_t t;
    uint32_t v = 0;

    /* Part A: 홈 1 에 몰린 키 1, 17, 33 + 홈 2 의 키 2 (cap 16) */
    ht_init(&t, small, 16);
    CHECK(rh_put(&t, 1, 100) == HT_OK);
    CHECK(rh_put(&t, 17, 1700) == HT_OK);
    CHECK(rh_put(&t, 2, 200) == HT_OK);
    ht_dump(&t, "[A] 1,17,2", 1, 5);
    CHECK(t.slot[3].key == 2 && t.slot[3].psl == 2);
    CHECK(rh_put(&t, 33, 3300) == HT_OK);              /* 슬롯 3 의 2(psl 2)를 밀어낸다 */
    ht_dump(&t, "[A] +33 (robin hood)", 1, 5);
    CHECK(t.slot[3].key == 33 && t.slot[3].psl == 3);
    CHECK(t.slot[4].key == 2 && t.slot[4].psl == 3);
    CHECK(rh_find(&t, 2, &v) && v == 200 && t.last_probes == 3);
    CHECK(!rh_find(&t, 49, NULL) && t.last_probes == 4);   /* 슬롯 4 의 psl 3 < 4 → 조기 종료 */
    printf("[A] find(49) miss after %u probes (linear probing would need 5)\n", t.last_probes);
    CHECK(!rh_find(&t, 5, NULL) && t.last_probes == 1);    /* 홈 5 가 EMPTY */
    CHECK(rh_put(&t, 33, 3301) == HT_UPDATED && t.used == 4);
    CHECK(rh_find(&t, 33, &v) && v == 3301);
    CHECK(rh_check(&t));

    /* Part A: 랩어라운드 — 15, 31, 47 은 홈 15, 0 은 홈 0 */
    ht_init(&t, small, 16);
    CHECK(rh_put(&t, 15, 1) == HT_OK && rh_put(&t, 31, 2) == HT_OK);
    CHECK(rh_put(&t, 47, 3) == HT_OK && rh_put(&t, 0, 4) == HT_OK);
    CHECK(t.slot[0].key == 31 && t.slot[1].key == 47 && t.slot[2].key == 0 && t.slot[2].psl == 3);
    CHECK(rh_check(&t));

    /* Part B: backward-shift 삭제 (랩어라운드 포함) */
    CHECK(rh_del(&t, 15));
    CHECK(t.slot[15].key == 31 && t.slot[15].psl == 1);
    CHECK(t.slot[0].key == 47 && t.slot[0].psl == 2);
    CHECK(t.slot[1].key == 0 && t.slot[1].psl == 2 && t.slot[2].psl == 0);
    CHECK(rh_check(&t) && t.used == 3);
    CHECK(!rh_del(&t, 15));                            /* 두 번 지우기 */
    CHECK(rh_del(&t, 0) && t.slot[1].psl == 0 && rh_check(&t));

    ht_init(&t, small, 16);
    rh_put(&t, 1, 100); rh_put(&t, 17, 1700); rh_put(&t, 2, 200); rh_put(&t, 33, 3300);
    CHECK(rh_del(&t, 17));
    ht_dump(&t, "[B] del 17 (shift)", 1, 5);
    CHECK(t.slot[2].key == 33 && t.slot[2].psl == 2 && t.slot[3].key == 2 && t.slot[3].psl == 2);
    CHECK(t.slot[4].psl == 0 && rh_check(&t));
    CHECK(rh_del(&t, 1) && t.slot[1].key == 33 && t.slot[2].key == 2 && t.slot[2].psl == 1);
    CHECK(rh_check(&t));

    /* Part B: 부하율 상한과 rehash */
    ht_init(&t, small, 16);
    CHECK(t.limit == 14);
    for (uint32_t k = 0; k < 14; k++) rh_put(&t, k * 7u, k);
    CHECK(t.used == 14 && rh_put(&t, 999, 0) == HT_FULL && t.used == 14);
    CHECK(rh_put(&t, 7, 77) == HT_UPDATED);            /* 꽉 차도 갱신은 된다 */
    CHECK(rh_rehash(&t, small2, 24) == NULL && t.slot == small);      /* 2의 거듭제곱 아님 */
    CHECK(rh_rehash(&t, small2, 8) == NULL && t.cap == 16);           /* 14개는 8칸에 안 들어감 */
    CHECK(rh_rehash(&t, small2, 32) == small && t.slot == small2 && t.cap == 32 && t.limit == 28);
    CHECK(t.used == 14 && rh_check(&t));
    {
        bool all = true;
        for (uint32_t k = 0; k < 14; k++)
            all = all && rh_find(&t, k * 7u, &v) && v == (k == 1u ? 77u : k);
        CHECK(all);
    }
    CHECK(rh_put(&t, 999, 9) == HT_OK && rh_check(&t));

    /* Part B: 참조 모델 대조 랜덤 테스트 — 키 3000종(1/4 은 홈이 몰리는 키), 맵이 16 ↔ 4096 사이로 자라고 줄어든다 */
    {
        static uint32_t pool[POOL], ref_val[POOL];
        static bool ref_has[POOL];
        map_t m = { .spare = g_buf_b };
        uint32_t ref_n = 0, max_cap = 0;
        bool ok = true;
        ht_init(&m.t, g_buf_a, MIN_CAP);
        rng_state = 2024u;
        for (uint32_t i = 0; i < POOL; i++)
            pool[i] = (i % 4u == 0u) ? (i << 6) : xorshift32();   /* i=4j → j<<8: 홈이 256의 배수에만 몰림 */
        for (uint32_t op = 0; op < 30000u && ok; op++) {
            uint32_t r = xorshift32(), idx = r % POOL, key = pool[idx], val = r >> 4;
            uint32_t pct = (r >> 20) % 100u;
            uint32_t put_pct = op < 10000u ? 75u : op < 20000u ? 10u : 50u;   /* 성장 → 축소 → 혼합 */
            uint32_t del_pct = op < 10000u ? 25u : op < 20000u ? 40u : 25u;
            if (pct < put_pct) {
                int rc = map_put(&m, key, val);
                ok = ok && rc == (ref_has[idx] ? HT_UPDATED : HT_OK);
                if (!ref_has[idx]) ref_n++;
                ref_has[idx] = true; ref_val[idx] = val;
            } else if (pct < put_pct + del_pct) {
                ok = ok && map_del(&m, key) == ref_has[idx];
                if (ref_has[idx]) ref_n--;
                ref_has[idx] = false;
            } else {
                ok = ok && rh_find(&m.t, key, &v) == ref_has[idx] && (!ref_has[idx] || v == ref_val[idx]);
            }
            ok = ok && rh_check(&m.t) && m.t.used == ref_n;
            if (m.t.cap > max_cap) max_cap = m.t.cap;
        }
        for (uint32_t i = 0; i < POOL && ok; i++)
            ok = rh_find(&m.t, pool[i], &v) == ref_has[i] && (!ref_has[i] || v == ref_val[i]);
        CHECK(ok);
        CHECK(m.grows >= 8 && m.shrinks >= 1 && max_cap == MAX_CAP);
        printf("[B] random 30000 ops vs reference: %s (grows=%u shrinks=%u max cap=%u, now cap=%u used=%u)\n",
               ok ? "match" : "MISMATCH", m.grows, m.shrinks, max_cap, m.t.cap, m.t.used);
        printf("[B] worst single put: rehash moved %u entries (stop-the-world)\n", m.max_moved);
    }

    /* Part C: 같은 키 열을 선형 탐사 표와 Robin Hood 표에 넣고 psl 분포와 실패 조회 비교 */
    {
        static const double alphas[] = { 0.50, 0.60, 0.70, 0.80, 0.85, 0.90, 0.95 };
        enum { SEEDS = 4, MISSES = 20000 };
        ht_t lp, rh;
        bool same_sum = true, rh_max_le = true, rh_var_lt = true, rh_miss_lt = true;
        double rh_max95 = 0.0, lp_max95 = 0.0;
        printf("[C] alpha | avg psl  lp     rh | max psl  lp     rh | var  lp      rh | miss  lp      rh\n");
        for (unsigned a = 0; a < sizeof alphas / sizeof alphas[0]; a++) {
            double avg_lp = 0, avg_rh = 0, mx_lp = 0, mx_rh = 0, var_lp = 0, var_rh = 0, ms_lp = 0, ms_rh = 0;
            for (uint32_t s = 0; s < SEEDS; s++) {
                ht_init(&lp, g_meas_lp, MEAS_CAP); lp.limit = MEAS_CAP - 1u;
                ht_init(&rh, g_meas_rh, MEAS_CAP); rh.limit = MEAS_CAP - 1u;
                rng_state = 0x9E3779B9u + s * 7919u;
                uint32_t n = (uint32_t)(alphas[a] * MEAS_CAP);
                for (uint32_t tries = 0; rh.used < n && tries < 4u * MEAS_CAP; tries++) {
                    uint32_t key = xorshift32();
                    if (rh_put(&rh, key, key) == HT_OK) lp_put(&lp, key, key);
                }
                psl_stats_t sl, sr;
                psl_stats(&lp, &sl);
                psl_stats(&rh, &sr);
                same_sum = same_sum && sl.n == n && sr.n == n && sl.sum == sr.sum && rh_check(&rh);
                rh_max_le = rh_max_le && sr.max <= sl.max;
                double ml = sl.n ? (double)sl.sum / sl.n : 0.0, mr = sr.n ? (double)sr.sum / sr.n : 0.0;
                double vl = sl.n ? (double)sl.sumsq / sl.n - ml * ml : 0.0;
                double vr = sr.n ? (double)sr.sumsq / sr.n - mr * mr : 0.0;
                rh_var_lt = rh_var_lt && vr < vl;
                uint64_t pl = 0, pr = 0;
                for (uint32_t k = 0; k < (uint32_t)MISSES;) {
                    uint32_t key = xorshift32();
                    if (rh_find(&rh, key, NULL)) continue;               /* 우연히 있는 키는 제외 */
                    pr += rh.last_probes;
                    lp_find(&lp, key);
                    pl += lp.last_probes;
                    k++;
                }
                avg_lp += ml / SEEDS; avg_rh += mr / SEEDS;
                mx_lp += (double)sl.max / SEEDS; mx_rh += (double)sr.max / SEEDS;
                var_lp += vl / SEEDS; var_rh += vr / SEEDS;
                ms_lp += (double)pl / MISSES / SEEDS; ms_rh += (double)pr / MISSES / SEEDS;
            }
            printf("[C] %5.2f | %9.2f %6.2f | %9.1f %6.1f | %7.2f %7.2f | %8.2f %7.2f\n",
                   alphas[a], avg_lp, avg_rh, mx_lp, mx_rh, var_lp, var_rh, ms_lp, ms_rh);
            rh_miss_lt = rh_miss_lt && ms_rh < ms_lp;
            if (a == 6) { rh_max95 = mx_rh; lp_max95 = mx_lp; }
        }
        CHECK(same_sum);                                  /* 평균 psl(성공 조회)은 정확히 같다 */
        CHECK(rh_max_le && rh_var_lt);                    /* 최대·분산은 Robin Hood 가 작다 */
        CHECK(rh_miss_lt);                                /* 조기 종료로 실패 조회도 짧다 */
        CHECK(rh_max95 * 4.0 < lp_max95);                 /* α=0.95 최대 탐사: 몇 배 차이 */
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
