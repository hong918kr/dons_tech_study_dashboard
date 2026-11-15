/* ALGO-B07 추가 연습 — 한 번에 검증 (자기완결)
 *
 *   연습 1  merge 의 `<` vs `<=`: 키 순서는 맞는데 안정성이 깨진다 (전부 같은 키 8개 → 완전히 뒤집힘)
 *   연습 2  scratch 를 n/2 칸으로: 왼쪽 run 만 복사하고 앞에서부터 병합
 *   연습 3  top-down 리스트 머지소트 (fast/slow 분할) — 재귀 깊이 = ⌈lg n⌉ + 1 을 실측 (O(1) 메모리가 아님)
 *   연습 4  머지소트로 역전 쌍(inversion) 세기 — 타임스탬프 뒤바뀜 개수, O(n²) 브루트포스와 대조
 *   연습 5  k-way 병합: 플래시에 정렬된 run k 개 → run 당 커서 하나로 병합 (외부 정렬의 병합 단계)
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { int32_t key; uint32_t seq; } rec_t;

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t g_rng = 0x1234ABCDu;
static uint32_t rnd(void) { uint32_t x = g_rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g_rng = x; }

enum { PAT_RANDOM, PAT_SORTED, PAT_REVERSED, PAT_EQUAL, PAT_FEWKEYS, NPAT };
static void gen(rec_t *a, size_t n, int pat) {
    for (size_t i = 0; i < n; i++) {
        int32_t k;
        switch (pat) {
        case PAT_RANDOM:   k = (int32_t)(rnd() >> 1); break;
        case PAT_SORTED:   k = (int32_t)i; break;
        case PAT_REVERSED: k = (int32_t)(n - i); break;
        case PAT_EQUAL:    k = 7; break;
        default:           k = (int32_t)(rnd() % 4u); break;
        }
        a[i].key = k; a[i].seq = (uint32_t)i;
    }
}
static void ref_stable(rec_t *a, size_t n) {                  /* 안정 삽입 정렬 = 기준 */
    for (size_t i = 1; i < n; i++) {
        rec_t x = a[i]; size_t j = i;
        while (j > 0 && a[j - 1].key > x.key) { a[j] = a[j - 1]; j--; }
        a[j] = x;
    }
}
static bool same(const rec_t *a, const rec_t *b, size_t n) {
    for (size_t i = 0; i < n; i++) if (a[i].key != b[i].key || a[i].seq != b[i].seq) return false;
    return true;
}
static bool keys_sorted(const rec_t *a, size_t n) {
    for (size_t i = 1; i < n; i++) if (a[i - 1].key > a[i].key) return false;
    return true;
}
static bool is_stable(const rec_t *a, size_t n) {
    for (size_t i = 1; i < n; i++) if (a[i - 1].key == a[i].key && a[i - 1].seq > a[i].seq) return false;
    return true;
}
static unsigned ceil_lg(size_t n) { unsigned c = 0; while (((size_t)1 << c) < n) c++; return c; }

/* ---------------- 연습 1: `<` 병합 ---------------- */
static void merge_lt(rec_t *dst, const rec_t *src, size_t lo, size_t mid, size_t hi) {
    size_t i = lo, j = mid, k = lo;
    while (i < mid && j < hi) dst[k++] = (src[i].key < src[j].key) ? src[i++] : src[j++];   /* 같으면 오른쪽! */
    while (i < mid) dst[k++] = src[i++];
    while (j < hi)  dst[k++] = src[j++];
}
static void merge_le(rec_t *dst, const rec_t *src, size_t lo, size_t mid, size_t hi) {
    size_t i = lo, j = mid, k = lo;
    while (i < mid && j < hi) dst[k++] = (src[i].key <= src[j].key) ? src[i++] : src[j++];
    while (i < mid) dst[k++] = src[i++];
    while (j < hi)  dst[k++] = src[j++];
}
typedef void (*merge_fn)(rec_t *, const rec_t *, size_t, size_t, size_t);
static void td(rec_t *a, rec_t *t, size_t lo, size_t hi, merge_fn m) {
    if (hi - lo < 2) return;
    size_t mid = lo + (hi - lo) / 2;
    td(a, t, lo, mid, m); td(a, t, mid, hi, m);
    memcpy(t + lo, a + lo, (hi - lo) * sizeof *a);
    m(a, t, lo, mid, hi);
}

/* ---------------- 연습 2: n/2 scratch ---------------- */
/* 왼쪽 run a[lo,mid) 만 tmp[0..L) 로 복사. 그다음 tmp 와 a[mid,hi) 를 a[lo..) 에 앞에서부터 병합.
 * 쓰는 위치 k = lo + (i) + (j - mid) ≤ j 이므로 아직 안 읽은 오른쪽 원소를 덮지 않는다. */
static void half_rec(rec_t *a, rec_t *tmp, size_t lo, size_t hi) {
    if (hi - lo < 2) return;
    size_t mid = lo + (hi - lo) / 2;
    half_rec(a, tmp, lo, mid); half_rec(a, tmp, mid, hi);
    size_t L = mid - lo, i = 0, j = mid, k = lo;
    memcpy(tmp, a + lo, L * sizeof *a);
    while (i < L && j < hi) a[k++] = (tmp[i].key <= a[j].key) ? tmp[i++] : a[j++];
    while (i < L) a[k++] = tmp[i++];
    /* 오른쪽 나머지는 이미 제자리 */
}
static void msort_half(rec_t *a, size_t n, rec_t *tmp /* n/2 칸 */) { if (n >= 2) half_rec(a, tmp, 0, n); }

/* ---------------- 연습 3: top-down 리스트 (fast/slow) ---------------- */
typedef struct node { struct node *next; int32_t key; uint32_t seq; } node_t;
static unsigned g_depth, g_maxdepth;
static node_t *lmerge(node_t *a, node_t *b) {
    node_t *head = NULL, **pp = &head;
    while (a && b) {
        if (a->key <= b->key) { *pp = a; a = a->next; } else { *pp = b; b = b->next; }
        pp = &(*pp)->next;
    }
    *pp = a ? a : b;
    return head;
}
static node_t *lsort_td(node_t *h) {
    if (++g_depth > g_maxdepth) g_maxdepth = g_depth;
    if (h && h->next) {
        node_t *slow = h, *fast = h->next;                     /* 앞쪽이 ⌈n/2⌉ 개 */
        while (fast && fast->next) { slow = slow->next; fast = fast->next->next; }
        node_t *right = slow->next; slow->next = NULL;
        h = lmerge(lsort_td(h), lsort_td(right));
    }
    g_depth--;
    return h;
}

/* ---------------- 연습 4: 역전 쌍 세기 ---------------- */
static uint64_t inv_rec(rec_t *a, rec_t *t, size_t lo, size_t hi) {
    if (hi - lo < 2) return 0;
    size_t mid = lo + (hi - lo) / 2;
    uint64_t c = inv_rec(a, t, lo, mid) + inv_rec(a, t, mid, hi);
    memcpy(t + lo, a + lo, (hi - lo) * sizeof *a);
    size_t i = lo, j = mid, k = lo;
    while (i < mid && j < hi) {
        if (t[i].key <= t[j].key) a[k++] = t[i++];             /* 같은 키는 역전이 아니다 → <= */
        else { a[k++] = t[j++]; c += mid - i; }                /* t[j] 가 왼쪽에 남은 mid-i 개 모두보다 작다 */
    }
    while (i < mid) a[k++] = t[i++];
    while (j < hi)  a[k++] = t[j++];
    return c;
}
static uint64_t inv_brute(const rec_t *a, size_t n) {
    uint64_t c = 0;
    for (size_t i = 0; i < n; i++) for (size_t j = i + 1; j < n; j++) c += a[i].key > a[j].key;
    return c;
}

/* ---------------- 연습 5: k-way 병합 ---------------- */
/* run r = recs[off[r] .. off[r+1]) (각각 정렬됨). 커서 cur[r] 하나씩만 RAM 에 둔다고 생각한다.
 * 매번 k 개 머리 중 최소를 선형으로 고른다; 같으면 번호가 작은 run (= 원래 앞쪽) → 안정. */
static unsigned long kway(const rec_t *recs, const size_t *off, size_t k, rec_t *out) {
    size_t cur[16]; unsigned long cmp = 0; size_t o = 0;
    for (size_t r = 0; r < k; r++) cur[r] = off[r];
    for (;;) {
        size_t best = k;
        for (size_t r = 0; r < k; r++) {
            if (cur[r] == off[r + 1]) continue;                /* 다 읽은 run */
            if (best == k) { best = r; continue; }
            cmp++;
            if (recs[cur[r]].key < recs[cur[best]].key) best = r;   /* 엄격히 작을 때만 바꾼다 → 안정 */
        }
        if (best == k) break;
        out[o++] = recs[cur[best]++];
    }
    return cmp;
}

#define BIG 100000
static rec_t A[BIG], B[BIG], T[BIG + 1], R[BIG];
static node_t pool[BIG];

int main(void) {
    /* 연습 1 */
    {
        int keys_ok = 1, broke = 0, distinct_same = 1;
        for (int trial = 0; trial < 200; trial++) {
            size_t n = 2 + rnd() % 63;
            gen(A, n, PAT_FEWKEYS); memcpy(B, A, n * sizeof *A);
            td(A, T, 0, n, merge_lt); td(B, T, 0, n, merge_le);
            if (!keys_sorted(A, n)) keys_ok = 0;
            if (!is_stable(A, n)) broke++;
            gen(A, n, PAT_RANDOM); memcpy(B, A, n * sizeof *A);  /* 키가 전부 다르면 둘이 같다 */
            td(A, T, 0, n, merge_lt); td(B, T, 0, n, merge_le);
            if (!same(A, B, n)) distinct_same = 0;
        }
        printf("ex1: '<' merge on 200 random 4-key arrays: keys sorted=%s, stability broken in %d/200\n",
               keys_ok ? "yes" : "NO", broke);
        CHECK(keys_ok && broke > 0);
        CHECK(distinct_same);
        gen(A, 8, PAT_EQUAL); td(A, T, 0, 8, merge_lt);
        bool reversed = true;
        for (uint32_t i = 0; i < 8; i++) reversed &= A[i].seq == 7 - i;
        CHECK(reversed);                                       /* 같은 키 8개 → seq 7,6,…,0 */
    }
    /* 연습 2 */
    {
        int bad = 0, guard_bad = 0;
        for (int pat = 0; pat < NPAT; pat++)
            for (size_t n = 0; n <= 64; n++) {
                gen(A, n, pat); memcpy(R, A, n * sizeof *A); ref_stable(R, n);
                T[n / 2].key = 0x5A5A5A5A;                     /* scratch 끝 바로 뒤 */
                msort_half(A, n, T);
                if (!same(A, R, n)) bad++;
                if (T[n / 2].key != 0x5A5A5A5A) guard_bad++;
            }
        gen(A, BIG, PAT_FEWKEYS); T[BIG / 2].key = 0x5A5A5A5A;
        msort_half(A, BIG, T);
        CHECK(bad == 0 && guard_bad == 0);
        CHECK(keys_sorted(A, BIG) && is_stable(A, BIG) && T[BIG / 2].key == 0x5A5A5A5A);
    }
    /* 연습 3 */
    {
        int bad = 0, depth_bad = 0;
        for (size_t n = 0; n <= 2000; n++) {
            gen(A, n, n <= 64 ? PAT_FEWKEYS : PAT_RANDOM);
            node_t *h = NULL, **pp = &h;
            for (size_t i = 0; i < n; i++) { pool[i].key = A[i].key; pool[i].seq = A[i].seq; *pp = &pool[i]; pp = &pool[i].next; }
            *pp = NULL;
            g_depth = g_maxdepth = 0;
            h = lsort_td(h);
            if (g_maxdepth != (n < 2 ? 1u : ceil_lg(n) + 1)) depth_bad++;
            size_t i = 0;
            for (node_t *p = h; p && i < n; p = p->next, i++) { B[i].key = p->key; B[i].seq = p->seq; }
            memcpy(R, A, n * sizeof *A); ref_stable(R, n);
            if (i != n || !same(B, R, n)) bad++;
        }
        size_t n = BIG;
        gen(A, n, PAT_RANDOM);
        node_t *h = NULL, **pp = &h;
        for (size_t i = 0; i < n; i++) { pool[i].key = A[i].key; pool[i].seq = A[i].seq; *pp = &pool[i]; pp = &pool[i].next; }
        *pp = NULL;
        g_depth = g_maxdepth = 0; h = lsort_td(h);
        printf("ex3: top-down list sort n=%d max depth=%u (ceil(lg n)+1=%u)\n", BIG, g_maxdepth, ceil_lg(BIG) + 1);
        CHECK(bad == 0);
        CHECK(depth_bad == 0 && g_maxdepth == 18);
    }
    /* 연습 4 */
    {
        int bad = 0;
        for (int pat = 0; pat < NPAT; pat++)
            for (size_t n = 0; n <= 64; n++) {
                gen(A, n, pat);
                uint64_t want = inv_brute(A, n);
                if (inv_rec(A, T, 0, n) != want) bad++;
                if (pat == PAT_REVERSED && want != (uint64_t)n * (n ? n - 1 : 0) / 2) bad++;
                if (pat == PAT_EQUAL && want != 0) bad++;
            }
        gen(A, 3000, PAT_FEWKEYS);
        uint64_t want = inv_brute(A, 3000), got = inv_rec(A, T, 0, 3000);
        printf("ex4: n=3000 4-key inversions brute=%llu merge=%llu\n", (unsigned long long)want, (unsigned long long)got);
        CHECK(bad == 0);
        CHECK(got == want);
    }
    /* 연습 5 */
    {
        int bad = 0, cmp_bad = 0;
        for (int trial = 0; trial < 300; trial++) {
            size_t k = 1 + rnd() % 8, off[17], n = 0;
            for (size_t r = 0; r < k; r++) {
                off[r] = n;
                size_t len = rnd() % 40;                       /* 빈 run 도 나온다 */
                gen(A + n, len, PAT_FEWKEYS);
                ref_stable(A + n, len);                        /* run 하나 = RAM 에서 정렬해 쓴 것 */
                n += len;
            }
            off[k] = n;
            for (size_t i = 0; i < n; i++) A[i].seq = (uint32_t)i;   /* 이어 붙인 순서 = 원래 순서 */
            memcpy(R, A, n * sizeof *A); ref_stable(R, n);
            unsigned long c = kway(A, off, k, B);
            if (!same(B, R, n)) bad++;
            if (c > (unsigned long)(k - 1) * n) cmp_bad++;
        }
        CHECK(bad == 0);
        CHECK(cmp_bad == 0);
    }
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
