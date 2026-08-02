// 17_sorting_searching.c — Sorting & Searching (정렬 & 탐색)
//                          · Sort/Search · REFERENCE SOLUTION
// ---------------------------------------------------------------------------
// 이진탐색 변종(binary search / lower·upper bound / 회전배열) + 고전 정렬
// (merge / quick / counting / insertion) + quickselect(k번째) + 두 배열
// 최근접 매칭(브루트포스 → 정렬+이진탐색 스케일업). Anduril 폰스크린 단골.
//
// 빌드:  cc -std=c11 -Wall -Wextra solutions/17_sorting_searching.c -o /tmp/andb_17
// 실행:  /tmp/andb_17
// ---------------------------------------------------------------------------

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>      // malloc/free (count 배열, 정렬 복사본)
#include <stdio.h>
#include <string.h>      // 테스트 검증(memcmp) + memcpy

// ============================================================================
// 1. binary_search — 정렬된 배열에서 target 의 인덱스, 없으면 -1.
//    off-by-one 의 정석: [lo, hi] 폐구간, mid=lo+(hi-lo)/2 로 오버플로 회피.
// ============================================================================
int binary_search(const int *a, size_t n, int target) {
    if (!a || n == 0) return -1;
    size_t lo = 0, hi = n - 1;                 // 폐구간 [lo, hi]
    while (lo <= hi) {                          // lo==hi 도 검사해야 함
        size_t mid = lo + (hi - lo) / 2;        // (lo+hi) 오버플로 회피
        if (a[mid] == target) return (int)mid;
        if (a[mid] < target) lo = mid + 1;
        else {
            if (mid == 0) break;                // size_t 언더플로 방어
            hi = mid - 1;
        }
    }
    return -1;
}

// ============================================================================
// 2. lower_bound / upper_bound — 반개구간 [lo, hi) 이진탐색.
//    lower_bound: target 이상(>=)이 처음 나오는 위치.
//    upper_bound: target 초과(>) 가 처음 나오는 위치.
//    둘 다 [0, n] 범위를 반환(못 찾으면 n). 중복값·삽입점 계산의 기본기.
// ============================================================================
size_t lower_bound(const int *a, size_t n, int target) {
    size_t lo = 0, hi = n;                      // 반개구간 [lo, hi)
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (a[mid] < target) lo = mid + 1;      // mid 는 답이 될 수 없음
        else hi = mid;                          // mid 가 후보(>=target)
    }
    return lo;                                   // == 첫 >= target 위치(없으면 n)
}
size_t upper_bound(const int *a, size_t n, int target) {
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (a[mid] <= target) lo = mid + 1;     // <=target 은 답이 될 수 없음
        else hi = mid;                          // >target 후보
    }
    return lo;                                   // == 첫 > target 위치(없으면 n)
}

// ============================================================================
// 3. search_rotated — 회전된 정렬배열에서 target 인덱스, 없으면 -1.
//    중복 없음 가정. 매 스텝 절반은 반드시 "정렬돼 있다" → 그쪽에 target 이
//    들어있는지 범위로 판정해 탐색 구간을 반으로 줄인다. O(log n).
// ============================================================================
int search_rotated(const int *a, size_t n, int target) {
    if (!a || n == 0) return -1;
    size_t lo = 0, hi = n - 1;
    while (lo <= hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (a[mid] == target) return (int)mid;
        if (a[lo] <= a[mid]) {                   // 왼쪽 절반이 정렬됨
            if (a[lo] <= target && target < a[mid]) {
                if (mid == 0) break;
                hi = mid - 1;                    // 왼쪽 정렬구간 안에 있음
            } else lo = mid + 1;
        } else {                                 // 오른쪽 절반이 정렬됨
            if (a[mid] < target && target <= a[hi]) lo = mid + 1;
            else {
                if (mid == 0) break;
                hi = mid - 1;
            }
        }
    }
    return -1;
}

// ============================================================================
// 4. merge_sort — 안정 정렬(stable), O(n log n) 시간, O(n) 보조 버퍼.
//    보조 버퍼를 한 번만 malloc 해 재귀에서 재사용. 병합이 핵심.
// ============================================================================
static void merge_run(int *a, int *tmp, size_t lo, size_t mid, size_t hi) {
    size_t i = lo, j = mid, k = lo;
    while (i < mid && j < hi) {                  // <= 로 왼쪽 우선 → 안정성
        if (a[i] <= a[j]) tmp[k++] = a[i++];
        else tmp[k++] = a[j++];
    }
    while (i < mid) tmp[k++] = a[i++];
    while (j < hi)  tmp[k++] = a[j++];
    for (size_t t = lo; t < hi; t++) a[t] = tmp[t];
}
static void merge_rec(int *a, int *tmp, size_t lo, size_t hi) {
    if (hi - lo < 2) return;                     // 원소 1개 이하면 정렬됨
    size_t mid = lo + (hi - lo) / 2;
    merge_rec(a, tmp, lo, mid);
    merge_rec(a, tmp, mid, hi);
    merge_run(a, tmp, lo, mid, hi);
}
void merge_sort(int *a, size_t n) {
    if (!a || n < 2) return;
    int *tmp = malloc(n * sizeof *tmp);
    if (!tmp) return;                            // 메모리 부족: 원본 보존
    merge_rec(a, tmp, 0, n);                     // 반개구간 [0, n)
    free(tmp);
}

// ============================================================================
// 5. quicksort — Lomuto 파티션, 재귀. 평균 O(n log n), 최악 O(n^2).
//    중앙값-of-3 피벗으로 이미 정렬된 입력의 최악을 완화.
// ============================================================================
static void swap_int(int *x, int *y) { int t = *x; *x = *y; *y = t; }

// [lo, hi] 폐구간을 pivot=a[hi] 기준 분할, 최종 피벗 위치 반환(Lomuto).
static size_t lomuto_partition(int *a, size_t lo, size_t hi) {
    // median-of-3: a[lo], a[mid], a[hi] 의 중앙값을 hi 로 보냄
    size_t mid = lo + (hi - lo) / 2;
    if (a[mid] < a[lo]) swap_int(&a[mid], &a[lo]);
    if (a[hi]  < a[lo]) swap_int(&a[hi],  &a[lo]);
    if (a[hi]  < a[mid]) swap_int(&a[hi], &a[mid]);
    swap_int(&a[mid], &a[hi]);                   // 중앙값을 피벗 자리(hi)로
    int pivot = a[hi];
    size_t i = lo;                               // a[lo..i) < pivot 유지
    for (size_t j = lo; j < hi; j++)
        if (a[j] < pivot) swap_int(&a[i++], &a[j]);
    swap_int(&a[i], &a[hi]);                      // 피벗을 제자리로
    return i;
}
static void quick_rec(int *a, size_t lo, size_t hi) {   // 폐구간 [lo, hi]
    while (lo < hi) {
        size_t p = lomuto_partition(a, lo, hi);
        if (p - lo < hi - p) {                   // 작은 쪽만 재귀 → 스택 O(log n)
            if (p > lo) quick_rec(a, lo, p - 1);
            lo = p + 1;
        } else {
            if (hi > p) quick_rec(a, p + 1, hi);
            if (p == 0) break;
            hi = p - 1;
        }
    }
}
void quicksort(int *a, size_t n) {
    if (!a || n < 2) return;
    quick_rec(a, 0, n - 1);
}

// ============================================================================
// 6. quickselect_kth — k번째로 작은 값(0-index: k=0 이 최소). 평균 O(n).
//    quicksort 처럼 분할하되 목표 k 가 들어있는 쪽만 재귀. 배열은 재배치됨.
//    범위를 벗어난 k 는 INT32_MIN sentinel 반환(테스트에선 유효 k 만 사용).
// ============================================================================
int quickselect_kth(int *a, size_t n, size_t k) {
    if (!a || k >= n) return INT32_MIN;          // 잘못된 k
    size_t lo = 0, hi = n - 1;                    // 폐구간
    while (lo < hi) {
        size_t p = lomuto_partition(a, lo, hi);
        if (p == k) return a[p];                  // 피벗이 정확히 k번째
        if (k < p) {
            if (p == 0) break;
            hi = p - 1;                           // 왼쪽만
        } else lo = p + 1;                        // 오른쪽만
    }
    return a[k];                                   // lo==hi==k 로 수렴
}

// ============================================================================
// 7. counting_sort — 값이 [0, max_val] 인 정수 배열을 O(n + K) 로 정렬.
//    비교정렬 하한(n log n)을 우회. 사실상 히스토그램 누적. 안정 정렬 가능.
//    max_val 음수/과대 시 아무것도 안 함(방어).
// ============================================================================
void counting_sort(int *a, size_t n, int max_val) {
    if (!a || n < 2 || max_val < 0) return;
    size_t K = (size_t)max_val + 1;
    size_t *cnt = calloc(K, sizeof *cnt);         // 0으로 초기화
    if (!cnt) return;
    for (size_t i = 0; i < n; i++) {              // 히스토그램 집계
        if (a[i] < 0 || a[i] > max_val) { free(cnt); return; }  // 범위 밖
        cnt[a[i]]++;
    }
    size_t o = 0;
    for (size_t v = 0; v < K; v++)                // 값 오름차순으로 펼침
        for (size_t c = 0; c < cnt[v]; c++) a[o++] = (int)v;
    free(cnt);
}

// ============================================================================
// 8. nearest_match_two_arrays — [핵심/Anduril 폰스크린 기출]
//    A 의 각 원소마다 B 에서 가장 가까운 값을 찾아 out_nearest[i] 에 담고,
//    모든 |A[i]-nearest| 중 최댓값을 *out_max_dist 로 돌려준다.
//    반환: 0(성공) / -1(입력 오류: NULL 또는 B 가 비어있음).
//
//    (a) 브루트포스: 각 A[i] 마다 B 전체 스캔 → O(n*m).
//    (b) 스케일업 팔로업: B 를 정렬(O(m log m)) 후 각 A[i] 를 이진탐색으로
//        삽입점(lower_bound) 을 찾고 그 좌/우 후보만 비교 → O((n+m) log m).
//        n,m 이 커질수록(수천 표적×수천 트랙) 이 최적화가 결정적.
// ============================================================================
static long labs_l(long x) { return x < 0 ? -x : x; }

int nearest_brute(const int *a, size_t n, const int *b, size_t m,
                  int *out_nearest, int *out_max_dist) {
    if (!a || !b || !out_nearest || !out_max_dist || m == 0) return -1;
    long max_dist = 0;
    for (size_t i = 0; i < n; i++) {
        int best = b[0];
        long bestd = labs_l((long)a[i] - b[0]);
        for (size_t j = 1; j < m; j++) {          // B 전체 선형 스캔
            long d = labs_l((long)a[i] - b[j]);
            // 동거리면 더 큰 값을 선택 → 정렬+이진탐색 버전과 타이브레이크 일치
            if (d < bestd || (d == bestd && b[j] > best)) { bestd = d; best = b[j]; }
        }
        out_nearest[i] = best;
        if (bestd > max_dist) max_dist = bestd;   // 전체 최대 거리 갱신
    }
    *out_max_dist = (int)max_dist;
    return 0;
}

// 정렬된 s[0..len) 에서 x 이상이 처음 나오는 위치(lower_bound 재사용 형태).
static size_t lb_sorted(const int *s, size_t len, int x) {
    size_t lo = 0, hi = len;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (s[mid] < x) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

int nearest_fast(const int *a, size_t n, const int *b, size_t m,
                 int *out_nearest, int *out_max_dist) {
    if (!a || !b || !out_nearest || !out_max_dist || m == 0) return -1;
    int *sb = malloc(m * sizeof *sb);             // B 를 건드리지 않도록 복사
    if (!sb) return -1;
    memcpy(sb, b, m * sizeof *sb);
    quicksort(sb, m);                             // O(m log m)

    long max_dist = 0;
    for (size_t i = 0; i < n; i++) {
        size_t pos = lb_sorted(sb, m, a[i]);      // 삽입점 이진탐색 O(log m)
        int best; long bestd;
        if (pos == m) {                           // 모든 값이 a[i] 미만
            best = sb[m - 1];
            bestd = labs_l((long)a[i] - best);
        } else {
            best = sb[pos];                        // 우측 후보(>= a[i])
            bestd = labs_l((long)a[i] - best);
            if (pos > 0) {                         // 좌측 후보(< a[i]) 도 비교
                long ld = labs_l((long)a[i] - sb[pos - 1]);
                if (ld < bestd) { bestd = ld; best = sb[pos - 1]; }
            }
        }
        out_nearest[i] = best;
        if (bestd > max_dist) max_dist = bestd;
    }
    *out_max_dist = (int)max_dist;
    free(sb);
    return 0;
}

// ============================================================================
// 9. insertion_sort — 안정 정렬, O(n^2) 최악이지만 거의 정렬된/작은 n 에서 빠름.
//    많은 라이브러리가 quicksort 재귀 바닥에서 insertion 으로 전환한다.
// ============================================================================
void insertion_sort(int *a, size_t n) {
    if (!a || n < 2) return;
    for (size_t i = 1; i < n; i++) {
        int key = a[i];
        size_t j = i;                             // a[0..i) 는 이미 정렬됨
        while (j > 0 && a[j - 1] > key) {         // key 자리 만들기(우측 시프트)
            a[j] = a[j - 1];
            j--;
        }
        a[j] = key;
    }
}

// ============================================================================
// ---- Test harness (PASS/FAIL) ----
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
        int a[] = {1, 2, 2, 2, 4, 5};             // 중복 2 세 개
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
        int a[] = {4, 5, 6, 7, 0, 1, 2};          // pivot=4 회전
        check_int("search_rotated(..., 0)", search_rotated(a, 7, 0), 4);
        check_int("search_rotated(..., 4)", search_rotated(a, 7, 4), 0);
        check_int("search_rotated(..., 2)", search_rotated(a, 7, 2), 6);
        check_int("search_rotated(..., 7)", search_rotated(a, 7, 7), 3);
        check_int("search_rotated(..., 3) miss", search_rotated(a, 7, 3), -1);
        int nr[] = {1, 2, 3, 4, 5};               // 회전 안 됨
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
        int sorted[] = {1, 2, 3, 4, 5, 6, 7, 8};   // 최악 유발 입력
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
        int a[] = {7, 2, 9, 1, 5, 3};              // sorted: 1,2,3,5,7,9
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
        int h[] = {0, 5, 0, 5, 0, 3, 3};           // 히스토그램 스타일
        int he[]= {0, 0, 0, 3, 3, 5, 5};
        counting_sort(h, 7, 5);
        check_arr("counting_sort(histogram)", h, he, 7);
        int z[] = {0, 0, 0};
        int ze[]= {0, 0, 0};
        counting_sort(z, 3, 0);                    // max=0: 전부 0
        check_arr("counting_sort(all-zero, max=0)", z, ze, 3);
    }

    printf("\n== 8. nearest_match_two_arrays (brute + fast) ==\n");
    {
        int A[] = {3, 10, 22};
        int B[] = {1, 5, 12, 20};
        // 3->1|5 (거리2 둘 다, lower_bound 는 5 선택) ; 10->12(2) ; 22->20(2)
        int near1[3] = {0}, near2[3] = {0};
        int md1 = -1, md2 = -1;
        int r1 = nearest_brute(A, 3, B, 4, near1, &md1);
        int r2 = nearest_fast (A, 3, B, 4, near2, &md2);
        check_int("nearest_brute return", r1, 0);
        check_int("nearest_fast return",  r2, 0);
        check_int("brute max_dist", md1, 2);
        check_int("fast  max_dist", md2, 2);
        check_cond("brute==fast nearest[]", memcmp(near1, near2, sizeof near1) == 0, "match");
        // 명시적 최근접 값 확인(동거리는 상단/우측 후보 선택으로 결정적)
        int expn[] = {5, 12, 20};
        check_arr("nearest values", near2, expn, 3);

        // 경계: A 값이 B 최소보다 작고 최대보다 큼
        int A2[] = {-100, 1000};
        int B2[] = {0, 50, 100};
        int n1[2] = {0}, n2[2] = {0};
        int m1 = 0, m2 = 0;
        nearest_brute(A2, 2, B2, 3, n1, &m1);
        nearest_fast (A2, 2, B2, 3, n2, &m2);
        check_int("edge brute max_dist(1000-100)", m1, 900);
        check_int("edge fast  max_dist(1000-100)", m2, 900);
        check_cond("edge brute==fast", memcmp(n1, n2, sizeof n1) == 0, "match");

        // 오류: B 비어있음
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
        int sorted[] = {1, 2, 3, 4, 5};            // best case O(n)
        insertion_sort(sorted, 5);
        check_cond("insertion_sort(already sorted)", is_sorted_arr(sorted, 5), "sorted");
    }

    // ---- summary ----
    printf("\n==================================================\n");
    printf("RESULT: %d passed, %d failed\n", g_pass, g_fail);
    printf("==================================================\n");
    return g_fail ? 1 : 0;
}
