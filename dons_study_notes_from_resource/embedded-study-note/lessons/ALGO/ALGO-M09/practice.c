/* ALGO-M09 추가 연습 — 한 번에 검증
 *   연습 1  최소 제거 수 (LeetCode 435, 반개구간) 와 최소 화살 수 (LeetCode 452, 닫힌구간 — 맞닿으면 같이 터짐)
 *   연습 2  정렬·병합된 목록에 구간 하나 삽입 (LeetCode 57) — O(n), 정렬 없이
 *   연습 3  두 구간 목록의 교집합 (LeetCode 986 의 반개구간 판) — 두 포인터
 *   연습 4  채널이 k 개뿐일 때 최대 몇 개 전송을 받을 수 있나 — 끝 순서 + best-fit (가장 늦게 빈 채널)
 *           와 earliest-free(힙 top) 의 차이
 *   연습 5  가중치 구간 스케줄링 — 그리디가 틀리는 반례와 DP (끝 순서 + 이진 탐색, ALGO-B02 의 upper_bound)
 * 모든 함수를 브루트포스(부분집합 전수 / 격자 비트맵)와 대조. 알고리즘 쪽 malloc·재귀 없음.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct { uint32_t s, e; } ival_t;   /* 연습 1 의 화살 문제만 닫힌구간 [s, e] (s <= e) 로 읽는다 */
#define MAX_IV 256
#define MAX_CH 16

/* ---- 정렬 도구 (solution.c 와 같은 인덱스 힙 정렬) ---- */
enum { BY_END = 0, BY_START = 1 };
static int idx_less(const ival_t *iv, uint16_t a, uint16_t b, int key) {
    uint32_t a1 = key == BY_END ? iv[a].e : iv[a].s, b1 = key == BY_END ? iv[b].e : iv[b].s;
    uint32_t a2 = key == BY_END ? iv[a].s : iv[a].e, b2 = key == BY_END ? iv[b].s : iv[b].e;
    if (a1 != b1) return a1 < b1;
    if (a2 != b2) return a2 < b2;
    return a < b;
}
static void idx_sift(const ival_t *iv, uint16_t *h, size_t i, size_t n, int key) {
    for (;;) {
        size_t c = 2 * i + 1;
        if (c >= n) return;
        if (c + 1 < n && idx_less(iv, h[c], h[c + 1], key)) c++;
        if (!idx_less(iv, h[i], h[c], key)) return;
        uint16_t t = h[i]; h[i] = h[c]; h[c] = t; i = c;
    }
}
static void sort_idx(const ival_t *iv, size_t n, uint16_t *idx, int key) {
    for (size_t i = 0; i < n; i++) idx[i] = (uint16_t)i;
    for (size_t i = n / 2; i-- > 0;) idx_sift(iv, idx, i, n, key);
    for (size_t m = n; m > 1; m--) {
        uint16_t t = idx[0]; idx[0] = idx[m - 1]; idx[m - 1] = t;
        idx_sift(iv, idx, 0, m - 1, key);
    }
}

/* ======================= 연습 1 ======================= */
/* 반개구간: 겹침을 없애려고 지워야 하는 최소 개수 = n - (끝 순서 그리디로 고른 개수) */
size_t min_removals(const ival_t *iv, size_t n, uint16_t *work) {
    sort_idx(iv, n, work, BY_END);
    size_t keep = 0;
    uint32_t last_end = 0;
    for (size_t t = 0; t < n; t++)
        if (iv[work[t]].s >= last_end) { keep++; last_end = iv[work[t]].e; }
    return n - keep;
}
/* 닫힌구간 [s, e] 풍선, 화살은 한 점 x 에서 s <= x <= e 인 풍선을 모두 터뜨린다. 최소 화살 수 */
size_t min_arrows(const ival_t *b, size_t n, uint16_t *work) {
    if (n == 0) return 0;
    sort_idx(b, n, work, BY_END);
    size_t arrows = 1;
    uint32_t pos = b[work[0]].e;                  /* 첫 화살은 가장 먼저 끝나는 풍선의 끝에 */
    for (size_t t = 1; t < n; t++)
        if (b[work[t]].s > pos) { arrows++; pos = b[work[t]].e; }   /* 엄격한 > : 닿으면 같이 터짐 */
    return arrows;
}

/* ======================= 연습 2 ======================= */
/* a[0..n) 은 정렬·병합된 목록 (a[k].e < a[k+1].s). x 를 넣고 다시 병합해 out 에 쓴다. 맞닿으면 합친다. O(n) */
size_t insert_interval(const ival_t *a, size_t n, ival_t x, ival_t *out) {
    size_t i = 0, m = 0;
    while (i < n && a[i].e < x.s) out[m++] = a[i++];          /* 완전히 왼쪽 (닿지도 않음) */
    while (i < n && a[i].s <= x.e) {                          /* 겹치거나 닿음 → x 를 키운다 */
        if (a[i].s < x.s) x.s = a[i].s;
        if (a[i].e > x.e) x.e = a[i].e;
        i++;
    }
    out[m++] = x;
    while (i < n) out[m++] = a[i++];                          /* 완전히 오른쪽 */
    return m;
}

/* ======================= 연습 3 ======================= */
/* a, b 는 각각 정렬·병합된 목록. 교집합 [max s, min e) 가 비어 있지 않으면 쓴다. 끝이 이른 쪽을 전진 */
size_t intersect_lists(const ival_t *a, size_t na, const ival_t *b, size_t nb, ival_t *out) {
    size_t i = 0, j = 0, m = 0;
    while (i < na && j < nb) {
        uint32_t lo = a[i].s > b[j].s ? a[i].s : b[j].s;
        uint32_t hi = a[i].e < b[j].e ? a[i].e : b[j].e;
        if (lo < hi) out[m++] = (ival_t){ lo, hi };
        if (a[i].e < b[j].e) i++; else j++;
    }
    return m;
}

/* ======================= 연습 4 ======================= */
/* k 개 채널로 받을 수 있는 최대 전송 수. chan[i] = 채널 또는 0xFF(거절). best_fit = 1 이면 "비어 있는 채널 중
 * 마지막 끝이 가장 늦은 것", 0 이면 "가장 일찍 빈 것"(힙 top 과 같은 선택) — 후자는 최적이 아니다. */
size_t max_with_k(const ival_t *x, size_t n, unsigned k, int best_fit, uint16_t *work, uint8_t *chan) {
    uint32_t last[MAX_CH];
    if (k > MAX_CH) k = MAX_CH;
    for (unsigned c = 0; c < k; c++) last[c] = 0;
    sort_idx(x, n, work, BY_END);
    size_t got = 0;
    for (size_t t = 0; t < n; t++) {
        uint16_t i = work[t];
        int pick = -1;
        for (unsigned c = 0; c < k; c++) {
            if (last[c] > x[i].s) continue;                   /* 바쁨 */
            if (pick < 0 || (best_fit ? last[c] > last[pick] : last[c] < last[pick])) pick = (int)c;
        }
        if (pick < 0) { chan[i] = 0xFF; continue; }
        chan[i] = (uint8_t)pick;
        last[pick] = x[i].e;
        got++;
    }
    return got;
}

/* ======================= 연습 5 ======================= */
/* 가중치 합이 최대인 비겹침 부분집합. dp[t] = 끝 순서 앞 t 개로 얻는 최대 가중치.
 * dp[t+1] = max(dp[t], w + dp[p]),  p = 끝 순서에서 e <= s 인 구간 수 (upper_bound, ALGO-B02).
 * pick[] 에 고른 원래 인덱스, *npick 에 개수. 최대 가중치를 돌려준다. */
uint64_t wis_max(const ival_t *iv, const uint32_t *w, size_t n, uint16_t *work, uint64_t *dp,
                 uint16_t *pick, size_t *npick) {
    sort_idx(iv, n, work, BY_END);
    dp[0] = 0;
    for (size_t t = 0; t < n; t++) {
        uint32_t s = iv[work[t]].s;
        size_t lo = 0, hi = t;                                /* [0, lo): e <= s,  [hi, t): e > s */
        while (lo < hi) {
            size_t mid = lo + (hi - lo) / 2;
            if (iv[work[mid]].e <= s) lo = mid + 1; else hi = mid;
        }
        uint64_t take = (uint64_t)w[work[t]] + dp[lo];
        dp[t + 1] = take > dp[t] ? take : dp[t];
    }
    size_t k = 0, t = n;                                      /* 역추적: dp[t] != dp[t-1] 이면 t-1 번째를 골랐다 */
    while (t > 0) {
        if (dp[t] == dp[t - 1]) { t--; continue; }
        pick[k++] = work[t - 1];
        uint32_t s = iv[work[t - 1]].s;
        size_t lo = 0, hi = t - 1;
        while (lo < hi) {
            size_t mid = lo + (hi - lo) / 2;
            if (iv[work[mid]].e <= s) lo = mid + 1; else hi = mid;
        }
        t = lo;
    }
    *npick = k;
    return dp[n];
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t g_rng = 0x1234567u;
static uint32_t xorshift32(void) {
    uint32_t x = g_rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return g_rng = x;
}
static int ov(ival_t a, ival_t b) { return a.s < b.e && b.s < a.e; }

#define G 24
static size_t runs(const unsigned char *cell, ival_t *out) {
    size_t m = 0;
    for (uint32_t t = 0; t < G;) {
        if (!cell[t]) { t++; continue; }
        uint32_t s = t;
        while (t < G && cell[t]) t++;
        out[m++] = (ival_t){ s, t };
    }
    return m;
}
static void paint(unsigned char *cell, const ival_t *iv, size_t n) {
    for (size_t i = 0; i < n; i++) for (uint32_t t = iv[i].s; t < iv[i].e; t++) cell[t] = 1;
}
static int same(const ival_t *a, size_t na, const ival_t *b, size_t nb) {
    if (na != nb) return 0;
    for (size_t i = 0; i < na; i++) if (a[i].s != b[i].s || a[i].e != b[i].e) return 0;
    return 1;
}
/* 무작위 구간 n 개 (좌표 < G) */
static void rand_ivals(ival_t *iv, size_t n, uint32_t maxlen) {
    for (size_t i = 0; i < n; i++) {
        uint32_t len = 1 + xorshift32() % maxlen, s = xorshift32() % (G - len + 1);
        iv[i] = (ival_t){ s, s + len };
    }
}
/* 무작위 정렬·병합 목록: 격자 칸을 칠해서 run 으로 읽는다 */
static size_t rand_canon(ival_t *out) {
    unsigned char cell[G];
    for (int t = 0; t < G; t++) cell[t] = (xorshift32() % 3) != 0;
    return runs(cell, out);
}
static unsigned depth_of(const ival_t *iv, size_t n, uint32_t mask) {
    unsigned best = 0;
    for (size_t i = 0; i < n; i++) {
        if (!(mask >> i & 1)) continue;
        unsigned c = 0;
        for (size_t j = 0; j < n; j++) c += (mask >> j & 1) && iv[j].s <= iv[i].s && iv[i].s < iv[j].e;
        if (c > best) best = c;
    }
    return best;
}

static uint16_t g_work[MAX_IV], g_pick[MAX_IV];
static uint8_t g_chan[MAX_IV];
static uint64_t g_dp[MAX_IV + 1];

int main(void) {
    /* ---- 연습 1 ---- */
    {
        ival_t e1[] = { {1,2}, {2,3}, {3,4}, {1,3} }, e2[] = { {1,2}, {1,2}, {1,2} }, e3[] = { {1,2}, {2,3} };
        CHECK(min_removals(e1, 4, g_work) == 1 && min_removals(e2, 3, g_work) == 2 && min_removals(e3, 2, g_work) == 0);
        ival_t b1[] = { {10,16}, {2,8}, {1,6}, {7,12} }, b2[] = { {1,2}, {3,4}, {5,6}, {7,8} }, b3[] = { {1,2}, {2,3}, {3,4}, {4,5} };
        CHECK(min_arrows(b1, 4, g_work) == 2);      /* LeetCode 452 예제 */
        CHECK(min_arrows(b2, 4, g_work) == 4);
        CHECK(min_arrows(b3, 4, g_work) == 2);      /* 닫힌구간: [1,2],[2,3] 이 x=2 하나로 */
        CHECK(min_arrows(NULL, 0, g_work) == 0);
        unsigned long cases = 0, bad_r = 0, bad_a = 0;
        for (int it = 0; it < 3000; it++) {         /* 브루트포스: 제거 = 부분집합 전수, 화살 = 격자 점 집합 전수 */
            size_t n = xorshift32() % 11;
            ival_t iv[16];
            rand_ivals(iv, n, 8);
            unsigned best = 0;
            for (uint32_t m = 0; m < (1u << n); m++) {
                int ok = 1; unsigned c = 0;
                for (size_t i = 0; i < n && ok; i++) if (m >> i & 1) { c++; for (size_t j = i + 1; j < n; j++) if ((m >> j & 1) && ov(iv[i], iv[j])) ok = 0; }
                if (ok && c > best) best = c;
            }
            if (min_removals(iv, n, g_work) != n - best) bad_r++;
            ival_t bl[16];                           /* 닫힌구간 풍선: [s, e-1] (점 1개짜리 풍선 포함), 좌표 < 12 */
            for (size_t i = 0; i < n; i++) { uint32_t s = xorshift32() % 12, len = xorshift32() % 4; bl[i] = (ival_t){ s, s + len < 12 ? s + len : 11 }; }
            unsigned amin = 99;
            for (uint32_t pts = 0; pts < (1u << 12); pts++) {
                int ok = 1;
                for (size_t i = 0; i < n && ok; i++) {
                    int hit = 0;
                    for (uint32_t x = bl[i].s; x <= bl[i].e && !hit; x++) hit = pts >> x & 1;
                    ok = hit;
                }
                if (ok) { unsigned c = 0; for (uint32_t q = pts; q; q &= q - 1) c++; if (c < amin) amin = c; }
            }
            if (min_arrows(bl, n, g_work) != amin) bad_a++;
            cases++;
        }
        printf("ex1: %lu random cases, removal mismatches=%lu arrow mismatches=%lu\n", cases, bad_r, bad_a);
        CHECK(bad_r == 0 && bad_a == 0);
    }

    /* ---- 연습 2 ---- */
    {
        ival_t a1[] = { {1,3}, {6,9} }, w1[] = { {1,5}, {6,9} };
        ival_t a2[] = { {1,2}, {3,5}, {6,7}, {8,10}, {12,16} }, w2[] = { {1,2}, {3,10}, {12,16} };
        ival_t out[64];
        size_t m;
        m = insert_interval(a1, 2, (ival_t){ 2, 5 }, out); CHECK(same(out, m, w1, 2));    /* LeetCode 57 예제 */
        m = insert_interval(a2, 5, (ival_t){ 4, 8 }, out); CHECK(same(out, m, w2, 3));    /* [8,10) 은 8 에서 닿아 합쳐짐 */
        m = insert_interval(NULL, 0, (ival_t){ 4, 8 }, out); CHECK(m == 1 && out[0].s == 4 && out[0].e == 8);
        unsigned long bad = 0, cases = 0;
        for (int it = 0; it < 5000; it++) {
            ival_t a[G], ref[G];
            size_t n = rand_canon(a);
            ival_t x;
            rand_ivals(&x, 1, 10);
            unsigned char cell[G] = { 0 };
            paint(cell, a, n); paint(cell, &x, 1);
            size_t mr = runs(cell, ref);
            m = insert_interval(a, n, x, out);
            if (!same(out, m, ref, mr)) bad++;
            cases++;
        }
        printf("ex2: %lu random inserts vs bitmap, mismatches=%lu\n", cases, bad);
        CHECK(bad == 0);
    }

    /* ---- 연습 3 ---- */
    {   /* LeetCode 986 예제를 반개구간으로 읽으면 점 교집합 [5,5] [24,24] [25,25] 는 사라진다 */
        ival_t a[] = { {0,2}, {5,10}, {13,23}, {24,25} }, b[] = { {1,5}, {8,12}, {15,24}, {25,26} };
        ival_t w[] = { {1,2}, {8,10}, {15,23} }, out[64];
        size_t m = intersect_lists(a, 4, b, 4, out);
        CHECK(same(out, m, w, 3));
        CHECK(intersect_lists(a, 4, NULL, 0, out) == 0);
        unsigned long bad = 0, cases = 0;
        for (int it = 0; it < 5000; it++) {
            ival_t x[G], y[G], ref[G];
            size_t nx = rand_canon(x), ny = rand_canon(y);
            unsigned char cx[G] = { 0 }, cy[G] = { 0 }, c[G];
            paint(cx, x, nx); paint(cy, y, ny);
            for (int t = 0; t < G; t++) c[t] = cx[t] & cy[t];
            size_t mr = runs(c, ref);
            m = intersect_lists(x, nx, y, ny, out);
            if (!same(out, m, ref, mr)) bad++;
            cases++;
        }
        printf("ex3: %lu random list pairs vs bitmap AND, mismatches=%lu\n", cases, bad);
        CHECK(bad == 0);
    }

    /* ---- 연습 4 ---- */
    {   /* best-fit 는 맞고 earliest-free 는 틀리는 최소 예: k = 2 */
        ival_t ce[] = { {0,1}, {0,3}, {3,6}, {2,7} };
        CHECK(max_with_k(ce, 4, 2, 1, g_work, g_chan) == 4);
        CHECK(max_with_k(ce, 4, 2, 0, g_work, g_chan) == 3);
        unsigned long cases = 0, bad_bf = 0, worse_ef = 0;
        for (int it = 0; it < 3000; it++) {
            size_t n = xorshift32() % 11;
            unsigned k = 1 + xorshift32() % 3;
            ival_t iv[16];
            rand_ivals(iv, n, 9);
            unsigned best = 0;                      /* 부분집합 전수: 깊이 <= k 이면 k 채널로 가능 (본문 정리) */
            for (uint32_t m = 0; m < (1u << n); m++) {
                unsigned c = 0;
                for (uint32_t q = m; q; q &= q - 1) c++;
                if (c > best && depth_of(iv, n, m) <= k) best = c;
            }
            size_t got = max_with_k(iv, n, k, 1, g_work, g_chan);
            int valid = 1; size_t placed = 0;
            for (size_t i = 0; i < n; i++) {
                if (g_chan[i] == 0xFF) continue;
                placed++;
                if (g_chan[i] >= k) valid = 0;
                for (size_t j = i + 1; j < n; j++) if (g_chan[j] == g_chan[i] && ov(iv[i], iv[j])) valid = 0;
            }
            if (got != best || placed != got || !valid) bad_bf++;
            if (max_with_k(iv, n, k, 0, g_work, g_chan) < best) worse_ef++;
            cases++;
        }
        printf("ex4: %lu random cases (k=1..3), best-fit mismatches=%lu, earliest-free suboptimal=%lu\n", cases, bad_bf, worse_ef);
        CHECK(bad_bf == 0);
        CHECK(worse_ef > 0);
    }

    /* ---- 연습 5 ---- */
    {
        ival_t ce[] = { {0,10}, {0,5}, {5,10} };
        uint32_t cw[] = { 10, 1, 1 };
        size_t np;
        CHECK(wis_max(ce, cw, 3, g_work, g_dp, g_pick, &np) == 10 && np == 1 && g_pick[0] == 0);
        CHECK(wis_max(NULL, NULL, 0, g_work, g_dp, g_pick, &np) == 0 && np == 0);
        uint32_t big[] = { UINT32_MAX, UINT32_MAX, UINT32_MAX };   /* 합이 32비트를 넘는다 → uint64_t */
        ival_t chain[] = { {0,1}, {1,2}, {2,3} };
        CHECK(wis_max(chain, big, 3, g_work, g_dp, g_pick, &np) == 3ull * UINT32_MAX && np == 3);
        unsigned long cases = 0, bad = 0, greedy_worse = 0;
        for (int it = 0; it < 3000; it++) {
            size_t n = xorshift32() % 13;
            ival_t iv[16];
            uint32_t w[16];
            rand_ivals(iv, n, 9);
            for (size_t i = 0; i < n; i++) w[i] = 1 + xorshift32() % 20;
            uint64_t best = 0;
            for (uint32_t m = 0; m < (1u << n); m++) {
                int ok = 1; uint64_t sum = 0;
                for (size_t i = 0; i < n && ok; i++) if (m >> i & 1) { sum += w[i]; for (size_t j = i + 1; j < n; j++) if ((m >> j & 1) && ov(iv[i], iv[j])) ok = 0; }
                if (ok && sum > best) best = sum;
            }
            uint64_t got = wis_max(iv, w, n, g_work, g_dp, g_pick, &np);
            uint64_t sum = 0; int valid = 1;
            for (size_t a = 0; a < np; a++) { sum += w[g_pick[a]]; for (size_t b = a + 1; b < np; b++) if (ov(iv[g_pick[a]], iv[g_pick[b]])) valid = 0; }
            if (got != best || sum != got || !valid) bad++;
            sort_idx(iv, n, g_work, BY_END);           /* 비교: 무가중 끝 순서 그리디의 가중치 합 */
            uint64_t gs = 0; uint32_t last = 0;
            for (size_t t = 0; t < n; t++) if (iv[g_work[t]].s >= last) { gs += w[g_work[t]]; last = iv[g_work[t]].e; }
            if (gs < best) greedy_worse++;
            cases++;
        }
        printf("ex5: %lu random weighted cases, DP mismatches=%lu, unweighted greedy suboptimal=%lu\n", cases, bad, greedy_worse);
        CHECK(bad == 0);
        CHECK(greedy_worse > 0);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
