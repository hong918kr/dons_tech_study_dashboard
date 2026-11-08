/* DS-M13 추가 연습 — 한 번에 검증
 *   연습 1: 이진 트라이 삭제 + 죽은 가지 가지치기 (free list 로 노드 재사용)
 *   연습 2: stride-8 멀티비트 트라이 (controlled prefix expansion) — 조회 메모리 접근 <= 4
 *   연습 3: longer-prefixes 열거 — 서브트리 전위 순회 = (prefix, len) 정렬 순서
 *   연습 4: IP 범위 → 최소 CIDR 목록 (그리디 vs 재귀 분할 대조)
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define NH_NONE 0xFFFFu
#define IP(a, b, c, d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))

static uint32_t bit_at(uint32_t a, uint32_t i) { return (a >> (31u - i)) & 1u; }      /* i <= 31 */
static uint32_t mask(uint32_t len) { return len == 0 ? 0u : 0xFFFFFFFFu << (32u - len); }  /* len 0..32 */

static uint32_t rng = 1u;
static uint32_t xorshift32(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

/* 참조 모델: 선형 탐색 */
typedef struct { uint32_t prefix; uint8_t len; uint16_t nh; } route_t;
#define REF_MAX 2048u
static route_t g_ref[REF_MAX];
static uint32_t g_ref_n;
static void ref_set(uint32_t p, uint32_t len, uint16_t nh) {
    p &= mask(len);
    for (uint32_t i = 0; i < g_ref_n; i++)
        if (g_ref[i].prefix == p && g_ref[i].len == len) { g_ref[i].nh = nh; return; }
    if (g_ref_n < REF_MAX) g_ref[g_ref_n++] = (route_t){ p, (uint8_t)len, nh };
}
static bool ref_del(uint32_t p, uint32_t len) {
    p &= mask(len);
    for (uint32_t i = 0; i < g_ref_n; i++)
        if (g_ref[i].prefix == p && g_ref[i].len == len) { g_ref[i] = g_ref[--g_ref_n]; return true; }
    return false;
}
static uint16_t ref_lookup(uint32_t addr) {
    uint16_t best = NH_NONE; int blen = -1;
    for (uint32_t i = 0; i < g_ref_n; i++)
        if ((addr & mask(g_ref[i].len)) == g_ref[i].prefix && (int)g_ref[i].len > blen) { blen = g_ref[i].len; best = g_ref[i].nh; }
    return best;
}
static void gen_dense(uint32_t *p, uint32_t *len) {
    uint32_t r = xorshift32();
    *p = (r & 7u) == 0 ? xorshift32() : (IP(10, 0, 0, 0) | ((r >> 3) & 3u) << 16 | (xorshift32() & 0xFFFFu));
    *len = (r >> 8) % 33u;
}
static uint32_t gen_addr(void) {
    uint32_t r = xorshift32();
    if (g_ref_n == 0 || (r & 1u)) return xorshift32();
    const route_t *e = &g_ref[(r >> 1) % g_ref_n];
    return e->prefix | (xorshift32() & ~mask(e->len));
}

/* ---------- 연습 1: 이진 트라이 + 삭제 (가지치기) ---------- */
#define BT_POOL 16384u
typedef struct { uint16_t child[2]; uint16_t nh; } bt_node_t;
typedef struct { bt_node_t n[BT_POOL]; uint16_t free_head; uint32_t used, routes; } btrie_t;

static void bt_init(btrie_t *t) {
    t->n[0] = (bt_node_t){ { 0, 0 }, NH_NONE };
    t->used = 1; t->routes = 0; t->free_head = 0;
    for (uint32_t i = BT_POOL - 1u; i >= 1u; i--) { t->n[i].child[0] = t->free_head; t->free_head = (uint16_t)i; }
}
static uint32_t bt_free_cnt(const btrie_t *t) { return BT_POOL - t->used; }
static bool bt_insert(btrie_t *t, uint32_t p, uint32_t len, uint16_t nh) {
    if (len > 32u || nh == NH_NONE) return false;
    p &= mask(len);
    uint32_t cur = 0, i = 0;
    while (i < len && t->n[cur].child[bit_at(p, i)] != 0) { cur = t->n[cur].child[bit_at(p, i)]; i++; }
    if (len - i > bt_free_cnt(t)) return false;
    for (; i < len; i++) {
        uint16_t k = t->free_head;
        t->free_head = t->n[k].child[0]; t->used++;
        t->n[k] = (bt_node_t){ { 0, 0 }, NH_NONE };
        t->n[cur].child[bit_at(p, i)] = k;
        cur = k;
    }
    if (t->n[cur].nh == NH_NONE) t->routes++;
    t->n[cur].nh = nh;
    return true;
}
static uint16_t bt_lookup(const btrie_t *t, uint32_t addr) {
    uint32_t cur = 0; uint16_t best = NH_NONE;
    for (uint32_t i = 0;; i++) {
        if (t->n[cur].nh != NH_NONE) best = t->n[cur].nh;
        if (i == 32u) break;
        uint16_t c = t->n[cur].child[bit_at(addr, i)];
        if (c == 0) break;
        cur = c;
    }
    return best;
}
/* 경로를 내려가며 노드를 path[] 에 기록 → nh 를 지우고, 아래에서부터 "경로도 자식도 없는" 노드를 떼어 반납 */
static bool bt_delete(btrie_t *t, uint32_t p, uint32_t len) {
    if (len > 32u) return false;
    p &= mask(len);
    uint16_t path[33];
    uint32_t cur = 0;
    path[0] = 0;
    for (uint32_t i = 0; i < len; i++) {
        uint16_t c = t->n[cur].child[bit_at(p, i)];
        if (c == 0) return false;
        cur = c; path[i + 1] = c;
    }
    if (t->n[cur].nh == NH_NONE) return false;
    t->n[cur].nh = NH_NONE; t->routes--;
    for (uint32_t d = len; d > 0; d--) {                   /* 루트(d = 0)는 절대 반납하지 않는다 */
        uint16_t k = path[d];
        if (t->n[k].nh != NH_NONE || t->n[k].child[0] != 0 || t->n[k].child[1] != 0) break;
        t->n[path[d - 1]].child[bit_at(p, d - 1)] = 0;
        t->n[k].child[0] = t->free_head; t->free_head = k; t->used--;
    }
    return true;
}
static bool bt_check_rec(const btrie_t *t, uint32_t k, uint32_t depth, uint32_t *nodes, uint32_t *routes) {
    const bt_node_t *n = &t->n[k];
    if (k >= BT_POOL || depth > 32u || ++*nodes > t->used) return false;
    if (n->nh != NH_NONE) (*routes)++;
    if (k != 0 && n->nh == NH_NONE && n->child[0] == 0 && n->child[1] == 0) return false;
    for (uint32_t b = 0; b < 2u; b++)
        if (n->child[b] != 0 && !bt_check_rec(t, n->child[b], depth + 1u, nodes, routes)) return false;
    return true;
}
static bool bt_check(const btrie_t *t) {
    uint32_t nodes = 0, routes = 0, f = 0;
    for (uint16_t k = t->free_head; k != 0 && f < BT_POOL; k = t->n[k].child[0]) f++;
    return bt_check_rec(t, 0, 0, &nodes, &routes) && nodes == t->used && routes == t->routes && f == BT_POOL - t->used;
}

/* ---------- 연습 2: stride-8 멀티비트 트라이 ---------- */
#define MB_POOL 256u
typedef struct { uint16_t nh; uint16_t child; uint8_t plen; } mb_ent_t;   /* 6 B (패딩 포함) */
typedef struct { mb_ent_t e[256]; } mb_node_t;                             /* 1536 B */
typedef struct { mb_node_t n[MB_POOL]; uint32_t used; } mtrie_t;           /* n[0] = 루트 */

static void mb_init(mtrie_t *t) {
    t->used = 1;
    for (uint32_t i = 0; i < 256u; i++) t->n[0].e[i] = (mb_ent_t){ NH_NONE, 0, 0 };
}
/* len 이 속한 단계 d = (len-1)/8 (len 0 은 단계 0) 의 노드까지 내려가, 2^(8(d+1) - len) 칸에 펼쳐 쓴다.
 * 이미 더 긴 prefix 가 차지한 칸(plen > len)은 건드리지 않는다 */
static bool mb_insert(mtrie_t *t, uint32_t p, uint32_t len, uint16_t nh) {
    if (len > 32u || nh == NH_NONE) return false;
    p &= mask(len);
    uint32_t d_end = len == 0 ? 0u : (len - 1u) / 8u, cur = 0;
    for (uint32_t d = 0; d < d_end; d++) {
        mb_ent_t *e = &t->n[cur].e[(p >> (24u - 8u * d)) & 0xFFu];
        if (e->child == 0) {
            if (t->used >= MB_POOL) return false;
            uint32_t k = t->used++;
            for (uint32_t i = 0; i < 256u; i++) t->n[k].e[i] = (mb_ent_t){ NH_NONE, 0, 0 };
            e->child = (uint16_t)k;
        }
        cur = e->child;
    }
    uint32_t span = 8u * (d_end + 1u) - len;               /* 펼칠 비트 수 0..8 */
    uint32_t base = (p >> (24u - 8u * d_end)) & 0xFFu;     /* 이 단계 바이트 (아래 span 비트는 0) */
    for (uint32_t j = 0; j < (1u << span); j++) {
        mb_ent_t *e = &t->n[cur].e[base + j];
        if (e->nh == NH_NONE || e->plen <= len) { e->nh = nh; e->plen = (uint8_t)len; }
    }
    return true;
}
static uint32_t g_mb_access;
static uint16_t mb_lookup(const mtrie_t *t, uint32_t addr) {
    uint16_t best = NH_NONE;
    uint32_t cur = 0;
    for (uint32_t d = 0; d < 4u; d++) {
        const mb_ent_t *e = &t->n[cur].e[(addr >> (24u - 8u * d)) & 0xFFu];
        g_mb_access++;
        if (e->nh != NH_NONE) best = e->nh;
        if (e->child == 0) break;
        cur = e->child;
    }
    return best;
}

/* ---------- 연습 3: longer-prefixes (서브트리 전위 순회) ---------- */
static uint32_t lp_walk(const btrie_t *t, uint32_t k, uint32_t key, uint32_t depth, route_t *out, uint32_t max, uint32_t cnt) {
    if (t->n[k].nh != NH_NONE) {
        if (cnt < max) out[cnt] = (route_t){ key, (uint8_t)depth, t->n[k].nh };
        cnt++;
    }
    for (uint32_t b = 0; b < 2u; b++)                      /* 0 쪽 먼저 → 주소 오름차순 */
        if (t->n[k].child[b] != 0)
            cnt = lp_walk(t, t->n[k].child[b], key | (b << (31u - depth)), depth + 1u, out, max, cnt);
    return cnt;
}
/* (p,len) 안에 들어가는 모든 경로(자기 자신 포함)를 정렬 순서로 out 에 최대 max 개. 총 개수 반환 */
static uint32_t bt_longer(const btrie_t *t, uint32_t p, uint32_t len, route_t *out, uint32_t max) {
    p &= mask(len);
    uint32_t cur = 0;
    for (uint32_t i = 0; i < len; i++) {
        cur = t->n[cur].child[bit_at(p, i)];
        if (cur == 0) return 0;
    }
    return lp_walk(t, cur, p, len, out, max, 0);
}
static bool route_before(route_t a, route_t b) { return a.prefix != b.prefix ? a.prefix < b.prefix : a.len < b.len; }

/* ---------- 연습 4: 범위 → 최소 CIDR ---------- */
/* 그리디: lo 에 정렬된 가장 큰 블록 중 범위를 넘지 않는 것. 크기 계산은 64비트 (2^32 = /0 블록) */
static uint32_t range_to_cidr(uint32_t lo, uint32_t hi, route_t *out, uint32_t max) {
    uint64_t a = lo, end = (uint64_t)hi + 1u;               /* [a, end) */
    uint32_t cnt = 0;
    while (a < end) {
        uint64_t size = a == 0 ? (1ull << 32) : (a & (~a + 1u));   /* lo 의 최하위 1 비트 = 정렬 한계 */
        while (size > end - a) size >>= 1;
        uint32_t len = 32u;
        for (uint64_t s = size; s > 1u; s >>= 1) len--;
        if (cnt < max) out[cnt] = (route_t){ (uint32_t)a, (uint8_t)len, 0 };
        cnt++;
        a += size;
    }
    return cnt;
}
/* 독립 검증용: 정렬 블록 [base, base+2^k) 을 재귀로 쪼개 범위 안에 완전히 든 최대 블록만 모은다 */
static uint32_t split_cover(uint64_t base, uint32_t k, uint64_t lo, uint64_t end, route_t *out, uint32_t max, uint32_t cnt) {
    uint64_t top = base + (1ull << k);
    if (top <= lo || base >= end) return cnt;
    if (lo <= base && top <= end) {
        if (cnt < max) out[cnt] = (route_t){ (uint32_t)base, (uint8_t)(32u - k), 0 };
        return cnt + 1u;
    }
    cnt = split_cover(base, k - 1u, lo, end, out, max, cnt);
    return split_cover(base + (1ull << (k - 1u)), k - 1u, lo, end, out, max, cnt);
}

static btrie_t g_bt;
static mtrie_t g_mt;
static route_t g_out[512], g_out2[512];

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    btrie_t *bt = &g_bt;

    /* 연습 1: 삭제 후 죽은 가지가 남지 않는다 */
    bt_init(bt);
    CHECK(bt_insert(bt, IP(10, 0, 0, 0), 8, 1) && bt_insert(bt, IP(10, 1, 2, 3), 32, 2) && bt->used == 33);
    CHECK(bt_delete(bt, IP(10, 1, 2, 3), 32) && bt->used == 9 && bt_check(bt) && bt_lookup(bt, IP(10, 1, 2, 3)) == 1);
    CHECK(!bt_delete(bt, IP(10, 1, 2, 3), 32) && !bt_delete(bt, IP(10, 0, 0, 0), 7) && bt->routes == 1);
    CHECK(bt_insert(bt, 0, 0, 7) && bt_delete(bt, 0, 0) && bt->used == 9 && bt_check(bt));   /* /0 = 루트, 반납 안 함 */
    CHECK(bt_delete(bt, IP(10, 99, 0, 0), 8) && bt->used == 1 && bt->routes == 0 && bt_check(bt));
    {
        bool ok = true;
        uint32_t ops = 0;
        bt_init(bt); g_ref_n = 0; rng = 0x5EEDu;
        for (int op = 0; op < 20000 && ok; op++) {
            uint32_t r = xorshift32(), p, l;
            if (r % 100u < (g_ref_n < 300u ? 60u : 40u)) {
                gen_dense(&p, &l);
                uint16_t nh = (uint16_t)(xorshift32() % 100u);
                ok = bt_insert(bt, p, l, nh); ref_set(p, l, nh);
            } else {
                if (g_ref_n > 0 && (r >> 8) % 4u != 0) { const route_t *e = &g_ref[(r >> 10) % g_ref_n]; p = e->prefix; l = e->len; }
                else gen_dense(&p, &l);
                ok = bt_delete(bt, p, l) == ref_del(p, l);
            }
            ok = ok && bt_check(bt) && bt->routes == g_ref_n;
            for (int q = 0; q < 4 && ok; q++) { uint32_t a = gen_addr(); ok = bt_lookup(bt, a) == ref_lookup(a); }
            ops++;
        }
        while (ok && g_ref_n > 0) { route_t e = g_ref[0]; ok = bt_delete(bt, e.prefix, e.len) && ref_del(e.prefix, e.len); }
        ok = ok && bt->used == 1 && bt_check(bt);
        CHECK(ok);
        printf("[1] binary trie insert/delete %u ops vs linear, drained to root only: %s\n", ops, ok ? "ok" : "BAD");
    }

    /* 연습 2: 멀티비트 트라이 — 짧은 prefix 가 긴 prefix 를 덮어쓰지 않는지 (삽입 순서 무관) */
    {
        mtrie_t *mt = &g_mt;
        mb_init(mt);
        CHECK(mb_insert(mt, IP(10, 1, 2, 0), 24, 3) && mb_insert(mt, IP(10, 0, 0, 0), 8, 1) &&
              mb_insert(mt, IP(10, 1, 0, 0), 16, 2) && mb_insert(mt, IP(10, 1, 2, 128), 25, 5) && mb_insert(mt, 0, 0, 7));
        CHECK(mb_lookup(mt, IP(10, 1, 2, 200)) == 5 && mb_lookup(mt, IP(10, 1, 2, 1)) == 3 && mb_lookup(mt, IP(10, 1, 3, 1)) == 2 &&
              mb_lookup(mt, IP(10, 2, 0, 0)) == 1 && mb_lookup(mt, IP(9, 0, 0, 0)) == 7);
        CHECK(mb_insert(mt, IP(10, 0, 0, 0), 7, 6) && mb_lookup(mt, IP(11, 0, 0, 1)) == 6 && mb_lookup(mt, IP(10, 2, 0, 0)) == 1);
        bool ok = true;
        mb_init(mt); g_ref_n = 0; rng = 0xABCDu;
        for (int i = 0; i < 300 && ok; i++) {
            uint32_t p, l; gen_dense(&p, &l);
            uint16_t nh = (uint16_t)(xorshift32() % 100u);
            ok = mb_insert(mt, p, l, nh); ref_set(p, l, nh);
        }
        g_mb_access = 0;
        uint32_t maxa = 0;
        for (int i = 0; i < 50000 && ok; i++) {
            uint32_t a = gen_addr(), before = g_mb_access;
            ok = mb_lookup(mt, a) == ref_lookup(a);
            if (g_mb_access - before > maxa) maxa = g_mb_access - before;
        }
        CHECK(ok && maxa <= 4u);
        printf("[2] stride-8 multibit: %u routes -> %u nodes (%u B), 50000 lookups avg %.2f max %u accesses: %s\n",
               g_ref_n, mt->used, mt->used * (uint32_t)sizeof(mb_node_t), (double)g_mb_access / 50000.0, maxa, ok ? "match" : "MISMATCH");
    }

    /* 연습 3: longer-prefixes — 참조 모델을 걸러 정렬한 결과와 같아야 한다 */
    {
        bool ok = true;
        bt_init(bt); g_ref_n = 0; rng = 0x77u;
        for (int i = 0; i < 400; i++) { uint32_t p, l; gen_dense(&p, &l); uint16_t nh = (uint16_t)i; bt_insert(bt, p, l, nh); ref_set(p, l, nh); }
        static const uint32_t qp[4] = { IP(10, 0, 0, 0), IP(10, 1, 0, 0), IP(10, 2, 128, 0), 0 };
        static const uint32_t ql[4] = { 14, 16, 17, 0 };
        uint32_t total0 = 0;
        for (int q = 0; q < 4 && ok; q++) {
            uint32_t n2 = 0;
            for (uint32_t i = 0; i < g_ref_n; i++)
                if ((g_ref[i].prefix & mask(ql[q])) == qp[q] && g_ref[i].len >= ql[q] && n2 < 512u) g_out2[n2++] = g_ref[i];
            for (uint32_t i = 1; i < n2; i++) {                 /* 삽입 정렬 */
                route_t x = g_out2[i]; uint32_t j = i;
                while (j > 0 && route_before(x, g_out2[j - 1])) { g_out2[j] = g_out2[j - 1]; j--; }
                g_out2[j] = x;
            }
            uint32_t n1 = bt_longer(bt, qp[q], ql[q], g_out, 512);
            ok = n1 == n2;
            for (uint32_t i = 0; i < n1 && ok; i++)
                ok = g_out[i].prefix == g_out2[i].prefix && g_out[i].len == g_out2[i].len && g_out[i].nh == g_out2[i].nh;
            if (q == 3) total0 = n1;
        }
        CHECK(ok && total0 == g_ref_n);                         /* 0.0.0.0/0 의 longer-prefixes = 전체 */
        CHECK(bt_longer(bt, IP(10, 1, 0, 0), 16, g_out, 2) >= 2);   /* 개수는 다 세고 기록은 max 까지 */
        printf("[3] longer-prefixes == filtered+sorted linear table (4 queries, %u routes): %s\n", g_ref_n, ok ? "ok" : "BAD");
    }

    /* 연습 4: 범위 → CIDR */
    {
        uint32_t n = range_to_cidr(IP(10, 0, 0, 5), IP(10, 0, 0, 20), g_out, 512);
        static const route_t want[5] = { { IP(10, 0, 0, 5), 32, 0 }, { IP(10, 0, 0, 6), 31, 0 }, { IP(10, 0, 0, 8), 29, 0 },
                                         { IP(10, 0, 0, 16), 30, 0 }, { IP(10, 0, 0, 20), 32, 0 } };
        bool ok = n == 5;
        for (uint32_t i = 0; i < 5 && ok; i++) ok = g_out[i].prefix == want[i].prefix && g_out[i].len == want[i].len;
        CHECK(ok);
        CHECK(range_to_cidr(0, 0xFFFFFFFFu, g_out, 512) == 1 && g_out[0].prefix == 0 && g_out[0].len == 0);
        CHECK(range_to_cidr(0xFFFFFFFFu, 0xFFFFFFFFu, g_out, 512) == 1 && g_out[0].len == 32 &&
              range_to_cidr(1, 0xFFFFFFFEu, g_out, 512) == 62);    /* 양 끝 한 칸씩 빠지면 31 + 31 블록 */
        ok = true;
        uint32_t maxn = 0;
        rng = 0x4242u;
        for (int t = 0; t < 3000 && ok; t++) {
            uint32_t a = xorshift32(), b = (t & 1) ? a + (xorshift32() & 0xFFFFu) : xorshift32();
            uint32_t lo = a < b ? a : b, hi = a < b ? b : a;
            uint32_t n1 = range_to_cidr(lo, hi, g_out, 512);
            uint32_t n2 = split_cover(0, 32, lo, (uint64_t)hi + 1u, g_out2, 512, 0);
            ok = n1 == n2 && n1 <= 62u;
            uint64_t next = lo;                                  /* 빈틈·겹침 없이 [lo, hi] 를 정확히 덮는다 */
            for (uint32_t i = 0; i < n1 && ok; i++) {
                ok = g_out[i].prefix == g_out2[i].prefix && g_out[i].len == g_out2[i].len &&
                     g_out[i].prefix == next && (g_out[i].prefix & ~mask(g_out[i].len)) == 0;
                next += 1ull << (32u - g_out[i].len);
            }
            ok = ok && next == (uint64_t)hi + 1u;
            if (n1 > maxn) maxn = n1;
            if (ok && t < 50) {                                  /* 트라이에 넣어 경계 주소로 확인 */
                bt_init(bt);
                for (uint32_t i = 0; i < n1; i++) bt_insert(bt, g_out[i].prefix, g_out[i].len, 1);
                ok = bt_lookup(bt, lo) == 1 && bt_lookup(bt, hi) == 1 && (lo == 0 || bt_lookup(bt, lo - 1u) == NH_NONE) &&
                     (hi == 0xFFFFFFFFu || bt_lookup(bt, hi + 1u) == NH_NONE);
            }
        }
        CHECK(ok);
        printf("[4] range->CIDR greedy == recursive split for 3000 random ranges (max %u blocks): %s\n", maxn, ok ? "ok" : "BAD");
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
