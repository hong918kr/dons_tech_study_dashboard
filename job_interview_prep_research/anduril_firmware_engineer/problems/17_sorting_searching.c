// 17_sorting_searching.c — Sorting & Searching (정렬 & 탐색)
//                          · Sort/Search · PRACTICE STUB (직접 채워넣기)
// ---------------------------------------------------------------------------
// 각 함수의 '// TODO' 를 구현하고 다시 실행 -> [FAIL] 이 [PASS] 로 바뀌면 성공.
// (미구현 상태에서도 컴파일/실행은 되며 대부분 FAIL 로 뜬다.)
//
// 빌드:  cc -std=c11 -Wall -Wextra problems/17_sorting_searching.c -o /tmp/andb_17
// 실행:  /tmp/andb_17
//   또는: make prob N=17_sorting_searching
//
// 주제: 이진탐색 변종 + 고전 정렬(merge/quick/counting/insertion) + quickselect
//       + 두 배열 최근접 매칭(브루트포스 → 정렬+이진탐색 스케일업).
// ---------------------------------------------------------------------------

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>      // malloc/free/calloc
#include <stdio.h>
#include <string.h>      // 테스트 검증(memcmp) + memcpy

/* ---------------------------------------------------------------------------
 * 1. binary_search
 *   KO: 정렬된 배열 a[0..n)에서 target의 인덱스를 반환, 없으면 -1.
 *       폐구간 [lo,hi] + mid=lo+(hi-lo)/2 로 오버플로/off-by-one 방어.
 *   EN: Return index of target in sorted a, else -1. Watch off-by-one.
 *   ex: binary_search({1,3,5,7,9,11}, 6, 7) -> 3
 * ------------------------------------------------------------------------- */
int binary_search(const int *a, size_t n, int target) {
    (void)a; (void)n; (void)target;
    // TODO: implement
    return -1;
}

/* ---------------------------------------------------------------------------
 * 2. lower_bound / upper_bound  (반개구간 [lo,hi) 이진탐색)
 *   KO: lower_bound = target 이상(>=)이 처음 나오는 위치.
 *       upper_bound = target 초과(>) 가 처음 나오는 위치. 못 찾으면 n.
 *       (개수 = upper_bound - lower_bound.)
 *   EN: lower_bound = first index with a[i]>=target; upper_bound = first >.
 *   ex: a={1,2,2,2,4,5}: lower_bound(...,2)=1, upper_bound(...,2)=4
 * ------------------------------------------------------------------------- */
size_t lower_bound(const int *a, size_t n, int target) {
    (void)a; (void)n; (void)target;
    // TODO: implement
    return 0;
}
size_t upper_bound(const int *a, size_t n, int target) {
    (void)a; (void)n; (void)target;
    // TODO: implement
    return 0;
}

/* ---------------------------------------------------------------------------
 * 3. search_rotated  (회전된 정렬배열, 중복 없음)
 *   KO: 회전된 오름차순 배열에서 target 인덱스, 없으면 -1. O(log n).
 *       매 스텝 절반은 정렬돼 있음 → 그 범위에 target이 있는지로 방향 결정.
 *   EN: Search a rotated sorted array (no dups) in O(log n).
 *   ex: {4,5,6,7,0,1,2}: search_rotated(...,0) -> 4
 * ------------------------------------------------------------------------- */
int search_rotated(const int *a, size_t n, int target) {
    (void)a; (void)n; (void)target;
    // TODO: implement
    return -1;
}

/* ---------------------------------------------------------------------------
 * 4. merge_sort  (안정 정렬, O(n log n) 시간 / O(n) 보조버퍼)
 *   KO: a[0..n)를 오름차순 안정 정렬. 보조 버퍼는 한 번만 malloc해 재사용.
 *   EN: Stable merge sort in-place-ish (one aux buffer).
 *   ex: {5,2,8,1} -> {1,2,5,8}
 * ------------------------------------------------------------------------- */
void merge_sort(int *a, size_t n) {
    (void)a; (void)n;
    // TODO: implement (힌트: merge(lo,mid,hi) + 재귀 분할)
}

/* ---------------------------------------------------------------------------
 * 5. quicksort  (Lomuto 파티션, 재귀 — 평균 O(n log n))
 *   KO: a[0..n)를 오름차순 정렬. median-of-3 피벗으로 최악(이미 정렬) 완화.
 *   EN: In-place quicksort (Lomuto), median-of-3 pivot.
 *   ex: {5,2,8,1} -> {1,2,5,8}
 * ------------------------------------------------------------------------- */
void quicksort(int *a, size_t n) {
    (void)a; (void)n;
    // TODO: implement (힌트: partition 후 양쪽 재귀)
}

/* ---------------------------------------------------------------------------
 * 6. quickselect_kth  (k번째로 작은 값, 0-index: k=0이 최소)
 *   KO: 배열을 부분정렬(재배치)해 k번째로 작은 값을 O(평균 n)에 찾는다.
 *       분할 후 k가 들어있는 쪽만 재귀. k>=n 이면 INT32_MIN.
 *   EN: k-th smallest (0-indexed) via partial partition, average O(n).
 *   ex: {7,2,9,1,5,3}, k=2 -> 3   (sorted: 1,2,3,5,7,9)
 * ------------------------------------------------------------------------- */
int quickselect_kth(int *a, size_t n, size_t k) {
    (void)a; (void)n; (void)k;
    // TODO: implement
    return INT32_MIN;
}

/* ---------------------------------------------------------------------------
 * 7. counting_sort  (값이 [0, max_val]인 정수, O(n + K))
 *   KO: 비교정렬 하한(n log n)을 우회. 히스토그램 집계 후 값 순서로 펼침.
 *       max_val 음수/범위 밖 값은 방어(아무것도 안 함).
 *   EN: Counting sort for values in [0, max_val]; O(n+K).
 *   ex: {4,2,2,8,3,3,1}, max=8 -> {1,2,2,3,3,4,8}
 * ------------------------------------------------------------------------- */
void counting_sort(int *a, size_t n, int max_val) {
    (void)a; (void)n; (void)max_val;
    // TODO: implement (힌트: calloc(max_val+1) 카운트 배열)
}

/* ---------------------------------------------------------------------------
 * 8. nearest_match_two_arrays   [★ Anduril 폰스크린 기출]
 *   KO: A의 각 원소마다 B에서 가장 가까운 값을 out_nearest[i]에 담고,
 *       모든 |A[i]-nearest| 중 최댓값을 *out_max_dist로 반환.
 *       반환: 0(성공) / -1(NULL 또는 m==0).
 *       동거리(tie)면 '더 큰 값'을 선택(두 구현 결과 일치용).
 *     (a) nearest_brute: 각 A[i]마다 B 전체 스캔 → O(n*m).
 *     (b) nearest_fast : B를 정렬 후 이진탐색(lower_bound)으로 좌/우 후보만
 *                        비교 → O((n+m) log m).  [스케일업 팔로업]
 *   EN: For each A[i] find nearest value in B; also return max distance.
 *       Brute O(n*m); then sort B + binary search for O((n+m) log m).
 *   ex: A={3,10,22}, B={1,5,12,20} -> nearest={5,12,20}, max_dist=2
 * ------------------------------------------------------------------------- */
int nearest_brute(const int *a, size_t n, const int *b, size_t m,
                  int *out_nearest, int *out_max_dist) {
    (void)a; (void)n; (void)b; (void)m; (void)out_nearest; (void)out_max_dist;
    // TODO: implement (브루트포스 O(n*m))
    return -1;
}
int nearest_fast(const int *a, size_t n, const int *b, size_t m,
                 int *out_nearest, int *out_max_dist) {
    (void)a; (void)n; (void)b; (void)m; (void)out_nearest; (void)out_max_dist;
    // TODO: implement (B 복사→정렬→이진탐색, O((n+m) log m))
    return -1;
}

/* ---------------------------------------------------------------------------
 * 9. insertion_sort  (안정 정렬, 거의 정렬된/작은 n에서 빠름)
 *   KO: a[0..n)를 오름차순 안정 정렬. key를 왼쪽 정렬구간에 삽입.
 *   EN: Stable insertion sort; great for nearly-sorted / small n.
 *   ex: {5,2,8,1} -> {1,2,5,8}
 * ------------------------------------------------------------------------- */
void insertion_sort(int *a, size_t n) {
    (void)a; (void)n;
    // TODO: implement
}

// ============================================================================
// ---- Test harness (건드리지 말 것: 구현을 채우면 FAIL -> PASS) ----
// ============================================================================
static int g_pass = 0, g_fail = 0;

static void check_int(const char *call, long got, long want) {
    bool ok = (got == want);
    printf("%-54s -> %ld  %s", call, got, ok ? "[PASS]" : "[FAIL]");
    if (!ok) printf(" (expected %ld)", want);
    printf("\n");
    ok ? g_pass++ : g_fail++;
}
static void check_cond(const char *call, bool ok, const char *note) {
    printf("%-54s -> %s  %s\n", call, note, ok ? "[PASS]" : "[FAIL]");
    ok ? g_pass++ : g_fail++;
}
static void check_arr(const char *call, const int *got, const int *want, size_t n) {
    bool ok = (memcmp(got, want, n * sizeof(int)) == 0);
    printf("%-54s -> [", call);
    for (size_t i = 0; i < n; i++) printf("%s%d", i ? "," : "", got[i]);
    printf("]  %s", ok ? "[PASS]" : "[FAIL]");
    if (!ok) {
        printf(" (expected [");
        for (size_t i = 0; i < n; i++) printf("%s%d", i ? "," : "", want[i]);
        printf("])");
    }
    printf("\n");
    ok ? g_pass++ : g_fail++;
}
static bool is_sorted_arr(const int *a, size_t n) {
    for (size_t i = 1; i < n; i++) if (a[i - 1] > a[i]) return false;
    return true;
}

int main(void) {
    printf("== 1. binary_search ==\n");
    {
        int a[] = {1, 3, 5, 7, 9, 11};
        check_int("binary_search({1,3,5,7,9,11}, 7)", binary_search(a, 6, 7), 3);
        check_int("binary_search(..., 1) first",      binary_search(a, 6, 1), 0);
        check_int("binary_search(..., 11) last",      binary_search(a, 6, 11), 5);
        check_int("binary_search(..., 4) missing",    binary_search(a, 6, 4), -1);
        check_int("binary_search(..., 0) below",      binary_search(a, 6, 0), -1);
        check_int("binary_search(..., 99) above",     binary_search(a, 6, 99), -1);
        check_int("binary_search(empty)",             binary_search(a, 0, 5), -1);
        int one[] = {42};
        check_int("binary_search({42}, 42)",          binary_search(one, 1, 42), 0);
    }

    printf("\n== 2. lower_bound / upper_bound ==\n");
    {
        int a[] = {1, 2, 2, 2, 4, 5};
        check_int("lower_bound(..., 2)", (long)lower_bound(a, 6, 2), 1);
        check_int("upper_bound(..., 2)", (long)upper_bound(a, 6, 2), 4);
        check_int("count of 2 = ub-lb", (long)(upper_bound(a,6,2)-lower_bound(a,6,2)), 3);
        check_int("lower_bound(..., 3) gap", (long)lower_bound(a, 6, 3), 4);
        check_int("upper_bound(..., 3) gap", (long)upper_bound(a, 6, 3), 4);
        check_int("lower_bound(..., 0) below", (long)lower_bound(a, 6, 0), 0);
        check_int("lower_bound(..., 9) above", (long)lower_bound(a, 6, 9), 6);
        check_int("upper_bound(..., 5) last",  (long)upper_bound(a, 6, 5), 6);
    }

    printf("\n== 3. search_rotated ==\n");
    {
        int a[] = {4, 5, 6, 7, 0, 1, 2};
        check_int("search_rotated(..., 0)", search_rotated(a, 7, 0), 4);
        check_int("search_rotated(..., 4)", search_rotated(a, 7, 4), 0);
        check_int("search_rotated(..., 2)", search_rotated(a, 7, 2), 6);
        check_int("search_rotated(..., 7)", search_rotated(a, 7, 7), 3);
        check_int("search_rotated(..., 3) miss", search_rotated(a, 7, 3), -1);
        int nr[] = {1, 2, 3, 4, 5};
        check_int("search_rotated(no-rot, 4)", search_rotated(nr, 5, 4), 3);
        int one[] = {1};
        check_int("search_rotated({1}, 1)", search_rotated(one, 1, 1), 0);
        check_int("search_rotated({1}, 2) miss", search_rotated(one, 1, 2), -1);
    }

    printf("\n== 4. merge_sort (stable, O(n log n)) ==\n");
    {
        int a[] = {5, 2, 8, 1, 9, 3, 7, 4, 6, 0};
        int e[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
        merge_sort(a, 10);
        check_arr("merge_sort(10 shuffled)", a, e, 10);
        int dup[] = {3, 1, 3, 1, 2, 2};
        int de[]  = {1, 1, 2, 2, 3, 3};
        merge_sort(dup, 6);
        check_arr("merge_sort(dups)", dup, de, 6);
        int rev[] = {5, 4, 3, 2, 1};
        int re[]  = {1, 2, 3, 4, 5};
        merge_sort(rev, 5);
        check_arr("merge_sort(reversed)", rev, re, 5);
        int one[] = {7};
        merge_sort(one, 1);
        check_cond("merge_sort(n=1 no-op)", one[0] == 7, "unchanged");
    }

    printf("\n== 5. quicksort (Lomuto, median-of-3) ==\n");
    {
        int a[] = {5, 2, 8, 1, 9, 3, 7, 4, 6, 0};
        int e[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
        quicksort(a, 10);
        check_arr("quicksort(10 shuffled)", a, e, 10);
        int sorted[] = {1, 2, 3, 4, 5, 6, 7, 8};
        quicksort(sorted, 8);
        check_cond("quicksort(already sorted)", is_sorted_arr(sorted, 8), "sorted");
        int dup[] = {2, 2, 2, 2, 2};
        quicksort(dup, 5);
        check_cond("quicksort(all equal)", is_sorted_arr(dup, 5), "sorted");
        int neg[] = {3, -1, 4, -5, 0, 2};
        int ne[]  = {-5, -1, 0, 2, 3, 4};
        quicksort(neg, 6);
        check_arr("quicksort(with negatives)", neg, ne, 6);
    }

    printf("\n== 6. quickselect_kth (0-indexed) ==\n");
    {
        int a[] = {7, 2, 9, 1, 5, 3};
        check_int("quickselect_kth(k=0 min)", quickselect_kth(a, 6, 0), 1);
        int b[] = {7, 2, 9, 1, 5, 3};
        check_int("quickselect_kth(k=5 max)", quickselect_kth(b, 6, 5), 9);
        int c[] = {7, 2, 9, 1, 5, 3};
        check_int("quickselect_kth(k=2 median-ish)", quickselect_kth(c, 6, 2), 3);
        int d[] = {7, 2, 9, 1, 5, 3};
        check_int("quickselect_kth(k=3)", quickselect_kth(d, 6, 3), 5);
        int e[] = {4};
        check_int("quickselect_kth({4}, k=0)", quickselect_kth(e, 1, 0), 4);
        int f[] = {5, 5, 5, 5};
        check_int("quickselect_kth(all-equal, k=2)", quickselect_kth(f, 4, 2), 5);
    }

    printf("\n== 7. counting_sort (values in [0, max]) ==\n");
    {
        int a[] = {4, 2, 2, 8, 3, 3, 1};
        int e[] = {1, 2, 2, 3, 3, 4, 8};
        counting_sort(a, 7, 8);
        check_arr("counting_sort({4,2,2,8,3,3,1}, max=8)", a, e, 7);
        int h[] = {0, 5, 0, 5, 0, 3, 3};
        int he[]= {0, 0, 0, 3, 3, 5, 5};
        counting_sort(h, 7, 5);
        check_arr("counting_sort(histogram)", h, he, 7);
        int z[] = {0, 0, 0};
        int ze[]= {0, 0, 0};
        counting_sort(z, 3, 0);
        check_arr("counting_sort(all-zero, max=0)", z, ze, 3);
    }

    printf("\n== 8. nearest_match_two_arrays (brute + fast) ==\n");
    {
        int A[] = {3, 10, 22};
        int B[] = {1, 5, 12, 20};
        int near1[3] = {0}, near2[3] = {0};
        int md1 = -1, md2 = -1;
        int r1 = nearest_brute(A, 3, B, 4, near1, &md1);
        int r2 = nearest_fast (A, 3, B, 4, near2, &md2);
        check_int("nearest_brute return", r1, 0);
        check_int("nearest_fast return",  r2, 0);
        check_int("brute max_dist", md1, 2);
        check_int("fast  max_dist", md2, 2);
        check_cond("brute==fast nearest[]", memcmp(near1, near2, sizeof near1) == 0, "match");
        int expn[] = {5, 12, 20};
        check_arr("nearest values", near2, expn, 3);

        int A2[] = {-100, 1000};
        int B2[] = {0, 50, 100};
        int n1[2] = {0}, n2[2] = {0};
        int m1 = 0, m2 = 0;
        nearest_brute(A2, 2, B2, 3, n1, &m1);
        nearest_fast (A2, 2, B2, 3, n2, &m2);
        check_int("edge brute max_dist(1000-100)", m1, 900);
        check_int("edge fast  max_dist(1000-100)", m2, 900);
        check_cond("edge brute==fast", memcmp(n1, n2, sizeof n1) == 0, "match");

        int dummy[1] = {0}; int md = 0;
        check_int("nearest_fast(empty B) -> -1", nearest_fast(A, 3, dummy, 0, near2, &md), -1);
        check_int("nearest_brute(NULL) -> -1", nearest_brute(NULL, 3, B, 4, near1, &md1), -1);
    }

    printf("\n== 9. insertion_sort (stable, small-n) ==\n");
    {
        int a[] = {5, 2, 8, 1, 9, 3};
        int e[] = {1, 2, 3, 5, 8, 9};
        insertion_sort(a, 6);
        check_arr("insertion_sort(shuffled)", a, e, 6);
        int rev[] = {4, 3, 2, 1};
        int re[]  = {1, 2, 3, 4};
        insertion_sort(rev, 4);
        check_arr("insertion_sort(reversed)", rev, re, 4);
        int sorted[] = {1, 2, 3, 4, 5};
        insertion_sort(sorted, 5);
        check_cond("insertion_sort(already sorted)", is_sorted_arr(sorted, 5), "sorted");
    }

    // ---- summary ----
    printf("\n==================================================\n");
    printf("RESULT: %d passed, %d failed\n", g_pass, g_fail);
    printf("==================================================\n");
    return g_fail ? 1 : 0;
}
