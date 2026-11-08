/* DS-B12 이진 힙과 우선순위 큐
 * (만료 시각, 콜백 id) 의 min-heap — 소프트웨어 타이머 큐. malloc 없음, 고정 배열.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define HEAP_CAP 1024u

typedef struct {
    uint32_t expire;                     /* 만료 tick */
    uint16_t cb_id;                      /* 실행할 콜백 번호 */
} tev_t;

typedef struct {
    tev_t    a[HEAP_CAP];                /* a[0] = 최소. 자식 2i+1, 2i+2 / 부모 (i-1)/2 */
    uint32_t n;
} heap_t;

/* 순서: expire 먼저, 같으면 cb_id — 전순서(total order)라 pop 순서가 유일하게 정해진다 */
static bool ev_before(tev_t x, tev_t y) {
    return x.expire != y.expire ? x.expire < y.expire : x.cb_id < y.cb_id;
}
static uint32_t g_cmp;                   /* 힙 연산의 비교 횟수 (Part C 측정) */
static bool ev_less(tev_t x, tev_t y) { g_cmp++; return ev_before(x, y); }
static void ev_swap(tev_t *x, tev_t *y) { tev_t t = *x; *x = *y; *y = t; }

#define PARENT(i) (((i) - 1u) / 2u)
#define LEFT(i)   (2u * (i) + 1u)

static void heap_init(heap_t *h) { h->n = 0; }

/* ---------------- Part A: push / peek (sift-up) ---------------- */
/* i 의 원소가 부모보다 작으면 자리를 바꾸며 위로 올린다 */
static void sift_up(heap_t *h, uint32_t i) {
    while (i > 0 && ev_less(h->a[i], h->a[PARENT(i)])) {
        ev_swap(&h->a[i], &h->a[PARENT(i)]);
        i = PARENT(i);
    }
}

static bool heap_push(heap_t *h, tev_t e) {
    if (h->n == HEAP_CAP) return false;  /* 가득 참 — 타이머 등록 실패를 호출자에게 알린다 */
    h->a[h->n] = e;                      /* 맨 끝(마지막 잎)에 넣고 */
    sift_up(h, h->n);                    /* 위로 올린다 */
    h->n++;
    return true;
}

static bool heap_peek(const heap_t *h, tev_t *out) {
    if (h->n == 0) return false;
    *out = h->a[0];
    return true;
}

/* ---------------- Part B: pop (sift-down) ---------------- */
/* i 의 원소가 더 작은 자식보다 크면 그 자식과 바꾸며 아래로 내린다 */
static void sift_down(heap_t *h, uint32_t i) {
    for (;;) {
        uint32_t l = LEFT(i), r = l + 1u, m = i;
        if (l < h->n && ev_less(h->a[l], h->a[m])) m = l;
        if (r < h->n && ev_less(h->a[r], h->a[m])) m = r;
        if (m == i) return;
        ev_swap(&h->a[i], &h->a[m]);
        i = m;
    }
}

static bool heap_pop(heap_t *h, tev_t *out) {
    if (h->n == 0) return false;
    *out = h->a[0];
    h->n--;
    h->a[0] = h->a[h->n];                /* 마지막 잎을 루트로 올리고 */
    sift_down(h, 0);                     /* 아래로 내린다 */
    return true;
}

/* ---------------- Part C: heapify (Floyd) + 타이머 서비스 ---------------- */
/* 임의 배열을 O(n) 에 힙으로: 마지막 내부 노드 (n/2 - 1) 부터 0 까지 sift_down */
static bool heap_heapify(heap_t *h, const tev_t *src, uint32_t n) {
    if (n > HEAP_CAP) return false;
    memcpy(h->a, src, n * sizeof *src);
    h->n = n;
    for (uint32_t i = n / 2u; i-- > 0;) sift_down(h, i);
    return true;
}

/* tick ISR 이 깨운 타이머 태스크: now 까지 만료된 타이머를 순서대로 꺼내
 * fired[] 에 cb_id 를 기록(= 콜백 호출)하고 개수를 돌려준다. 최대 max 개 */
static uint32_t timer_service(heap_t *h, uint32_t now, uint16_t *fired, uint32_t max) {
    uint32_t k = 0;
    tev_t e;
    while (k < max && heap_peek(h, &e) && e.expire <= now) {
        heap_pop(h, &e);
        fired[k++] = e.cb_id;
    }
    return k;
}

/* ---------------- 검사 도구 (주어짐) ---------------- */
/* 힙 속성: 모든 i>0 에 대해 부모 <= 자식 (비교 횟수에 안 잡히는 ev_before 사용) */
static bool heap_check(const heap_t *h) {
    if (h->n > HEAP_CAP) return false;
    for (uint32_t i = 1; i < h->n; i++)
        if (ev_before(h->a[i], h->a[PARENT(i)])) return false;
    return true;
}

/* 참조용 삽입 정렬 */
static void ref_sort(tev_t *v, uint32_t n) {
    for (uint32_t i = 1; i < n; i++) {
        tev_t x = v[i];
        uint32_t j = i;
        while (j > 0 && ev_before(x, v[j - 1])) { v[j] = v[j - 1]; j--; }
        v[j] = x;
    }
}

static uint32_t rng_state = 1u;
static uint32_t xorshift32(void) {
    uint32_t x = rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return rng_state = x;
}

static tev_t mk(uint32_t expire, uint16_t id) { return (tev_t){ expire, id }; }
static bool ev_eq(tev_t x, tev_t y) { return x.expire == y.expire && x.cb_id == y.cb_id; }

static heap_t g_h;
static tev_t g_src[HEAP_CAP], g_ref[HEAP_CAP];

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    heap_t *h = &g_h;
    tev_t e = { 0, 0 };

    /* Part A: push 할 때마다 힙 속성 + peek == 지금까지의 최소 */
    {
        static const tev_t in[] = { {30, 1}, {10, 2}, {20, 3}, {5, 4}, {10, 1}, {40, 5}, {5, 0} };
        heap_init(h);
        CHECK(!heap_peek(h, &e));
        bool ok = true;
        tev_t min = in[0];
        for (unsigned i = 0; i < sizeof in / sizeof in[0]; i++) {
            if (ev_before(in[i], min)) min = in[i];
            ok = ok && heap_push(h, in[i]) && heap_check(h) && heap_peek(h, &e) && ev_eq(e, min);
        }
        CHECK(ok);
        CHECK(h->n == 7 && ev_eq(h->a[0], mk(5, 0)));
        heap_init(h);
        for (uint32_t i = 0; i < HEAP_CAP; i++) heap_push(h, mk(HEAP_CAP - i, (uint16_t)i));
        CHECK(h->n == HEAP_CAP && heap_check(h) && ev_eq(h->a[0], mk(1, HEAP_CAP - 1)));
        CHECK(!heap_push(h, mk(0, 0)) && h->n == HEAP_CAP);   /* 가득 차면 거절 */
    }

    /* Part B: pop 순서 == 정렬 순서, 빈 힙 pop 거절 */
    {
        static const tev_t in[] = { {30, 1}, {10, 2}, {20, 3}, {5, 4}, {10, 1}, {40, 5}, {5, 0} };
        static const tev_t want[] = { {5, 0}, {5, 4}, {10, 1}, {10, 2}, {20, 3}, {30, 1}, {40, 5} };
        heap_init(h);
        for (unsigned i = 0; i < 7; i++) heap_push(h, in[i]);
        bool ok = true;
        for (unsigned i = 0; i < 7; i++) ok = ok && heap_pop(h, &e) && ev_eq(e, want[i]) && heap_check(h);
        CHECK(ok);
        CHECK(h->n == 0 && !heap_pop(h, &e));
    }
    /* Part B: 랜덤 push/pop 20000회 — 정렬 배열 참조 모델과 매번 대조 */
    {
        heap_init(h);
        uint32_t ref_n = 0;
        bool ok = true;
        rng_state = 0xC0FFEEu;
        for (int op = 0; op < 20000 && ok; op++) {
            uint32_t r = xorshift32();
            if ((r & 3u) != 0) {                                  /* 3/4 확률 push */
                tev_t x = mk((r >> 8) % 500u, (uint16_t)(r >> 20));
                bool pushed = heap_push(h, x);
                ok = pushed == (ref_n < HEAP_CAP);
                if (pushed) {                                      /* 정렬 위치에 삽입 */
                    uint32_t j = ref_n++;
                    while (j > 0 && ev_before(x, g_ref[j - 1])) { g_ref[j] = g_ref[j - 1]; j--; }
                    g_ref[j] = x;
                }
            } else {
                bool popped = heap_pop(h, &e);
                ok = popped == (ref_n > 0);
                if (popped) {
                    ok = ok && ev_eq(e, g_ref[0]);
                    memmove(g_ref, g_ref + 1, (ref_n - 1u) * sizeof g_ref[0]);
                    ref_n--;
                }
            }
            ok = ok && h->n == ref_n && heap_check(h);
        }
        uint32_t left = h->n;
        for (uint32_t i = 0; ok && i < left; i++) ok = heap_pop(h, &e) && ev_eq(e, g_ref[i]);
        CHECK(ok);
        printf("[B] random 20000 push/pop vs sorted reference: %s (drained %u)\n", ok ? "match" : "MISMATCH", left);
    }

    /* Part C: heapify → 힙 속성 + pop 순서 == 정렬 결과 */
    {
        uint32_t n = 1000;
        rng_state = 99u;
        for (uint32_t i = 0; i < n; i++) g_src[i] = mk(xorshift32() % 10000u, (uint16_t)i);
        memcpy(g_ref, g_src, n * sizeof g_src[0]);
        ref_sort(g_ref, n);
        CHECK(heap_heapify(h, g_src, n) && h->n == n && heap_check(h));
        bool ok = true;
        for (uint32_t i = 0; i < n && ok; i++) ok = heap_pop(h, &e) && ev_eq(e, g_ref[i]) && heap_check(h);
        CHECK(ok);
        CHECK(heap_heapify(h, g_src, 0) && h->n == 0 && !heap_peek(h, &e));
        CHECK(heap_heapify(h, g_src, 1) && heap_peek(h, &e) && ev_eq(e, g_src[0]));
        CHECK(!heap_heapify(h, g_src, HEAP_CAP + 1u));
    }

    /* Part C: 비교 횟수 — n번 push vs heapify (n = 1023) */
    {
        const uint32_t n = 1023;
        static const char *name[] = { "random", "ascending", "descending" };
        bool ok = true;
        printf("[C] input       | n pushes cmp | heapify cmp | (n = %u, n*log2(n) ~ %u)\n", n, n * 10u);
        for (int kind = 0; kind < 3; kind++) {
            rng_state = 4242u;
            for (uint32_t i = 0; i < n; i++) {
                uint32_t ex = kind == 0 ? xorshift32() % 100000u : kind == 1 ? i : n - i;
                g_src[i] = mk(ex, (uint16_t)i);
            }
            heap_init(h);
            g_cmp = 0;
            for (uint32_t i = 0; i < n; i++) heap_push(h, g_src[i]);
            uint32_t c_push = g_cmp;
            ok = ok && heap_check(h);
            g_cmp = 0;
            heap_heapify(h, g_src, n);
            uint32_t c_heapify = g_cmp;
            ok = ok && heap_check(h) && h->n == n && c_heapify <= 2u * n;
            printf("[C] %-11s | %12u | %11u |\n", name[kind], c_push, c_heapify);
        }
        CHECK(ok);
    }

    /* Part C: 타이머 서비스 — now 까지 만료된 것만, 만료 순서대로 */
    {
        uint16_t fired[8];
        heap_init(h);
        heap_push(h, mk(100, 1)); heap_push(h, mk(50, 7)); heap_push(h, mk(75, 3));
        heap_push(h, mk(50, 2));  heap_push(h, mk(200, 4));
        CHECK(timer_service(h, 40, fired, 8) == 0 && h->n == 5);
        CHECK(timer_service(h, 60, fired, 8) == 2 && fired[0] == 2 && fired[1] == 7);
        CHECK(timer_service(h, 100, fired, 8) == 2 && fired[0] == 3 && fired[1] == 1);
        CHECK(h->n == 1 && heap_peek(h, &e) && e.expire == 200 && heap_check(h));
        heap_push(h, mk(150, 9));
        CHECK(timer_service(h, 1000, fired, 1) == 1 && fired[0] == 9 && h->n == 1);  /* max 로 한 번에 처리량 제한 */
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
