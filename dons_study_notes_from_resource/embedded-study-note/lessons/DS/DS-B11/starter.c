/* DS-B11 해시 테이블: 오픈 어드레싱과 Tombstone
 * 선형 탐사(linear probing) 해시 테이블. malloc 없음 — 고정 크기 정적 배열.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define HT_MAX_CAP 4096u                 /* 2의 거듭제곱 */

typedef enum { SLOT_EMPTY = 0, SLOT_FULL = 1, SLOT_TOMB = 2 } slot_state_t;

typedef struct {
    uint32_t key;
    uint32_t val;
    uint8_t  state;                      /* slot_state_t */
} slot_t;

typedef struct {
    slot_t   slot[HT_MAX_CAP];
    uint32_t cap, mask;                  /* cap = 2^k <= HT_MAX_CAP, mask = cap - 1 */
    uint32_t used;                       /* FULL 슬롯 수 */
    uint32_t tombs;                      /* TOMB 슬롯 수 */
    uint32_t last_probes;                /* 직전 연산이 들여다본 슬롯 수 */
} ht_t;

enum { HT_OK = 0, HT_UPDATED = 1, HT_FULL = -1 };

static void ht_init(ht_t *t, uint32_t cap) {
    memset(t->slot, 0, sizeof t->slot);  /* 전부 EMPTY */
    t->cap = cap;
    t->mask = cap - 1u;
    t->used = t->tombs = t->last_probes = 0;
}

/* 홈 슬롯 = 키의 하위 비트. 키가 이미 고르게 퍼져 있다고 가정한다 (연습 3 참고). */
static uint32_t ht_home(const ht_t *t, uint32_t key) { return key & t->mask; }

/* ---------------- Part A: 선형 탐사 조회 / 삽입 ---------------- */
/* key 가 들어 있는 슬롯 번호, 없으면 -1.
 * home 부터 i, i+1, ... (mod cap) 순서로 본다. EMPTY 를 만나면 "없음" 으로 멈추고,
 * TOMB 는 건너뛴다. 최대 cap 칸만 본다 (EMPTY 가 하나도 없는 표에서 무한 루프 방지). */
static int32_t ht_locate(ht_t *t, uint32_t key) {
    (void)t; (void)key;
    /* TODO: home 부터 선형 탐사. last_probes 를 0으로 두고 한 칸 볼 때마다 ++ */
    return -1;
}

static bool ht_find(ht_t *t, uint32_t key, uint32_t *val) {
    int32_t i = ht_locate(t, key);
    if (i < 0) return false;
    if (val) *val = t->slot[i].val;
    return true;
}

/* 있으면 값 갱신(HT_UPDATED), 없으면 새로 넣음(HT_OK), 자리가 없으면 HT_FULL.
 * 새 키는 "처음 본 TOMB" 를 재사용하되, 그 전에 체인 끝(EMPTY)까지 가서
 * 같은 키가 뒤쪽에 없는지 반드시 확인한다. */
static int ht_insert(ht_t *t, uint32_t key, uint32_t val) {
    (void)t; (void)key; (void)val;
    /* TODO: 같은 키 → 갱신, 아니면 첫 TOMB(없으면 끝의 EMPTY)에 넣는다. used/tombs 갱신 */
    return HT_FULL;
}

/* ❌ 버그 재현용: 그냥 EMPTY 로 되돌리는 삭제 — 뒤에 이어진 체인을 끊는다 */
static bool ht_delete_naive(ht_t *t, uint32_t key) {
    int32_t i = ht_locate(t, key);
    if (i < 0) return false;
    t->slot[i].state = SLOT_EMPTY;
    t->used--;
    return true;
}

/* ---------------- Part B: tombstone 삭제 ---------------- */
/* EMPTY 가 아니라 TOMB 로 표시한다 → 조회는 지나가고, 삽입은 재사용한다 */
static bool ht_delete(ht_t *t, uint32_t key) {
    (void)t; (void)key;
    /* TODO: ht_locate 로 찾고 SLOT_TOMB 로 표시. used--, tombs++ */
    return false;
}

/* 불변식 검사 (주어짐):
 * (1) used / tombs 카운트가 실제 슬롯 상태와 일치
 * (2) 모든 FULL 키에 대해 home → 자기 자리 사이에 EMPTY 가 없다 (있으면 조회가 못 찾는다)
 * (3) 같은 키가 그 사이에 또 있지 않다 (중복 없음) */
static bool ht_check(const ht_t *t) {
    uint32_t full = 0, tomb = 0;
    for (uint32_t i = 0; i < t->cap; i++) {
        const slot_t *s = &t->slot[i];
        if (s->state == SLOT_TOMB) tomb++;
        if (s->state != SLOT_FULL) continue;
        full++;
        for (uint32_t j = ht_home(t, s->key); j != i; j = (j + 1u) & t->mask) {
            if (t->slot[j].state == SLOT_EMPTY) return false;
            if (t->slot[j].state == SLOT_FULL && t->slot[j].key == s->key) return false;
        }
    }
    return full == t->used && tomb == t->tombs;
}

/* 슬롯 lo..hi 를 한 줄로 찍는다: 키, '.'=EMPTY, 'T'=TOMB */
static void ht_dump(const ht_t *t, const char *tag, uint32_t lo, uint32_t hi) {
    printf("%-22s", tag);
    for (uint32_t i = lo; i <= hi; i++) {
        const slot_t *s = &t->slot[i];
        if (s->state == SLOT_FULL) printf(" [%u:%3u]", i, s->key);
        else printf(" [%u:  %c]", i, s->state == SLOT_TOMB ? 'T' : '.');
    }
    printf("\n");
}

/* ---------------- Part C: 부하율별 평균 탐사 길이 ---------------- */
static uint32_t rng_state = 1u;
static uint32_t xorshift32(void) {
    uint32_t x = rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return rng_state = x;
}

static ht_t g_big, g_small;              /* 각 ~48KB — 스택이 아니라 .bss 에 둔다 */
static uint32_t g_keys[HT_MAX_CAP];

/* cap=4096 표를 부하율 alpha 까지 무작위 키로 채운 뒤
 * succ = 넣은 키 전부를 조회할 때의 평균 탐사 수
 * fail = 없는 키 20000개를 조회할 때의 평균 탐사 수 */
static void measure_load(double alpha, uint32_t seed, double *succ, double *fail) {
    ht_t *t = &g_big;
    ht_init(t, HT_MAX_CAP);
    rng_state = seed;
    uint32_t n = (uint32_t)(alpha * HT_MAX_CAP), k = 0;
    for (uint32_t tries = 0; t->used < n && tries < 8u * HT_MAX_CAP; tries++) {
        uint32_t key = xorshift32();
        if (ht_insert(t, key, key) == HT_OK) g_keys[k++] = key;
    }
    /* TODO: succ = g_keys[0..k) 를 ht_find 한 last_probes 평균
     *       fail = 없는 무작위 키 20000개를 ht_find 한 last_probes 평균 */
    *succ = 0.0;
    *fail = 0.0;
}

static double knuth_succ(double a) { return 0.5 * (1.0 + 1.0 / (1.0 - a)); }
static double knuth_fail(double a) { return 0.5 * (1.0 + 1.0 / ((1.0 - a) * (1.0 - a))); }

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    ht_t *t = &g_small;
    uint32_t v = 0;

    /* Part A: 같은 home(5)에 몰린 키 3개 → 5, 6, 7 번 슬롯 */
    ht_init(t, 16);
    CHECK(ht_insert(t, 5, 500) == HT_OK);
    CHECK(ht_insert(t, 21, 2100) == HT_OK);
    CHECK(ht_insert(t, 37, 3700) == HT_OK);
    CHECK(t->slot[7].state == SLOT_FULL && t->slot[7].key == 37);
    CHECK(ht_find(t, 37, &v) && v == 3700 && t->last_probes == 3);
    CHECK(!ht_find(t, 53, NULL) && t->last_probes == 4);            /* 5,6,7 → 8 EMPTY */
    CHECK(ht_insert(t, 37, 3701) == HT_UPDATED && t->used == 3);
    CHECK(ht_insert(t, 15, 1) == HT_OK && ht_insert(t, 31, 2) == HT_OK);   /* 15 → 0 (랩) */
    CHECK(t->slot[0].key == 31 && ht_find(t, 31, &v) && v == 2);
    CHECK(ht_check(t));

    /* Part A: 버그 재현 — tombstone 없이 지우면 뒤쪽 키를 못 찾는다 */
    ht_dump(t, "[A] before delete", 5, 8);
    ht_delete_naive(t, 21);
    ht_dump(t, "[A] naive delete(21)", 5, 8);
    bool found = ht_find(t, 37, &v);
    printf("[A] find(37) = %s (probes=%u)  <- 37 is still in slot 7!\n",
           found ? "found" : "NOT FOUND", t->last_probes);
    CHECK(!found);                                    /* 버그가 재현되어야 정상 */
    CHECK(t->slot[7].state == SLOT_FULL && t->slot[7].key == 37);
    CHECK(!ht_check(t));                              /* 불변식 검사기가 잡아낸다 */

    /* Part B: 같은 시나리오를 tombstone 으로 */
    ht_init(t, 16);
    ht_insert(t, 5, 500); ht_insert(t, 21, 2100); ht_insert(t, 37, 3700);
    CHECK(ht_delete(t, 21) && t->slot[6].state == SLOT_TOMB && t->tombs == 1);
    ht_dump(t, "[B] tomb delete(21)", 5, 8);
    found = ht_find(t, 37, &v);
    printf("[B] find(37) = %s (probes=%u)\n", found ? "found" : "NOT FOUND", t->last_probes);
    CHECK(found && v == 3700 && t->last_probes == 3);
    CHECK(ht_check(t));
    CHECK(!ht_delete(t, 21));                         /* 두 번 지우기 */
    CHECK(ht_insert(t, 37, 3799) == HT_UPDATED);      /* 묘비(6)에 중복으로 넣으면 안 됨 */
    CHECK(t->slot[6].state == SLOT_TOMB && t->used == 2);
    CHECK(ht_insert(t, 53, 5300) == HT_OK && t->slot[6].key == 53 && t->tombs == 0);
    CHECK(ht_check(t));

    /* Part B: 꽉 찬 표 / 묘비로 가득 찬 표에서도 루프가 끝나야 한다 */
    ht_init(t, 16);
    for (uint32_t k = 0; k < 16; k++) ht_insert(t, k, k);
    CHECK(t->used == 16 && ht_insert(t, 16, 0) == HT_FULL);
    CHECK(!ht_find(t, 99, NULL) && t->last_probes == 16);
    for (uint32_t k = 0; k < 16; k++) ht_delete(t, k);
    CHECK(t->used == 0 && t->tombs == 16);
    CHECK(!ht_find(t, 3, NULL) && t->last_probes == 16);
    CHECK(ht_insert(t, 99, 1) == HT_OK && ht_check(t));

    /* Part B: 참조 모델 대조 랜덤 테스트 (키 48종, home 8곳에 몰리게 key = idx*8) */
    {
        static bool ref_has[48];
        static uint32_t ref_val[48];
        uint32_t ref_n = 0;
        bool ok = true;
        ht_init(t, 64);
        rng_state = 12345u;
        for (int op = 0; op < 20000 && ok; op++) {
            uint32_t r = xorshift32(), idx = r % 48u, key = idx * 8u, val = r >> 8;
            switch ((r >> 16) % 3u) {
            case 0: {
                int rc = ht_insert(t, key, val);
                ok = ok && rc == (ref_has[idx] ? HT_UPDATED : HT_OK);
                if (!ref_has[idx]) ref_n++;
                ref_has[idx] = true; ref_val[idx] = val;
                break;
            }
            case 1:
                ok = ok && ht_delete(t, key) == ref_has[idx];
                if (ref_has[idx]) ref_n--;
                ref_has[idx] = false;
                break;
            default:
                ok = ok && ht_find(t, key, &v) == ref_has[idx] && (!ref_has[idx] || v == ref_val[idx]);
            }
            ok = ok && ht_check(t) && t->used == ref_n;
        }
        CHECK(ok);
        printf("[B] random 20000 ops vs reference: %s (used=%u tombs=%u)\n",
               ok ? "match" : "MISMATCH", t->used, t->tombs);
    }

    /* Part C: 부하율별 평균 탐사 길이 (8회 평균) vs Knuth 근사식 */
    {
        static const double alphas[] = { 0.10, 0.25, 0.50, 0.75, 0.90, 0.95 };
        double s_prev = 0.0, f_prev = 0.0, s50 = 0.0, f50 = 0.0, f90 = 0.0;
        bool mono = true;
        printf("[C] alpha | succ(meas) succ(Knuth) | fail(meas) fail(Knuth)\n");
        for (unsigned a = 0; a < sizeof alphas / sizeof alphas[0]; a++) {
            double s = 0.0, f = 0.0;
            for (uint32_t trial = 0; trial < 8; trial++) {
                double ss, ff;
                measure_load(alphas[a], 0x9E3779B9u + trial * 7919u, &ss, &ff);
                s += ss / 8.0; f += ff / 8.0;
            }
            printf("[C] %5.2f | %10.2f %11.2f | %10.2f %11.2f\n",
                   alphas[a], s, knuth_succ(alphas[a]), f, knuth_fail(alphas[a]));
            mono = mono && s > s_prev && f > f_prev;
            s_prev = s; f_prev = f;
            if (a == 2) { s50 = s; f50 = f; }
            if (a == 4) f90 = f;
        }
        CHECK(mono);
        CHECK(s50 > 1.35 && s50 < 1.65);              /* 이론 1.50 */
        CHECK(f50 > 2.10 && f50 < 2.90);              /* 이론 2.50 */
        CHECK(f90 > 20.0);                            /* 이론 50.5 — 급격히 나빠진다 */
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
