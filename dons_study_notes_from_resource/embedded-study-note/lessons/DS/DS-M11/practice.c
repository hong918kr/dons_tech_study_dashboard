/* DS-M11 추가 연습 — 한 번에 검증
 * 공용: d-ary indexed min-heap (d = 2 면 본문과 같은 이진 힙). 불변식 pos[heap[i]] == i.
 * 연습 1 O(n) 일괄 구성(build) / 연습 2 d-ary 와 Dijkstra 비교 횟수 /
 * 연습 3 세대(generation) 핸들로 ABA 안전한 타이머 취소 / 연습 4 k-way 병합의 replace-top
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CAP  1024u
#define NONE 0xFFFFu

typedef struct {
    uint16_t heap[CAP], pos[CAP];
    uint32_t key[CAP];
    uint32_t n, d;                       /* d = 자식 수 (2, 4, 8 ...) */
} dpq_t;

static uint32_t g_cmp_up, g_cmp_down;    /* sift_up / sift_down 에서의 비교 횟수 */
static bool before(const dpq_t *q, uint32_t a, uint32_t b) {
    return q->key[a] != q->key[b] ? q->key[a] < q->key[b] : a < b;
}
static void dswap(dpq_t *q, uint32_t i, uint32_t j) {
    uint16_t t = q->heap[i]; q->heap[i] = q->heap[j]; q->heap[j] = t;
    q->pos[q->heap[i]] = (uint16_t)i;
    q->pos[q->heap[j]] = (uint16_t)j;
}
static void dup(dpq_t *q, uint32_t i) {
    while (i > 0) {
        uint32_t p = (i - 1u) / q->d;
        g_cmp_up++;
        if (!before(q, q->heap[i], q->heap[p])) return;
        dswap(q, i, p);
        i = p;
    }
}
static void ddown(dpq_t *q, uint32_t i) {
    for (;;) {
        uint32_t c0 = q->d * i + 1u, m = i;
        for (uint32_t c = c0; c < c0 + q->d && c < q->n; c++) {
            g_cmp_down++;
            if (before(q, q->heap[c], q->heap[m])) m = c;
        }
        if (m == i) return;
        dswap(q, i, m);
        i = m;
    }
}
static void dinit(dpq_t *q, uint32_t d) { q->n = 0; q->d = d; for (uint32_t i = 0; i < CAP; i++) q->pos[i] = NONE; }
static bool dhas(const dpq_t *q, uint32_t id) { return id < CAP && q->pos[id] != NONE; }
static bool dinsert(dpq_t *q, uint32_t id, uint32_t key) {
    if (id >= CAP || q->pos[id] != NONE) return false;
    q->key[id] = key; q->heap[q->n] = (uint16_t)id; q->pos[id] = (uint16_t)q->n;
    dup(q, q->n++);
    return true;
}
static bool dremove(dpq_t *q, uint32_t id) {
    if (!dhas(q, id)) return false;
    uint32_t i = q->pos[id];
    q->pos[id] = NONE;
    if (i == --q->n) return true;
    q->heap[i] = q->heap[q->n]; q->pos[q->heap[i]] = (uint16_t)i;
    if (i > 0 && before(q, q->heap[i], q->heap[(i - 1u) / q->d])) dup(q, i); else ddown(q, i);
    return true;
}
static bool dpop(dpq_t *q, uint32_t *id, uint32_t *key) {
    if (q->n == 0) return false;
    *id = q->heap[0]; *key = q->key[*id];
    return dremove(q, *id);
}
static bool ddecrease(dpq_t *q, uint32_t id, uint32_t key) {
    if (!dhas(q, id) || key > q->key[id]) return false;
    q->key[id] = key; dup(q, q->pos[id]);
    return true;
}
static bool dincrease(dpq_t *q, uint32_t id, uint32_t key) {
    if (!dhas(q, id) || key < q->key[id]) return false;
    q->key[id] = key; ddown(q, q->pos[id]);
    return true;
}
static bool dcheck(const dpq_t *q) {
    uint32_t live = 0;
    for (uint32_t id = 0; id < CAP; id++)
        if (q->pos[id] != NONE) { live++; if (q->pos[id] >= q->n || q->heap[q->pos[id]] != id) return false; }
    if (live != q->n) return false;
    for (uint32_t i = 0; i < q->n; i++) {
        if (q->heap[i] >= CAP || q->pos[q->heap[i]] != i) return false;
        if (i > 0 && before(q, q->heap[i], q->heap[(i - 1u) / q->d])) return false;
    }
    return true;
}

static uint32_t rng = 1u;
static uint32_t xorshift32(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

/* ---------- 연습 1: O(n) 일괄 구성 — pos 까지 함께 세운다 ---------- */
static bool dbuild(dpq_t *q, const uint16_t *ids, const uint32_t *keys, uint32_t n) {
    dinit(q, q->d);
    if (n > CAP) return false;
    for (uint32_t i = 0; i < n; i++) {
        if (ids[i] >= CAP || q->pos[ids[i]] != NONE) { dinit(q, q->d); return false; }   /* 중복 id 거절 */
        q->heap[i] = ids[i]; q->pos[ids[i]] = (uint16_t)i; q->key[ids[i]] = keys[i];
    }
    q->n = n;
    if (n > 1) for (uint32_t i = (n - 2u) / q->d + 1u; i-- > 0;) ddown(q, i);   /* 마지막 내부 노드부터 */
    return true;
}

/* ---------- 연습 2: d-ary 힙 Dijkstra ---------- */
#define GV 256u
#define GE 8192u
static uint16_t g_head[GV + 1u], g_to[GE], g_w[GE];
static void make_graph(uint32_t nv, uint32_t ne, uint32_t seed) {
    static uint16_t eu[GE], ev[GE], ew[GE];
    rng = seed;
    memset(g_head, 0, sizeof g_head);
    for (uint32_t e = 0; e < ne; e++) {
        eu[e] = (uint16_t)(xorshift32() % nv); ev[e] = (uint16_t)(xorshift32() % nv); ew[e] = (uint16_t)(1u + xorshift32() % 1000u);
        g_head[eu[e] + 1u]++;
    }
    for (uint32_t u = 0; u < nv; u++) g_head[u + 1u] = (uint16_t)(g_head[u + 1u] + g_head[u]);
    uint16_t fill[GV];
    memcpy(fill, g_head, sizeof fill);
    for (uint32_t e = 0; e < ne; e++) { uint32_t s = fill[eu[e]]++; g_to[s] = ev[e]; g_w[s] = ew[e]; }
}
static uint32_t dijkstra_d(dpq_t *q, uint32_t d, uint32_t nv, uint32_t *dist) {
    uint32_t ndec = 0, u, du;
    for (uint32_t v = 0; v < nv; v++) dist[v] = UINT32_MAX;
    dinit(q, d);
    dist[0] = 0; dinsert(q, 0, 0);
    while (dpop(q, &u, &du))
        for (uint32_t e = g_head[u]; e < g_head[u + 1u]; e++) {
            uint32_t v = g_to[e], nd = du + g_w[e];
            if (nd >= dist[v]) continue;
            dist[v] = nd;
            if (dhas(q, v)) { ddecrease(q, v, nd); ndec++; } else dinsert(q, v, nd);
        }
    return ndec;
}

/* ---------- 연습 3: 세대 핸들 — 오래된 핸들로 새 타이머를 취소하지 못하게 ---------- */
#define TSLOTS 64u
typedef uint32_t thandle_t;              /* (gen << 16) | slot. 0 = 무효 핸들 */
static uint16_t g_gen[TSLOTS];
static uint16_t g_free[TSLOTS];
static uint32_t g_nfree;
static void tm_init(dpq_t *q) {
    dinit(q, 2);
    g_nfree = 0;
    for (uint32_t s = TSLOTS; s-- > 0;) g_free[g_nfree++] = (uint16_t)s;   /* LIFO: 방금 반납한 슬롯을 곧바로 재사용 */
    memset(g_gen, 0, sizeof g_gen);
}
static thandle_t tm_start(dpq_t *q, uint32_t expire) {
    if (g_nfree == 0) return 0;
    uint32_t s = g_free[--g_nfree];
    if (++g_gen[s] == 0) g_gen[s] = 1;                                    /* gen 0 은 쓰지 않는다 → 핸들 0 은 무효 */
    dinsert(q, s, expire);
    return ((thandle_t)g_gen[s] << 16) | s;
}
static bool tm_cancel(dpq_t *q, thandle_t h) {
    uint32_t s = h & 0xFFFFu, gen = h >> 16;
    if (s >= TSLOTS || gen == 0 || g_gen[s] != gen || !dhas(q, s)) return false;   /* 세대가 다르면 남의 타이머 */
    dremove(q, s);
    g_free[g_nfree++] = (uint16_t)s;
    return true;
}
static uint32_t tm_service(dpq_t *q, uint32_t now, thandle_t *fired, uint32_t max) {
    uint32_t k = 0, s, ex;
    while (k < max && q->n > 0 && q->key[q->heap[0]] <= now) {
        dpop(q, &s, &ex);
        fired[k++] = ((thandle_t)g_gen[s] << 16) | s;
        g_free[g_nfree++] = (uint16_t)s;                                  /* 만료 → 슬롯 반납 */
    }
    return k;
}

/* ---------- 연습 4: k-way 병합 — pop+insert 대신 루트 키만 바꾸기 ---------- */
#define KS 16u
#define KLEN 256u
static uint32_t g_stream[KS][KLEN], g_len[KS], g_idx[KS];
static uint32_t g_out[KS * KLEN], g_all[KS * KLEN];
static uint32_t kmerge(dpq_t *q, bool replace_top) {
    uint32_t n = 0, s, k;
    dinit(q, 2);
    for (s = 0; s < KS; s++) { g_idx[s] = 0; if (g_len[s]) dinsert(q, s, g_stream[s][0]); }
    while (q->n > 0) {
        s = q->heap[0];
        g_out[n++] = q->key[s];
        uint32_t nx = ++g_idx[s];
        if (replace_top) {
            if (nx < g_len[s]) dincrease(q, s, g_stream[s][nx]);   /* 정렬된 스트림 → 다음 값 >= 현재: increase */
            else dremove(q, s);
        } else {
            dpop(q, &s, &k);
            if (nx < g_len[s]) dinsert(q, s, g_stream[s][nx]);
        }
    }
    return n;
}

static void isort(uint32_t *v, uint32_t n) {
    for (uint32_t i = 1; i < n; i++) { uint32_t x = v[i], j = i; while (j > 0 && x < v[j - 1]) { v[j] = v[j - 1]; j--; } v[j] = x; }
}

static dpq_t g_q;
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    dpq_t *q = &g_q;
    uint32_t id, key;

    /* 연습 1: build 후 불변식 + pop 순서 == 정렬, 비교 횟수 build vs n번 insert */
    {
        static uint16_t ids[CAP];
        static uint32_t keys[CAP], sorted[CAP];
        const uint32_t n = 1000;
        rng = 11u;
        for (uint32_t i = 0; i < n; i++) { ids[i] = (uint16_t)((i * 7u) % CAP); keys[i] = (xorshift32() % 5000u) * CAP + ids[i]; }
        for (uint32_t i = 0; i < n; i++) sorted[i] = keys[i];      /* 키에 id 를 섞어 넣어 키가 서로 다르게 */
        isort(sorted, n);
        q->d = 2;
        CHECK(dbuild(q, ids, keys, n) && dcheck(q) && q->n == n);
        bool ok = true;
        for (uint32_t i = 0; i < n && ok; i++) ok = dpop(q, &id, &key) && key == sorted[i] && dcheck(q);
        CHECK(ok);
        ids[5] = ids[4];
        CHECK(!dbuild(q, ids, keys, 10) && q->n == 0 && dcheck(q));   /* 중복 id → 거절, 빈 상태 */
        for (uint32_t i = 0; i < CAP; i++) { ids[i] = (uint16_t)i; keys[i] = CAP - i; }   /* 내림차순 = insert 최악 */
        g_cmp_up = g_cmp_down = 0;
        dinit(q, 2);
        for (uint32_t i = 0; i < CAP; i++) dinsert(q, ids[i], keys[i]);
        uint32_t c_ins = g_cmp_up + g_cmp_down;
        g_cmp_up = g_cmp_down = 0;
        dbuild(q, ids, keys, CAP);
        uint32_t c_build = g_cmp_up + g_cmp_down;
        CHECK(dcheck(q) && c_build <= 2u * CAP && c_build < c_ins);
        printf("[1] n=1024 descending keys: %u inserts = %u cmp, build = %u cmp\n", CAP, c_ins, c_build);
    }

    /* 연습 2: Dijkstra 비교 횟수 — d = 2 / 4 / 8, 결과 dist 는 모두 같아야 */
    {
        static uint32_t dist2[GV], distd[GV];
        static const uint32_t es[2] = { 512u, 8192u };
        bool same = true;
        printf("[2] V=256  E     d | decrease  sift_up cmp  sift_down cmp  total\n");
        for (uint32_t gi = 0; gi < 2; gi++) {
            make_graph(GV, es[gi], 99u + gi);
            for (uint32_t d = 2; d <= 8; d *= 2) {
                g_cmp_up = g_cmp_down = 0;
                uint32_t ndec = dijkstra_d(q, d, GV, d == 2 ? dist2 : distd);
                if (d != 2) same = same && memcmp(dist2, distd, sizeof dist2) == 0;
                printf("[2]       %4u  %u | %8u  %11u  %13u  %5u\n", es[gi], d, ndec, g_cmp_up, g_cmp_down, g_cmp_up + g_cmp_down);
            }
        }
        CHECK(same);
    }

    /* 연습 3: 세대 핸들 — 재사용된 슬롯을 옛 핸들로 취소하면 거절 */
    {
        thandle_t fired[8];
        tm_init(q);
        thandle_t h1 = tm_start(q, 100);
        CHECK(tm_service(q, 100, fired, 8) == 1 && fired[0] == h1);         /* h1 만료, 슬롯 반납 */
        thandle_t h2 = tm_start(q, 200);
        CHECK((h1 & 0xFFFFu) == (h2 & 0xFFFFu) && h1 != h2);                /* 같은 슬롯, 다른 세대 */
        CHECK(!tm_cancel(q, h1) && dhas(q, h2 & 0xFFFFu));                  /* 옛 핸들은 새 타이머를 못 건드림 */
        CHECK(tm_cancel(q, h2) && !tm_cancel(q, h2) && !tm_cancel(q, 0) && q->n == 0);
        /* 랜덤: 발급된 모든 핸들 기록, 아무 핸들이나 취소 → 결과 == 그 핸들이 아직 살아 있나 */
        static thandle_t hs[20000];
        static bool alive[20000];
        uint32_t nh = 0, now = 0, stale_rejected = 0;
        bool ok = true;
        tm_init(q);
        rng = 5u;
        for (uint32_t step = 0; step < 20000u && ok; step++) {
            uint32_t r = xorshift32();
            if (r % 3u == 0) {
                bool full = g_nfree == 0;
                thandle_t h = tm_start(q, now + 1u + (r >> 8) % 64u);
                ok = (h == 0) == full;
                if (h) { hs[nh] = h; alive[nh++] = true; }
            } else if (r % 3u == 1 && nh > 0) {
                uint32_t j = (r >> 8) % nh;
                bool c = tm_cancel(q, hs[j]);
                ok = c == alive[j];
                stale_rejected += !alive[j];
                alive[j] = false;
            } else {
                now += (r >> 8) % 8u;
                uint32_t k = tm_service(q, now, fired, 8);
                for (uint32_t i = 0; i < k; i++)                            /* 만료된 핸들은 죽음 */
                    for (uint32_t j = nh; j-- > 0;) if (hs[j] == fired[i] && alive[j]) { alive[j] = false; break; }
            }
            ok = ok && dcheck(q) && q->n + g_nfree == TSLOTS;
        }
        CHECK(ok && stale_rejected > 1000);
        printf("[3] random start/cancel/expire: handles %u, stale cancels rejected %u -> %s\n", nh, stale_rejected, ok ? "ok" : "BROKEN");
    }

    /* 연습 4: 16개 정렬 스트림 병합 — replace-top vs pop+insert */
    {
        uint32_t total = 0;
        rng = 77u;
        for (uint32_t s = 0; s < KS; s++) {
            g_len[s] = s == 3 ? 0 : xorshift32() % KLEN;                   /* 빈 스트림 하나 포함 */
            uint32_t v = 0;
            for (uint32_t i = 0; i < g_len[s]; i++) { v += xorshift32() % 50u; g_stream[s][i] = v; g_all[total++] = v; }
        }
        isort(g_all, total);
        g_cmp_up = g_cmp_down = 0;
        uint32_t n1 = kmerge(q, true);
        uint32_t c1 = g_cmp_up + g_cmp_down;
        bool ok1 = n1 == total && memcmp(g_out, g_all, total * sizeof g_all[0]) == 0;
        g_cmp_up = g_cmp_down = 0;
        uint32_t n2 = kmerge(q, false);
        uint32_t c2 = g_cmp_up + g_cmp_down;
        bool ok2 = n2 == total && memcmp(g_out, g_all, total * sizeof g_all[0]) == 0;
        CHECK(ok1 && ok2 && c1 < c2);
        printf("[4] merge %u values from %u streams: replace-top %u cmp, pop+insert %u cmp\n", total, KS, c1, c2);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
