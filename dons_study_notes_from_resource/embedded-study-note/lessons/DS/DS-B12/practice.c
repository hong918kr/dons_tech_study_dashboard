/* DS-B12 추가 연습 — 한 번에 검증 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t rng = 1u;
static uint32_t xorshift32(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

static void isort_u32(uint32_t *v, uint32_t n) {
    for (uint32_t i = 1; i < n; i++) {
        uint32_t x = v[i], j = i;
        while (j > 0 && x < v[j - 1]) { v[j] = v[j - 1]; j--; }
        v[j] = x;
    }
}

/* ---------- 연습 1: 제자리 힙 정렬 (max-heap, 추가 메모리 0) ---------- */
static void sift_down_max(uint32_t *a, uint32_t n, uint32_t i) {
    for (;;) {
        uint32_t l = 2u * i + 1u, r = l + 1u, m = i;
        if (l < n && a[l] > a[m]) m = l;
        if (r < n && a[r] > a[m]) m = r;
        if (m == i) return;
        uint32_t t = a[i]; a[i] = a[m]; a[m] = t;
        i = m;
    }
}
static void heap_sort(uint32_t *a, uint32_t n) {
    for (uint32_t i = n / 2u; i-- > 0;) sift_down_max(a, n, i);    /* Floyd heapify */
    for (uint32_t end = n; end > 1; end--) {                        /* 최대를 맨 뒤로 */
        uint32_t t = a[0]; a[0] = a[end - 1]; a[end - 1] = t;
        sift_down_max(a, end - 1u, 0);
    }
}

/* ---------- 연습 2: 스트림에서 가장 큰 k개 — 크기 k 의 min-heap ---------- */
static void sift_down_min(uint32_t *a, uint32_t n, uint32_t i) {
    for (;;) {
        uint32_t l = 2u * i + 1u, r = l + 1u, m = i;
        if (l < n && a[l] < a[m]) m = l;
        if (r < n && a[r] < a[m]) m = r;
        if (m == i) return;
        uint32_t t = a[i]; a[i] = a[m]; a[m] = t;
        i = m;
    }
}
/* top[0..k) 에 지금까지 본 값 중 가장 큰 k개를 유지. 루트 = 그중 최소 = 커트라인 */
static void topk_feed(uint32_t *top, uint32_t *cnt, uint32_t k, uint32_t x) {
    if (*cnt < k) {
        uint32_t i = (*cnt)++;
        top[i] = x;
        while (i > 0 && top[i] < top[(i - 1u) / 2u]) {               /* sift up */
            uint32_t p = (i - 1u) / 2u, t = top[i]; top[i] = top[p]; top[p] = t; i = p;
        }
    } else if (x > top[0]) {                                        /* 커트라인보다 크면 교체 */
        top[0] = x;
        sift_down_min(top, k, 0);
    }
}

/* ---------- 연습 3·4 공용: 타이머 힙 + id → 위치 맵 ---------- */
#define TCAP 256u
typedef struct { uint32_t expire; uint16_t id; } tev_t;
typedef struct {
    tev_t    a[TCAP];
    uint32_t n;
    int16_t  pos[TCAP];                  /* 연습 4: id 가 힙의 몇 번에 있나 (-1 = 없음) */
    bool     wrap_safe;                  /* 연습 3: 랩어라운드 안전 비교 사용 */
} theap_t;

/* 연습 3: tick 카운터가 넘쳐도 맞는 비교 — 둘의 차이가 2^31 미만일 때 유효 */
static bool tick_before(uint32_t a, uint32_t b) { return (int32_t)(a - b) < 0; }

static bool before(const theap_t *h, tev_t x, tev_t y) {
    if (x.expire != y.expire)
        return h->wrap_safe ? tick_before(x.expire, y.expire) : x.expire < y.expire;
    return x.id < y.id;
}
static void th_swap(theap_t *h, uint32_t i, uint32_t j) {
    tev_t t = h->a[i]; h->a[i] = h->a[j]; h->a[j] = t;
    h->pos[h->a[i].id] = (int16_t)i;                                /* 위치 맵도 함께 갱신 */
    h->pos[h->a[j].id] = (int16_t)j;
}
static void th_up(theap_t *h, uint32_t i) {
    while (i > 0 && before(h, h->a[i], h->a[(i - 1u) / 2u])) { th_swap(h, i, (i - 1u) / 2u); i = (i - 1u) / 2u; }
}
static void th_down(theap_t *h, uint32_t i) {
    for (;;) {
        uint32_t l = 2u * i + 1u, r = l + 1u, m = i;
        if (l < h->n && before(h, h->a[l], h->a[m])) m = l;
        if (r < h->n && before(h, h->a[r], h->a[m])) m = r;
        if (m == i) return;
        th_swap(h, i, m);
        i = m;
    }
}
static void th_init(theap_t *h, bool wrap_safe) {
    h->n = 0; h->wrap_safe = wrap_safe;
    for (uint32_t i = 0; i < TCAP; i++) h->pos[i] = -1;
}
static bool th_push(theap_t *h, uint32_t expire, uint16_t id) {
    if (h->n == TCAP || id >= TCAP || h->pos[id] >= 0) return false;  /* 같은 id 중복 등록 금지 */
    h->a[h->n] = (tev_t){ expire, id };
    h->pos[id] = (int16_t)h->n;
    th_up(h, h->n++);
    return true;
}
/* i 번 원소를 제거: 마지막 원소로 메운 뒤, 그 원소가 위/아래 어느 쪽으로든 갈 수 있다 */
static void th_remove_at(theap_t *h, uint32_t i) {
    h->pos[h->a[i].id] = -1;
    h->n--;
    if (i == h->n) return;                                          /* 마지막이었으면 끝 */
    h->a[i] = h->a[h->n];
    h->pos[h->a[i].id] = (int16_t)i;
    if (i > 0 && before(h, h->a[i], h->a[(i - 1u) / 2u])) th_up(h, i);
    else th_down(h, i);
}
static bool th_pop(theap_t *h, tev_t *out) {
    if (h->n == 0) return false;
    *out = h->a[0];
    th_remove_at(h, 0);
    return true;
}
/* 연습 4: 타이머 취소 — O(log n) */
static bool th_cancel(theap_t *h, uint16_t id) {
    if (id >= TCAP || h->pos[id] < 0) return false;
    th_remove_at(h, (uint32_t)h->pos[id]);
    return true;
}
static bool th_check(const theap_t *h) {
    for (uint32_t i = 1; i < h->n; i++) if (before(h, h->a[i], h->a[(i - 1u) / 2u])) return false;
    uint32_t live = 0;
    for (uint32_t id = 0; id < TCAP; id++) {
        if (h->pos[id] < 0) continue;
        live++;
        if ((uint32_t)h->pos[id] >= h->n || h->a[h->pos[id]].id != id) return false;
    }
    return live == h->n;
}

static uint32_t g_a[1000], g_b[1000], g_stream[5000];
static theap_t g_th;

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    /* 연습 1 */
    rng = 31337u;
    for (uint32_t i = 0; i < 1000; i++) g_a[i] = g_b[i] = xorshift32() % 5000u;
    heap_sort(g_a, 1000);
    isort_u32(g_b, 1000);
    CHECK(memcmp(g_a, g_b, sizeof g_a) == 0);
    uint32_t one[1] = { 7 };
    heap_sort(one, 1);
    heap_sort(one, 0);
    CHECK(one[0] == 7);

    /* 연습 2: 5000개 스트림에서 가장 큰 10개 */
    {
        uint32_t top[10], cnt = 0;
        for (uint32_t i = 0; i < 5000; i++) { g_stream[i] = xorshift32(); topk_feed(top, &cnt, 10, g_stream[i]); }
        isort_u32(g_stream, 5000);
        isort_u32(top, 10);
        CHECK(cnt == 10 && memcmp(top, g_stream + 4990, sizeof top) == 0);
    }

    /* 연습 3: tick 이 0xFFFFFF00 에서 곧 넘친다. 만료 = now + 0x80, + 0x100, + 0x180 */
    {
        theap_t *h = &g_th;
        const uint32_t now = 0xFFFFFF00u;
        tev_t e;
        uint16_t order_naive[3], order_wrap[3];
        for (int pass = 0; pass < 2; pass++) {
            th_init(h, pass == 1);
            th_push(h, now + 0x180u, 3);                           /* = 0x00000080 (넘친 뒤) */
            th_push(h, now + 0x80u, 1);                            /* = 0xFFFFFF80 */
            th_push(h, now + 0x100u, 2);                           /* = 0x00000000 (넘친 뒤) */
            for (int i = 0; i < 3; i++) { th_pop(h, &e); (pass ? order_wrap : order_naive)[i] = e.id; }
        }
        printf("[3] pop order  naive: %u %u %u | wrap-safe: %u %u %u\n",
               order_naive[0], order_naive[1], order_naive[2], order_wrap[0], order_wrap[1], order_wrap[2]);
        CHECK(order_naive[0] == 2 && order_naive[1] == 3 && order_naive[2] == 1);   /* 틀린 순서 */
        CHECK(order_wrap[0] == 1 && order_wrap[1] == 2 && order_wrap[2] == 3);      /* 올바른 순서 */
    }

    /* 연습 4: 랜덤 등록 / 취소 / 만료 — 힙 속성 + 위치 맵 일관성 */
    {
        theap_t *h = &g_th;
        static bool live[TCAP];
        th_init(h, true);
        memset(live, 0, sizeof live);
        rng = 2718u;
        bool ok = true;
        uint32_t cancels = 0;
        for (int op = 0; op < 20000 && ok; op++) {
            uint32_t r = xorshift32();
            uint16_t id = (uint16_t)((r >> 8) % TCAP);
            switch (r % 3u) {
            case 0: ok = th_push(h, (r >> 16) % 1000u, id) == !live[id]; live[id] = true; break;
            case 1: ok = th_cancel(h, id) == live[id]; cancels += live[id]; live[id] = false; break;
            default: {
                tev_t e, nx;
                bool had = h->n > 0;
                ok = th_pop(h, &e) == had;
                if (had) {
                    ok = ok && live[e.id];
                    live[e.id] = false;
                    if (h->n) { nx = h->a[0]; ok = ok && !before(h, nx, e); }   /* 꺼낸 것이 최소였나 */
                }
            }
            }
            ok = ok && th_check(h);
        }
        printf("[4] random push/cancel/pop: %s (cancels=%u)\n", ok ? "ok" : "BROKEN", cancels);
        CHECK(ok && cancels > 100);
        CHECK(!th_cancel(h, TCAP) && th_check(h));
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
