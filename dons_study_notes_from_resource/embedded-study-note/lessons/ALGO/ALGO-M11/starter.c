/* ALGO-M11 힙 활용: Top-K와 스트리밍 중앙값
 *
 * Part A: 두 힙(max-heap lo + min-heap hi)으로 스트림 전체의 실시간 중앙값 (부팅 시 자이로 바이어스 보정)
 * Part B: 슬라이딩 윈도우 중앙값 — 슬롯 핸들 기반 indexed heap 두 개로 "가장 오래된 샘플 삭제"
 * Part C: |x - 윈도우 중앙값| 기준 상위 K개 이상치(스파이크)를 크기 K 의 min-heap 으로 추적
 *
 * 테스트: 정렬 기반 참조(정렬 배열 삽입 / 윈도우 복사 + qsort)와 대조 — {0,1,2} 값 수열 전수,
 *         xorshift32 고정 시드 랜덤 스트림, LeetCode 295/480 예제, INT32_MIN/MAX 경계, 불변식 검사기.
 * malloc 없음: 모든 배열은 MAX 상수 크기의 정적 배열이거나 호출자가 준 작업 공간.
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c && ./a.out
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------- 공통: 값 힙 (DS-B12 에서 자세히) ---------------- */
#define RM_MAX  8192                 /* Part A 가 담을 수 있는 최대 샘플 수 */
#define RM_HEAP (RM_MAX / 2 + 1)     /* 힙 한 개의 칸 수 (재균형 직전 잠깐 +1 이 될 수 있다) */

typedef struct { int32_t a[RM_HEAP]; int n; bool is_max; } vheap_t;

/* x 가 y 보다 루트 쪽에 있어야 하나? max-heap 이면 x > y, min-heap 이면 x < y.
 * "-x 를 min-heap 에 넣기" 대신 방향 플래그 — INT32_MIN 을 부호 반전하면 UB (7. 실수 2) */
static bool vh_above(const vheap_t *h, int32_t x, int32_t y) { return h->is_max ? x > y : x < y; }

static void vh_push(vheap_t *h, int32_t x) {
    int i = h->n++;
    while (i > 0) {
        int p = (i - 1) / 2;
        if (!vh_above(h, x, h->a[p])) break;
        h->a[i] = h->a[p];
        i = p;
    }
    h->a[i] = x;
}
static int32_t vh_pop(vheap_t *h) {
    int32_t top = h->a[0], x = h->a[--h->n];
    int i = 0, n = h->n;
    for (;;) {
        int c = 2 * i + 1;
        if (c >= n) break;
        if (c + 1 < n && vh_above(h, h->a[c + 1], h->a[c])) c++;
        if (!vh_above(h, h->a[c], x)) break;
        h->a[i] = h->a[c];
        i = c;
    }
    h->a[i] = x;
    return top;
}

/* ---------------- Part A: 실시간 중앙값 (두 힙) ---------------- */
/* lo = 작은 절반(max-heap), hi = 큰 절반(min-heap).
 * 불변식 I1: max(lo) <= min(hi)    I2: lo.n - hi.n ∈ {0, 1}
 * → lo 의 top 이 아래쪽 중앙값(lower median, 0-based 순위 (n-1)/2). */
typedef struct { vheap_t lo, hi; } rmed_t;

static void rm_init(rmed_t *m) { m->lo.n = m->hi.n = 0; m->lo.is_max = true; m->hi.is_max = false; }
static int rm_count(const rmed_t *m) { return m->lo.n + m->hi.n; }

/* x 를 넣고 I1, I2 를 되살린다. 가득 차 있으면(RM_MAX) false. */
static bool rm_push(rmed_t *m, int32_t x) {
    (void)m; (void)x; (void)vh_push; (void)vh_pop;
    return false; /* TODO: I1 을 지키는 힙에 넣고, 크기 차가 {0,1} 을 벗어나면 top 하나를 옮긴다 */
}

/* 아래쪽 중앙값 (count >= 1 전제) */
static int32_t rm_median_lo(const rmed_t *m) {
    (void)m;
    return 0; /* TODO: 아래쪽 중앙값 */
}

/* 중앙값 × 2: 홀수 개면 2·mid, 짝수 개면 가운데 두 값의 합. int64 라 넘치지 않고 float 도 없다. */
static int64_t rm_median_x2(const rmed_t *m) {
    (void)m;
    return 0; /* TODO: 홀수면 2·mid, 짝수면 두 top 의 합 (int64) */
}

/* ---------------- Part B: 슬라이딩 윈도우 중앙값 (indexed heap, DS-M11 에서 자세히) ---------------- */
#define WMAX 1024
/* 힙 원소 = 슬롯 번호(0..k-1). 값은 w->val[slot]. pos/side 로 "슬롯이 어느 힙 몇 번 칸에 있나"를 O(1) 에 안다. */
typedef struct { uint16_t a[WMAX]; int n; bool is_max; } iheap_t;
typedef struct {
    int32_t  val[WMAX];      /* 링 버퍼: 슬롯 s 의 샘플 */
    uint16_t pos[WMAX];      /* 슬롯 s 의 힙 안 위치 */
    uint8_t  side[WMAX];     /* 0 = lo(max-heap), 1 = hi(min-heap) */
    iheap_t  lo, hi;
    int k, head, count;      /* head = 다음에 덮어쓸(= 가장 오래된) 슬롯 */
} wmed_t;

static bool wm_init(wmed_t *w, int k) {
    if (k < 1 || k > WMAX) return false;
    w->k = k; w->head = w->count = 0;
    w->lo.n = w->hi.n = 0; w->lo.is_max = true; w->hi.is_max = false;
    return true;
}
static iheap_t *wm_heap(wmed_t *w, int side) { return side ? &w->hi : &w->lo; }
static bool ih_above(const wmed_t *w, const iheap_t *h, int s, int t) {
    return h->is_max ? w->val[s] > w->val[t] : w->val[s] < w->val[t];
}
static void ih_set(wmed_t *w, iheap_t *h, int i, int s) { h->a[i] = (uint16_t)s; w->pos[s] = (uint16_t)i; }
static void ih_up(wmed_t *w, iheap_t *h, int i) {
    int s = h->a[i];
    while (i > 0) {
        int p = (i - 1) / 2;
        if (!ih_above(w, h, s, h->a[p])) break;
        ih_set(w, h, i, h->a[p]);
        i = p;
    }
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
static void ih_push(wmed_t *w, int side, int s) {
    iheap_t *h = wm_heap(w, side);
    w->side[s] = (uint8_t)side;
    ih_set(w, h, h->n, s);
    h->n++;
    ih_up(w, h, h->n - 1);
}
static int ih_pop(wmed_t *w, int side) {
    iheap_t *h = wm_heap(w, side);
    int s = h->a[0];
    h->n--;
    if (h->n > 0) { ih_set(w, h, 0, h->a[h->n]); ih_down(w, h, 0); }
    return s;
}

/* 힙 side 의 i 번 칸(임의 위치)을 지운다: 마지막 칸을 i 로 옮기고, 부모보다 위여야 하면 up, 아니면 down. */
static void ih_remove_at(wmed_t *w, int side, int i) {
    (void)w; (void)side; (void)i;
    /* TODO: 마지막 칸을 i 로 옮기고 up 또는 down (pos 갱신은 ih_set 이 한다) */
}

/* 샘플 x 를 넣는다. 창이 차 있으면 가장 오래된 샘플(head 슬롯)을 먼저 지운다.
 * 창이 가득 찼으면(count == k) *med = 아래쪽 중앙값, true. 아직 덜 찼으면 false. */
static bool wm_push(wmed_t *w, int32_t x, int32_t *med) {
    (void)w; (void)x; (void)med; (void)ih_push; (void)ih_pop; (void)ih_remove_at;
    return false; /* TODO: 가장 오래된 슬롯 삭제 → 삽입 → 재균형 → head 전진 → 창이 찼으면 중앙값 */
}

/* ---------------- Part C: 상위 K 개 이상치 ---------------- */
typedef struct { uint32_t score; int32_t idx; int32_t val; int32_t med; } outlier_t;
#define TOPK_MAX 64
typedef struct { outlier_t a[TOPK_MAX]; int n, K; } topk_t;

/* 전순서: 점수가 크면 더 "강함", 점수가 같으면 먼저 온(idx 작은) 쪽이 강함. */
static bool ol_better(const outlier_t *x, const outlier_t *y) {
    return x->score != y->score ? x->score > y->score : x->idx < y->idx;
}
static void tk_init(topk_t *t, int K) { t->n = 0; t->K = K < 0 ? 0 : (K > TOPK_MAX ? TOPK_MAX : K); }
/* min-heap: 루트 = 가장 약한 원소 (= 들어오는 후보가 이겨야 할 문턱) */
static void tk_up(topk_t *t, int i) {
    outlier_t x = t->a[i];
    while (i > 0) {
        int p = (i - 1) / 2;
        if (!ol_better(&t->a[p], &x)) break;     /* 부모가 x 보다 약하거나 같으면 멈춤 */
        t->a[i] = t->a[p];
        i = p;
    }
    t->a[i] = x;
}
static void tk_down(topk_t *t, int i) {
    outlier_t x = t->a[i];
    int n = t->n;
    for (;;) {
        int c = 2 * i + 1;
        if (c >= n) break;
        if (c + 1 < n && ol_better(&t->a[c], &t->a[c + 1])) c++;   /* 더 약한 자식 */
        if (!ol_better(&x, &t->a[c])) break;
        t->a[i] = t->a[c];
        i = c;
    }
    t->a[i] = x;
}

/* 후보 o 를 제시: K 개 미만이면 넣고, 꽉 찼으면 루트(가장 약한 것)보다 강할 때만 교체. */
static void topk_offer(topk_t *t, const outlier_t *o) {
    (void)t; (void)o; (void)tk_up; (void)tk_down;
    /* TODO: K 개 미만이면 push, 아니면 루트보다 강할 때만 루트 교체 + sift-down */
}

/* 힙을 비우며 out[0..n-1] 에 강한 순(점수 내림차순, 같으면 idx 오름차순)으로 담고 n 을 반환. */
static int topk_sorted(topk_t *t, outlier_t *out) {
    (void)t; (void)out;
    return 0; /* TODO: 루트(가장 약한 것)부터 꺼내 out 뒤쪽부터 채운다 */
}

/* 길이 win 의 윈도우 중앙값 m_t (창이 찬 뒤부터) 에 대해 점수 |x_t - m_t| 상위 K 개를 out 에 강한 순으로.
 * w = 호출자가 준 작업 공간. 반환: 담은 개수, win 이 범위 밖이면 -1. */
static int spike_scan(const int32_t *x, int n, int win, int K, wmed_t *w, outlier_t *out) {
    (void)x; (void)n; (void)win; (void)K; (void)w; (void)out; (void)tk_init;
    return 0; /* TODO: wm_init → 샘플마다 wm_push → 점수 |x - m| (int64) → topk_offer → topk_sorted */
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

/* 랜덤 샘플: mode 0 = int32 전 범위, 1 = 0..9 (동률 많음), 2 = 극값 7종, 3 = 잡음 섞인 램프 */
static int32_t gen(uint32_t *s, int mode, int i) {
    static const int32_t ext[7] = { INT32_MIN, INT32_MIN + 1, -1, 0, 1, INT32_MAX - 1, INT32_MAX };
    switch (mode) {
    case 0:  return (int32_t)xs32(s);
    case 1:  return (int32_t)(xs32(s) % 10u);
    case 2:  return ext[xs32(s) % 7u];
    default: return i * 3 + (int32_t)(xs32(s) % 7u) - 3;
    }
}

static int cmp_i32(const void *a, const void *b) {
    int32_t x = *(const int32_t *)a, y = *(const int32_t *)b;
    return (x > y) - (x < y);
}
/* 참조 1: 정렬 배열에 삽입 (샘플당 O(n)) */
static void ref_insert(int32_t *s, int *n, int32_t x) {
    int i = *n;
    while (i > 0 && s[i - 1] > x) { s[i] = s[i - 1]; i--; }
    s[i] = x;
    (*n)++;
}
/* 참조 2: 윈도우 x[end-k+1 .. end] 를 복사해 qsort, 아래쪽 중앙값 */
static int32_t ref_win_median(const int32_t *x, int end, int k) {
    static int32_t tmp[WMAX];
    memcpy(tmp, x + end - k + 1, (size_t)k * sizeof tmp[0]);
    qsort(tmp, (size_t)k, sizeof tmp[0], cmp_i32);
    return tmp[(k - 1) / 2];
}
/* 참조 3: 모든 점수를 계산해 정렬 후 앞 K 개 */
static int cmp_ol(const void *a, const void *b) {
    const outlier_t *x = a, *y = b;
    return ol_better(x, y) ? -1 : ol_better(y, x) ? 1 : 0;
}
static int ref_spikes(const int32_t *x, int n, int win, int K, outlier_t *out) {
    static outlier_t all[4096];
    int m = 0;
    for (int i = win - 1; i < n; i++) {
        int32_t md = ref_win_median(x, i, win);
        int64_t d = (int64_t)x[i] - md;
        all[m++] = (outlier_t){ (uint32_t)(d < 0 ? -d : d), i, x[i], md };
    }
    qsort(all, (size_t)m, sizeof all[0], cmp_ol);
    int r = m < K ? m : K;
    memcpy(out, all, (size_t)r * sizeof all[0]);
    return r;
}

/* 불변식 검사기 */
static bool vheap_ok(const vheap_t *h) {
    for (int i = 1; i < h->n; i++) if (vh_above(h, h->a[i], h->a[(i - 1) / 2])) return false;
    return true;
}
static bool rm_ok(const rmed_t *m) {
    if (!vheap_ok(&m->lo) || !vheap_ok(&m->hi)) return false;
    if (m->lo.n - m->hi.n < 0 || m->lo.n - m->hi.n > 1) return false;
    return m->hi.n == 0 || m->lo.a[0] <= m->hi.a[0];
}
static bool wm_ok(const wmed_t *w) {
    const iheap_t *hs[2] = { &w->lo, &w->hi };
    for (int sd = 0; sd < 2; sd++) {
        const iheap_t *h = hs[sd];
        for (int i = 0; i < h->n; i++) {
            int s = h->a[i];
            if (w->pos[s] != i || w->side[s] != sd) return false;
            if (i > 0 && ih_above(w, h, s, h->a[(i - 1) / 2])) return false;
        }
    }
    if (w->lo.n + w->hi.n != w->count) return false;
    if (w->lo.n - w->hi.n < 0 || w->lo.n - w->hi.n > 1) return false;
    return w->hi.n == 0 || w->val[w->lo.a[0]] <= w->val[w->hi.a[0]];
}

static int32_t xs_buf[4096];
static int32_t sorted_buf[RM_MAX];
static wmed_t g_w;
static rmed_t g_m;

/* {0,1,2} 값의 길이 len 수열 번호 code → xs_buf */
static void decode3(int code, int len) { for (int i = 0; i < len; i++) { xs_buf[i] = code % 3; code /= 3; } }

int main(void) {
    uint32_t seed = 0x2545F491u;

    /* ===== Part A: 실시간 중앙값 ===== */
    rm_init(&g_m);
    rm_push(&g_m, 1); rm_push(&g_m, 2);
    CHECK(rm_median_x2(&g_m) == 3);                                /* LeetCode 295: [1,2] → 1.5 */
    rm_push(&g_m, 3);
    CHECK(rm_median_lo(&g_m) == 2 && rm_median_x2(&g_m) == 4);     /* [1,2,3] → 2 */

    rm_init(&g_m);
    rm_push(&g_m, INT32_MIN); rm_push(&g_m, INT32_MAX);
    CHECK(rm_median_lo(&g_m) == INT32_MIN && rm_median_x2(&g_m) == -1);   /* 합은 int64 로 -1 */
    rm_push(&g_m, INT32_MAX);
    CHECK(rm_median_lo(&g_m) == INT32_MAX && rm_median_x2(&g_m) == 2 * (int64_t)INT32_MAX);

    rm_init(&g_m);
    int cap_ok = 1;
    for (int i = 0; i < RM_MAX; i++) cap_ok &= rm_push(&g_m, 7);
    CHECK(cap_ok && rm_count(&g_m) == RM_MAX && rm_median_lo(&g_m) == 7);
    CHECK(!rm_push(&g_m, 7) && rm_count(&g_m) == RM_MAX);          /* 가득 차면 거절 */

    /* 전수: {0,1,2} 값의 길이 1..8 모든 수열, 모든 접두사에서 참조와 비교 */
    int badA = 0, seqA = 0;
    for (int len = 1, p3 = 3; len <= 8; len++, p3 *= 3) {
        for (int code = 0; code < p3; code++, seqA++) {
            decode3(code, len);
            rm_init(&g_m);
            int sn = 0;
            for (int i = 0; i < len; i++) {
                rm_push(&g_m, xs_buf[i]);
                ref_insert(sorted_buf, &sn, xs_buf[i]);
                int64_t want2 = (int64_t)sorted_buf[(sn - 1) / 2] + sorted_buf[sn / 2];
                if (rm_count(&g_m) != sn || rm_median_lo(&g_m) != sorted_buf[(sn - 1) / 2] ||
                    rm_median_x2(&g_m) != want2 || !rm_ok(&g_m)) badA++;
            }
        }
    }
    printf("Part A exhaustive: %d sequences over {0,1,2}, len 1..8, mismatches=%d\n", seqA, badA);
    CHECK(badA == 0);

    /* 랜덤 스트림: 4 모드 × 30개, 길이 1..3000 */
    int badAr = 0, pushesA = 0;
    for (int t = 0; t < 120; t++) {
        int mode = t % 4, n = 1 + (int)(xs32(&seed) % 3000u);
        rm_init(&g_m);
        int sn = 0;
        for (int i = 0; i < n; i++, pushesA++) {
            int32_t x = gen(&seed, mode, i);
            rm_push(&g_m, x);
            ref_insert(sorted_buf, &sn, x);
            int64_t want2 = (int64_t)sorted_buf[(sn - 1) / 2] + sorted_buf[sn / 2];
            if (rm_median_lo(&g_m) != sorted_buf[(sn - 1) / 2] || rm_median_x2(&g_m) != want2) badAr++;
            if ((i & 63) == 0 && !rm_ok(&g_m)) badAr++;
        }
    }
    printf("Part A random: 120 streams, %d pushes, mismatches=%d\n", pushesA, badAr);
    CHECK(badAr == 0);

    /* ===== Part B: 슬라이딩 윈도우 중앙값 ===== */
    {
        static const int32_t e1[8] = { 1, 3, -1, -3, 5, 3, 6, 7 };            /* LeetCode 480 예 1, k=3 */
        static const int32_t w1[6] = { 1, -1, -1, 3, 5, 6 };
        static const int32_t e2[9] = { 1, 2, 3, 4, 2, 3, 1, 4, 2 };           /* LeetCode 480 예 2, k=3 */
        static const int32_t w2[7] = { 2, 3, 3, 3, 2, 3, 2 };
        int32_t got[16], m;
        int g = 0;
        wm_init(&g_w, 3);
        for (int i = 0; i < 8; i++) if (wm_push(&g_w, e1[i], &m)) got[g++] = m;
        CHECK(g == 6 && memcmp(got, w1, sizeof w1) == 0);
        g = 0;
        wm_init(&g_w, 3);
        for (int i = 0; i < 9; i++) if (wm_push(&g_w, e2[i], &m)) got[g++] = m;
        CHECK(g == 7 && memcmp(got, w2, sizeof w2) == 0);
        /* k = 4 (짝수 → 아래쪽 중앙값): [1,3,-1,-3]→-1, [3,-1,-3,5]→-1, [-1,-3,5,3]→-1, [-3,5,3,6]→3, [5,3,6,7]→5 */
        static const int32_t w4[5] = { -1, -1, -1, 3, 5 };
        g = 0;
        wm_init(&g_w, 4);
        for (int i = 0; i < 8; i++) if (wm_push(&g_w, e1[i], &m)) got[g++] = m;
        CHECK(g == 5 && memcmp(got, w4, sizeof w4) == 0);
        /* k = 1: 중앙값 = 자기 자신. k = 2: 둘 중 작은 값 */
        int ok1 = 1;
        wm_init(&g_w, 1);
        for (int i = 0; i < 8; i++) ok1 &= wm_push(&g_w, e1[i], &m) && m == e1[i];
        CHECK(ok1);
        int ok2 = 1;
        wm_init(&g_w, 2);
        for (int i = 0; i < 8; i++) {
            bool r = wm_push(&g_w, e1[i], &m);
            if (i == 0) ok2 &= !r;
            else ok2 &= r && m == (e1[i] < e1[i - 1] ? e1[i] : e1[i - 1]) && wm_ok(&g_w);
        }
        CHECK(ok2);
        CHECK(!wm_init(&g_w, 0) && !wm_init(&g_w, WMAX + 1) && wm_init(&g_w, WMAX));
    }

    /* 전수: {0,1,2} 값의 길이 1..7 모든 수열 × 모든 k(1..len) */
    int badB = 0, runsB = 0;
    for (int len = 1, p3 = 3; len <= 7; len++, p3 *= 3) {
        for (int code = 0; code < p3; code++) {
            decode3(code, len);
            for (int k = 1; k <= len; k++, runsB++) {
                wm_init(&g_w, k);
                for (int i = 0; i < len; i++) {
                    int32_t m = 12345;
                    bool r = wm_push(&g_w, xs_buf[i], &m);
                    if (r != (i >= k - 1) || (r && m != ref_win_median(xs_buf, i, k)) || !wm_ok(&g_w)) badB++;
                }
            }
        }
    }
    printf("Part B exhaustive: %d (sequence, k) runs over {0,1,2}, len 1..7, mismatches=%d\n", runsB, badB);
    CHECK(badB == 0);

    /* 랜덤: k ∈ {1,2,3,4,5,8,31,64,255,1024}, 4 모드 */
    static const int ks[10] = { 1, 2, 3, 4, 5, 8, 31, 64, 255, 1024 };
    int badBr = 0;
    long samplesB = 0;
    for (int t = 0; t < 160; t++) {
        int k = ks[t % 10], mode = (t / 10) % 4;
        int n = k + (int)(xs32(&seed) % (k >= 255 ? 1200u : 3000u));
        for (int i = 0; i < n; i++) xs_buf[i] = gen(&seed, mode, i);
        wm_init(&g_w, k);
        for (int i = 0; i < n; i++, samplesB++) {
            int32_t m = 0;
            bool r = wm_push(&g_w, xs_buf[i], &m);
            if (r != (i >= k - 1) || (r && m != ref_win_median(xs_buf, i, k))) badBr++;
            if ((i & 127) == 0 && !wm_ok(&g_w)) badBr++;
        }
    }
    printf("Part B random: 160 streams, %ld samples, mismatches=%d\n", samplesB, badBr);
    CHECK(badBr == 0);
    printf("sizeof(wmed_t) = %zu bytes for WMAX = %d\n", sizeof(wmed_t), WMAX);

    /* ===== Part C: 상위 K 개 이상치 ===== */
    {
        static outlier_t got[TOPK_MAX], want[TOPK_MAX], cand[1009];
        /* 0) top-K 힙 단독: 점수 0..20 (동률 많음), idx 는 뒤섞인 서로 다른 값 → 정렬 후 앞 K 개와 같아야 */
        int badK = 0;
        for (int t = 0; t < 300; t++) {
            int nc = (int)(xs32(&seed) % 1009u), K = 1 + (int)(xs32(&seed) % TOPK_MAX);
            topk_t tk;
            tk_init(&tk, K);
            for (int i = 0; i < nc; i++) {
                cand[i] = (outlier_t){ xs32(&seed) % 21u, (i * 37) % 1009, i, 0 };
                topk_offer(&tk, &cand[i]);
            }
            int a = topk_sorted(&tk, got);
            qsort(cand, (size_t)nc, sizeof cand[0], cmp_ol);
            int b = nc < K ? nc : K;
            if (a != b || memcmp(got, cand, (size_t)b * sizeof got[0]) != 0 || tk.n != 0) badK++;
        }
        printf("Part C top-K heap alone: 300 trials (scores 0..20, shuffled idx), mismatches=%d\n", badK);
        CHECK(badK == 0);

        /* 1) 부드러운 신호(삼각파 + 잡음 ±20)에 스파이크 8개를 심고 K=8, win=31 로 찾기 */
        static const int spike_at[8] = { 100, 517, 1033, 1500, 2222, 2900, 3333, 3998 };
        static const int32_t spike_amp[8] = { 4000, -3500, 5000, 3000, -4500, 3800, -3200, 4200 };
        int n = 4000;
        for (int i = 0; i < n; i++) {
            int ph = i % 400;
            xs_buf[i] = (ph < 200 ? ph : 400 - ph) * 5 + (int32_t)(xs32(&seed) % 41u) - 20;
        }
        for (int j = 0; j < 8; j++) xs_buf[spike_at[j]] += spike_amp[j];
        int r = spike_scan(xs_buf, n, 31, 8, &g_w, got);
        int found = 0;
        for (int a = 0; a < r; a++)
            for (int j = 0; j < 8; j++) found += got[a].idx == spike_at[j];
        printf("spikes found (K=8, win=31):");
        for (int a = 0; a < r; a++) printf(" %d(%u)", got[a].idx, got[a].score);
        printf("\n");
        CHECK(r == 8 && found == 8);
        CHECK(got[0].idx == 1033 && got[0].score >= 4800);              /* 가장 큰 스파이크(+5000)가 1등 */
        int rr = ref_spikes(xs_buf, n, 31, 8, want);
        CHECK(rr == r && memcmp(got, want, (size_t)r * sizeof got[0]) == 0);

        /* 2) 경계: 창보다 짧은 입력, K > 후보 수, K = 0, win 범위 밖 */
        CHECK(spike_scan(xs_buf, 30, 31, 8, &g_w, got) == 0);
        CHECK(spike_scan(xs_buf, 35, 31, 8, &g_w, got) == 5);          /* 후보 35-31+1 = 5 개 */
        CHECK(spike_scan(xs_buf, 100, 31, 0, &g_w, got) == 0);
        CHECK(spike_scan(xs_buf, 100, 0, 8, &g_w, got) == -1);

        /* 3) 넘침: 점수 = INT32_MAX - INT32_MIN = 2^32 - 1 */
        int32_t ex[4] = { INT32_MIN, INT32_MIN, INT32_MIN, INT32_MAX };
        r = spike_scan(ex, 4, 3, 1, &g_w, got);
        CHECK(r == 1 && got[0].idx == 3 && got[0].med == INT32_MIN && got[0].score == UINT32_MAX);

        /* 4) 랜덤 대조: 모드 4종, win 1..64, K 1..TOPK_MAX, 점수 동률 많은 경우 포함 */
        int badC = 0;
        for (int t = 0; t < 200; t++) {
            int mode = t % 4, win = 1 + (int)(xs32(&seed) % 64u), K = 1 + (int)(xs32(&seed) % TOPK_MAX);
            int nn = (int)(xs32(&seed) % 1500u);
            for (int i = 0; i < nn; i++) xs_buf[i] = gen(&seed, mode, i);
            int a = spike_scan(xs_buf, nn, win, K, &g_w, got);
            int b = ref_spikes(xs_buf, nn, win, K, want);
            if (a != b || memcmp(got, want, (size_t)(a > 0 ? a : 0) * sizeof got[0]) != 0) badC++;
        }
        printf("Part C random: 200 streams vs sort-all reference, mismatches=%d\n", badC);
        CHECK(badC == 0);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
