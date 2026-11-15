/* DS-A07 추가 연습 — 한 번에 검증
 * 연습 1: 메모리 추가 없는 제자리 재시드 (Pagh–Rodler 의 방법)
 * 연습 2: 두 방향을 다 보고 짧은 경로 고르기
 * 연습 3: 경로를 안 걷는 입장 판정 — 증분 union-find 와 삭제의 함정
 * 연습 4: 두 시드가 같으면 한계가 어디까지 떨어지나
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(e) do { g_total++; if (e) { g_pass++; printf("PASS %s\n", #e); } \
                      else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #e); } while (0)

#define CK_EMPTY    0xFFFFFFFFu
#define CK_MAX_PATH 128u
typedef enum { CK_OK, CK_UPDATED, CK_BAD_KEY, CK_CYCLE, CK_BUDGET, CK_FULL } ck_status_t;
typedef enum { WALK_FOUND, WALK_CYCLE, WALK_LONG } walk_t;
typedef struct { uint32_t key, val; } ck_slot_t;
typedef uint32_t (*ck_hash_fn)(uint32_t seed, uint32_t key);
typedef struct { ck_slot_t *slots; uint32_t cap, mask, n, seed[2]; ck_hash_fn hash; uint32_t limit; } ck_t;

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }
static uint32_t ck_mix(uint32_t seed, uint32_t key) {
    uint32_t h = key ^ seed;
    h ^= h >> 16; h *= 0x85ebca6bu; h ^= h >> 13; h *= 0xc2b2ae35u; h ^= h >> 16;
    return h;
}
static uint32_t h_stuck(uint32_t seed, uint32_t key) { (void)seed; return key & 0xFFu; }

static inline uint32_t ck_home(const ck_t *t, uint32_t w, uint32_t k) { return (w ? t->cap : 0u) + (t->hash(t->seed[w], k) & t->mask); }
static inline uint32_t ck_other(const ck_t *t, uint32_t s, uint32_t k) { return ck_home(t, s >= t->cap ? 0u : 1u, k); }
static void ck_clear(ck_t *t) { for (uint32_t i = 0; i < 2u * t->cap; i++) { t->slots[i].key = CK_EMPTY; t->slots[i].val = 0; } t->n = 0; }
static void ck_init(ck_t *t, ck_slot_t *b, uint32_t cap, ck_hash_fn h, uint32_t limit, uint32_t s0, uint32_t s1) {
    t->slots = b; t->cap = cap; t->mask = cap - 1; t->hash = h; t->limit = limit; t->seed[0] = s0; t->seed[1] = s1; ck_clear(t);
}
static bool ck_find(const ck_t *t, uint32_t k, uint32_t *v) {
    if (k == CK_EMPTY) return false;
    uint32_t s = ck_home(t, 0, k);
    if (t->slots[s].key == k) { *v = t->slots[s].val; return true; }
    s = ck_home(t, 1, k);
    if (t->slots[s].key == k) { *v = t->slots[s].val; return true; }
    return false;
}
static bool ck_check(const ck_t *t) {
    uint32_t used = 0;
    for (uint32_t s = 0; s < 2u * t->cap; s++) {
        uint32_t k = t->slots[s].key;
        if (k == CK_EMPTY) continue;
        used++;
        if (s != ck_home(t, 0, k) && s != ck_home(t, 1, k)) return false;
        if (t->slots[ck_other(t, s, k)].key == k) return false;
    }
    return used == t->n;
}
static walk_t ck_walk(const ck_t *t, uint32_t start, uint32_t *path, uint32_t *len) {
    uint32_t s = start, tortoise = start, power = 1, lam = 0, n = 0;
    for (;;) {
        if (n > t->limit) { *len = n; return WALK_LONG; }
        path[n++] = s;
        if (t->slots[s].key == CK_EMPTY) { *len = n; return WALK_FOUND; }
        s = ck_other(t, s, t->slots[s].key);
        if (s == tortoise) { *len = n; return WALK_CYCLE; }
        if (++lam == power) { tortoise = s; power <<= 1; lam = 0; }
    }
}
static void apply_path(ck_t *t, const uint32_t *path, uint32_t len, uint32_t key, uint32_t val) {
    for (uint32_t i = len - 1; i > 0; i--) t->slots[path[i]] = t->slots[path[i - 1]];
    t->slots[path[0]].key = key; t->slots[path[0]].val = val;
    t->n++;
}
/* 본문 ck_put 과 같다 (T0 쪽 먼저, 안 되면 T1 쪽). */
static ck_status_t ck_put(ck_t *t, uint32_t k, uint32_t v, uint32_t *kicks) {
    uint32_t path[CK_MAX_PATH + 1], len = 0, a = ck_home(t, 0, k), b = ck_home(t, 1, k);
    *kicks = 0;
    if (t->slots[a].key == CK_EMPTY) { t->slots[a].key = k; t->slots[a].val = v; t->n++; return CK_OK; }
    if (t->slots[b].key == CK_EMPTY) { t->slots[b].key = k; t->slots[b].val = v; t->n++; return CK_OK; }
    walk_t r0 = ck_walk(t, a, path, &len), r = r0;
    if (r0 != WALK_FOUND) r = ck_walk(t, b, path, &len);
    if (r != WALK_FOUND) return (r0 == WALK_CYCLE && r == WALK_CYCLE) ? CK_CYCLE : CK_BUDGET;
    apply_path(t, path, len, k, v);
    *kicks = len - 1;
    return CK_OK;
}

/* ---------- 연습 1: 제자리 재시드 ----------
 * 이 키가 지금 자기 슬롯에 있는가? 새 시드 기준으로 계산하면 "옮겼나/아직인가" 비트가 필요 없다.
 * carry 를 놓고, 쫓겨난 키가 제자리였으면 반대쪽으로, 제자리가 아니었으면 새로 삽입(T0 부터). */
static bool inplace_insert(ck_t *t, ck_slot_t carry, ck_slot_t *left) {
    uint32_t s = ck_home(t, 0, carry.key);
    for (uint32_t kicks = 0; kicks <= t->limit; kicks++) {
        ck_slot_t old = t->slots[s];
        t->slots[s] = carry;
        if (old.key == CK_EMPTY) { t->n++; return true; }
        bool in_place = (s == ck_home(t, 0, old.key)) || (s == ck_home(t, 1, old.key));
        carry = old;
        s = in_place ? ck_other(t, s, carry.key) : ck_home(t, 0, carry.key);
    }
    *left = carry;
    return false;
}
/* attempts 번까지 새 시드로 재시도. 성공 CK_OK. 다 실패하면 CK_FULL + 손에 든 키를 *homeless 로. */
static ck_status_t ck_reseed_inplace(ck_t *t, uint32_t attempts, uint32_t *rng, ck_slot_t *homeless) {
    ck_slot_t carry = { CK_EMPTY, 0 };
    for (uint32_t a = 0; a < attempts; a++) {
        t->seed[0] = xs32(rng); t->seed[1] = xs32(rng);
        if (carry.key != CK_EMPTY) {
            ck_slot_t left;
            if (!inplace_insert(t, carry, &left)) { carry = left; continue; }
            carry.key = CK_EMPTY;
        }
        bool ok = true;
        for (uint32_t s = 0; s < 2u * t->cap && ok; s++) {
            ck_slot_t k = t->slots[s];
            if (k.key == CK_EMPTY) continue;
            if (s == ck_home(t, 0, k.key) || s == ck_home(t, 1, k.key)) continue;
            t->slots[s].key = CK_EMPTY; t->slots[s].val = 0; t->n--;
            ck_slot_t left;
            if (!inplace_insert(t, k, &left)) { carry = left; ok = false; }
        }
        if (ok) return CK_OK;
    }
    *homeless = carry;
    return CK_FULL;
}

static ck_slot_t g_a[2 * 1024], g_b[2 * 1024];

static void ex1(void) {
    printf("-- 연습 1: 제자리 재시드 --\n");
    ck_t t; uint32_t rng = 0xBEEF1234u, kicks, keys[900], nk = 0, v = 0, bad = 0;
    ck_init(&t, g_a, 1024, ck_mix, 64, 0x11111111u, 0x22222222u);
    while (nk < 900) {
        uint32_t k = xs32(&rng);
        if (ck_put(&t, k, k ^ 0xAAAAu, &kicks) == CK_OK) keys[nk++] = k;
    }
    uint32_t before0 = t.seed[0];
    ck_slot_t homeless = { CK_EMPTY, 0 };
    CHECK(ck_reseed_inplace(&t, 16, &rng, &homeless) == CK_OK);
    CHECK(t.seed[0] != before0);
    CHECK(ck_check(&t) && t.n == 900);
    for (uint32_t i = 0; i < nk; i++) if (!ck_find(&t, keys[i], &v) || v != (keys[i] ^ 0xAAAAu)) bad++;
    CHECK(bad == 0);

    /* 시드가 안 먹는 해시: 실패해도 키가 하나도 안 사라지는가. 슬롯 쌍이 같은 키 3개 */
    static ck_slot_t s[2 * 16];
    ck_init(&t, s, 16, h_stuck, 8, 7, 9);
    uint32_t k0 = 0x10004u, k1 = 0x20004u, k2 = 0x30004u;
    CHECK(ck_put(&t, k0, 1, &kicks) == CK_OK);
    CHECK(ck_put(&t, k1, 2, &kicks) == CK_OK);
    t.slots[5].key = k2; t.slots[5].val = 3; t.n++;     /* 다른 시드로 놓였던 키라고 가정 */
    homeless.key = CK_EMPTY;
    CHECK(ck_reseed_inplace(&t, 3, &rng, &homeless) == CK_FULL);
    uint32_t seen = (homeless.key == k0 || homeless.key == k1 || homeless.key == k2) ? 1u : 0u;
    for (uint32_t i = 0; i < 32; i++) {
        uint32_t x = t.slots[i].key;
        if (x == k0 || x == k1 || x == k2) seen++;
    }
    printf("   stuck hash: n=%u homeless=0x%x, keys accounted for=%u/3\n", t.n, homeless.key, seen);
    CHECK(seen == 3 && t.n == 2);
}

/* ---------- 연습 2: 짧은 쪽 고르기 ---------- */
static ck_status_t ck_put_shorter(ck_t *t, uint32_t k, uint32_t v, uint32_t *kicks) {
    uint32_t pa[CK_MAX_PATH + 1], pb[CK_MAX_PATH + 1], la = 0, lb = 0, a = ck_home(t, 0, k), b = ck_home(t, 1, k);
    *kicks = 0;
    if (t->slots[a].key == CK_EMPTY) { t->slots[a].key = k; t->slots[a].val = v; t->n++; return CK_OK; }
    if (t->slots[b].key == CK_EMPTY) { t->slots[b].key = k; t->slots[b].val = v; t->n++; return CK_OK; }
    walk_t ra = ck_walk(t, a, pa, &la), rb = ck_walk(t, b, pb, &lb);
    if (ra != WALK_FOUND && rb != WALK_FOUND)
        return (ra == WALK_CYCLE && rb == WALK_CYCLE) ? CK_CYCLE : CK_BUDGET;
    bool use_a = ra == WALK_FOUND && (rb != WALK_FOUND || la <= lb);
    apply_path(t, use_a ? pa : pb, use_a ? la : lb, k, v);
    *kicks = (use_a ? la : lb) - 1;
    return CK_OK;
}

static void ex2(void) {
    printf("-- 연습 2: 짧은 경로 고르기 --\n");
    ck_t x, y; uint32_t rng, k1, k2; uint64_t sx = 0, sy = 0; uint32_t mx = 0, my = 0, n = 0;
    for (uint32_t tr = 0; tr < 40; tr++) {
        ck_init(&x, g_a, 1024, ck_mix, CK_MAX_PATH, 0x1234u * (tr + 1), 0x8765u * (tr + 1));
        ck_init(&y, g_b, 1024, ck_mix, CK_MAX_PATH, 0x1234u * (tr + 1), 0x8765u * (tr + 1));
        rng = 0xFACE0001u + tr;
        while (x.n < 922u) {   /* load 0.45 */
            uint32_t k = xs32(&rng);
            if (ck_put(&x, k, 1, &k1) != CK_OK || ck_put_shorter(&y, k, 1, &k2) != CK_OK) break;
            sx += k1; sy += k2; n++;
            if (k1 > mx) mx = k1;
            if (k2 > my) my = k2;
        }
    }
    printf("   T0-first: total kicks %llu max %u   shorter: total %llu max %u   (inserts %u)\n",
           (unsigned long long)sx, mx, (unsigned long long)sy, my, n);
    CHECK(sy < sx);
    CHECK(my <= mx);
    CHECK(ck_check(&y));
}

/* ---------- 연습 3: 증분 union-find 입장 판정 ---------- */
#define UF_CAP 32u
typedef struct { uint32_t parent[2 * UF_CAP]; bool cyc[2 * UF_CAP]; } uf_t;
static uint32_t uf_root(uf_t *u, uint32_t x) { while (u->parent[x] != x) { u->parent[x] = u->parent[u->parent[x]]; x = u->parent[x]; } return x; }
static void uf_init(uf_t *u) { for (uint32_t i = 0; i < 2 * UF_CAP; i++) { u->parent[i] = i; u->cyc[i] = false; } }
/* 간선 (a,b) 를 더해도 "연결 성분의 간선 수 ≤ 정점 수" 인가? 그렇다면 그래프에 반영하고 true. */
static bool uf_try_add(uf_t *u, uint32_t a, uint32_t b) {
    uint32_t ra = uf_root(u, a), rb = uf_root(u, b);
    if (ra == rb) {
        if (u->cyc[ra]) return false;
        u->cyc[ra] = true;
        return true;
    }
    if (u->cyc[ra] && u->cyc[rb]) return false;
    u->parent[ra] = rb;
    u->cyc[rb] = u->cyc[rb] || u->cyc[ra];
    return true;
}

static ck_slot_t g_s[2 * UF_CAP];

static void ex3(void) {
    printf("-- 연습 3: union-find 입장 판정 --\n");
    ck_t t; uf_t u; uint32_t rng = 0x55AA55AAu, kicks, bad = 0, adds = 0, rejects = 0;
    uint32_t stale_rejects = 0, walk_ok = 0;
    for (uint32_t tr = 0; tr < 2000; tr++) {
        ck_init(&t, g_s, UF_CAP, ck_mix, CK_MAX_PATH, xs32(&rng), xs32(&rng));
        uf_init(&u);
        uint32_t keys[128], nk = 0;
        for (uint32_t i = 0; i < 100; i++) {
            uint32_t k = xs32(&rng), a = ck_home(&t, 0, k), b = ck_home(&t, 1, k);
            bool ok = uf_try_add(&u, a, b);
            uint32_t pre = t.n;
            ck_status_t st = ck_put(&t, k, 1, &kicks);
            if (ok != (st == CK_OK)) bad++;
            if (st == CK_OK) { adds++; keys[nk++] = k; if (t.n != pre + 1) bad++; }
            else rejects++;
            if (t.n == 2 * UF_CAP) break;
        }
        /* 삭제 뒤: UF 는 줄어들지 않으므로, 실제로는 들어가는 키를 거절할 수 있다 */
        for (uint32_t i = 0; i < nk; i += 2) {
            uint32_t s = ck_home(&t, 0, keys[i]);
            if (t.slots[s].key != keys[i]) s = ck_home(&t, 1, keys[i]);
            t.slots[s].key = CK_EMPTY; t.slots[s].val = 0; t.n--;
        }
        for (uint32_t i = 0; i < 20; i++) {
            uint32_t k = xs32(&rng), a = ck_home(&t, 0, k), b = ck_home(&t, 1, k);
            bool ok = uf_try_add(&u, a, b);
            ck_status_t st = ck_put(&t, k, 1, &kicks);
            if (st == CK_OK) walk_ok++;
            if (!ok && st == CK_OK) stale_rejects++;
        }
        if (!ck_check(&t)) bad++;
    }
    printf("   no-delete phase: adds=%u rejects=%u mismatches=%u; after deletes: walk ok=%u, stale rejects=%u\n",
           adds, rejects, bad, walk_ok, stale_rejects);
    CHECK(bad == 0);
    CHECK(rejects > 100u);
    CHECK(stale_rejects > 0);
}

/* ---------- 연습 4: 두 시드가 같으면 ---------- */
static double first_failure(uint32_t s0, uint32_t s1, uint32_t *rng) {
    ck_t t; uint32_t kicks;
    ck_init(&t, g_a, 1024, ck_mix, CK_MAX_PATH, s0, s1);
    for (;;) {
        uint32_t k = xs32(rng);
        if (ck_put(&t, k, 1, &kicks) != CK_OK) break;
        if (t.n == 2048) break;
    }
    return (double)t.n / 2048.0;
}
static void ex4(void) {
    printf("-- 연습 4: 같은 시드 --\n");
    uint32_t rng = 0x31337000u; double sum_diff = 0, sum_same = 0;
    for (int i = 0; i < 40; i++) {
        uint32_t s0 = xs32(&rng), s1 = xs32(&rng);
        sum_diff += first_failure(s0, s1, &rng);
        sum_same += first_failure(s0, s0, &rng);
    }
    printf("   mean first-failure load: distinct seeds %.3f, same seed %.3f\n", sum_diff / 40, sum_same / 40);
    CHECK(sum_diff / 40 > 0.45);
    CHECK(sum_same / 40 < 0.15);
}

int main(void) {
    ex1(); ex2(); ex3(); ex4();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
