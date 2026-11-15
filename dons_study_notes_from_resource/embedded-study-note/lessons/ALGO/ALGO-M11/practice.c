/* ALGO-M11 추가 연습 — 한 번에 검증
 * 연습 1 lazy deletion 윈도우 중앙값 (LeetCode 480 정석) + 램프 입력에서 힙이 자라는 것 측정
 * 연습 2 "덮어쓰기(replace)" 윈도우 중앙값 — 삭제+삽입 대신 제자리 갱신 + top 교환 한 번, 이동 횟수 비교
 * 연습 3 스트림의 K 번째 큰 값 (LeetCode 703) — 크기 K min-heap
 * 연습 4 정렬된 센서 로그 K 개 병합 (LeetCode 23 의 배열판) — 크기 K 힙
 * 연습 5 실시간 p-분위수 (nearest-rank p90/p99) — 두 힙의 크기 비율을 바꾼다
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c && ./a.out
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }
static int cmp_i32(const void *a, const void *b) {
    int32_t x = *(const int32_t *)a, y = *(const int32_t *)b;
    return (x > y) - (x < y);
}
#define NMAX 40000
static int32_t xs[NMAX];
static int32_t ref_win_median(const int32_t *x, int end, int k) {
    static int32_t tmp[NMAX];
    memcpy(tmp, x + end - k + 1, (size_t)k * sizeof tmp[0]);
    qsort(tmp, (size_t)k, sizeof tmp[0], cmp_i32);
    return tmp[(k - 1) / 2];
}

/* ===================== 연습 1: lazy deletion ===================== */
/* 원소 = (값, 샘플 번호). 번호가 서로 달라 (값, 번호) 사전식 순서는 전순서 → "어느 힙에 있나"를 비교로 안다. */
typedef struct { int32_t v; int32_t i; } pr_t;
static bool pr_less(pr_t a, pr_t b) { return a.v != b.v ? a.v < b.v : a.i < b.i; }
typedef struct { pr_t a[NMAX]; int n; bool is_max; } pheap_t;
static bool ph_above(const pheap_t *h, pr_t x, pr_t y) { return h->is_max ? pr_less(y, x) : pr_less(x, y); }
static void ph_push(pheap_t *h, pr_t x) {
    int i = h->n++;
    while (i > 0 && ph_above(h, x, h->a[(i - 1) / 2])) { h->a[i] = h->a[(i - 1) / 2]; i = (i - 1) / 2; }
    h->a[i] = x;
}
static pr_t ph_pop(pheap_t *h) {
    pr_t top = h->a[0], x = h->a[--h->n];
    int i = 0;
    for (;;) {
        int c = 2 * i + 1;
        if (c >= h->n) break;
        if (c + 1 < h->n && ph_above(h, h->a[c + 1], h->a[c])) c++;
        if (!ph_above(h, h->a[c], x)) break;
        h->a[i] = h->a[c];
        i = c;
    }
    h->a[i] = x;
    return top;
}
typedef struct {
    pheap_t lo, hi;              /* 물리 원소 = 유효 + 지연된(stale) 원소 */
    int lo_valid, hi_valid;      /* 유효 원소 수 — 균형은 이 값으로 맞춘다 */
    int k, t, start, peak;       /* start = 창의 첫 샘플 번호. 번호 < start 면 stale */
    int32_t ring[NMAX];
} lazy_t;
static void lz_init(lazy_t *L, int k) {
    memset(L, 0, sizeof *L);
    L->lo.is_max = true; L->k = k;
}
static void lz_prune(lazy_t *L, pheap_t *h) { while (h->n > 0 && h->a[0].i < L->start) ph_pop(h); }
static bool lz_push(lazy_t *L, int32_t x, int32_t *med) {
    int t = L->t++;
    pr_t e = { x, t };
    lz_prune(L, &L->lo); lz_prune(L, &L->hi);
    bool to_lo = L->lo_valid > 0 ? !pr_less(L->lo.a[0], e) : !(L->hi_valid > 0 && pr_less(L->hi.a[0], e));
    if (to_lo) { ph_push(&L->lo, e); L->lo_valid++; } else { ph_push(&L->hi, e); L->hi_valid++; }
    if (t >= L->k) {                                   /* 나가는 샘플 (번호 t-k): 지우지 않고 장부만 고친다 */
        pr_t o = { L->ring[t % L->k], t - L->k };
        lz_prune(L, &L->lo);
        if (L->lo_valid > 0 && !pr_less(L->lo.a[0], o)) L->lo_valid--; else L->hi_valid--;
        L->start = t - L->k + 1;
    }
    L->ring[t % L->k] = x;
    if (L->lo_valid > L->hi_valid + 1) {
        lz_prune(L, &L->lo); ph_push(&L->hi, ph_pop(&L->lo)); L->lo_valid--; L->hi_valid++;
    } else if (L->hi_valid > L->lo_valid) {
        lz_prune(L, &L->hi); ph_push(&L->lo, ph_pop(&L->hi)); L->hi_valid--; L->lo_valid++;
    }
    if (L->lo.n + L->hi.n > L->peak) L->peak = L->lo.n + L->hi.n;
    if (t < L->k - 1) return false;
    lz_prune(L, &L->lo);
    *med = L->lo.a[0].v;
    return true;
}

/* ===================== 연습 2: 제자리 갱신 (본문 indexed heap + 이동 카운터) ===================== */
#define WMAX 1024
typedef struct { uint16_t a[WMAX]; int n; bool is_max; } iheap_t;
typedef struct {
    int32_t val[WMAX]; uint16_t pos[WMAX]; uint8_t side[WMAX];
    iheap_t lo, hi; int k, head, count; long moves;
} wmed_t;
static void wm_init(wmed_t *w, int k) {
    w->k = k; w->head = w->count = 0; w->moves = 0;
    w->lo.n = w->hi.n = 0; w->lo.is_max = true; w->hi.is_max = false;
}
static iheap_t *wm_heap(wmed_t *w, int side) { return side ? &w->hi : &w->lo; }
static bool ih_above(const wmed_t *w, const iheap_t *h, int s, int t) {
    return h->is_max ? w->val[s] > w->val[t] : w->val[s] < w->val[t];
}
static void ih_set(wmed_t *w, iheap_t *h, int i, int s) { h->a[i] = (uint16_t)s; w->pos[s] = (uint16_t)i; w->moves++; }
static void ih_up(wmed_t *w, iheap_t *h, int i) {
    int s = h->a[i];
    while (i > 0 && ih_above(w, h, s, h->a[(i - 1) / 2])) { ih_set(w, h, i, h->a[(i - 1) / 2]); i = (i - 1) / 2; }
    ih_set(w, h, i, s);
}
static void ih_down(wmed_t *w, iheap_t *h, int i) {
    int s = h->a[i], n = h->n;
    for (;;) {
        int c = 2 * i + 1;
        if (c >= n) break;
        if (c + 1 < n && ih_above(w, h, h->a[c + 1], h->a[c])) c++;
        if (!ih_above(w, h, h->a[c], s)) break;
        ih_set(w, h, i, h->a[c]);
        i = c;
    }
    ih_set(w, h, i, s);
}
static void ih_fix(wmed_t *w, iheap_t *h, int i) {       /* i 의 값이 바뀌었을 때: 위 또는 아래 */
    if (i > 0 && ih_above(w, h, h->a[i], h->a[(i - 1) / 2])) ih_up(w, h, i); else ih_down(w, h, i);
}
static void ih_push(wmed_t *w, int side, int s) {
    iheap_t *h = wm_heap(w, side);
    w->side[s] = (uint8_t)side; ih_set(w, h, h->n, s); h->n++; ih_up(w, h, h->n - 1);
}
static int ih_pop(wmed_t *w, int side) {
    iheap_t *h = wm_heap(w, side);
    int s = h->a[0];
    h->n--;
    if (h->n > 0) { ih_set(w, h, 0, h->a[h->n]); ih_down(w, h, 0); }
    return s;
}
static void ih_remove_at(wmed_t *w, int side, int i) {
    iheap_t *h = wm_heap(w, side);
    int last = --h->n;
    if (i == last) return;
    ih_set(w, h, i, h->a[last]);
    ih_fix(w, h, i);
}
static void wm_insert(wmed_t *w, int s, int32_t x) {     /* 본문 Part B 의 ②③ */
    w->val[s] = x;
    int side;
    if (w->lo.n > 0) side = (x <= w->val[w->lo.a[0]]) ? 0 : 1;
    else side = (w->hi.n > 0 && x > w->val[w->hi.a[0]]) ? 1 : 0;
    ih_push(w, side, s);
    w->count++;
    if (w->lo.n > w->hi.n + 1) ih_push(w, 1, ih_pop(w, 0));
    else if (w->hi.n > w->lo.n) ih_push(w, 0, ih_pop(w, 1));
}
static bool wm_finish(wmed_t *w, int32_t *med) {
    w->head = (w->head + 1 == w->k) ? 0 : w->head + 1;
    if (w->count < w->k) return false;
    *med = w->val[w->lo.a[0]];
    return true;
}
/* 본문 방식: 삭제 + 삽입 */
static bool wm_push_delins(wmed_t *w, int32_t x, int32_t *med) {
    int s = w->head;
    if (w->count == w->k) { ih_remove_at(w, w->side[s], w->pos[s]); w->count--; }
    wm_insert(w, s, x);
    return wm_finish(w, med);
}
/* 연습 2 답: 창이 찼으면 슬롯 값을 제자리에서 바꾸고(같은 힙, 같은 칸) 고친 뒤, 두 top 이 뒤집혔으면 한 번 교환 */
static bool wm_push_replace(wmed_t *w, int32_t x, int32_t *med) {
    int s = w->head;
    if (w->count < w->k) { wm_insert(w, s, x); return wm_finish(w, med); }
    w->val[s] = x;
    ih_fix(w, wm_heap(w, w->side[s]), w->pos[s]);
    if (w->hi.n > 0 && w->val[w->lo.a[0]] > w->val[w->hi.a[0]]) {
        int a = w->lo.a[0], b = w->hi.a[0];
        ih_set(w, &w->lo, 0, b); w->side[b] = 0;
        ih_set(w, &w->hi, 0, a); w->side[a] = 1;
        ih_down(w, &w->lo, 0);
        ih_down(w, &w->hi, 0);
    }
    return wm_finish(w, med);
}

/* ===================== 공통: 값 힙 (연습 3, 5) ===================== */
#define QMAX 4096
typedef struct { int32_t a[QMAX]; int n; bool is_max; } vheap_t;
static bool vh_above(const vheap_t *h, int32_t x, int32_t y) { return h->is_max ? x > y : x < y; }
static void vh_push(vheap_t *h, int32_t x) {
    int i = h->n++;
    while (i > 0 && vh_above(h, x, h->a[(i - 1) / 2])) { h->a[i] = h->a[(i - 1) / 2]; i = (i - 1) / 2; }
    h->a[i] = x;
}
static int32_t vh_pop(vheap_t *h) {
    int32_t top = h->a[0], x = h->a[--h->n];
    int i = 0;
    for (;;) {
        int c = 2 * i + 1;
        if (c >= h->n) break;
        if (c + 1 < h->n && vh_above(h, h->a[c + 1], h->a[c])) c++;
        if (!vh_above(h, h->a[c], x)) break;
        h->a[i] = h->a[c];
        i = c;
    }
    h->a[i] = x;
    return top;
}

/* ===================== 연습 3: K 번째 큰 값 (LeetCode 703) ===================== */
typedef struct { vheap_t h; int k; } kth_t;          /* min-heap 에 지금까지 큰 값 K 개 → 루트 = K 번째 큰 값 */
static int32_t kth_add(kth_t *q, int32_t x) {
    if (q->h.n < q->k) vh_push(&q->h, x);
    else if (x > q->h.a[0]) { q->h.a[0] = x; vh_push(&q->h, vh_pop(&q->h)); }   /* 교체 = pop 후 push 로 간단히 */
    return q->h.a[0];                                 /* n < k 면 "지금까지 가장 작은 값" (LeetCode 는 n >= k-1 보장) */
}

/* ===================== 연습 4: 정렬된 센서 로그 K 개 병합 ===================== */
typedef struct { uint32_t ts; int32_t sensor; int32_t pos; } rec_t;
static bool rec_less(const rec_t *a, const rec_t *b) {
    if (a->ts != b->ts) return a->ts < b->ts;
    if (a->sensor != b->sensor) return a->sensor < b->sensor;
    return a->pos < b->pos;
}
#define KS 16
#define PER 256
static uint32_t logs[KS][PER];
static int loglen[KS];
/* 각 로그의 "다음 원소" 하나씩만 min-heap 에 → 힙 크기 <= K, 전체 O(N log K) */
static int merge_logs(int K, rec_t *out) {
    rec_t h[KS];
    int n = 0, m = 0;
    for (int s = 0; s < K; s++) if (loglen[s] > 0) {
        rec_t e = { logs[s][0], s, 0 };
        int i = n++;
        while (i > 0 && rec_less(&e, &h[(i - 1) / 2])) { h[i] = h[(i - 1) / 2]; i = (i - 1) / 2; }
        h[i] = e;
    }
    while (n > 0) {
        rec_t top = h[0];
        out[m++] = top;
        rec_t e;
        if (top.pos + 1 < loglen[top.sensor]) {     /* 같은 로그의 다음 원소로 루트를 교체 */
            e = (rec_t){ logs[top.sensor][top.pos + 1], top.sensor, top.pos + 1 };
        } else {                                      /* 로그가 끝났으면 마지막 원소로 채우고 크기 감소 */
            e = h[--n];
            if (n == 0) break;
        }
        int i = 0;
        for (;;) {
            int c = 2 * i + 1;
            if (c >= n) break;
            if (c + 1 < n && rec_less(&h[c + 1], &h[c])) c++;
            if (!rec_less(&h[c], &e)) break;
            h[i] = h[c];
            i = c;
        }
        h[i] = e;
    }
    return m;
}
static int cmp_rec(const void *a, const void *b) {
    const rec_t *x = a, *y = b;
    return rec_less(x, y) ? -1 : rec_less(y, x) ? 1 : 0;
}

/* ===================== 연습 5: 실시간 p-분위수 (nearest-rank) ===================== */
/* p = num/den. lo(max-heap) 크기를 r = ceil(p·n) 으로 유지 → lo 의 top = r 번째 작은 값 = nearest-rank p-분위수 */
typedef struct { vheap_t lo, hi; int num, den; } pq_t;
static void pq_init(pq_t *q, int num, int den) {
    q->lo.n = q->hi.n = 0; q->lo.is_max = true; q->hi.is_max = false; q->num = num; q->den = den;
}
static int32_t pq_push(pq_t *q, int32_t x) {
    if (q->lo.n > 0 && x <= q->lo.a[0]) vh_push(&q->lo, x);
    else if (q->lo.n == 0 && !(q->hi.n > 0 && x > q->hi.a[0])) vh_push(&q->lo, x);
    else vh_push(&q->hi, x);
    int n = q->lo.n + q->hi.n;
    int r = (int)(((int64_t)n * q->num + q->den - 1) / q->den);   /* ceil(p·n), num >= 1 이면 r >= 1 */
    while (q->lo.n > r) vh_push(&q->hi, vh_pop(&q->lo));
    while (q->lo.n < r) vh_push(&q->lo, vh_pop(&q->hi));
    return q->lo.a[0];
}

static lazy_t g_lz;
static wmed_t g_wa, g_wb;
static rec_t merged[KS * PER], refrec[KS * PER];

int main(void) {
    uint32_t seed = 0x1234567u;

    /* ---- 연습 1 ---- */
    {
        static const int32_t e1[8] = { 1, 3, -1, -3, 5, 3, 6, 7 }, w1[6] = { 1, -1, -1, 3, 5, 6 };
        int32_t got[8], m;
        int g = 0;
        lz_init(&g_lz, 3);
        for (int i = 0; i < 8; i++) if (lz_push(&g_lz, e1[i], &m)) got[g++] = m;
        CHECK(g == 6 && memcmp(got, w1, sizeof w1) == 0);              /* LeetCode 480 예 1 */

        static const int ks[6] = { 1, 2, 3, 8, 31, 255 };
        int bad = 0;
        for (int t = 0; t < 60; t++) {
            int k = ks[t % 6], n = k + (int)(xs32(&seed) % 2000u), mode = (t / 6) % 3;
            for (int i = 0; i < n; i++)
                xs[i] = mode == 0 ? (int32_t)xs32(&seed) : mode == 1 ? (int32_t)(xs32(&seed) % 5u) : i / 3;
            lz_init(&g_lz, k);
            for (int i = 0; i < n; i++) {
                bool r = lz_push(&g_lz, xs[i], &m);
                if (r != (i >= k - 1) || (r && m != ref_win_median(xs, i, k))) bad++;
            }
        }
        printf("ex1 lazy deletion: 60 random streams vs qsort reference, mismatches=%d\n", bad);
        CHECK(bad == 0);

        /* 같은 k=31, n=20000 에서 물리 힙 크기(유효 + stale)의 최댓값 */
        const int n = 20000, k = 31;
        const char *names[4] = { "random", "ramp up", "ramp down", "constant" };
        int peaks[4];
        for (int c = 0; c < 4; c++) {
            for (int i = 0; i < n; i++)
                xs[i] = c == 0 ? (int32_t)(xs32(&seed) % 1000u) : c == 1 ? i : c == 2 ? -i : 42;
            lz_init(&g_lz, k);
            for (int i = 0; i < n; i++) lz_push(&g_lz, xs[i], &m);
            peaks[c] = g_lz.peak;
            printf("ex1 lazy peak heap entries (k=%d, n=%d) %-9s: %d\n", k, n, names[c], peaks[c]);
        }
        CHECK(peaks[1] > n / 2 && peaks[2] > n / 2);   /* 램프: stale 원소가 바닥에 깔려 n 에 비례해 자란다 */
    }

    /* ---- 연습 2 ---- */
    {
        static const int ks[5] = { 2, 5, 31, 255, 1024 };
        int bad = 0;
        long mv_del = 0, mv_rep = 0;
        for (int t = 0; t < 25; t++) {
            int k = ks[t % 5], n = k + 3000, mode = t / 5 % 3;
            for (int i = 0; i < n; i++)
                xs[i] = mode == 0 ? (int32_t)xs32(&seed) : mode == 1 ? (int32_t)(xs32(&seed) % 7u) : i + (int32_t)(xs32(&seed) % 9u);
            wm_init(&g_wa, k); wm_init(&g_wb, k);
            for (int i = 0; i < n; i++) {
                int32_t ma = 0, mb = 0;
                bool ra = wm_push_delins(&g_wa, xs[i], &ma), rb = wm_push_replace(&g_wb, xs[i], &mb);
                if (ra != rb || (ra && (ma != mb || ma != ref_win_median(xs, i, k)))) bad++;
            }
            if (k == 255 && mode == 0) { mv_del = g_wa.moves; mv_rep = g_wb.moves; }
        }
        printf("ex2 replace vs delete+insert: 25 streams, mismatches=%d\n", bad);
        printf("ex2 heap slot writes (k=255, random, n=3255): delete+insert=%ld replace=%ld\n", mv_del, mv_rep);
        CHECK(bad == 0);
        CHECK(mv_rep < mv_del);
    }

    /* ---- 연습 3 ---- */
    {
        static kth_t q;
        q.h.n = 0; q.h.is_max = false; q.k = 3;
        static const int32_t init[4] = { 4, 5, 8, 2 }, add[5] = { 3, 5, 10, 9, 4 }, want[5] = { 4, 5, 5, 8, 8 };
        for (int i = 0; i < 4; i++) kth_add(&q, init[i]);
        int ok = 1;
        for (int i = 0; i < 5; i++) ok &= kth_add(&q, add[i]) == want[i];
        CHECK(ok);                                                     /* LeetCode 703 예 */
        int bad = 0;
        static int32_t sorted[QMAX];
        for (int t = 0; t < 40; t++) {
            int k = 1 + (int)(xs32(&seed) % 100u), n = (int)(xs32(&seed) % 3000u), sn = 0;
            q.h.n = 0; q.k = k;
            for (int i = 0; i < n; i++) {
                int32_t x = (t & 1) ? (int32_t)xs32(&seed) : (int32_t)(xs32(&seed) % 20u);
                int32_t r = kth_add(&q, x);
                int j = sn++;                                          /* 내림차순 정렬 배열 참조 */
                while (j > 0 && sorted[j - 1] < x) { sorted[j] = sorted[j - 1]; j--; }
                sorted[j] = x;
                if (r != sorted[(sn < k ? sn : k) - 1]) bad++;
            }
        }
        printf("ex3 kth largest stream: 40 streams vs sorted reference, mismatches=%d\n", bad);
        CHECK(bad == 0);
    }

    /* ---- 연습 4 ---- */
    {
        int bad = 0;
        for (int t = 0; t < 200; t++) {
            int K = 1 + (int)(xs32(&seed) % KS), total = 0;
            for (int s = 0; s < K; s++) {
                loglen[s] = (int)(xs32(&seed) % (PER + 1u));          /* 빈 로그도 나온다 */
                uint32_t ts = xs32(&seed) % 50u;
                for (int i = 0; i < loglen[s]; i++) {
                    ts += xs32(&seed) % 4u;                            /* 증가(같은 시각 허용) */
                    logs[s][i] = ts;
                    refrec[total++] = (rec_t){ ts, s, i };
                }
            }
            int m = merge_logs(K, merged);
            qsort(refrec, (size_t)total, sizeof refrec[0], cmp_rec);
            if (m != total || memcmp(merged, refrec, (size_t)m * sizeof merged[0]) != 0) bad++;
        }
        printf("ex4 merge K sorted logs: 200 cases vs concat+qsort, mismatches=%d\n", bad);
        CHECK(bad == 0);
    }

    /* ---- 연습 5 ---- */
    {
        static pq_t q;
        static int32_t sorted[QMAX];
        static const int ps[5][2] = { { 1, 2 }, { 9, 10 }, { 99, 100 }, { 1, 100 }, { 1, 1 } };
        int bad = 0;
        for (int t = 0; t < 50; t++) {
            int num = ps[t % 5][0], den = ps[t % 5][1], n = 1 + (int)(xs32(&seed) % QMAX), sn = 0;
            pq_init(&q, num, den);
            for (int i = 0; i < n; i++) {
                int32_t x = (t & 1) ? (int32_t)xs32(&seed) : (int32_t)(xs32(&seed) % 10u);
                int32_t r = pq_push(&q, x);
                int j = sn++;
                while (j > 0 && sorted[j - 1] > x) { sorted[j] = sorted[j - 1]; j--; }
                sorted[j] = x;
                int rank = (int)(((int64_t)sn * num + den - 1) / den);
                if (r != sorted[rank - 1]) bad++;
            }
        }
        printf("ex5 running p-quantile (p=1/2,9/10,99/100,1/100,1): 50 streams, mismatches=%d\n", bad);
        CHECK(bad == 0);
        pq_init(&q, 9, 10);
        int32_t r = 0;
        for (int i = 1; i <= 10; i++) r = pq_push(&q, i * 10);          /* 10,20,...,100 → p90 = 9 번째 = 90 */
        CHECK(r == 90);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
