/* ALGO-B05 추가 연습 — 한 번에 검증
 *
 * 연습 1: 칵테일 셰이커 정렬 — 버블의 "거북이(turtle)" 문제를 양방향 패스로 고친다
 * 연습 2: 이진 삽입 정렬 — 비교는 O(n log n), 밀기는 그대로 inv. 안정하려면 upper_bound
 * 연습 3: 최소 반례 전수 탐색 — 선택 정렬이 불안정해지는 가장 작은 n, 삽입·버블은 전부 안정
 * 연습 4: 사이클 정렬 — 배열 쓰기 최소 (서로 다른 키: 쓰기 = 제자리 아닌 원소 수)
 * 연습 5: 중앙값 창의 국소 수정 — 바뀐 한 원소만 좌/우로 옮겨 비교 ≤ N
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { int16_t key; uint16_t idx; } Rec;
typedef struct { uint32_t cmp, swaps, shifts, writes; } SortStats;

static int rec_less(Rec x, Rec y, SortStats *st) { st->cmp++; return x.key < y.key; }
static void rec_swap(Rec *a, int i, int j, SortStats *st) { Rec t = a[i]; a[i] = a[j]; a[j] = t; st->swaps++; st->writes += 2; }

/* 본문과 같은 세 정렬 (연습 3·4 의 비교 대상) */
static void insertion_sort(Rec *a, int n, SortStats *st) {
    for (int i = 1; i < n; i++) {
        Rec x = a[i]; int j = i;
        while (j > 0 && rec_less(x, a[j - 1], st)) { a[j] = a[j - 1]; st->shifts++; st->writes++; j--; }
        if (j != i) { a[j] = x; st->writes++; }
    }
}
static void selection_sort(Rec *a, int n, SortStats *st) {
    for (int i = 0; i + 1 < n; i++) {
        int m = i;
        for (int j = i + 1; j < n; j++) if (rec_less(a[j], a[m], st)) m = j;
        if (m != i) rec_swap(a, i, m, st);
    }
}
static void bubble_sort(Rec *a, int n, SortStats *st) {
    for (int pass = 0; pass + 1 < n; pass++) {
        int swapped = 0;
        for (int j = 0; j + 1 < n - pass; j++)
            if (rec_less(a[j + 1], a[j], st)) { rec_swap(a, j, j + 1, st); swapped = 1; }
        if (!swapped) break;
    }
}
static int is_stable_sorted(const Rec *a, int n) {
    for (int i = 1; i < n; i++) {
        if (a[i - 1].key > a[i].key) return 0;
        if (a[i - 1].key == a[i].key && a[i - 1].idx > a[i].idx) return 0;
    }
    return 1;
}

/* ---- 연습 1: 칵테일 셰이커 정렬 ---- */
static void cocktail_sort(Rec *a, int n, SortStats *st) {
    int lo = 0, hi = n - 1;
    while (lo < hi) {
        int last = lo;                                   /* 마지막으로 교환한 자리 → 다음 경계 */
        for (int j = lo; j < hi; j++)
            if (rec_less(a[j + 1], a[j], st)) { rec_swap(a, j, j + 1, st); last = j; }
        hi = last;
        if (lo >= hi) break;
        last = hi;
        for (int j = hi; j > lo; j--)
            if (rec_less(a[j], a[j - 1], st)) { rec_swap(a, j - 1, j, st); last = j; }
        lo = last;
    }
}

/* ---- 연습 2: 이진 삽입 정렬 (upper_bound: x 보다 "큰" 첫 자리 → 같은 키 뒤에 넣는다) ---- */
static void binary_insertion_sort(Rec *a, int n, SortStats *st) {
    for (int i = 1; i < n; i++) {
        Rec x = a[i];
        int lo = 0, hi = i;                              /* [lo, hi) 안에서 첫 a[k] > x */
        while (lo < hi) {
            int mid = lo + (hi - lo) / 2;
            if (rec_less(x, a[mid], st)) hi = mid; else lo = mid + 1;
        }
        for (int k = i; k > lo; k--) { a[k] = a[k - 1]; st->shifts++; st->writes++; }
        if (lo != i) { a[lo] = x; st->writes++; }
    }
}

/* ---- 연습 4: 사이클 정렬 (서로 다른 키 가정) ---- */
static void cycle_sort(Rec *a, int n, SortStats *st) {
    for (int start = 0; start + 1 < n; start++) {
        Rec item = a[start];
        int pos = start;
        for (int j = start + 1; j < n; j++) if (rec_less(a[j], item, st)) pos++;
        if (pos == start) continue;                      /* 이미 제자리: 쓰기 0 */
        while (pos != start) {                           /* 사이클을 따라 한 번씩만 쓴다 */
            Rec t = a[pos]; a[pos] = item; item = t; st->writes++;
            pos = start;
            for (int j = start + 1; j < n; j++) if (rec_less(a[j], item, st)) pos++;
        }
        a[start] = item; st->writes++;
    }
}

/* ---- 연습 5: 중앙값 창, 국소 수정 ---- */
#define MW_MAX 32
typedef struct { int16_t ring[MW_MAX], sorted[MW_MAX]; int n, head, count; uint32_t cmp_last; } MedWin;
static void medwin_init(MedWin *w, int n) { memset(w, 0, sizeof *w); w->n = n; }
static int16_t medwin_push_local(MedWin *w, int16_t x) {
    int p;
    if (w->count < w->n) { p = w->count++; }
    else { p = 0; while (p < w->count - 1 && w->sorted[p] != w->ring[w->head]) p++; }
    w->sorted[p] = x;
    w->ring[w->head] = x;
    w->head = (w->head + 1 == w->n) ? 0 : w->head + 1;
    uint32_t c = 0;
    int16_t *s = w->sorted;
    while (p > 0) { c++; if (!(x < s[p - 1])) break; s[p] = s[p - 1]; p--; }        /* 왼쪽으로 */
    s[p] = x;
    while (p + 1 < w->count) { c++; if (!(s[p + 1] < x)) break; s[p] = s[p + 1]; p++; } /* 오른쪽으로 */
    s[p] = x;
    w->cmp_last = c;
    return s[(w->count - 1) / 2];
}

/* ---------------- 테스트 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t g_rng = 0x9E3779B9u;
static uint32_t xs32(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }
static int cmp_key_idx(const void *p, const void *q) {
    const Rec *x = p, *y = q;
    if (x->key != y->key) return x->key < y->key ? -1 : 1;
    return (x->idx > y->idx) - (x->idx < y->idx);
}
static void ref_sort(Rec *out, const Rec *in, int n) { memcpy(out, in, (size_t)n * sizeof *in); qsort(out, (size_t)n, sizeof *out, cmp_key_idx); }
static int same_recs(const Rec *a, const Rec *b, int n) {
    for (int i = 0; i < n; i++) if (a[i].key != b[i].key || a[i].idx != b[i].idx) return 0;
    return 1;
}
static void make_recs(Rec *a, int n, int keyrange) {
    for (int i = 0; i < n; i++) { a[i].key = (int16_t)(xs32() % (uint32_t)keyrange); a[i].idx = (uint16_t)i; }
}
static void make_perm(Rec *a, int n) {
    for (int i = 0; i < n; i++) { a[i].key = (int16_t)i; a[i].idx = (uint16_t)i; }
    for (int i = n - 1; i > 0; i--) { int j = (int)(xs32() % (uint32_t)(i + 1)); int16_t t = a[i].key; a[i].key = a[j].key; a[j].key = t; }
}
static uint32_t inversions(const Rec *a, int n) {
    uint32_t c = 0;
    for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) c += a[i].key > a[j].key;
    return c;
}
static uint32_t ceil_log2(uint32_t x) { uint32_t r = 0; while ((1u << r) < x) r++; return r; }

int main(void) {
    static Rec in[256], a[256], ref[256];
    SortStats st;

    /* 연습 1 */
    {
        int bad = 0;
        for (int t = 0; t < 300; t++) {
            int n = (int)(xs32() % 80u);
            make_recs(in, n, (t & 1) ? 6 : 5000);
            ref_sort(ref, in, n);
            memcpy(a, in, sizeof(Rec) * (size_t)n); memset(&st, 0, sizeof st);
            cocktail_sort(a, n, &st);
            bad += !same_recs(a, ref, n) || st.swaps != inversions(in, n);
        }
        CHECK(bad == 0);
        int n = 64;                                         /* 거북이: 1,2,...,63,0 */
        for (int i = 0; i < n; i++) { in[i].key = (int16_t)(i + 1 == n ? 0 : i + 1); in[i].idx = (uint16_t)i; }
        SortStats sb, sc; memset(&sb, 0, sizeof sb); memset(&sc, 0, sizeof sc);
        memcpy(a, in, sizeof(Rec) * 64); bubble_sort(a, n, &sb);
        memcpy(a, in, sizeof(Rec) * 64); cocktail_sort(a, n, &sc);
        printf("ex1 turtle n=64: bubble cmp=%u, cocktail cmp=%u\n", sb.cmp, sc.cmp);
        CHECK(sb.cmp == 2016u && sc.cmp <= 3u * 64u);
    }
    /* 연습 2 */
    {
        int bad = 0;
        for (int t = 0; t < 300; t++) {
            int n = (int)(xs32() % 120u);
            make_recs(in, n, (t & 1) ? 4 : 30000);
            ref_sort(ref, in, n);
            memcpy(a, in, sizeof(Rec) * (size_t)n); memset(&st, 0, sizeof st);
            binary_insertion_sort(a, n, &st);
            uint32_t bound = 0;
            for (int i = 1; i < n; i++) bound += ceil_log2((uint32_t)i + 1u);
            bad += !same_recs(a, ref, n) || st.shifts != inversions(in, n) || st.cmp > bound;
        }
        CHECK(bad == 0);
        make_perm(in, 256);
        SortStats si, sbi; memset(&si, 0, sizeof si); memset(&sbi, 0, sizeof sbi);
        memcpy(a, in, sizeof(Rec) * 256); insertion_sort(a, 256, &si);
        memcpy(a, in, sizeof(Rec) * 256); binary_insertion_sort(a, 256, &sbi);
        printf("ex2 random perm n=256: insertion cmp=%u shifts=%u | binary insertion cmp=%u shifts=%u\n",
               si.cmp, si.shifts, sbi.cmp, sbi.shifts);
        CHECK(sbi.shifts == si.shifts && sbi.cmp * 5u < si.cmp);
    }
    /* 연습 3 */
    {
        int first_unstable_n = 0, unstable3 = 0, ins_bub_bad = 0;
        for (int n = 1; n <= 6; n++) {
            int total = 1; for (int i = 0; i < n; i++) total *= 3;
            for (int code = 0; code < total; code++) {
                int c = code;
                for (int i = 0; i < n; i++) { in[i].key = (int16_t)(c % 3); in[i].idx = (uint16_t)i; c /= 3; }
                memcpy(a, in, sizeof(Rec) * (size_t)n); memset(&st, 0, sizeof st);
                selection_sort(a, n, &st);
                if (!is_stable_sorted(a, n)) { if (!first_unstable_n) first_unstable_n = n; if (n == 3) unstable3++; }
                memcpy(a, in, sizeof(Rec) * (size_t)n); insertion_sort(a, n, &st); ins_bub_bad += !is_stable_sorted(a, n);
                memcpy(a, in, sizeof(Rec) * (size_t)n); bubble_sort(a, n, &st);    ins_bub_bad += !is_stable_sorted(a, n);
            }
        }
        printf("ex3 keys in {0,1,2}, n<=6: selection first unstable at n=%d, unstable inputs at n=3: %d/27, insertion/bubble unstable: %d\n",
               first_unstable_n, unstable3, ins_bub_bad);
        CHECK(first_unstable_n == 3);
        CHECK(unstable3 == 3);              /* [1,1,0] [2,2,0] [2,2,1] */
        CHECK(ins_bub_bad == 0);
    }
    /* 연습 4 */
    {
        int bad = 0;
        uint32_t sum_cyc = 0, sum_sel = 0;
        for (int t = 0; t < 300; t++) {
            int n = 1 + (int)(xs32() % 64u);
            make_perm(in, n);
            uint32_t misplaced = 0;
            for (int i = 0; i < n; i++) misplaced += in[i].key != i;
            SortStats sc, ss; memset(&sc, 0, sizeof sc); memset(&ss, 0, sizeof ss);
            memcpy(a, in, sizeof(Rec) * (size_t)n); cycle_sort(a, n, &sc);
            int ok = 1;
            for (int i = 0; i < n; i++) ok &= a[i].key == i;
            memcpy(ref, in, sizeof(Rec) * (size_t)n); selection_sort(ref, n, &ss);
            bad += !ok || sc.writes != misplaced || sc.writes > ss.writes;
            sum_cyc += sc.writes; sum_sel += ss.writes;
        }
        printf("ex4 300 random perms: total writes cycle=%u selection=%u\n", sum_cyc, sum_sel);
        CHECK(bad == 0);
    }
    /* 연습 5 */
    {
        const int Ns[] = { 1, 2, 9, 32 };
        for (int q = 0; q < 4; q++) {
            int N = Ns[q], bad = 0;
            uint32_t maxc = 0;
            MedWin w; medwin_init(&w, N);
            int16_t hist[4000];
            for (int t = 0; t < 4000; t++) {
                int16_t x = (int16_t)(xs32() % 7u == 0 ? (int)(xs32() % 65536u) - 32768 : 1000 + (int)(xs32() % 9u));
                hist[t] = x;
                int16_t m = medwin_push_local(&w, x);
                int cnt = t + 1 < N ? t + 1 : N;
                int16_t win[MW_MAX];
                for (int i = 0; i < cnt; i++) win[i] = hist[t - cnt + 1 + i];
                for (int i = 1; i < cnt; i++)
                    for (int j = i; j > 0 && win[j] < win[j - 1]; j--) { int16_t s = win[j]; win[j] = win[j - 1]; win[j - 1] = s; }
                bad += m != win[(cnt - 1) / 2] || memcmp(w.sorted, win, (size_t)cnt * sizeof win[0]) != 0;
                if (w.cmp_last > maxc) maxc = w.cmp_last;
            }
            printf("ex5 local fix N=%2d: mismatches=%d, max cmp/push=%u\n", N, bad, maxc);
            CHECK(bad == 0 && maxc <= (uint32_t)N);
        }
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
