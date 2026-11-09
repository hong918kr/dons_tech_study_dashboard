/* DS-A07 Cuckoo 해싱: 최악 O(1) 조회
 * 두 테이블 + 두 해시 + 버킷당 1칸. 조회는 언제나 슬롯 2칸만 본다. malloc 없음.
 * Part A: 고전 축출 사슬(kick chain) / Part B: 읽기 전용 경로 탐색 + Brent 사이클 감지 + 새 시드 rebuild /
 * Part C: ISR 조회 + 태스크 갱신 (뱅크 2개, cur 한 워드로 커밋)
 * 불변식: 표의 모든 키는 자기 두 슬롯 home0(k), home1(k) 중 하나에 있다.
 */
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(e) do { g_total++; if (e) { g_pass++; printf("PASS %s\n", #e); } \
                      else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #e); } while (0)

#define CK_EMPTY    0xFFFFFFFFu  /* 예약 키: 빈 슬롯 */
#define CK_MAX_PATH 128u         /* 경로 배열 크기 = 삽입 WCET 예산의 상한 */

typedef enum { CK_OK, CK_UPDATED, CK_BAD_KEY, CK_CYCLE, CK_BUDGET, CK_FULL } ck_status_t;
typedef enum { WALK_FOUND, WALK_CYCLE, WALK_LONG } walk_t;

typedef struct { uint32_t key, val; } ck_slot_t;
typedef uint32_t (*ck_hash_fn)(uint32_t seed, uint32_t key);

/* slots[0..cap) = T0, slots[cap..2cap) = T1. 슬롯 번호 s 하나로 테이블과 인덱스를 함께 표현한다. */
typedef struct {
    ck_slot_t *slots;
    uint32_t cap, mask, n;
    uint32_t seed[2];
    ck_hash_fn hash;
    uint32_t limit;              /* 허용 축출 횟수 (≤ CK_MAX_PATH) */
} ck_t;

/* 뱅크 2개: 하나는 활성(ISR 이 읽음), 하나는 rebuild 용 예비. */
typedef struct {
    ck_t bank[2];
    _Atomic uint32_t cur;
    uint32_t rng, max_attempts, rehashes, failed_attempts;
} ck_map_t;

static uint32_t xs32(uint32_t *s) {
    uint32_t x = *s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return *s = x;
}

/* Murmur3 finalizer 에 시드를 섞은 해시. 32비트 전단사라 32비트 MCU 에서도 싸다. */
static uint32_t ck_mix(uint32_t seed, uint32_t key) {
    uint32_t h = key ^ seed;
    h ^= h >> 16; h *= 0x85ebca6bu; h ^= h >> 13; h *= 0xc2b2ae35u; h ^= h >> 16;
    return h;
}

/* 테스트용: 시드 0/1 이면 키 비트가 곧 인덱스 → 손으로 충돌을 만든다. 시드 ≥ 2 면 진짜 해시. */
#define KEY(i0, i1, tag) (((uint32_t)(tag) << 16) | ((uint32_t)(i1) << 8) | (uint32_t)(i0))
static uint32_t h_forced(uint32_t seed, uint32_t key) {
    if (seed == 0) return key & 0xFFu;
    if (seed == 1) return (key >> 8) & 0xFFu;
    return ck_mix(seed, key);
}
/* 시드를 무시하는 해시: 재해시가 절대 도움이 안 되는 경우를 만든다. */
static uint32_t h_stuck(uint32_t seed, uint32_t key) { (void)seed; return key & 0xFFu; }

static inline uint32_t ck_home(const ck_t *t, uint32_t which, uint32_t key) {
    return (which ? t->cap : 0u) + (t->hash(t->seed[which], key) & t->mask);
}
/* 슬롯 s 에 있는 key 의 "다른 쪽" 슬롯. 두 슬롯은 항상 다른 테이블에 있다. */
static inline uint32_t ck_other(const ck_t *t, uint32_t s, uint32_t key) {
    return ck_home(t, s >= t->cap ? 0u : 1u, key);
}

/* 슬롯 쓰기는 전부 여기로 모인다. 실제 타깃에서는 이 두 스토어가 인터럽트 금지 구간이다
 * (8바이트 전체가 원자적이지 않으므로). 훅은 "이 쓰기 직후 ISR 이 끼어든다" 를 흉내 낸다. */
static void (*g_store_hook)(void);
static uint64_t g_writes;
static void ck_store(ck_t *t, uint32_t s, uint32_t key, uint32_t val) {
    t->slots[s].key = key;
    t->slots[s].val = val;
    g_writes++;
    if (g_store_hook) g_store_hook();
}

static void ck_clear(ck_t *t) {
    for (uint32_t i = 0; i < 2u * t->cap; i++) { t->slots[i].key = CK_EMPTY; t->slots[i].val = 0; }
    t->n = 0;
}

static void ck_init(ck_t *t, ck_slot_t *buf, uint32_t cap, ck_hash_fn hash, uint32_t limit,
                    uint32_t s0, uint32_t s1) {
    t->slots = buf; t->cap = cap; t->mask = cap - 1u; t->hash = hash; t->limit = limit;
    t->seed[0] = s0; t->seed[1] = s1;
    ck_clear(t);
}

/* ========== Part A ========== */

/* key 가 있는 슬롯 번호, 없으면 -1. 비교한 슬롯 수를 *probes 에 더한다. 반드시 2칸 이내.
 * CK_EMPTY 는 키가 아니다: 빈 슬롯에 "맞아 버리지" 않게 조심. */
static int32_t ck_locate(const ck_t *t, uint32_t key, uint32_t *probes) {
    /* TODO: ck_locate: 두 슬롯만 비교한다 */
    (void)t; (void)key; (void)probes;
    return -1;
}

static uint32_t g_max_probes;
static bool ck_find(const ck_t *t, uint32_t key, uint32_t *val, uint32_t *probes) {
    uint32_t p = 0;
    int32_t s = ck_locate(t, key, &p);
    if (p > g_max_probes) g_max_probes = p;
    if (probes) *probes = p;
    if (s < 0) return false;
    if (val) *val = t->slots[s].val;
    return true;
}

/* 슬롯을 비우기만 하면 된다 (묘비 없음). 지웠으면 true. */
static bool ck_del(ck_t *t, uint32_t key) {
    /* TODO: ck_del: 찾아서 비우고 n 감소 */
    (void)t; (void)key;
    return false;
}

/* 고전 삽입: T0 부터 놓고, 쫓겨난 키를 다른 쪽으로 보내는 일을 반복한다.
 * 같은 키면 값만 갱신(CK_UPDATED). 빈 칸에 도달하면 CK_OK.
 * limit 번 축출해도 안 끝나면 CK_BUDGET: 그 순간 손에 든 키를 *homeless 로 돌려준다
 * (표에는 n 개가 그대로 있고, 집 없는 키 1개만 밖에 있다). *kicks = 축출 횟수. */
static ck_status_t ck_put_walk(ck_t *t, uint32_t key, uint32_t val, uint32_t *kicks,
                               ck_slot_t *homeless) {
    /* TODO: ck_put_walk: 놓고, 쫓겨난 키를 들고 반대편으로 */
    (void)t; (void)key; (void)val; (void)homeless;
    *kicks = 0;
    return CK_BUDGET;
}

/* ========== Part B ========== */

/* 슬롯 start 에서 "점유자를 다른 쪽으로 보내면 어디로 가나" 를 읽기만 하며 따라간다.
 * path[0..*len) 에 지나간 슬롯을 적는다. 빈 슬롯에 닿으면 WALK_FOUND (path 끝이 빈 슬롯, 축출 = *len-1).
 * 슬롯 그래프는 함수형(functional) 그래프이므로 Brent 로 O(1) 메모리 사이클 감지 → WALK_CYCLE.
 * 축출 횟수가 t->limit 을 넘어야 하면 WALK_LONG. path 는 t->limit+1 칸까지만 쓴다. */
static walk_t ck_walk(const ck_t *t, uint32_t start, uint32_t *path, uint32_t *len) {
    /* TODO: ck_walk: Brent 사이클 감지 + 경로 기록 */
    (void)t; (void)start; (void)path;
    *len = 0;
    return WALK_LONG;
}

/* 경로 우선 삽입. 실패하면 표를 한 글자도 바꾸지 않는다.
 * 1) 이미 있으면 갱신  2) 두 집 중 빈 곳이 있으면 바로 넣기  3) T0 쪽 walk, 안 되면 T1 쪽 walk
 * 4) 빈 슬롯 쪽 끝에서부터 뒤로 복사 → 어느 순간에도 모든 키가 자기 슬롯 중 하나에 있다.
 * 둘 다 사이클이면 CK_CYCLE(이 시드로는 구조적으로 불가능), 예산 초과가 끼면 CK_BUDGET. */
static ck_status_t ck_put(ck_t *t, uint32_t key, uint32_t val, uint32_t *kicks) {
    /* TODO: ck_put: 경로를 먼저 찾고 뒤에서부터 옮긴다 */
    (void)t; (void)key; (void)val;
    *kicks = 0;
    return CK_CYCLE;
}

/* dst 를 비우고 시드 (s0, s1) 로 src 의 모든 키와 (있으면) extra 를 넣는다.
 * src 는 절대 건드리지 않는다. 전부 성공하면 CK_OK, 하나라도 실패하면 그 상태를 반환. */
static ck_status_t ck_rebuild(ck_t *dst, const ck_t *src, const ck_slot_t *extra,
                              uint32_t s0, uint32_t s1) {
    /* TODO: ck_rebuild: 새 시드로 예비 버퍼에 다시 넣기 */
    (void)dst; (void)src; (void)extra; (void)s0; (void)s1;
    return CK_FULL;
}

/* ========== Part C ========== */

static void map_init(ck_map_t *m, ck_slot_t *buf0, ck_slot_t *buf1, uint32_t cap, ck_hash_fn hash,
                     uint32_t limit, uint32_t s0, uint32_t s1, uint32_t rng) {
    ck_init(&m->bank[0], buf0, cap, hash, limit, s0, s1);
    ck_init(&m->bank[1], buf1, cap, hash, limit, s0, s1);
    atomic_init(&m->cur, 0u);
    m->rng = rng; m->max_attempts = 16; m->rehashes = 0; m->failed_attempts = 0;
}

/* ISR 쪽 조회: cur 를 acquire 로 한 번만 읽고 그 뱅크에서 찾는다. */
static bool map_lookup(ck_map_t *m, uint32_t key, uint32_t *val, uint32_t *probes) {
    /* TODO: map_lookup: cur 를 한 번만 읽기 */
    (void)m; (void)key; (void)val; (void)probes;
    return false;
}

/* 태스크 쪽 삽입: 활성 뱅크에 ck_put. CK_CYCLE/CK_BUDGET 이면 예비 뱅크에 새 시드로 rebuild(키 포함),
 * 성공하면 cur 를 release 로 바꿔 커밋(rehashes++). max_attempts 를 다 쓰면 CK_FULL, 활성 뱅크는 그대로. */
static ck_status_t map_put(ck_map_t *m, uint32_t key, uint32_t val) {
    /* TODO: map_put: 실패 시 예비 뱅크 rebuild 후 한 워드 커밋 */
    (void)m; (void)key; (void)val;
    return CK_FULL;
}

static bool map_del(ck_map_t *m, uint32_t key) {
    /* TODO: map_del: 활성 뱅크에서 지우기 */
    (void)m; (void)key;
    return false;
}

/* ========== 하네스 ========== */

/* 불변식: 점유 칸 수 == n, 모든 키가 자기 두 슬롯 중 하나, 같은 키가 두 슬롯에 동시에 없음.
 * 키가 있을 수 있는 곳이 두 슬롯뿐이라 중복 검사가 키당 O(1) 이다. */
static bool ck_check(const ck_t *t) {
    uint32_t used = 0;
    for (uint32_t s = 0; s < 2u * t->cap; s++) {
        uint32_t k = t->slots[s].key;
        if (k == CK_EMPTY) continue;
        used++;
        uint32_t h0 = ck_home(t, 0, k), h1 = ck_home(t, 1, k);
        if (s != h0 && s != h1) return false;
        if (t->slots[ck_other(t, s, k)].key == k) return false;
    }
    return used == t->n;
}

#define S_CAP 16u
static ck_slot_t g_s[2 * S_CAP], g_s2[2 * S_CAP], g_snap[2 * S_CAP];

static uint32_t slot_of(const ck_t *t, uint32_t key) {
    uint32_t p = 0;
    return (uint32_t)ck_locate(t, key, &p);
}

/* 검사기 자체를 먼저 검사한다: 깨진 표를 깨졌다고 말하는가 */
static void test_checker(void) {
    printf("-- 불변식 검사기 자가 점검 --\n");
    ck_t t;
    uint32_t k = KEY(1, 2, 0);
    ck_init(&t, g_s, S_CAP, h_forced, 20, 0, 1);
    CHECK(ck_check(&t));
    ck_store(&t, 0, k, 1); t.n = 1;                    /* 자기 슬롯이 아닌 곳 */
    CHECK(!ck_check(&t));
    ck_store(&t, 0, CK_EMPTY, 0);
    ck_store(&t, 1, k, 1);                             /* 맞는 자리, 하지만 */
    t.n = 2;
    CHECK(!ck_check(&t));                              /* n 이 틀림 */
    t.n = 1;
    CHECK(ck_check(&t));
    ck_store(&t, S_CAP + 2, k, 1); t.n = 2;            /* 같은 키가 두 슬롯에 */
    CHECK(!ck_check(&t));
}

static void test_a_scripted(void) {
    printf("-- Part A: 고전 축출 사슬 --\n");
    ck_t t; uint32_t kicks = 99, v = 0, pr = 0; ck_slot_t home = {0, 0};
    ck_init(&t, g_s, S_CAP, h_forced, 20, 0, 1);

    CHECK(!ck_find(&t, KEY(1, 2, 0), &v, &pr) && pr == 2);
    CHECK(ck_put_walk(&t, CK_EMPTY, 1, &kicks, &home) == CK_BAD_KEY);
    pr = 9;
    CHECK(!ck_find(&t, CK_EMPTY, &v, &pr) && pr == 0);    /* 빈 슬롯에 맞으면 안 된다 */

    uint32_t a = KEY(1, 2, 0), b = KEY(1, 5, 0), c = KEY(3, 2, 0), d = KEY(3, 5, 0);
    CHECK(ck_put_walk(&t, a, 10, &kicks, &home) == CK_OK && kicks == 0);
    CHECK(ck_put_walk(&t, a, 11, &kicks, &home) == CK_UPDATED && t.n == 1);
    CHECK(ck_find(&t, a, &v, &pr) && v == 11 && pr == 1);
    CHECK(ck_put_walk(&t, b, 20, &kicks, &home) == CK_OK && kicks == 1);
    CHECK(slot_of(&t, b) == 1 && slot_of(&t, a) == S_CAP + 2);
    CHECK(ck_put_walk(&t, c, 30, &kicks, &home) == CK_OK && kicks == 0);
    CHECK(ck_put_walk(&t, d, 40, &kicks, &home) == CK_OK && kicks == 3);
    CHECK(slot_of(&t, d) == 3 && slot_of(&t, c) == S_CAP + 2);
    CHECK(slot_of(&t, a) == 1 && slot_of(&t, b) == S_CAP + 5);
    CHECK(ck_check(&t) && t.n == 4);

    CHECK(ck_del(&t, c) && !ck_del(&t, c) && t.n == 3);
    CHECK(!ck_find(&t, c, &v, NULL));
    CHECK(ck_find(&t, d, &v, NULL) && v == 40);         /* 지워도 다른 키 조회에 영향 없음 */
    CHECK(ck_check(&t));

    /* 사이클이 닫혀도 끝이 아니다: 사슬이 자기 자신을 쫓아내고 반대편으로 간다 */
    ck_init(&t, g_s, S_CAP, h_forced, 20, 0, 1);
    uint32_t p = KEY(1, 2, 0), q = KEY(1, 2, 1), s = KEY(8, 3, 0), r = KEY(8, 7, 0), x = KEY(1, 7, 0);
    CHECK(ck_put_walk(&t, p, 1, &kicks, &home) == CK_OK);
    CHECK(ck_put_walk(&t, q, 2, &kicks, &home) == CK_OK && kicks == 1);
    CHECK(ck_put_walk(&t, s, 3, &kicks, &home) == CK_OK);
    CHECK(ck_put_walk(&t, r, 4, &kicks, &home) == CK_OK && kicks == 1);
    CHECK(ck_put_walk(&t, x, 5, &kicks, &home) == CK_OK && kicks == 3);
    CHECK(slot_of(&t, x) == S_CAP + 7);                  /* 자기 T1 슬롯으로 되돌아 나감 */
    CHECK(ck_check(&t) && t.n == 5);

    /* 같은 슬롯 쌍을 쓰는 키 3개: 정점 2개에 간선 3개 → 배치 불가능 */
    ck_init(&t, g_s, S_CAP, h_forced, 20, 0, 1);
    uint32_t k0 = KEY(4, 6, 0), k1 = KEY(4, 6, 1), k2 = KEY(4, 6, 2);
    CHECK(ck_put_walk(&t, k0, 100, &kicks, &home) == CK_OK);
    CHECK(ck_put_walk(&t, k1, 101, &kicks, &home) == CK_OK);
    CHECK(ck_put_walk(&t, k2, 102, &kicks, &home) == CK_BUDGET && kicks == 20);
    CHECK(t.n == 2 && ck_check(&t));
    {   /* 표의 2개 + homeless 1개 = 키 3개가 하나도 안 사라졌다 */
        uint32_t found = 0;
        found += ck_find(&t, k0, &v, NULL); found += ck_find(&t, k1, &v, NULL); found += ck_find(&t, k2, &v, NULL);
        CHECK(found == 2 && !ck_find(&t, home.key, &v, NULL));
        CHECK(home.key == k0 || home.key == k1 || home.key == k2);
    }
}

#define KS 1000u
static ck_slot_t g_m[2 * 1024];
static uint32_t g_ref[KS]; static bool g_has[KS];

static void test_a_random(void) {
    ck_t t; uint32_t rng = 0x1234ABCDu, bad = 0, kicks; ck_slot_t home = {0, 0};
    uint32_t homeless = 0, checks = 0;
    memset(g_has, 0, sizeof g_has);
    ck_init(&t, g_m, 1024, ck_mix, CK_MAX_PATH, 0x9E3779B9u, 0x85EBCA6Bu);
    g_max_probes = 0;
    for (uint32_t i = 0; i < 1000000u; i++) {
        uint32_t r = xs32(&rng), k = r % KS, op = (r >> 16) % 10u;
        if (op < 5) {
            ck_status_t st = ck_put_walk(&t, k, i, &kicks, &home);
            if (st == CK_UPDATED && !g_has[k]) bad++;
            if (st == CK_OK && g_has[k]) bad++;
            g_has[k] = true; g_ref[k] = i;
            if (st == CK_BUDGET) { homeless++; if (home.key >= KS) bad++; else g_has[home.key] = false; }
        } else if (op < 8) {
            if (ck_del(&t, k) != g_has[k]) bad++;
            g_has[k] = false;
        } else {
            uint32_t v = 0;
            bool f = ck_find(&t, k, &v, NULL);
            if (f != g_has[k] || (f && v != g_ref[k])) bad++;
        }
        if ((i & 1023u) == 0) { checks++; if (!ck_check(&t)) bad++; }
    }
    if (!ck_check(&t)) bad++;
    for (uint32_t k = 0; k < KS; k++) {
        uint32_t v = 0;
        bool f = ck_find(&t, k, &v, NULL);
        if (f != g_has[k] || (f && v != g_ref[k])) bad++;
    }
    printf("   random 1M ops: n=%u/2048, homeless=%u, full checks=%u, max_probes=%u\n",
           t.n, homeless, checks, g_max_probes);
    CHECK(bad == 0);
    CHECK(g_max_probes <= 2);
    CHECK(t.n > 400);
}

static void test_a_small(void) {
    ck_t t; uint32_t rng = 0x77777777u, bad = 0, kicks, homeless = 0; ck_slot_t home = {0, 0};
    uint32_t has[48]; memset(has, 0, sizeof has);
    ck_init(&t, g_s, S_CAP, ck_mix, 20, 11, 22);
    for (uint32_t i = 0; i < 60000u; i++) {
        uint32_t r = xs32(&rng), k = r % 48u;
        if ((r >> 8) & 3u) {
            ck_status_t st = ck_put_walk(&t, k, i, &kicks, &home);
            has[k] = 1u + i;
            if (st == CK_BUDGET) { homeless++; has[home.key] = 0; }
        } else {
            if (ck_del(&t, k) != (has[k] != 0)) bad++;
            has[k] = 0;
        }
        if (!ck_check(&t)) bad++;
        for (uint32_t j = 0; j < 48u; j++) {
            uint32_t v = 0;
            bool f = ck_find(&t, j, &v, NULL);
            if (f != (has[j] != 0) || (f && v != has[j] - 1u)) bad++;
        }
    }
    printf("   small table: 60000 ops, check every op, homeless=%u\n", homeless);
    CHECK(bad == 0);
    CHECK(homeless > 0);                 /* 실패 경로를 실제로 지나갔는가 */
}

/* 오라클: union-find 로 cuckoo 그래프를 만들어 "키 하나를 더 놓을 수 있나" 를 정확히 판정한다.
 * 연결 성분마다 간선 수 ≤ 정점 수이면 배치 가능. */
static uint32_t uf_find(uint32_t *p, uint32_t x) {
    while (p[x] != x) { p[x] = p[p[x]]; x = p[x]; }
    return x;
}
static bool oracle_can_add(const ck_t *t, uint32_t key) {
    uint32_t p[2 * S_CAP], edges[2 * S_CAP] = {0}, verts[2 * S_CAP] = {0};
    for (uint32_t i = 0; i < 2u * t->cap; i++) p[i] = i;
    for (uint32_t i = 0; i <= 2u * t->cap; i++) {
        uint32_t k = (i == 2u * t->cap) ? key : t->slots[i].key;
        if (k == CK_EMPTY) continue;
        uint32_t a = uf_find(p, ck_home(t, 0, k)), b = uf_find(p, ck_home(t, 1, k));
        if (a != b) p[a] = b;
    }
    for (uint32_t i = 0; i < 2u * t->cap; i++) verts[uf_find(p, i)]++;
    for (uint32_t i = 0; i <= 2u * t->cap; i++) {
        uint32_t k = (i == 2u * t->cap) ? key : t->slots[i].key;
        if (k != CK_EMPTY) edges[uf_find(p, ck_home(t, 0, k))]++;
    }
    uint32_t root = uf_find(p, ck_home(t, 0, key));
    return edges[root] <= verts[root];
}

static void test_b_scripted(void) {
    printf("-- Part B: 경로 우선 삽입과 사이클 감지 --\n");
    ck_t t; uint32_t kicks = 9, v = 0, len = 0, path[CK_MAX_PATH + 1]; ck_slot_t home = {0, 0};
    ck_init(&t, g_s, S_CAP, h_forced, CK_MAX_PATH, 0, 1);

    /* 사이클 2칸: p, q 가 T0[1], T1[2] 를 채운다 */
    uint32_t p = KEY(1, 2, 0), q = KEY(1, 2, 1), s = KEY(8, 3, 0), r = KEY(8, 7, 0), x = KEY(1, 7, 0);
    CHECK(ck_put(&t, p, 1, &kicks) == CK_OK && kicks == 0);
    CHECK(ck_put(&t, q, 2, &kicks) == CK_OK && kicks == 0);    /* 빈 T1[2] 로 바로 */
    CHECK(ck_put(&t, p, 7, &kicks) == CK_UPDATED && t.n == 2);
    CHECK(ck_walk(&t, 1, path, &len) == WALK_CYCLE);
    CHECK(ck_put(&t, s, 3, &kicks) == CK_OK);
    CHECK(ck_put(&t, r, 4, &kicks) == CK_OK && slot_of(&t, r) == S_CAP + 7);

    /* T0 쪽은 사이클, T1 쪽은 2칸 뒤 빈 슬롯 → 반대편으로 성공 */
    uint64_t w0 = g_writes;
    CHECK(ck_put(&t, x, 5, &kicks) == CK_OK && kicks == 2);
    CHECK(g_writes - w0 == 3u);                                /* 축출 k번 = 쓰기 k+1번 */
    CHECK(slot_of(&t, x) == S_CAP + 7 && slot_of(&t, r) == 8 && slot_of(&t, s) == S_CAP + 3);
    CHECK(ck_check(&t) && t.n == 5);
    CHECK(ck_find(&t, p, &v, NULL) && v == 7);

    /* 3개째 키는 구조적으로 불가능: CK_CYCLE, 그리고 표는 1바이트도 안 변한다 */
    ck_init(&t, g_s, S_CAP, h_forced, CK_MAX_PATH, 0, 1);
    uint32_t k0 = KEY(4, 6, 0), k1 = KEY(4, 6, 1), k2 = KEY(4, 6, 2);
    CHECK(ck_put(&t, k0, 1, &kicks) == CK_OK);
    CHECK(ck_put(&t, k1, 2, &kicks) == CK_OK);
    memcpy(g_snap, g_s, sizeof g_s);
    CHECK(ck_put(&t, k2, 3, &kicks) == CK_CYCLE);
    CHECK(memcmp(g_snap, g_s, sizeof g_s) == 0 && t.n == 2);

    /* 예산: T0 쪽 3칸, T1 쪽 2칸. limit 으로 경로 선택이 달라진다 */
    uint32_t c1 = KEY(1, 2, 0), c2 = KEY(3, 2, 0), c3 = KEY(3, 4, 0);
    uint32_t d1 = KEY(7, 6, 0), d2 = KEY(7, 8, 0), y = KEY(1, 6, 0);
    ck_init(&t, g_s, S_CAP, h_forced, CK_MAX_PATH, 0, 1);
    CHECK(ck_put_walk(&t, c2, 1, &kicks, &home) == CK_OK);
    CHECK(ck_put_walk(&t, c3, 2, &kicks, &home) == CK_OK && kicks == 1);
    CHECK(ck_put_walk(&t, c1, 3, &kicks, &home) == CK_OK);
    CHECK(ck_put_walk(&t, d1, 4, &kicks, &home) == CK_OK);
    CHECK(ck_put_walk(&t, d2, 5, &kicks, &home) == CK_OK && kicks == 1);
    memcpy(g_snap, g_s, sizeof g_s);
    t.limit = 1;
    CHECK(ck_put(&t, y, 6, &kicks) == CK_BUDGET);
    CHECK(memcmp(g_snap, g_s, sizeof g_s) == 0);
    t.limit = 2;
    CHECK(ck_put(&t, y, 6, &kicks) == CK_OK && kicks == 2);    /* T0 은 예산 초과, T1 로 성공 */
    CHECK(slot_of(&t, y) == S_CAP + 6 && ck_check(&t));
}

static void test_b_oracle(void) {
    ck_t t; uint32_t rng = 0xDEADBEEFu, bad = 0, cycles = 0, oks = 0, other_side = 0, kicks, maxk = 0;
    ck_init(&t, g_s, S_CAP, ck_mix, CK_MAX_PATH, 0, 0);
    for (uint32_t trial = 0; trial < 20000u; trial++) {
        t.seed[0] = xs32(&rng); t.seed[1] = xs32(&rng);
        ck_clear(&t);
        uint32_t fails = 0;
        while (fails < 5) {
            uint32_t k = xs32(&rng);
            if (k == CK_EMPTY) continue;
            bool can = oracle_can_add(&t, k);
            uint32_t s0 = ck_home(&t, 0, k), s1 = ck_home(&t, 1, k), path[CK_MAX_PATH + 1], len = 0;
            bool both_full = t.slots[s0].key != CK_EMPTY && t.slots[s1].key != CK_EMPTY;
            walk_t r0 = both_full ? ck_walk(&t, s0, path, &len) : WALK_FOUND;
            memcpy(g_snap, g_s, sizeof g_s);
            ck_status_t st = ck_put(&t, k, 1, &kicks);
            if (st == CK_BUDGET) bad++;
            if ((st == CK_CYCLE) == can) bad++;              /* 감지 결과와 오라클이 일치해야 한다 */
            if (st == CK_CYCLE) {
                cycles++; fails++;
                if (memcmp(g_snap, g_s, sizeof g_s) != 0) bad++;
            } else {
                oks++;
                if (kicks > maxk) maxk = kicks;
                if (both_full && r0 == WALK_CYCLE) other_side++;
            }
            if (!ck_check(&t)) bad++;
            if (t.n == 2u * S_CAP) break;
        }
    }
    printf("   oracle: ok=%u cycle=%u t1_rescue=%u max_kicks=%u\n", oks, cycles, other_side, maxk);
    CHECK(bad == 0);
    CHECK(cycles > 1000u && oks > 1000u && other_side > 100u);
}

static void test_b_rebuild(void) {
    ck_t a, b; uint32_t kicks, v = 0;
    ck_init(&a, g_s, S_CAP, h_forced, CK_MAX_PATH, 0, 1);
    ck_init(&b, g_s2, S_CAP, h_forced, CK_MAX_PATH, 0, 1);
    uint32_t k0 = KEY(4, 6, 0), k1 = KEY(4, 6, 1), k2 = KEY(4, 6, 2), k3 = KEY(9, 9, 0);
    ck_put(&a, k0, 10, &kicks); ck_put(&a, k1, 11, &kicks); ck_put(&a, k3, 13, &kicks);
    CHECK(ck_put(&a, k2, 12, &kicks) == CK_CYCLE);
    memcpy(g_snap, g_s, sizeof g_s);
    ck_slot_t extra = { k2, 12 };

    /* 시드 0/1 그대로면 실패, 그리고 src 는 안 변한다 */
    CHECK(ck_rebuild(&b, &a, &extra, 0, 1) == CK_CYCLE);
    CHECK(memcmp(g_snap, g_s, sizeof g_s) == 0 && a.n == 3);

    CHECK(ck_rebuild(&b, &a, &extra, 0x11111111u, 0x22222222u) == CK_OK);
    CHECK(b.n == 4 && ck_check(&b));
    CHECK(ck_find(&b, k0, &v, NULL) && v == 10);
    CHECK(ck_find(&b, k1, &v, NULL) && v == 11);
    CHECK(ck_find(&b, k2, &v, NULL) && v == 12);
    CHECK(ck_find(&b, k3, &v, NULL) && v == 13);
    CHECK(memcmp(g_snap, g_s, sizeof g_s) == 0);
}

/* ---- ISR 흉내: 슬롯 쓰기가 끝날 때마다 끼어들어 살아 있는 키를 전부 조회한다 ---- */
#define U_N 280u
static uint32_t g_ids[U_N], g_vals[U_N];
static bool g_live[U_N];
static ck_map_t *g_isr_map;
static const ck_t *g_isr_table;
static uint32_t g_isr_keys[8], g_isr_tvals[8], g_isr_tn;
static uint64_t g_isr_calls, g_isr_missing, g_isr_badval;
static uint32_t g_isr_maxp;

static void isr_map(void) {
    g_isr_calls++;
    for (uint32_t i = 0; i < U_N; i++) {
        if (!g_live[i]) continue;
        uint32_t v = 0, p = 0;
        if (!map_lookup(g_isr_map, g_ids[i], &v, &p)) g_isr_missing++;
        else if (v != g_vals[i]) g_isr_badval++;
        if (p > g_isr_maxp) g_isr_maxp = p;
    }
}
static void isr_table(void) {
    g_isr_calls++;
    for (uint32_t i = 0; i < g_isr_tn; i++) {
        uint32_t v = 0, p = 0;
        if (!ck_find(g_isr_table, g_isr_keys[i], &v, &p)) g_isr_missing++;
        else if (v != g_isr_tvals[i]) g_isr_badval++;
        if (p > g_isr_maxp) g_isr_maxp = p;
    }
}
static void isr_reset(void) { g_isr_calls = g_isr_missing = g_isr_badval = 0; g_isr_maxp = 0; }

static void test_c_window(void) {
    printf("-- Part C: ISR 조회 + 태스크 갱신 --\n");
    ck_t t; uint32_t kicks; ck_slot_t home = {0, 0};
    uint32_t a = KEY(1, 2, 0), b = KEY(1, 5, 0);
    ck_init(&t, g_s, S_CAP, h_forced, 20, 0, 1);
    g_isr_table = &t; g_isr_keys[0] = a; g_isr_tvals[0] = 10; g_isr_tn = 1;

    /* 고전 사슬: b 가 T0[1] 을 덮는 순간 a 는 어디에도 없다 */
    ck_put_walk(&t, a, 10, &kicks, &home);
    isr_reset(); g_store_hook = isr_table;
    ck_put_walk(&t, b, 20, &kicks, &home);
    g_store_hook = NULL;
    printf("   classic walk: isr calls=%llu missing=%llu\n", (unsigned long long)g_isr_calls,
           (unsigned long long)g_isr_missing);
    CHECK(kicks == 1 && g_isr_missing == 1);

    /* 경로 우선: 같은 장면에서 뒤에서부터 복사 → a 가 사라지는 순간이 없다 */
    ck_init(&t, g_s, S_CAP, h_forced, 20, 0, 1);
    uint32_t c = KEY(3, 5, 0), y = KEY(1, 5, 1);
    ck_put(&t, a, 10, &kicks);
    ck_put(&t, c, 30, &kicks);
    ck_put(&t, KEY(3, 9, 0), 31, &kicks);           /* c 와 T0[3] 공유 → T1[9] 로 */
    ck_put(&t, b, 20, &kicks);                      /* T0[1] 이 찼으니 T1[5] 로 */
    g_isr_keys[1] = b; g_isr_tvals[1] = 20;
    g_isr_keys[2] = c; g_isr_tvals[2] = 30; g_isr_tn = 3;
    isr_reset(); g_store_hook = isr_table;
    ck_status_t st = ck_put(&t, y, 40, &kicks);     /* T0[1], T1[5] 모두 참 → 축출 필요 */
    g_store_hook = NULL;
    printf("   path-first: status=%d kicks=%u isr calls=%llu missing=%llu\n", (int)st, kicks,
           (unsigned long long)g_isr_calls, (unsigned long long)g_isr_missing);
    CHECK(st == CK_OK && kicks >= 1 && g_isr_calls == kicks + 1u);
    CHECK(g_isr_missing == 0 && g_isr_badval == 0 && g_isr_maxp <= 2);
    CHECK(ck_check(&t));
}

#define MAP_CAP 256u
static ck_slot_t g_b0[2 * MAP_CAP], g_b1[2 * MAP_CAP];

static void test_c_map(void) {
    static ck_map_t m;
    uint32_t v = 0, rng = 0xC0FFEE11u, bad = 0, puts = 0, dels = 0;
    /* 강제 재해시: 슬롯 쌍이 같은 키 3개 */
    map_init(&m, g_b0, g_b1, MAP_CAP, h_forced, 32, 0, 1, 0xABCD1234u);
    uint32_t k0 = KEY(4, 6, 0), k1 = KEY(4, 6, 1), k2 = KEY(4, 6, 2);
    memset(g_live, 0, sizeof g_live);
    g_ids[0] = k0; g_vals[0] = 100; g_ids[1] = k1; g_vals[1] = 101;
    g_isr_map = &m; isr_reset();
    CHECK(map_put(&m, k0, 100) == CK_OK); g_live[0] = true;
    CHECK(map_put(&m, k1, 101) == CK_OK); g_live[1] = true;
    g_store_hook = isr_map;
    CHECK(map_put(&m, k2, 102) == CK_OK);
    g_store_hook = NULL;
    CHECK(m.rehashes == 1 && atomic_load(&m.cur) == 1u);
    CHECK(g_isr_calls > 0 && g_isr_missing == 0 && g_isr_badval == 0);
    CHECK(map_lookup(&m, k2, &v, NULL) && v == 102);
    CHECK(map_lookup(&m, k0, &v, NULL) && v == 100);

    /* 시드가 안 먹는 해시: 시도를 다 쓰면 CK_FULL, 기존 키는 그대로 */
    map_init(&m, g_b0, g_b1, MAP_CAP, h_stuck, 32, 7, 9, 0xABCD1234u);
    CHECK(map_put(&m, KEY(4, 0, 0), 1) == CK_OK);
    CHECK(map_put(&m, KEY(4, 0, 1), 2) == CK_OK);
    CHECK(map_put(&m, KEY(4, 0, 2), 3) == CK_FULL);
    CHECK(m.failed_attempts == m.max_attempts && m.rehashes == 0);
    CHECK(map_lookup(&m, KEY(4, 0, 0), &v, NULL) && v == 1);
    CHECK(map_lookup(&m, KEY(4, 0, 1), &v, NULL) && v == 2);
    CHECK(!map_lookup(&m, KEY(4, 0, 2), &v, NULL));

    /* CAN 29비트 ID → 핸들러: 슬롯 쓰기마다 ISR 이 끼어들어 살아 있는 키를 전부 확인 */
    map_init(&m, g_b0, g_b1, MAP_CAP, ck_mix, 32, 0x5A5A5A5Au, 0xA5A5A5A5u, 0x600DF00Du);
    memset(g_live, 0, sizeof g_live);
    for (uint32_t i = 0; i < U_N; i++) g_ids[i] = xs32(&rng) & 0x1FFFFFFFu;
    g_isr_map = &m; isr_reset(); g_max_probes = 0;
    g_store_hook = isr_map;
    for (uint32_t op = 0; op < 20000u; op++) {
        uint32_t r = xs32(&rng), i = r % U_N;
        if (!g_live[i]) {
            g_vals[i] = op;
            ck_status_t st = map_put(&m, g_ids[i], op);
            if (st != CK_OK) bad++;
            else g_live[i] = true;
            puts++;
        } else if (((r >> 12) & 7u) == 0) {
            g_live[i] = false;
            if (!map_del(&m, g_ids[i])) bad++;
            dels++;
        } else {
            g_vals[i] = op;                     /* 쓰기 직후 훅이 돌므로 새 값이 보여야 한다 */
            if (map_put(&m, g_ids[i], op) != CK_UPDATED) bad++;
        }
        const ck_t *b = &m.bank[atomic_load(&m.cur)];
        if (!ck_check(b)) bad++;
    }
    g_store_hook = NULL;
    const ck_t *b = &m.bank[atomic_load(&m.cur)];
    printf("   map: puts=%u dels=%u n=%u/%u rehashes=%u failed=%u isr_calls=%llu missing=%llu maxp=%u\n",
           puts, dels, b->n, 2u * MAP_CAP, m.rehashes, m.failed_attempts,
           (unsigned long long)g_isr_calls, (unsigned long long)g_isr_missing, g_isr_maxp);
    CHECK(bad == 0);
    CHECK(g_isr_missing == 0 && g_isr_badval == 0);
    CHECK(g_isr_maxp <= 2 && g_isr_maxp > 0);
    CHECK(m.rehashes > 0);
}

#define BIG_CAP 16384u
static ck_slot_t g_big[2 * BIG_CAP];

static void test_c_lookups(void) {
    static uint32_t keys[BIG_CAP * 2];
    ck_t t; uint32_t key_rng = 0x31415926u, miss_rng = 0x27182818u, kicks, nk = 0, fail = 0, missbad = 0;
    uint32_t hist[4] = {0};
    ck_init(&t, g_big, BIG_CAP, ck_mix, CK_MAX_PATH, 0x1F2E3D4Cu, 0x5B6A7988u);
    uint32_t target = (uint32_t)(0.45 * 2.0 * BIG_CAP);
    while (t.n < target) {
        uint32_t k = xs32(&key_rng);
        if (k == CK_EMPTY) continue;
        if (ck_put(&t, k, k ^ 0x5555u, &kicks) == CK_OK) keys[nk++] = k;
        else if (++fail > 1000u) break;                 /* 스텁에서도 끝나도록 */
    }
    CHECK(ck_check(&t));
    /* 100만 번 조회: 절반 hit(넣은 키를 순환), 절반 miss(무작위 키) */
    for (uint32_t i = 0; i < 1000000u; i++) {
        uint32_t k, p = 0, v = 0;
        bool hit = (i & 1u) && nk > 0;
        k = hit ? keys[(i >> 1) % nk] : xs32(&miss_rng);
        if (k == CK_EMPTY) k = 0;
        bool f = ck_find(&t, k, &v, &p);
        if (p > 3) p = 3;
        hist[p]++;
        if (hit && (!f || v != (k ^ 0x5555u))) missbad++;
    }
    printf("   1M lookups at load %.3f: probes 1=%u 2=%u >2=%u, put_fail=%u\n",
           (double)t.n / (2.0 * BIG_CAP), hist[1], hist[2], hist[3], fail);
    CHECK(hist[3] == 0 && hist[0] == 0);
    CHECK(missbad == 0 && nk == t.n && t.n >= target);
}

/* 부하율 한계: 키를 넣다가 처음 실패한 시점의 load. 넉넉한 예산, 시드 30개. */
static void test_c_ceiling(void) {
    static ck_slot_t buf[2 * 1024];
    double loads[30];
    uint32_t rng = 0x0BADCAFEu;
    for (int tr = 0; tr < 30; tr++) {
        ck_t t; uint32_t kicks;
        ck_init(&t, buf, 1024, ck_mix, CK_MAX_PATH, xs32(&rng), xs32(&rng));
        for (;;) {
            uint32_t k = xs32(&rng);
            if (k == CK_EMPTY) continue;
            ck_status_t st = ck_put(&t, k, 1, &kicks);
            if (st == CK_CYCLE || st == CK_BUDGET || st == CK_FULL) break;
            if (t.n >= 2u * 1024u) break;
        }
        loads[tr] = (double)t.n / 2048.0;
    }
    for (int i = 1; i < 30; i++) {
        double x = loads[i]; int j = i - 1;
        while (j >= 0 && loads[j] > x) { loads[j + 1] = loads[j]; j--; }
        loads[j + 1] = x;
    }
    printf("   first-failure load (cap=1024x2, 30 seeds): min %.3f median %.3f max %.3f\n",
           loads[0], loads[15], loads[29]);
    CHECK(loads[15] > 0.40 && loads[15] < 0.60);
    CHECK(loads[0] > 0.20);
}

int main(void) {
    test_checker();
    test_a_scripted();
    test_a_random();
    test_a_small();
    test_b_scripted();
    test_b_oracle();
    test_b_rebuild();
    test_c_window();
    test_c_map();
    test_c_lookups();
    test_c_ceiling();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
