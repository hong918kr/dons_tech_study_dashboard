/* ALGO-B05 기본 정렬 3종과 안정성: 삽입·선택·버블
 *
 * Part A: insertion_sort / selection_sort / bubble_sort(조기 종료) — (key, idx) 레코드,
 *         비교·교환·밀기·쓰기 횟수를 SortStats 에 센다.
 * Part B: is_stable_sorted (안정성 검사기), selection_sort_stable (swap 대신 회전).
 *         버그 전시: insertion_sort_le (<=), bubble_sort_ge (>=) 는 주어진다.
 * Part C: medwin_push — 최근 N개(≤32) 센서값의 정렬 창을 삽입 정렬로 유지하는 중앙값 필터.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { int16_t key; uint16_t idx; } Rec;   /* idx = 정렬 전 원래 위치 */

typedef struct {
    uint32_t cmp;     /* 키 비교 횟수 (rec_less 호출 수)            */
    uint32_t swaps;   /* 두 원소 교환 — 배열 쓰기 2번               */
    uint32_t shifts;  /* 한 칸 밀기 a[j] = a[j-1] — 배열 쓰기 1번   */
    uint32_t writes;  /* 배열 원소에 쓴 총 횟수 (플래시 마모 관점) */
} SortStats;

/* 비교는 반드시 이 함수로: 키만 본다(idx 는 안 본다). 엄격한 < 이다. */
static int rec_less(Rec x, Rec y, SortStats *st) { st->cmp++; return x.key < y.key; }
static void rec_swap(Rec *a, int i, int j, SortStats *st) {
    Rec t = a[i]; a[i] = a[j]; a[j] = t;
    st->swaps++; st->writes += 2;
}

/* ---------------- Part A: 정렬 3종 ---------------- */

/* 삽입 정렬: x 를 꺼내 두고, 앞쪽의 "x 보다 큰" 원소를 한 칸씩 민 뒤 빈 자리에 넣는다.
 * 밀 때마다 shifts++, writes++. 마지막 a[j] = x 는 j != i 일 때만 쓰고 writes++. */
static void insertion_sort(Rec *a, int n, SortStats *st) {
    for (int i = 1; i < n; i++) {
        Rec x = a[i];
        int j = i;
        while (j > 0 && rec_less(x, a[j - 1], st)) {   /* 같으면 멈춘다 → 안정 */
            a[j] = a[j - 1];
            st->shifts++; st->writes++;
            j--;
        }
        if (j != i) { a[j] = x; st->writes++; }
    }
}

/* 선택 정렬: [i, n) 의 최솟값(같은 값이면 첫 번째)을 찾아 a[i] 와 교환. m == i 면 교환 안 함. */
static void selection_sort(Rec *a, int n, SortStats *st) {
    for (int i = 0; i + 1 < n; i++) {
        int m = i;
        for (int j = i + 1; j < n; j++)
            if (rec_less(a[j], a[m], st)) m = j;
        if (m != i) rec_swap(a, i, m, st);
    }
}

/* 버블 정렬: 인접 쌍이 뒤집혀 있으면(a[j+1] < a[j]) 교환. 한 패스에 교환이 0번이면 끝. */
static void bubble_sort(Rec *a, int n, SortStats *st) {
    for (int pass = 0; pass + 1 < n; pass++) {
        int swapped = 0;
        for (int j = 0; j + 1 < n - pass; j++)
            if (rec_less(a[j + 1], a[j], st)) { rec_swap(a, j, j + 1, st); swapped = 1; }
        if (!swapped) break;
    }
}

/* ---------------- Part B: 안정성 ---------------- */

/* 키 오름차순이고, 같은 키끼리는 idx 가 오름차순이면 1. (빈 배열·1개는 1) */
static int is_stable_sorted(const Rec *a, int n) {
    for (int i = 1; i < n; i++) {
        if (a[i - 1].key > a[i].key) return 0;
        if (a[i - 1].key == a[i].key && a[i - 1].idx > a[i].idx) return 0;
    }
    return 1;
}

/* 안정 선택 정렬: 최솟값을 교환하지 말고, [i, m) 를 한 칸씩 오른쪽으로 밀고 a[i] 에 넣는다. */
static void selection_sort_stable(Rec *a, int n, SortStats *st) {
    for (int i = 0; i + 1 < n; i++) {
        int m = i;
        for (int j = i + 1; j < n; j++)
            if (rec_less(a[j], a[m], st)) m = j;          /* 같은 키 중 첫 번째 */
        if (m != i) {
            Rec x = a[m];
            for (int k = m; k > i; k--) { a[k] = a[k - 1]; st->shifts++; st->writes++; }
            a[i] = x; st->writes++;
        }
    }
}

/* 버그 전시 1 (주어짐): 삽입 정렬의 조건을 "x <= a[j-1]" 로 쓴 판. 같은 키도 넘어간다. */
static void insertion_sort_le(Rec *a, int n, SortStats *st) {
    for (int i = 1; i < n; i++) {
        Rec x = a[i];
        int j = i;
        while (j > 0 && !rec_less(a[j - 1], x, st)) {   /* !(a[j-1] < x)  ==  x <= a[j-1] */
            a[j] = a[j - 1]; st->shifts++; st->writes++;
            j--;
        }
        if (j != i) { a[j] = x; st->writes++; }
    }
}

/* 버그 전시 2 (주어짐): 버블 정렬의 조건을 "a[j] >= a[j+1]" 로 쓴 판. 같은 키도 교환한다. */
static void bubble_sort_ge(Rec *a, int n, SortStats *st) {
    for (int pass = 0; pass + 1 < n; pass++) {
        int swapped = 0;
        for (int j = 0; j + 1 < n - pass; j++)
            if (!rec_less(a[j], a[j + 1], st)) { rec_swap(a, j, j + 1, st); swapped = 1; }
        if (!swapped) break;
    }
}

/* ---------------- Part C: 중앙값 필터 창 ---------------- */
#define MW_MAX 32
typedef struct {
    int16_t ring[MW_MAX];    /* 도착 순서. 창이 차면 ring[head] 가 가장 오래된 값 */
    int16_t sorted[MW_MAX];  /* 같은 값들을 오름차순으로 유지                     */
    int n, head, count;
    uint32_t cmp_last;       /* 마지막 push 에서 삽입 정렬이 한 키 비교 횟수      */
} MedWin;

static void medwin_init(MedWin *w, int n) { memset(w, 0, sizeof *w); w->n = n; }

/* 새 샘플 x 를 넣고 현재 창의 중앙값(짝수 개면 아래쪽 중앙값 sorted[(count-1)/2])을 돌려준다.
 * 창이 찼으면 가장 오래된 값을 sorted 에서 찾아 x 로 덮어쓰고, sorted 전체에 삽입 정렬을 돌린다
 * (한 원소만 틀린 "거의 정렬된" 배열 → 비교 ≤ 2·count-3). cmp_last 에 비교 횟수를 남긴다. */
static int16_t medwin_push(MedWin *w, int16_t x) {
    if (w->count < w->n) {
        w->sorted[w->count++] = x;
    } else {
        int16_t old = w->ring[w->head];
        int p = 0;
        while (p < w->count - 1 && w->sorted[p] != old) p++;
        w->sorted[p] = x;
    }
    w->ring[w->head] = x;
    w->head = (w->head + 1 == w->n) ? 0 : w->head + 1;

    uint32_t cmp = 0;
    for (int i = 1; i < w->count; i++) {
        int16_t v = w->sorted[i];
        int j = i;
        while (j > 0) {
            cmp++;
            if (!(v < w->sorted[j - 1])) break;
            w->sorted[j] = w->sorted[j - 1];
            j--;
        }
        w->sorted[j] = v;
    }
    w->cmp_last = cmp;
    return w->sorted[(w->count - 1) / 2];
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t g_rng = 0x2545F491u;
static uint32_t xs32(void) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }

static void make_recs(Rec *a, int n, int keyrange) {
    for (int i = 0; i < n; i++) { a[i].key = (int16_t)(xs32() % (uint32_t)keyrange); a[i].idx = (uint16_t)i; }
}
static void from_keys(Rec *a, const int *k, int n) {
    for (int i = 0; i < n; i++) { a[i].key = (int16_t)k[i]; a[i].idx = (uint16_t)i; }
}
static int cmp_key_idx(const void *p, const void *q) {   /* 기준: (key, idx) 사전순 = 유일한 안정 결과 */
    const Rec *x = p, *y = q;
    if (x->key != y->key) return x->key < y->key ? -1 : 1;
    return (x->idx > y->idx) - (x->idx < y->idx);
}
static void ref_sort(Rec *out, const Rec *in, int n) { memcpy(out, in, (size_t)n * sizeof *in); qsort(out, (size_t)n, sizeof *out, cmp_key_idx); }
static int same_recs(const Rec *a, const Rec *b, int n) {
    for (int i = 0; i < n; i++) if (a[i].key != b[i].key || a[i].idx != b[i].idx) return 0;
    return 1;
}
/* 키는 기준과 같고, idx 는 0..n-1 의 순열이며, 각 idx 가 원래 키를 그대로 들고 있는가 */
static int sorted_permutation(const Rec *out, const Rec *in, const Rec *ref, int n) {
    unsigned char seen[1100] = {0};
    for (int i = 0; i < n; i++) {
        if (out[i].key != ref[i].key || out[i].idx >= n || seen[out[i].idx]) return 0;
        if (in[out[i].idx].key != out[i].key) return 0;
        seen[out[i].idx] = 1;
    }
    return 1;
}
/* 공식 검증용 (테스트 쪽, O(n^2) 무식한 계산) */
static uint32_t inversions(const Rec *a, int n) {           /* i<j 이고 a[i].key > a[j].key 인 쌍 수 */
    uint32_t c = 0;
    for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) c += a[i].key > a[j].key;
    return c;
}
static uint32_t new_prefix_min(const Rec *a, int n) {       /* i>=1 이고 a[i].key < min(a[0..i-1]) 인 i 개수 */
    uint32_t c = 0;
    for (int i = 1, m = n ? a[0].key : 0; i < n; i++) if (a[i].key < m) { c++; m = a[i].key; }
    return c;
}
static uint32_t max_left_greater(const Rec *a, int n) {     /* max_i #{ j<i : a[j].key > a[i].key } */
    uint32_t best = 0;
    for (int i = 0; i < n; i++) {
        uint32_t c = 0;
        for (int j = 0; j < i; j++) c += a[j].key > a[i].key;
        if (c > best) best = c;
    }
    return best;
}
static uint32_t bubble_cmp_formula(int n, uint32_t maxL) {  /* 패스 P = min(n-1, maxL+1), 비교 = Σ_{p<P}(n-1-p) */
    if (n < 2) return 0;
    uint32_t P = maxL + 1 < (uint32_t)(n - 1) ? maxL + 1 : (uint32_t)(n - 1);
    return P * (uint32_t)(n - 1) - P * (P - 1) / 2;
}

typedef void (*SortFn)(Rec *, int, SortStats *);

int main(void) {
    Rec in[1024], a[1024], ref[1024];
    SortStats st;
    const SortFn fns[3] = { insertion_sort, selection_sort, bubble_sort };
    const char *names[3] = { "insertion", "selection", "bubble" };

    /* ---- Part A: 이름 붙은 경계 사례 ---- */
    static const int e_two[] = { 2, 1 }, e_eq[] = { 7, 7, 7, 7, 7 }, e_sorted[] = { 1, 2, 3, 4, 5, 6 },
                     e_rev[] = { 6, 5, 4, 3, 2, 1 }, e_dup[] = { 3, 1, 3, 2, 1 },
                     e_ext[] = { 32767, -32768, 0, -32768, 32767, -1 };
    const int *cases[] = { e_two, e_two, e_eq, e_sorted, e_rev, e_dup, e_ext };
    const int lens[]   = { 0, 1, 5, 6, 6, 5, 6 };   /* 0개 · 1개는 e_two 의 앞부분 */
    for (int f = 0; f < 3; f++) {
        int bad = 0;
        for (int c = 0; c < 7; c++) {
            from_keys(in, cases[c], lens[c]);
            memcpy(a, in, sizeof(Rec) * (size_t)lens[c]);
            ref_sort(ref, in, lens[c]);
            memset(&st, 0, sizeof st);
            fns[f](a, lens[c], &st);
            int ok = (f == 1) ? sorted_permutation(a, in, ref, lens[c]) : same_recs(a, ref, lens[c]);
            if (!ok) { bad++; printf("  %s: edge case %d wrong\n", names[f], c); }
        }
        CHECK(bad == 0);
    }
    from_keys(in, e_two, 2); memcpy(a, in, sizeof(Rec) * 2); memset(&st, 0, sizeof st);
    insertion_sort(a, 2, &st);
    CHECK(a[0].key == 1 && a[1].key == 2 && st.cmp == 1 && st.shifts == 1 && st.writes == 2);

    /* 정렬된 입력: 삽입·버블은 n-1 비교로 끝(적응형), 선택은 그래도 n(n-1)/2 */
    from_keys(in, e_sorted, 6);
    for (int f = 0; f < 3; f++) {
        memcpy(a, in, sizeof(Rec) * 6); memset(&st, 0, sizeof st);
        fns[f](a, 6, &st);
        CHECK(st.cmp == (f == 1 ? 15u : 5u) && st.writes == 0);
    }

    /* ---- Part A: 무작위 대조 + 횟수 공식 검증 (n = 0..64, 키 범위 4 / 1000) ---- */
    {
        int bad[3] = {0, 0, 0}, badf[3] = {0, 0, 0};
        for (int t = 0; t < 600; t++) {
            int n = (int)(xs32() % 65u);
            make_recs(in, n, (t & 1) ? 4 : 1000);
            ref_sort(ref, in, n);
            uint32_t inv = inversions(in, n), z = new_prefix_min(in, n), L = max_left_greater(in, n);
            uint32_t nn = (uint32_t)n;
            for (int f = 0; f < 3; f++) {
                memcpy(a, in, sizeof(Rec) * (size_t)n); memset(&st, 0, sizeof st);
                fns[f](a, n, &st);
                int ok = (f == 1) ? sorted_permutation(a, in, ref, n) : same_recs(a, ref, n);
                if (!ok) bad[f]++;
                int fo = 1;
                if (f == 0) fo = st.shifts == inv && st.swaps == 0 && (n == 0 || st.cmp == inv + nn - 1 - z);
                if (f == 1) fo = st.cmp == nn * (nn ? nn - 1 : 0) / 2 && st.swaps <= (nn ? nn - 1 : 0) && st.writes == 2 * st.swaps;
                if (f == 2) fo = st.swaps == inv && st.writes == 2 * inv && st.cmp == bubble_cmp_formula(n, L);
                if (!fo) badf[f]++;
            }
        }
        printf("random 600 arrays: sort mismatches ins=%d sel=%d bub=%d, formula mismatches ins=%d sel=%d bub=%d\n",
               bad[0], bad[1], bad[2], badf[0], badf[1], badf[2]);
        CHECK(bad[0] == 0); CHECK(bad[1] == 0); CHECK(bad[2] == 0);
        CHECK(badf[0] == 0); CHECK(badf[1] == 0); CHECK(badf[2] == 0);
    }

    /* ---- Part B: 안정성 검사기 ---- */
    {
        Rec s1[] = { {1, 1}, {1, 4}, {2, 3}, {3, 0}, {3, 2} };
        Rec s2[] = { {1, 1}, {1, 4}, {2, 3}, {3, 2}, {3, 0} };   /* 같은 키 3 의 순서가 뒤집힘 */
        Rec s3[] = { {2, 0}, {1, 1} };                           /* 정렬 자체가 안 됨 */
        CHECK(is_stable_sorted(s1, 5) == 1);
        CHECK(is_stable_sorted(s2, 5) == 0);
        CHECK(is_stable_sorted(s3, 2) == 0);
        CHECK(is_stable_sorted(s1, 0) == 1 && is_stable_sorted(s3, 1) == 1);
    }
    /* 선택 정렬의 최소 반례: [(2,0), (2,1), (1,2)] → [(1,2), (2,1), (2,0)] */
    {
        static const int k[] = { 2, 2, 1 };
        from_keys(a, k, 3); memset(&st, 0, sizeof st);
        selection_sort(a, 3, &st);
        CHECK(a[0].idx == 2 && a[1].idx == 1 && a[2].idx == 0 && is_stable_sorted(a, 3) == 0);
        from_keys(a, k, 3); memset(&st, 0, sizeof st);
        insertion_sort(a, 3, &st);
        CHECK(is_stable_sorted(a, 3) == 1);
        from_keys(a, k, 3); memset(&st, 0, sizeof st);
        bubble_sort(a, 3, &st);
        CHECK(is_stable_sorted(a, 3) == 1);
        from_keys(a, k, 3); memset(&st, 0, sizeof st);
        selection_sort_stable(a, 3, &st);
        CHECK(a[0].idx == 2 && a[1].idx == 0 && a[2].idx == 1);
    }
    /* <= / >= 버그: 같은 키 두 개만으로 안정성이 깨진다 */
    {
        static const int k[] = { 5, 5 };
        from_keys(a, k, 2); memset(&st, 0, sizeof st);
        insertion_sort_le(a, 2, &st);
        CHECK(a[0].idx == 1 && is_stable_sorted(a, 2) == 0);
        /* 버블 >= : 전부 같은 키 8개 → 매 패스 교환이 생겨 조기 종료 불가, 비교 = 8*7/2 */
        static const int k8[] = { 4, 4, 4, 4, 4, 4, 4, 4 };
        from_keys(a, k8, 8); memset(&st, 0, sizeof st);
        bubble_sort_ge(a, 8, &st);
        CHECK(st.cmp == 28u && is_stable_sorted(a, 8) == 0);
        from_keys(a, k8, 8); memset(&st, 0, sizeof st);
        bubble_sort(a, 8, &st);
        CHECK(st.cmp == 7u && st.swaps == 0u);
    }
    /* 무작위: 안정 선택 정렬 == 기준, 그리고 is_stable_sorted 가 세 정렬을 올바르게 판정 */
    {
        int bad = 0, sel_unstable = 0, judge_bad = 0;
        for (int t = 0; t < 400; t++) {
            int n = (int)(xs32() % 41u);
            make_recs(in, n, 5);
            ref_sort(ref, in, n);
            memcpy(a, in, sizeof(Rec) * (size_t)n); memset(&st, 0, sizeof st);
            selection_sort_stable(a, n, &st);
            if (!same_recs(a, ref, n)) bad++;
            memcpy(a, in, sizeof(Rec) * (size_t)n); memset(&st, 0, sizeof st);
            selection_sort(a, n, &st);
            int stable = is_stable_sorted(a, n);
            if (stable != same_recs(a, ref, n)) judge_bad++;
            sel_unstable += !stable;
        }
        printf("random 400 arrays (keys 0..4): stable-selection mismatches=%d, plain selection unstable in %d/400\n",
               bad, sel_unstable);
        CHECK(bad == 0);
        CHECK(judge_bad == 0);
        CHECK(sel_unstable > 0);
    }

    /* ---- Part C: 중앙값 필터 ---- */
    {
        const int Ns[] = { 1, 2, 9, 32 };
        for (int q = 0; q < 4; q++) {
            int N = Ns[q], bad = 0;
            uint32_t maxcmp = 0;
            MedWin w; medwin_init(&w, N);
            int16_t hist[6000];
            for (int t = 0; t < 6000; t++) {
                int16_t x = (int16_t)(2048 + (int)(xs32() % 41u) - 20);   /* 12비트 ADC 근처 잡음 */
                if (xs32() % 50u == 0) x = (xs32() & 1u) ? 4095 : 0;       /* 가끔 튀는 값(스파이크) */
                if (t % 1000 == 999) x = INT16_MIN;                         /* 극값도 */
                hist[t] = x;
                int16_t med = medwin_push(&w, x);
                int cnt = t + 1 < N ? t + 1 : N;
                int16_t win[MW_MAX];
                for (int i = 0; i < cnt; i++) win[i] = hist[t - cnt + 1 + i];
                for (int i = 1; i < cnt; i++)                       /* 기준: 창을 통째로 정렬 */
                    for (int j = i; j > 0 && win[j] < win[j - 1]; j--) { int16_t s = win[j]; win[j] = win[j - 1]; win[j - 1] = s; }
                if (med != win[(cnt - 1) / 2] || memcmp(w.sorted, win, (size_t)cnt * sizeof win[0]) != 0) bad++;
                if (w.cmp_last > maxcmp) maxcmp = w.cmp_last;
            }
            int bound = N >= 2 ? 2 * N - 3 : 0;          /* 한 원소만 틀린 배열의 삽입 정렬 최악 */
            printf("median N=%2d: 6000 samples, mismatches=%d, max cmp/push=%u (bound 2N-3=%d)\n",
                   N, bad, maxcmp, bound);
            CHECK(bad == 0 && maxcmp <= (uint32_t)bound);
        }
        /* 스파이크 제거: 기저 1000 에 4연속 스파이크(N=9 의 절반 미만)는 출력에 안 나온다 */
        MedWin w; medwin_init(&w, 9);
        int16_t worst = 1000;
        for (int t = 0; t < 60; t++) {
            int16_t x = (t >= 30 && t < 34) ? 4095 : 1000;
            int16_t m = medwin_push(&w, x);
            if (m > worst) worst = m;
        }
        CHECK(worst == 1000);
    }
    /* Part C-2: EEPROM 에 있는 32개 레코드를 제자리 정렬할 때 쓰기 횟수 (선택 ≤ 2(n-1)) */
    {
        make_recs(in, 32, 30000);
        SortStats s3[3];
        for (int f = 0; f < 3; f++) {
            memcpy(a, in, sizeof(Rec) * 32); memset(&s3[f], 0, sizeof s3[f]);
            fns[f](a, 32, &s3[f]);
        }
        printf("EEPROM n=32 random: writes ins=%u sel=%u bub=%u\n", s3[0].writes, s3[1].writes, s3[2].writes);
        CHECK(s3[1].writes <= 62u && s3[1].writes < s3[0].writes && s3[0].writes < s3[2].writes);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
