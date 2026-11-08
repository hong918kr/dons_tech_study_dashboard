/* DS-M13 기수 트리와 최장 접두사 매칭(LPM)
 * IPv4 (prefix, len) → next-hop 라우팅 테이블.
 *   Part A: 비트 하나 = 간선 하나인 이진 트라이 (DS-B14 트라이의 알파벳 {0,1} 버전)
 *   Part B: 외길을 접은 경로 압축 트라이 (Patricia / radix) — 삽입 4가지 경우 + 삭제
 *   Part C: CIDR 문자열 파싱 + 패킷 배치 포워딩 라우터 테이블 + 측정
 * malloc 없음 — 노드는 정적 풀 + uint16_t 인덱스. 매 연산 후 불변식 검사 + 선형 탐색 참조 모델 대조.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define NH_NONE 0xFFFFu                  /* "경로 없음" next-hop 값 */
#define IP(a, b, c, d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))

static uint32_t g_visits;                /* 조회 때 방문한 노드 수 (Part C 측정) */

/* a 의 i 번째 비트 (0 = MSB). i <= 31 이어야 한다 — i == 32 면 31u - 32u 가 거대한 시프트 양(UB) */
static uint32_t bit_at(uint32_t a, uint32_t i) { return (a >> (31u - i)) & 1u; }

/* 공통 선행 비트 수 0..32. __builtin_clz(0) 은 UB 라 0 을 따로 처리 (Cortex-M 의 CLZ 명령은 0 → 32) */
static uint32_t clp(uint32_t a, uint32_t b) {
    uint32_t x = a ^ b;
    return x == 0 ? 32u : (uint32_t)__builtin_clz(x);
}

/* ---------------- Part A: prefix 마스크 + 이진 트라이 ---------------- */
/* 상위 len 비트만 1 인 마스크. len 0..32 (32 초과는 32 로 본다) */
static uint32_t pfx_mask(uint32_t len) {
    (void)len;
    /* TODO: len == 0 → 0 (0xFFFFFFFFu << 32 는 UB!), len >= 32 → 전부 1, 그 외 0xFFFFFFFFu << (32 - len) */
    return 0xFFFFFFFFu;
}

#define BT_POOL 65536u
typedef struct {
    uint16_t child[2];                   /* 0 = 없음 (노드 0 은 루트라 누구의 자식도 아님) */
    uint16_t nh;                         /* 루트→이 노드 경로가 등록된 prefix 면 next-hop, 아니면 NH_NONE */
} bt_node_t;                             /* 6 바이트 */
typedef struct {
    bt_node_t n[BT_POOL];
    uint32_t  used, cap, routes;         /* 사용 노드 수(루트 포함), 노드 상한, 등록 경로 수 */
} btrie_t;

static void bt_init(btrie_t *t, uint32_t cap) {
    t->cap = (cap == 0 || cap > BT_POOL) ? BT_POOL : cap;
    t->used = 1; t->routes = 0;
    t->n[0] = (bt_node_t){ { 0, 0 }, NH_NONE };
}

/* prefix 의 상위 len 비트를 따라 내려가며 없는 노드를 만들고, 끝 노드에 nh 를 단다.
 * 호스트 비트는 마스킹(정규화). 같은 prefix 재등록 = nh 교체. 풀이 모자라면 아무것도 바꾸지 않고 false */
static bool bt_insert(btrie_t *t, uint32_t prefix, uint32_t len, uint16_t nh) {
    (void)t; (void)prefix; (void)len; (void)nh;
    /* TODO: len > 32 / nh == NH_NONE 거절, prefix &= pfx_mask(len)
     *       1) 있는 경로를 따라가 필요한 새 노드 수(len - i)가 cap - used 이하인지 검증
     *       2) 남은 비트마다 노드를 붙이고, 끝 노드의 nh 설정 (새 경로면 routes++) */
    return false;
}

/* addr 의 비트를 따라 내려가며 마지막으로 본 nh 를 기억한다 (= 가장 긴 일치). *mlen = 일치 길이 */
static uint16_t bt_lookup(const btrie_t *t, uint32_t addr, uint32_t *mlen) {
    (void)t; (void)addr; (void)bit_at;
    /* TODO: 루트부터 깊이 i 의 노드마다 g_visits++, nh 가 있으면 (best, blen = i) 기억.
     *       i == 32 면 멈춤(bit_at(addr, 32) 은 UB), 아니면 child[bit_at(addr, i)] 로. 없으면 멈춤 */
    if (mlen) *mlen = 0;
    return NH_NONE;
}

/* ---------------- Part B: 경로 압축 트라이 (Patricia) ---------------- */
#define PT_POOL 8192u
typedef struct {
    uint32_t key;                        /* prefix (len 아래 비트는 항상 0) */
    uint16_t child[2];                   /* 0 = NIL. child[b] = key 다음 비트(len 번째)가 b 인 쪽 */
    uint16_t nh;                         /* NH_NONE 이면 갈림길(glue) 노드 — 경로가 아님 */
    uint8_t  len;                        /* 0..32 */
} pt_node_t;                             /* 4 + 4 + 2 + 1 (+1 패딩) = 12 바이트 */
typedef struct {
    pt_node_t n[PT_POOL];                /* n[0] 은 NIL 로 비워 둠 */
    uint16_t  root, free_head;
    uint32_t  used, cap, routes;
} ptrie_t;

static void pt_init(ptrie_t *t, uint32_t cap) {
    t->cap = (cap == 0 || cap > PT_POOL - 1u) ? PT_POOL - 1u : cap;
    t->root = 0; t->used = 0; t->routes = 0; t->free_head = 0;
    for (uint32_t i = PT_POOL - 1u; i >= 1u; i--) { t->n[i].child[0] = t->free_head; t->free_head = (uint16_t)i; }
}
/* free list 할당/반납 (DS-M02 메모리 풀에서 자세히) */
static uint16_t pt_new(ptrie_t *t, uint32_t key, uint32_t len, uint16_t nh) {
    if (t->used >= t->cap || t->free_head == 0) return 0;
    uint16_t k = t->free_head;
    t->free_head = t->n[k].child[0];
    t->used++;
    t->n[k] = (pt_node_t){ key, { 0, 0 }, nh, (uint8_t)len };
    return k;
}
static void pt_free(ptrie_t *t, uint16_t k) {   /* 독 값: 해제 후 사용을 불변식 검사가 잡게 */
    t->n[k] = (pt_node_t){ 0xDEADBEEFu, { t->free_head, 0 }, NH_NONE, 0xFFu };
    t->free_head = k; t->used--;
}

/* 링크(부모의 child 칸 또는 root)를 따라 내려가며 4가지 경우 중 하나로 끝난다:
 *  ① 빈 링크 → 새 잎   ② 같은 (key,len) → nh 교체   ③ 새 prefix 가 n 의 조상 → n 위에 끼움
 *  ④ n 의 라벨 중간에서 갈라짐 → 갈림길(glue) 노드 + 새 잎 (노드 2개)
 * 실패(풀 부족)하면 트리는 그대로 */
static bool pt_insert(ptrie_t *t, uint32_t prefix, uint32_t len, uint16_t nh) {
    (void)t; (void)prefix; (void)len; (void)nh; (void)clp; (void)pt_new;
    /* TODO: uint16_t *link = &t->root 에서 시작. c = min(clp(prefix, key), len, n->len)
     *   ① *link == 0            → 새 잎
     *   ② c == n->len == len    → nh 교체 (glue 였으면 routes++)
     *   ②' c == n->len < len    → link = &n->child[bit_at(prefix, n->len)] 로 계속
     *   ③ c == len < n->len     → 새 노드를 n 위에 끼움 (child[bit_at(n->key, len)] = n)
     *   ④ c < len, c < n->len   → glue(prefix/c) + 새 잎. 노드 2개 — 먼저 cap - used >= 2 확인 */
    return false;
}

/* 라벨 전체가 addr 와 맞는 동안 내려가며 마지막 경로 노드의 nh 를 기억 */
static uint16_t pt_lookup(const ptrie_t *t, uint32_t addr, uint32_t *mlen) {
    (void)t; (void)addr;
    /* TODO: 노드마다 g_visits++. (addr & pfx_mask(len)) != key 면 멈춤.
     *       nh 가 있으면 기억, len == 32 면 멈춤, 아니면 child[bit_at(addr, len)] 로 */
    if (mlen) *mlen = 0;
    return NH_NONE;
}

/* 정확히 (prefix,len) 인 경로를 지운다. 자식 2개 → glue 로 강등, 1개 → 자식을 끌어올림,
 * 0개 → 제거 후 부모가 glue 이고 자식 1개가 되면 부모도 끌어올림. 없거나 glue 면 false */
static bool pt_delete(ptrie_t *t, uint32_t prefix, uint32_t len) {
    (void)t; (void)prefix; (void)len; (void)pt_free;
    /* TODO: 링크를 따라 (prefix,len) 노드를 찾으며 부모 링크(plink)를 기억. 없거나 glue 면 false.
     *       자식 2개 → nh = NH_NONE (glue 로 강등)
     *       자식 0/1개 → *link = 그 자식(또는 0), pt_free. 잎이었고 부모가 glue 면 부모도 끌어올려 제거 */
    return false;
}

/* ---------------- Part C: CIDR 파싱 + 라우터 테이블 ---------------- */
/* "a.b.c.d/len" 또는 "a.b.c.d"(= /32). 옥텟 0..255 (숫자 1~3자리), len 0..32 (숫자 1~2자리), 뒤에 다른 문자 없음.
 * 값은 마스킹하지 않고 그대로 돌려주고, *host_bits = len 아래 비트가 켜져 있었는지. 실패 시 출력 인자 불변 */
static bool parse_cidr(const char *s, uint32_t *prefix, uint32_t *len, bool *host_bits) {
    (void)s; (void)prefix; (void)len; (void)host_bits;
    /* TODO: 옥텟 4개(숫자 1~3자리, <= 255)를 '.' 로 구분해 읽고, '/' 가 있으면 len(1~2자리, <= 32), 없으면 32.
     *       끝에 다른 문자가 있으면 false. 성공할 때만 출력 인자에 쓴다. host_bits = (a & ~pfx_mask(l)) != 0 */
    return false;
}

#define NH_MAX 8u
typedef struct { uint32_t dst; uint8_t ttl; } pkt_t;
typedef struct {
    ptrie_t  pt;
    uint32_t tx[NH_MAX];                 /* next-hop(포트) 별 전달 수 */
    uint32_t no_route, ttl_drop;         /* 경로 없음 / TTL 만료 */
    uint32_t normalized;                 /* 호스트 비트가 있어 마스킹된 설정 수 */
} router_t;

static void rt_init(router_t *r) {
    pt_init(&r->pt, 0);
    memset(r->tx, 0, sizeof r->tx);
    r->no_route = r->ttl_drop = r->normalized = 0;
}
static bool rt_add(router_t *r, const char *cidr, uint16_t nh) {
    uint32_t p, l; bool hb;
    if (nh >= NH_MAX || !parse_cidr(cidr, &p, &l, &hb)) return false;
    if (!pt_insert(&r->pt, p, l, nh)) return false;
    if (hb) r->normalized++;
    return true;
}
static bool rt_del(router_t *r, const char *cidr) {
    uint32_t p, l; bool hb;
    return parse_cidr(cidr, &p, &l, &hb) && pt_delete(&r->pt, p, l);
}

/* 패킷 배치 포워딩: TTL <= 1 → ttl_drop (ICMP time exceeded 대상), 경로 없음 → no_route,
 * 아니면 TTL 1 감소 + tx[nh]++. out_nh[i] = 결정된 next-hop (버리면 NH_NONE). 전달한 개수 반환 */
static uint32_t rt_forward(router_t *r, pkt_t *pk, uint32_t n, uint16_t *out_nh) {
    (void)r; (void)pk;
    /* TODO: 패킷마다 out_nh[i] = NH_NONE 로 시작. ttl <= 1 → ttl_drop++,
     *       pt_lookup 결과가 NH_NONE(또는 >= NH_MAX) → no_route++, 아니면 ttl--, tx[nh]++, out_nh[i] = nh */
    for (uint32_t i = 0; i < n; i++) out_nh[i] = NH_NONE;
    return 0;
}

/* ---------------- 검사 도구 (주어짐) ---------------- */
/* 독립 마스크: 64비트에서 시프트하므로 len 0..32 모두 정의됨 — pfx_mask 와 다른 식이라 서로 검증된다 */
static uint32_t mask64(uint32_t len) { return (uint32_t)~(0xFFFFFFFFull >> len); }

static bool bt_check_rec(const btrie_t *t, uint32_t k, uint32_t depth, uint32_t *nodes, uint32_t *routes) {
    const bt_node_t *n = &t->n[k];
    if (k >= t->used || depth > 32u || ++*nodes > t->used) return false;
    if (n->nh != NH_NONE) (*routes)++;
    if (k != 0 && n->nh == NH_NONE && n->child[0] == 0 && n->child[1] == 0) return false;  /* 죽은 가지 */
    for (uint32_t b = 0; b < 2u; b++)
        if (n->child[b] != 0 && !bt_check_rec(t, n->child[b], depth + 1u, nodes, routes)) return false;
    return true;
}
/* 이진 트라이 불변식: 도달 노드 수 == used, 경로 수 일치, 깊이 <= 32, 경로 없는 잎 없음 */
static bool bt_check(const btrie_t *t) {
    uint32_t nodes = 0, routes = 0;
    return t->used <= t->cap && bt_check_rec(t, 0, 0, &nodes, &routes) && nodes == t->used && routes == t->routes;
}

static bool pt_check_rec(const ptrie_t *t, uint16_t k, bool is_root, uint32_t plen, uint32_t pkey, uint32_t b,
                         uint32_t *nodes, uint32_t *routes) {
    if (k == 0 || k >= PT_POOL || ++*nodes > t->used) return false;
    const pt_node_t *n = &t->n[k];
    if (n->len > 32u || (n->key & ~mask64(n->len)) != 0) return false;           /* 호스트 비트 0 */
    if (!is_root && (n->len <= plen || (n->key & mask64(plen)) != pkey || bit_at(n->key, plen) != b))
        return false;                                                              /* 부모 라벨 + 분기 비트 */
    if (n->nh == NH_NONE && (n->child[0] == 0 || n->child[1] == 0)) return false;  /* glue 는 자식 2개 */
    if (n->len == 32u && (n->child[0] != 0 || n->child[1] != 0)) return false;
    if (n->nh != NH_NONE) (*routes)++;
    for (uint32_t c = 0; c < 2u; c++)
        if (n->child[c] != 0 && !pt_check_rec(t, n->child[c], false, n->len, n->key, c, nodes, routes)) return false;
    return true;
}
/* Patricia 불변식 + 풀 회계: 도달 노드 == used, free list 길이 == 남은 칸, 노드 <= 2·경로 - 1 */
static bool pt_check(const ptrie_t *t) {
    uint32_t nodes = 0, routes = 0, freen = 0;
    if (t->root != 0 && !pt_check_rec(t, t->root, true, 0, 0, 0, &nodes, &routes)) return false;
    for (uint16_t f = t->free_head; f != 0 && freen < PT_POOL; f = t->n[f].child[0]) freen++;
    return nodes == t->used && routes == t->routes && freen == PT_POOL - 1u - t->used &&
           (t->routes == 0 ? t->used == 0 : t->used <= 2u * t->routes - 1u);
}

/* 참조 모델: 정규화한 경로를 배열에 두고 선형 탐색 — 가장 긴 len 이 이긴다 */
typedef struct { uint32_t prefix; uint8_t len; uint16_t nh; } route_t;
#define REF_MAX 4096u
static route_t  g_ref[REF_MAX];
static uint32_t g_ref_n;
static void ref_set(uint32_t p, uint32_t len, uint16_t nh) {
    p &= mask64(len);
    for (uint32_t i = 0; i < g_ref_n; i++)
        if (g_ref[i].prefix == p && g_ref[i].len == len) { g_ref[i].nh = nh; return; }
    if (g_ref_n < REF_MAX) g_ref[g_ref_n++] = (route_t){ p, (uint8_t)len, nh };
}
static bool ref_del(uint32_t p, uint32_t len) {
    p &= mask64(len);
    for (uint32_t i = 0; i < g_ref_n; i++)
        if (g_ref[i].prefix == p && g_ref[i].len == len) { g_ref[i] = g_ref[--g_ref_n]; return true; }
    return false;
}
static uint16_t ref_lookup(uint32_t addr, uint32_t *mlen) {
    uint16_t best = NH_NONE;
    int blen = -1;
    for (uint32_t i = 0; i < g_ref_n; i++)
        if ((addr & mask64(g_ref[i].len)) == g_ref[i].prefix && (int)g_ref[i].len > blen) {
            blen = g_ref[i].len; best = g_ref[i].nh;
        }
    if (mlen) *mlen = blen < 0 ? 0u : (uint32_t)blen;
    return best;
}

static uint32_t rng_state = 1u;
static uint32_t xorshift32(void) {
    uint32_t x = rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return rng_state = x;
}
/* 겹침이 많은 경로: 대부분 10.0~3.x.x 안, len 0..32 균등 (/0, /32 포함). 호스트 비트는 일부러 남김 */
static void gen_dense(uint32_t *p, uint32_t *len) {
    uint32_t r = xorshift32();
    *p = (r & 7u) == 0 ? xorshift32() : (IP(10, 0, 0, 0) | ((r >> 3) & 3u) << 16 | (xorshift32() & 0xFFFFu));
    *len = (r >> 8) % 33u;
}
/* BGP 비슷한 분포: /21~/24 가 대부분, /8~/12 3%, /13~/20 20%, /25~/31 10%, /32 5% */
static void gen_bgp(uint32_t *p, uint32_t *len) {
    static const uint8_t top[8] = { 10, 172, 192, 100, 44, 8, 203, 63 };
    uint32_t r = xorshift32(), q = (r >> 3) % 100u, s = (r >> 10);
    *p = ((uint32_t)top[r & 7u] << 24) | (xorshift32() & 0x00FFFFFFu);
    *len = q < 3 ? 8u + s % 5u : q < 23 ? 13u + s % 8u : q < 85 ? 21u + s % 4u : q < 95 ? 25u + s % 7u : 32u;
}
/* 조회 주소: 절반은 기존 경로 안(호스트 비트 랜덤), 절반은 완전 무작위 */
static uint32_t gen_addr(void) {
    uint32_t r = xorshift32();
    if (g_ref_n == 0 || (r & 1u)) return xorshift32();
    const route_t *e = &g_ref[(r >> 1) % g_ref_n];
    return e->prefix | (xorshift32() & ~mask64(e->len));
}

static btrie_t  g_bt;
static ptrie_t  g_pt;
static router_t g_rt;

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    btrie_t *bt = &g_bt;
    ptrie_t *pt = &g_pt;
    uint32_t ml = 99;

    /* Part A: 마스크 — /0 과 /32 경계 */
    CHECK(pfx_mask(0) == 0u && pfx_mask(1) == 0x80000000u && pfx_mask(24) == 0xFFFFFF00u && pfx_mask(32) == 0xFFFFFFFFu);
    {
        bool ok = true;
        for (uint32_t l = 0; l <= 32u; l++) ok = ok && pfx_mask(l) == mask64(l);
        CHECK(ok);
    }
    /* Part A: 빈 테이블, 기본 경로(/0) 하나 */
    bt_init(bt, 0);
    CHECK(bt_lookup(bt, IP(1, 2, 3, 4), &ml) == NH_NONE && ml == 0 && bt_check(bt));
    CHECK(bt_insert(bt, IP(0, 0, 0, 0), 0, 7) && bt->used == 1 && bt->routes == 1);
    CHECK(bt_lookup(bt, 0u, &ml) == 7 && ml == 0 && bt_lookup(bt, 0xFFFFFFFFu, &ml) == 7 && ml == 0);
    /* Part A: 겹치는 prefix — 가장 긴 것이 이긴다 */
    CHECK(bt_insert(bt, IP(10, 0, 0, 0), 8, 1) && bt_insert(bt, IP(10, 1, 0, 0), 16, 2) &&
          bt_insert(bt, IP(10, 1, 2, 0), 24, 3) && bt_insert(bt, IP(10, 1, 2, 3), 32, 4) && bt_check(bt));
    CHECK(bt_lookup(bt, IP(10, 1, 2, 3), &ml) == 4 && ml == 32 && bt_lookup(bt, IP(10, 1, 2, 4), &ml) == 3 && ml == 24 &&
          bt_lookup(bt, IP(10, 1, 9, 9), &ml) == 2 && ml == 16 && bt_lookup(bt, IP(10, 200, 0, 1), &ml) == 1 && ml == 8 &&
          bt_lookup(bt, IP(11, 0, 0, 1), &ml) == 7 && ml == 0);
    CHECK(bt->used == 1 + 32 && bt->routes == 5);              /* 중첩 경로는 한 줄기를 공유: 노드 32개 + 루트 */
    /* Part A: 호스트 비트가 켜진 prefix 는 정규화, 같은 prefix 재등록은 교체 */
    CHECK(bt_insert(bt, IP(192, 168, 1, 77), 24, 5) && bt_lookup(bt, IP(192, 168, 1, 200), &ml) == 5 && ml == 24);
    CHECK(bt_insert(bt, IP(192, 168, 1, 0), 24, 6) && bt->routes == 6 && bt_lookup(bt, IP(192, 168, 1, 1), NULL) == 6);
    CHECK(!bt_insert(bt, IP(10, 0, 0, 0), 33, 1) && !bt_insert(bt, IP(10, 0, 0, 0), 8, NH_NONE) && bt_check(bt));
    /* Part A: 풀 부족 — 실패한 삽입은 노드를 하나도 쓰지 않는다 */
    bt_init(bt, 40);
    CHECK(bt_insert(bt, IP(10, 1, 2, 3), 32, 1) && bt->used == 33);
    CHECK(!bt_insert(bt, IP(200, 0, 0, 1), 32, 2) && bt->used == 33 && bt->routes == 1 && bt_check(bt));
    CHECK(bt_insert(bt, IP(10, 1, 2, 2), 32, 2) && bt->used == 34 && bt_check(bt));   /* 31비트 공유 → 1개만 */
    /* Part A: 랜덤 2000 경로 + 20000 주소를 선형 참조와 대조 */
    {
        bt_init(bt, 0); g_ref_n = 0; rng_state = 0x1234567u;
        bool ok = true;
        for (int i = 0; i < 2000 && ok; i++) {
            uint32_t p, l; gen_dense(&p, &l);
            uint16_t nh = (uint16_t)(xorshift32() % 100u);
            ok = bt_insert(bt, p, l, nh);
            ref_set(p, l, nh);
        }
        ok = ok && bt_check(bt) && bt->routes == g_ref_n;
        for (int i = 0; i < 20000 && ok; i++) {
            uint32_t a = gen_addr(), l1 = 0, l2 = 0;
            ok = bt_lookup(bt, a, &l1) == ref_lookup(a, &l2) && l1 == l2;
        }
        CHECK(ok);
        printf("[A] binary trie: %u routes -> %u nodes, 20000 lookups vs linear: %s\n",
               g_ref_n, bt->used, ok ? "match" : "MISMATCH");
    }

    /* Part B: 중첩 경로 4개 + /0 — glue 없이 노드 5개 */
    pt_init(pt, 0);
    CHECK(pt_lookup(pt, IP(1, 2, 3, 4), &ml) == NH_NONE && pt_check(pt));
    CHECK(pt_insert(pt, IP(10, 1, 2, 0), 24, 3) && pt_insert(pt, IP(10, 0, 0, 0), 8, 1) &&      /* ③ 조상 끼우기 */
          pt_insert(pt, IP(10, 1, 2, 3), 32, 4) && pt_insert(pt, IP(10, 1, 0, 0), 16, 2) &&
          pt_insert(pt, 0u, 0, 7) && pt->used == 5 && pt->routes == 5 && pt_check(pt));
    CHECK(pt_lookup(pt, IP(10, 1, 2, 3), &ml) == 4 && ml == 32 && pt_lookup(pt, IP(10, 1, 2, 4), &ml) == 3 && ml == 24 &&
          pt_lookup(pt, IP(10, 1, 9, 9), &ml) == 2 && ml == 16 && pt_lookup(pt, IP(10, 200, 0, 1), &ml) == 1 && ml == 8 &&
          pt_lookup(pt, IP(11, 0, 0, 1), &ml) == 7 && ml == 0 && pt_lookup(pt, 0xFFFFFFFFu, &ml) == 7);
    /* Part B: 갈라짐 ④ → glue 노드 (10/8 과 11/8 은 /7 에서 갈라짐) */
    pt_init(pt, 0);
    CHECK(pt_insert(pt, IP(10, 0, 0, 0), 8, 1) && pt_insert(pt, IP(11, 0, 0, 0), 8, 2) && pt->used == 3 && pt_check(pt));
    CHECK(pt->n[pt->root].len == 7 && pt->n[pt->root].key == IP(10, 0, 0, 0) && pt->n[pt->root].nh == NH_NONE);
    CHECK(pt_lookup(pt, IP(11, 9, 9, 9), NULL) == 2 && pt_lookup(pt, IP(12, 0, 0, 0), NULL) == NH_NONE);
    CHECK(!pt_delete(pt, IP(10, 0, 0, 0), 7) && !pt_delete(pt, IP(10, 0, 0, 0), 9) && pt->routes == 2);  /* glue·없는 경로 */
    CHECK(pt_insert(pt, IP(10, 0, 0, 0), 7, 9) && pt->used == 3 && pt->routes == 3 && pt_check(pt));     /* glue → 경로 승격 */
    CHECK(pt_delete(pt, IP(10, 0, 0, 0), 7) && pt->used == 3 && pt->n[pt->root].nh == NH_NONE && pt_check(pt)); /* 강등 */
    CHECK(pt_delete(pt, IP(11, 7, 7, 7), 8) && pt->used == 1 && pt->n[pt->root].len == 8 && pt_check(pt));  /* 잎 + glue 정리 */
    CHECK(pt_delete(pt, IP(10, 0, 0, 0), 8) && pt->used == 0 && pt->root == 0 && pt_check(pt));
    /* Part B: 자식 1개인 경로 삭제 → 자식을 끌어올림 */
    CHECK(pt_insert(pt, IP(10, 0, 0, 0), 8, 1) && pt_insert(pt, IP(10, 1, 0, 0), 16, 2) &&
          pt_delete(pt, IP(10, 0, 0, 0), 8) && pt->used == 1 && pt->n[pt->root].len == 16 && pt_check(pt));
    /* Part B: 풀 부족 — 갈라짐은 노드 2개가 필요, 1개만 남았으면 아무것도 바꾸지 않음 */
    pt_init(pt, 2);
    CHECK(pt_insert(pt, IP(10, 0, 0, 0), 8, 1) && !pt_insert(pt, IP(11, 0, 0, 0), 8, 2) && pt->used == 1 && pt_check(pt));
    CHECK(pt_insert(pt, IP(10, 1, 0, 0), 16, 2) && pt->used == 2 && !pt_insert(pt, IP(10, 2, 0, 0), 16, 3) && pt_check(pt));
    /* Part B: 랜덤 삽입/삭제 20000회 — 매번 불변식 + 참조 모델과 조회 4개 대조 */
    {
        pt_init(pt, 0); g_ref_n = 0; rng_state = 0xBEEF01u;
        bool ok = true;
        uint32_t ins = 0, del = 0, del_miss = 0;
        for (int op = 0; op < 20000 && ok; op++) {
            uint32_t r = xorshift32(), p, l;
            if (r % 100u < (g_ref_n < 300u ? 60u : 40u)) {
                gen_dense(&p, &l);
                uint16_t nh = (uint16_t)(xorshift32() % 100u);
                ok = pt_insert(pt, p, l, nh);
                ref_set(p, l, nh); ins++;
            } else {
                if (g_ref_n > 0 && (r >> 8) % 4u != 0) {       /* 있는 경로 (호스트 비트를 섞어서) */
                    const route_t *e = &g_ref[(r >> 10) % g_ref_n];
                    p = e->prefix | (xorshift32() & ~mask64(e->len)); l = e->len;
                } else gen_dense(&p, &l);                       /* 대부분 없는 경로 */
                bool a = pt_delete(pt, p, l), b = ref_del(p, l);
                ok = a == b; if (a) del++; else del_miss++;
            }
            ok = ok && pt_check(pt) && pt->routes == g_ref_n;
            for (int q = 0; q < 4 && ok; q++) {
                uint32_t a = gen_addr(), l1 = 0, l2 = 0;
                ok = pt_lookup(pt, a, &l1) == ref_lookup(a, &l2) && l1 == l2;
            }
        }
        uint32_t left = g_ref_n;
        while (ok && g_ref_n > 0) { route_t e = g_ref[0]; ok = pt_delete(pt, e.prefix, e.len) && ref_del(e.prefix, e.len) && pt_check(pt); }
        ok = ok && pt->used == 0 && pt->root == 0;
        CHECK(ok);
        printf("[B] patricia random ops: %u inserts, %u deletes, %u misses, drained %u: %s\n",
               ins, del, del_miss, left, ok ? "match" : "MISMATCH");
    }

    /* Part C: CIDR 파싱 */
    {
        uint32_t p = 1, l = 1; bool hb = true;
        CHECK(parse_cidr("10.0.0.0/8", &p, &l, &hb) && p == IP(10, 0, 0, 0) && l == 8 && !hb);
        CHECK(parse_cidr("0.0.0.0/0", &p, &l, &hb) && p == 0 && l == 0 && !hb &&
              parse_cidr("255.255.255.255/32", &p, &l, &hb) && p == 0xFFFFFFFFu && l == 32 && !hb);
        CHECK(parse_cidr("192.168.1.77/24", &p, &l, &hb) && p == IP(192, 168, 1, 77) && l == 24 && hb);
        CHECK(parse_cidr("1.2.3.4", &p, &l, &hb) && p == IP(1, 2, 3, 4) && l == 32 && !hb);
        CHECK(parse_cidr("8.8.8.8/0", &p, &l, &hb) && l == 0 && hb);      /* /0 이면 모든 비트가 호스트 비트 */
        static const char *bad[] = { "256.0.0.0/8", "10.0.0/8", "10.0.0.0/33", "10.0.0.0/", "10..0.0/8",
                                     "10.0.0.0/8x", "", "a.b.c.d/8", "1.2.3.4.5/8", "10.0.0.0/-1", "1234.0.0.0/8",
                                     "10.0.0.0/100", " 10.0.0.0/8", "10.0.0.0/8 " };
        bool ok = true;
        p = 0xAAAAAAAAu; l = 77;
        for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) ok = ok && !parse_cidr(bad[i], &p, &l, &hb);
        CHECK(ok && p == 0xAAAAAAAAu && l == 77);                        /* 실패 시 출력 불변 */
    }
    /* Part C: 라우터 — 기본 경로, 중첩, 호스트 경로, 정규화, TTL */
    {
        router_t *r = &g_rt;
        rt_init(r);
        CHECK(rt_add(r, "0.0.0.0/0", 0) && rt_add(r, "10.0.0.0/8", 1) && rt_add(r, "10.1.0.0/16", 2) &&
              rt_add(r, "10.1.2.3/32", 3) && rt_add(r, "192.168.1.77/24", 4));
        CHECK(r->normalized == 1 && r->pt.routes == 5 && pt_check(&r->pt));
        CHECK(!rt_add(r, "10.0.0.0/33", 1) && !rt_add(r, "10.0.0.0/8", NH_MAX) && r->pt.routes == 5);
        pkt_t pk[8] = { { IP(10, 1, 2, 3), 64 }, { IP(10, 1, 2, 4), 64 }, { IP(10, 9, 9, 9), 64 },
                        { IP(192, 168, 1, 200), 64 }, { IP(8, 8, 8, 8), 64 }, { IP(10, 1, 2, 3), 1 },
                        { IP(192, 168, 2, 1), 64 }, { IP(10, 1, 255, 255), 2 } };
        static const uint16_t want[8] = { 3, 2, 1, 4, 0, NH_NONE, 0, 2 };
        uint16_t out[8];
        CHECK(rt_forward(r, pk, 8, out) == 7 && memcmp(out, want, sizeof want) == 0);
        CHECK(r->tx[0] == 2 && r->tx[1] == 1 && r->tx[2] == 2 && r->tx[3] == 1 && r->tx[4] == 1 &&
              r->ttl_drop == 1 && r->no_route == 0 && pk[0].ttl == 63 && pk[7].ttl == 1 && pk[5].ttl == 1);
        /* 경로 플랩: /16 과 기본 경로를 지우면 10/8 로 떨어지고 나머지는 버려진다 */
        CHECK(rt_del(r, "10.1.99.99/16") && rt_del(r, "0.0.0.0/0") && !rt_del(r, "10.1.0.0/16") && pt_check(&r->pt));
        pkt_t pk2[3] = { { IP(10, 1, 2, 4), 64 }, { IP(8, 8, 8, 8), 64 }, { IP(10, 1, 2, 3), 64 } };
        static const uint16_t want2[3] = { 1, NH_NONE, 3 };
        CHECK(rt_forward(r, pk2, 3, out) == 2 && memcmp(out, want2, sizeof want2) == 0 && r->no_route == 1);
    }
    /* Part C: 측정 — 같은 경로 집합에서 이진 트라이 vs Patricia vs 선형 */
    {
        static const uint32_t Ns[3] = { 100, 1000, 3000 };
        bool ok = true;
        printf("[C] %5s %6s | %-24s | %-24s | %s\n", "N", "routes", "binary trie nodes/bytes", "patricia nodes/bytes",
               "visits/lookup avg(max): binary, patricia, linear");
        for (int t = 0; t < 3; t++) {
            bt_init(bt, 0); pt_init(pt, 0); g_ref_n = 0; rng_state = 777u + (uint32_t)t;
            for (uint32_t i = 0; i < Ns[t] && ok; i++) {
                uint32_t p, l; gen_bgp(&p, &l);
                uint16_t nh = (uint16_t)(i % 64u);
                ok = bt_insert(bt, p, l, nh) && pt_insert(pt, p, l, nh);
                ref_set(p, l, nh);
            }
            ok = ok && bt_insert(bt, 0, 0, 63) && pt_insert(pt, 0, 0, 63); ref_set(0, 0, 63);
            ok = ok && bt_check(bt) && pt_check(pt) && bt->routes == g_ref_n && pt->routes == g_ref_n;
            uint64_t vb = 0, vp = 0; uint32_t mb = 0, mp = 0;
            const uint32_t L = 100000u;
            for (uint32_t i = 0; i < L && ok; i++) {
                uint32_t a = gen_addr(), l1, l2, l3;
                g_visits = 0; uint16_t h1 = bt_lookup(bt, a, &l1); vb += g_visits; if (g_visits > mb) mb = g_visits;
                g_visits = 0; uint16_t h2 = pt_lookup(pt, a, &l2); vp += g_visits; if (g_visits > mp) mp = g_visits;
                if ((i & 7u) == 0) { uint16_t h3 = ref_lookup(a, &l3); ok = h3 == h1 && l3 == l1; }
                ok = ok && h1 == h2 && l1 == l2;
            }
            printf("[C] %5u %6u | %6u nodes %8u B | %6u nodes %8u B | %.1f(%u), %.1f(%u), %u\n",
                   Ns[t], g_ref_n, bt->used, bt->used * (uint32_t)sizeof(bt_node_t),
                   pt->used, pt->used * (uint32_t)sizeof(pt_node_t),
                   (double)vb / L, mb, (double)vp / L, mp, g_ref_n);
        }
        CHECK(ok);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
