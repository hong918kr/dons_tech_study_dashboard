/* DS-M11 인덱스 우선순위 큐: Decrease-key와 취소
 * id(0..1023) → 힙 슬롯 위치 pos[] 를 함께 유지하는 indexed min-heap. malloc 없음, 고정 배열.
 * 불변식: 모든 슬롯 i < n 에 대해 pos[heap[i]] == i, 그리고 key[heap[parent(i)]] <= key[heap[i]].
 * 검증: 매 연산 후 ipq_check() + 선형 탐색 참조 모델, 타이머 재설정/취소, Dijkstra(O(V^2) 참조와 대조).
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define IPQ_CAP  1024u                   /* id 공간 크기 = 힙 용량 (id 가 유일하므로 넘칠 수 없다) */
#define IPQ_NONE 0xFFFFu                 /* pos[id] == IPQ_NONE → 그 id 는 큐에 없음 */

typedef struct {
    uint16_t heap[IPQ_CAP];              /* heap[i] = 슬롯 i 에 있는 id. 자식 2i+1, 2i+2 / 부모 (i-1)/2 */
    uint16_t pos[IPQ_CAP];               /* pos[id] = id 가 있는 슬롯 (heap 의 역함수) */
    uint32_t key[IPQ_CAP];               /* key[id] — 키는 id 에 붙어 있다. 슬롯이 바뀌어도 안 옮긴다 */
    uint32_t n;
} ipq_t;

/* 순서: key 먼저, 같으면 id — 전순서(total order)라 pop 순서가 유일하게 정해진다 */
static bool id_before(const ipq_t *q, uint32_t a, uint32_t b) {
    return q->key[a] != q->key[b] ? q->key[a] < q->key[b] : a < b;
}
static uint32_t g_cmp;                   /* 비교 횟수 (관찰용) */
static bool slot_less(const ipq_t *q, uint32_t i, uint32_t j) { g_cmp++; return id_before(q, q->heap[i], q->heap[j]); }

#define PARENT(i) (((i) - 1u) / 2u)
#define LEFT(i)   (2u * (i) + 1u)

static void ipq_init(ipq_t *q) {
    q->n = 0;
    for (uint32_t id = 0; id < IPQ_CAP; id++) q->pos[id] = IPQ_NONE;
}
static bool ipq_contains(const ipq_t *q, uint32_t id) { return id < IPQ_CAP && q->pos[id] != IPQ_NONE; }

static bool ipq_peek(const ipq_t *q, uint32_t *id, uint32_t *key) {
    if (q->n == 0) return false;
    *id = q->heap[0];
    *key = q->key[q->heap[0]];
    return true;
}

/* ---------------- Part A: swap(+pos 갱신) / sift / insert / pop ---------------- */
/* 두 슬롯의 id 를 바꾸고, 바뀐 두 id 의 pos 도 함께 고친다 — 불변식을 지키는 유일한 통로 */
static void ipq_swap(ipq_t *q, uint32_t i, uint32_t j) {
    uint16_t t = q->heap[i]; q->heap[i] = q->heap[j]; q->heap[j] = t;
    q->pos[q->heap[i]] = (uint16_t)i;
    q->pos[q->heap[j]] = (uint16_t)j;
}

static void sift_up(ipq_t *q, uint32_t i) {
    while (i > 0 && slot_less(q, i, PARENT(i))) {
        ipq_swap(q, i, PARENT(i));
        i = PARENT(i);
    }
}

static void sift_down(ipq_t *q, uint32_t i) {
    for (;;) {
        uint32_t l = LEFT(i), r = l + 1u, m = i;
        if (l < q->n && slot_less(q, l, m)) m = l;
        if (r < q->n && slot_less(q, r, m)) m = r;
        if (m == i) return;
        ipq_swap(q, i, m);
        i = m;
    }
}

static bool ipq_insert(ipq_t *q, uint32_t id, uint32_t key) {
    if (id >= IPQ_CAP || q->pos[id] != IPQ_NONE) return false;    /* 범위 밖 / 이미 있음 */
    q->key[id] = key;
    q->heap[q->n] = (uint16_t)id;                                   /* 마지막 잎에 넣고 */
    q->pos[id] = (uint16_t)q->n;
    q->n++;
    sift_up(q, q->n - 1u);                                          /* 위로 */
    return true;
}

static bool ipq_pop(ipq_t *q, uint32_t *id, uint32_t *key) {
    if (q->n == 0) return false;
    uint32_t top = q->heap[0];
    *id = top;
    *key = q->key[top];
    q->pos[top] = IPQ_NONE;                                         /* 빠진 id 는 "없음" */
    q->n--;
    if (q->n > 0) {
        q->heap[0] = q->heap[q->n];                                 /* 마지막 잎을 루트로 */
        q->pos[q->heap[0]] = 0;
        sift_down(q, 0);
    }
    return true;
}

/* ---------------- Part B: decrease / increase / remove ---------------- */
/* 키가 작아지면 위로만 갈 수 있다 (아래 자식들은 원래도 더 컸으니 여전히 크다) */
static bool ipq_decrease_key(ipq_t *q, uint32_t id, uint32_t key) {
    if (!ipq_contains(q, id) || key > q->key[id]) return false;    /* 없음 / 계약 위반 → 거절, 그대로 */
    q->key[id] = key;
    sift_up(q, q->pos[id]);
    return true;
}

/* 키가 커지면 아래로만 갈 수 있다 (부모는 원래도 더 작았으니 여전히 작다) */
static bool ipq_increase_key(ipq_t *q, uint32_t id, uint32_t key) {
    if (!ipq_contains(q, id) || key < q->key[id]) return false;
    q->key[id] = key;
    sift_down(q, q->pos[id]);
    return true;
}

/* 임의 id 삭제: 마지막 원소로 구멍을 메운 뒤 위 또는 아래로 — 방향은 옮겨 온 값이 정한다 */
static bool ipq_remove(ipq_t *q, uint32_t id) {
    if (!ipq_contains(q, id)) return false;
    uint32_t i = q->pos[id];
    q->pos[id] = IPQ_NONE;
    q->n--;
    if (i == q->n) return true;                                     /* 마지막 슬롯이었다 — 메울 구멍 없음 */
    q->heap[i] = q->heap[q->n];
    q->pos[q->heap[i]] = (uint16_t)i;
    if (i > 0 && slot_less(q, i, PARENT(i))) sift_up(q, i);        /* 다른 서브트리에서 온 작은 값 → 위로 */
    else sift_down(q, i);                                           /* 아니면 아래로 */
    return true;
}

/* 방향을 모를 때: 작아지면 decrease, 커지거나 같으면 increase (주어짐) */
static bool ipq_update(ipq_t *q, uint32_t id, uint32_t key) {
    if (!ipq_contains(q, id)) return false;
    return key <= q->key[id] ? ipq_decrease_key(q, id, key) : ipq_increase_key(q, id, key);
}

/* ---------------- Part C: 타이머 재설정/취소 + Dijkstra ---------------- */
/* 타이머 시작 또는 재시작: 이미 돌고 있으면 만료 시각만 바꾼다 (중복 엔트리 없음) */
static bool timer_arm(ipq_t *q, uint32_t id, uint32_t expire) {
    if (id >= IPQ_CAP) return false;
    if (ipq_contains(q, id)) return ipq_update(q, id, expire);
    return ipq_insert(q, id, expire);
}
static bool timer_cancel(ipq_t *q, uint32_t id) { return ipq_remove(q, id); }

/* now 까지 만료된 타이머를 (만료, id) 순서로 최대 max 개 꺼내 fired[] 에 기록 (주어짐) */
static uint32_t timer_service(ipq_t *q, uint32_t now, uint16_t *fired, uint32_t max) {
    uint32_t k = 0, id, exp;
    while (k < max && ipq_peek(q, &id, &exp) && exp <= now) {
        ipq_pop(q, &id, &exp);
        fired[k++] = (uint16_t)id;
    }
    return k;
}

#define G_MAXV 256u
#define G_MAXE 4096u
#define INF    UINT32_MAX
typedef struct {                         /* 방향 그래프, CSR: u 의 간선 = [head[u], head[u+1]) */
    uint32_t nv, ne;
    uint16_t head[G_MAXV + 1u];
    uint16_t to[G_MAXE];
    uint16_t w[G_MAXE];                  /* 가중치 >= 0 (Dijkstra 전제) */
} graph_t;

/* 큐에는 "아직 확정 안 된, 거리가 유한한" 정점만 있다. 반환: decrease_key 호출 횟수 */
static uint32_t dijkstra_pq(const graph_t *g, ipq_t *q, uint32_t src, uint32_t *dist, uint16_t *prev) {
    uint32_t ndec = 0;
    for (uint32_t v = 0; v < g->nv; v++) { dist[v] = INF; prev[v] = IPQ_NONE; }
    ipq_init(q);
    dist[src] = 0;
    ipq_insert(q, src, 0);
    uint32_t u, du;
    while (ipq_pop(q, &u, &du)) {                                   /* du == dist[u], 이제 확정 */
        for (uint32_t e = g->head[u]; e < g->head[u + 1u]; e++) {
            uint32_t v = g->to[e], nd = du + g->w[e];
            if (nd >= dist[v]) continue;                            /* 개선 없음 (확정된 정점도 여기서 걸러짐) */
            dist[v] = nd;
            prev[v] = (uint16_t)u;
            if (ipq_contains(q, v)) { ipq_decrease_key(q, v, nd); ndec++; }
            else ipq_insert(q, v, nd);
        }
    }
    return ndec;
}

/* ---------------- 검사 도구 (주어짐) ---------------- */
/* 불변식: pos 와 heap 이 서로의 역함수 + 힙 속성 + 살아 있는 id 수 == n */
static bool ipq_check(const ipq_t *q) {
    if (q->n > IPQ_CAP) return false;
    uint32_t live = 0;
    for (uint32_t id = 0; id < IPQ_CAP; id++) {
        if (q->pos[id] == IPQ_NONE) continue;
        live++;
        if (q->pos[id] >= q->n || q->heap[q->pos[id]] != id) return false;     /* heap[pos[id]] == id */
    }
    if (live != q->n) return false;
    for (uint32_t i = 0; i < q->n; i++) {
        uint32_t id = q->heap[i];
        if (id >= IPQ_CAP || q->pos[id] != i) return false;                     /* pos[heap[i]] == i */
        if (i > 0 && id_before(q, id, q->heap[PARENT(i)])) return false;       /* 힙 속성 */
    }
    return true;
}

static uint32_t rng_state = 1u;
static uint32_t xorshift32(void) {
    uint32_t x = rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return rng_state = x;
}

/* 참조 모델: id 별 (살아있나, 키). 최소는 선형 탐색 */
static bool     g_live[IPQ_CAP];
static uint32_t g_rkey[IPQ_CAP];
static bool ref_min(uint32_t idmax, uint32_t *id) {
    bool found = false;
    for (uint32_t i = 0; i < idmax; i++)
        if (g_live[i] && (!found || g_rkey[i] < g_rkey[*id])) { *id = i; found = true; }
    return found;                        /* 같은 키면 먼저 본(작은) id 유지 → (key, id) 순서와 같다 */
}
static bool same_as_ref(const ipq_t *q) {
    uint32_t cnt = 0;
    for (uint32_t id = 0; id < IPQ_CAP; id++) {
        if (ipq_contains(q, id) != g_live[id]) return false;
        if (g_live[id]) { cnt++; if (q->key[id] != g_rkey[id]) return false; }
    }
    return cnt == q->n;
}

/* 랜덤 연산 nops 회: insert/decrease/increase/update/remove/pop 을 참조 모델과 매번 대조 */
static bool random_ops(ipq_t *q, uint32_t seed, uint32_t nops, uint32_t idmax, uint32_t keymod, uint32_t *ok_ops) {
    ipq_init(q);
    memset(g_live, 0, sizeof g_live);
    rng_state = seed;
    uint32_t maxn = 0;
    for (uint32_t op = 0; op < nops; op++) {
        uint32_t r = xorshift32(), id = (r >> 4) % idmax, k = xorshift32() % keymod;
        uint32_t oid, okey, rid;
        bool ok;
        switch (r & 7u) {
        case 0: case 1: case 2:                                                 /* insert */
            ok = ipq_insert(q, id, k) == !g_live[id];
            if (!g_live[id]) { g_live[id] = true; g_rkey[id] = k; ok_ops[0]++; }
            break;
        case 3:                                                                 /* decrease */
            ok = ipq_decrease_key(q, id, k) == (g_live[id] && k <= g_rkey[id]);
            if (g_live[id] && k <= g_rkey[id]) { g_rkey[id] = k; ok_ops[1]++; }
            break;
        case 4:                                                                 /* increase */
            ok = ipq_increase_key(q, id, k) == (g_live[id] && k >= g_rkey[id]);
            if (g_live[id] && k >= g_rkey[id]) { g_rkey[id] = k; ok_ops[2]++; }
            break;
        case 5:                                                                 /* update (양방향) */
            ok = ipq_update(q, id, k) == g_live[id];
            if (g_live[id]) { g_rkey[id] = k; ok_ops[3]++; }
            break;
        case 6:                                                                 /* remove(id) */
            ok = ipq_remove(q, id) == g_live[id];
            if (g_live[id]) { g_live[id] = false; ok_ops[4]++; }
            break;
        default:                                                                /* pop */
            if (ref_min(idmax, &rid)) {
                ok = ipq_pop(q, &oid, &okey) && oid == rid && okey == g_rkey[rid];
                g_live[rid] = false; ok_ops[5]++;
            } else ok = !ipq_pop(q, &oid, &okey);
            break;
        }
        if (q->n > maxn) maxn = q->n;
        if (!ok || !ipq_check(q) || !same_as_ref(q)) { printf("  mismatch at op %u (kind %u)\n", op, r & 7u); return false; }
    }
    ok_ops[6] = maxn;
    return true;
}

/* O(V^2) 참조 Dijkstra: 인접 행렬 + 매번 선형 탐색으로 최소 정점 선택 (힙 없음) */
static uint32_t g_w[G_MAXV][G_MAXV];
static void build_matrix(const graph_t *g) {
    for (uint32_t u = 0; u < g->nv; u++)
        for (uint32_t v = 0; v < g->nv; v++) g_w[u][v] = INF;
    for (uint32_t u = 0; u < g->nv; u++)
        for (uint32_t e = g->head[u]; e < g->head[u + 1u]; e++)
            if (g->w[e] < g_w[u][g->to[e]]) g_w[u][g->to[e]] = g->w[e];   /* 중복 간선은 최소만 */
}
static void dijkstra_ref(const graph_t *g, uint32_t src, uint32_t *dist) {
    bool done[G_MAXV] = { false };
    build_matrix(g);
    for (uint32_t v = 0; v < g->nv; v++) dist[v] = INF;
    dist[src] = 0;
    for (uint32_t it = 0; it < g->nv; it++) {
        uint32_t u = G_MAXV;
        for (uint32_t v = 0; v < g->nv; v++)
            if (!done[v] && dist[v] != INF && (u == G_MAXV || dist[v] < dist[u])) u = v;
        if (u == G_MAXV) break;
        done[u] = true;
        for (uint32_t v = 0; v < g->nv; v++)
            if (g_w[u][v] != INF && dist[u] + g_w[u][v] < dist[v]) dist[v] = dist[u] + g_w[u][v];
    }
}

/* 간선 리스트 → CSR (계수 정렬) */
static void graph_build(graph_t *g, uint32_t nv, uint32_t ne, const uint16_t *eu, const uint16_t *ev, const uint16_t *ew) {
    g->nv = nv; g->ne = ne;
    memset(g->head, 0, sizeof g->head);
    for (uint32_t e = 0; e < ne; e++) g->head[eu[e] + 1u]++;
    for (uint32_t u = 0; u < nv; u++) g->head[u + 1u] = (uint16_t)(g->head[u + 1u] + g->head[u]);
    uint16_t fill[G_MAXV];
    memcpy(fill, g->head, sizeof fill);
    for (uint32_t e = 0; e < ne; e++) { uint32_t s = fill[eu[e]]++; g->to[s] = ev[e]; g->w[s] = ew[e]; }
}

/* prev[] 검사: src 는 NONE, 도달 불가도 NONE, 나머지는 dist[p] + w(p,v) == dist[v] 이고 src 까지 따라가짐 */
static bool prev_ok(const graph_t *g, uint32_t src, const uint32_t *dist, const uint16_t *prev) {
    for (uint32_t v = 0; v < g->nv; v++) {
        if (v == src || dist[v] == INF) { if (prev[v] != IPQ_NONE) return false; continue; }
        uint32_t p = prev[v];
        if (p >= g->nv || dist[p] == INF || g_w[p][v] == INF || dist[p] + g_w[p][v] != dist[v]) return false;
        uint32_t x = v, steps = 0;
        while (x != src && steps++ <= g->nv) x = prev[x];
        if (x != src) return false;
    }
    return true;
}

static ipq_t    g_q;
static graph_t  g_g;
static uint16_t g_eu[G_MAXE], g_ev[G_MAXE], g_ew[G_MAXE];
static uint32_t g_d1[G_MAXV], g_d2[G_MAXV];
static uint16_t g_prev[G_MAXV];

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* 그림의 7개: id 0..6, key 1 50 2 60 70 3 4 를 순서대로 insert → heap[] = 0 1 2 3 4 5 6 */
static void load7(ipq_t *q) {
    static const uint32_t k7[7] = { 1, 50, 2, 60, 70, 3, 4 };
    ipq_init(q);
    for (uint32_t id = 0; id < 7; id++) ipq_insert(q, id, k7[id]);
}

int main(void) {
    ipq_t *q = &g_q;
    uint32_t id = 0, key = 0;

    /* Part A: 빈 큐, insert 할 때마다 불변식 + peek == 최소, 거절 조건 */
    {
        ipq_init(q);
        CHECK(!ipq_peek(q, &id, &key) && !ipq_pop(q, &id, &key) && ipq_check(q));
        static const uint32_t k7[7] = { 1, 50, 2, 60, 70, 3, 4 };
        bool ok = true;
        for (uint32_t i = 0; i < 7; i++)
            ok = ok && ipq_insert(q, i, k7[i]) && ipq_check(q) && ipq_peek(q, &id, &key) && id == 0 && key == 1;
        CHECK(ok && q->n == 7);
        CHECK(!ipq_insert(q, 3, 0) && !ipq_insert(q, IPQ_CAP, 0) && q->key[3] == 60 && ipq_check(q));
        static const uint32_t want[7] = { 0, 2, 5, 6, 1, 3, 4 };      /* 키 순: 1 2 3 4 50 60 70 */
        ok = true;
        for (uint32_t i = 0; i < 7; i++)
            ok = ok && ipq_pop(q, &id, &key) && id == want[i] && !ipq_contains(q, id) && ipq_check(q);
        CHECK(ok && q->n == 0);
        ok = true;                                                      /* 1024개 전부, 키 내림차순 */
        for (uint32_t i = 0; i < IPQ_CAP; i++) ok = ok && ipq_insert(q, i, IPQ_CAP - i);
        CHECK(ok && q->n == IPQ_CAP && ipq_check(q) && q->heap[0] == IPQ_CAP - 1u);
        for (uint32_t i = 0; i < IPQ_CAP && ok; i++) ok = ipq_pop(q, &id, &key) && id == IPQ_CAP - 1u - i && key == i + 1u;
        CHECK(ok && q->n == 0 && ipq_check(q));
    }

    /* Part B: remove 는 양방향 — 그림의 두 경우 */
    {
        load7(q);
        CHECK(ipq_remove(q, 4) && ipq_check(q));                        /* 마지막 id6(키 4)이 슬롯 4 → 위로 */
        CHECK(q->heap[1] == 6 && q->pos[6] == 1 && q->pos[1] == 4 && q->pos[4] == IPQ_NONE && q->n == 6);
        load7(q);
        CHECK(ipq_remove(q, 2) && ipq_check(q));                        /* id6(키 4)이 슬롯 2 → 아래로 */
        CHECK(q->heap[2] == 5 && q->heap[5] == 6 && q->pos[5] == 2 && q->pos[6] == 5);
        load7(q);
        bool ok = ipq_remove(q, 6) && ipq_check(q) && q->n == 6;       /* 마지막 슬롯 자체 */
        ok = ok && ipq_remove(q, 0) && ipq_check(q) && q->heap[0] == 2; /* 루트 */
        ok = ok && !ipq_remove(q, 0) && !ipq_remove(q, IPQ_CAP) && q->n == 5;
        CHECK(ok);
        ipq_init(q);
        CHECK(ipq_insert(q, 9, 5) && ipq_remove(q, 9) && q->n == 0 && ipq_check(q) && !ipq_contains(q, 9));
    }
    /* Part B: decrease / increase 계약과 이동 방향 */
    {
        load7(q);
        CHECK(!ipq_decrease_key(q, 3, 61) && q->key[3] == 60 && !ipq_decrease_key(q, 100, 0));
        CHECK(ipq_decrease_key(q, 4, 0) && ipq_check(q) && q->heap[0] == 4 && q->pos[0] == 1);   /* 슬롯4 → 루트 */
        CHECK(ipq_increase_key(q, 4, 0) && q->heap[0] == 4 && !ipq_increase_key(q, 1, 49));       /* 같은 키 OK, 작아짐 거절 */
        CHECK(ipq_increase_key(q, 4, 100) && ipq_check(q) && q->heap[0] == 0);                    /* 루트 → 아래로 */
        CHECK(ipq_update(q, 3, 0) && q->heap[0] == 3 && ipq_update(q, 3, 999) && ipq_check(q) && !ipq_update(q, 77, 1));
    }
    /* Part B: 랜덤 연산 vs 참조 모델 (보통 키 / 동점 폭주) */
    {
        uint32_t c1[7] = { 0 }, c2[7] = { 0 };
        bool ok1 = random_ops(q, 0xC0FFEEu, 30000u, 600u, 1000u, c1);
        bool ok2 = random_ops(q, 0xBEEFu, 30000u, IPQ_CAP, 8u, c2);
        CHECK(ok1);
        CHECK(ok2);
        printf("[B] random ops (keys 0..999): ins %u dec %u inc %u upd %u rm %u pop %u, max n %u -> %s\n",
               c1[0], c1[1], c1[2], c1[3], c1[4], c1[5], c1[6], ok1 ? "match" : "MISMATCH");
        printf("[B] random ops (keys 0..7)  : ins %u dec %u inc %u upd %u rm %u pop %u, max n %u -> %s\n",
               c2[0], c2[1], c2[2], c2[3], c2[4], c2[5], c2[6], ok2 ? "match" : "MISMATCH");
    }

    /* Part C: 타이머 재설정/취소 시나리오 */
    {
        uint16_t fired[8];
        ipq_init(q);
        timer_arm(q, 1, 100); timer_arm(q, 2, 50); timer_arm(q, 3, 75);
        timer_arm(q, 4, 200); timer_arm(q, 5, 60);
        CHECK(q->n == 5 && ipq_check(q));
        CHECK(timer_arm(q, 2, 150) && q->n == 5 && q->key[2] == 150 && ipq_check(q));   /* 재시작: 늦춤 */
        CHECK(timer_arm(q, 4, 70) && q->n == 5 && ipq_check(q));                         /* 재시작: 당김 */
        CHECK(timer_cancel(q, 3) && !timer_cancel(q, 3) && q->n == 4 && ipq_check(q));   /* 취소 */
        timer_arm(q, 6, 60);
        CHECK(timer_service(q, 59, fired, 8) == 0);
        CHECK(timer_service(q, 65, fired, 8) == 2 && fired[0] == 5 && fired[1] == 6);   /* 같은 60 → id 순 */
        CHECK(timer_service(q, 100, fired, 8) == 2 && fired[0] == 4 && fired[1] == 1);
        CHECK(!timer_cancel(q, 1) && q->n == 1 && timer_service(q, 1000, fired, 8) == 1 && fired[0] == 2);
        CHECK(!timer_arm(q, IPQ_CAP, 5) && q->n == 0 && ipq_check(q));
    }
    /* Part C: 랜덤 타이머 시뮬레이션 — arm/재시작/취소/시간 진행, 만료 순서를 참조와 대조 */
    {
        uint16_t fired[4];
        uint32_t now = 0, nfired = 0, nrestart = 0, ncancel = 0;
        bool ok = true;
        ipq_init(q);
        memset(g_live, 0, sizeof g_live);
        rng_state = 7u;
        for (uint32_t step = 0; step < 20000u && ok; step++) {
            uint32_t r = xorshift32(), t = r % 256u;
            switch ((r >> 8) % 4u) {
            case 0: case 1: {
                uint32_t ex = now + 1u + (r >> 12) % 500u;
                nrestart += g_live[t];
                ok = timer_arm(q, t, ex);
                g_live[t] = true; g_rkey[t] = ex;
            } break;
            case 2:
                ok = timer_cancel(q, t) == g_live[t];
                ncancel += g_live[t];
                g_live[t] = false;
                break;
            default: {
                now += (r >> 12) % 16u;
                uint32_t k = timer_service(q, now, fired, 4), j = 0, rid;
                while (j < 4 && ref_min(256u, &rid) && g_rkey[rid] <= now) {
                    ok = ok && j < k && fired[j] == rid;
                    g_live[rid] = false; j++;
                }
                ok = ok && j == k;
                nfired += k;
            } break;
            }
            ok = ok && ipq_check(q) && same_as_ref(q);
        }
        CHECK(ok && nrestart > 1000 && ncancel > 500);
        printf("[C] timer sim 20000 steps: fired %u, restarts %u, cancels %u -> %s\n",
               nfired, nrestart, ncancel, ok ? "match" : "MISMATCH");
    }

    /* Part C: Dijkstra — 손으로 추적한 5정점 예제 */
    {
        static const uint16_t eu[] = { 0, 0, 2, 1, 2, 2, 4 }, ev[] = { 1, 2, 1, 3, 3, 4, 3 }, ew[] = { 10, 3, 4, 2, 8, 2, 5 };
        graph_build(&g_g, 5, 7, eu, ev, ew);
        uint32_t ndec = dijkstra_pq(&g_g, q, 0, g_d1, g_prev);
        CHECK(g_d1[0] == 0 && g_d1[1] == 7 && g_d1[2] == 3 && g_d1[3] == 9 && g_d1[4] == 5);
        CHECK(g_prev[0] == IPQ_NONE && g_prev[1] == 2 && g_prev[2] == 0 && g_prev[3] == 1 && g_prev[4] == 2);
        CHECK(ndec == 3 && q->n == 0);
        printf("[C] 5-node example: dist = %u %u %u %u %u, decrease_key calls = %u\n",
               g_d1[0], g_d1[1], g_d1[2], g_d1[3], g_d1[4], ndec);
    }
    /* Part C: 랜덤 그래프 60개 — O(V^2) 참조와 dist 완전 일치 + prev 경로 검증 */
    {
        bool ok_d = true, ok_p = true;
        uint32_t tot_dec = 0, tot_e = 0, unreach = 0;
        rng_state = 2026u;
        for (uint32_t gi = 0; gi < 60u; gi++) {
            uint32_t nv = 1u + xorshift32() % 200u;
            uint32_t emax = nv * 8u < G_MAXE ? nv * 8u : G_MAXE;
            uint32_t ne = xorshift32() % (emax + 1u);
            for (uint32_t e = 0; e < ne; e++) {
                g_eu[e] = (uint16_t)(xorshift32() % nv);
                g_ev[e] = (uint16_t)(xorshift32() % nv);
                g_ew[e] = (uint16_t)(xorshift32() % 100u);              /* 0 포함 */
            }
            graph_build(&g_g, nv, ne, g_eu, g_ev, g_ew);
            uint32_t src = xorshift32() % nv;
            tot_dec += dijkstra_pq(&g_g, q, src, g_d1, g_prev);
            dijkstra_ref(&g_g, src, g_d2);
            ok_d = ok_d && memcmp(g_d1, g_d2, nv * sizeof g_d1[0]) == 0;
            ok_p = ok_p && prev_ok(&g_g, src, g_d1, g_prev);
            for (uint32_t v = 0; v < nv; v++) unreach += g_d2[v] == INF;
            tot_e += ne;
        }
        CHECK(ok_d);
        CHECK(ok_p);
        CHECK(unreach > 0 && tot_dec > 0);
        printf("[C] dijkstra 60 random graphs (E total %u): dist %s, prev %s, decrease_key %u, unreachable %u\n",
               tot_e, ok_d ? "match" : "MISMATCH", ok_p ? "ok" : "BAD", tot_dec, unreach);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
