// 11_arrays_hashing.c — Arrays & Hashing (배열 & 해싱) · DSA
//                       · Arrays/Hash · PRACTICE STUB (직접 채워넣기)
// ---------------------------------------------------------------------------
// 각 함수의 '// TODO' 를 구현하고 다시 실행 -> [FAIL] 이 [PASS] 로 바뀌면 성공.
// (미구현 상태에서도 컴파일/실행은 되며 대부분 FAIL 로 뜬다.)
//
// 빌드:  cc -std=c11 -Wall -Wextra problems/11_arrays_hashing.c -o /tmp/andb_11
// 실행:  /tmp/andb_11
//   또는: make prob N=11_arrays_hashing
//
// 주제: 해시맵/해시셋을 C로 직접 구현(개방주소법 or 체이닝) + prefix-sum +
//       two-pointer + 고정크기 sliding window. STL 없음 — 필요한 자료구조는
//       이 파일 안에서 직접 정의하라.
//
// 힌트: int 키 개방주소법 해시맵 스켈레톤
//   typedef struct { int key, val; bool used; } HMSlot;
//   size_t h = ((uint32_t)key * 2654435761u) & (cap - 1);   // cap = 2^k
//   // 선형 탐사(linear probing): 빈 슬롯을 만날 때까지 (h+1)&(cap-1) 순회
// ---------------------------------------------------------------------------

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * 1. two_sum — 해시맵
 *   KO: nums 에서 합이 target 인 서로 다른 두 인덱스를 찾아 out_i<out_j 로 채우고
 *       true. 없으면/NULL/원소<2 면 false. 값->인덱스 해시맵으로 O(n).
 *   EN: Find two distinct indices whose values sum to target (out_i<out_j).
 *   ex: two_sum({2,7,11,15},9) -> (0,1), true
 *       two_sum({3,3},6)       -> (0,1), true
 * ------------------------------------------------------------------------- */
bool two_sum(const int *nums, size_t n, int target, int *out_i, int *out_j) {
    (void)nums; (void)n; (void)target; (void)out_i; (void)out_j;
    // TODO: 값->인덱스 해시맵. target-nums[i] 조회 후 없으면 삽입.
    return false;
}

/* ---------------------------------------------------------------------------
 * 2. contains_duplicate — 해시셋
 *   KO: 중복 원소가 하나라도 있으면 true. NULL/빈/단일 -> false. 해시셋 O(n).
 *   EN: Return true if any value appears more than once.
 *   ex: contains_duplicate({1,2,3,1}) -> true
 *       contains_duplicate({1,2,3,4}) -> false
 * ------------------------------------------------------------------------- */
bool contains_duplicate(const int *nums, size_t n) {
    (void)nums; (void)n;
    // TODO: 해시셋에 삽입하며 이미 존재하면 true.
    return false;
}

/* ---------------------------------------------------------------------------
 * 3. product_except_self — 나눗셈 없이 prefix/suffix 곱
 *   KO: out[i] = (i 제외 모든 원소의 곱). 나눗셈 금지, 0 포함해도 정상.
 *       NULL/빈 -> no-op, 단일 -> {1}.
 *   EN: out[i] = product of all elements except nums[i], no division.
 *   ex: product_except_self({1,2,3,4}) -> {24,12,8,6}
 * ------------------------------------------------------------------------- */
void product_except_self(const int *nums, size_t n, long *out) {
    (void)nums; (void)n; (void)out;
    // TODO: 좌측 누적곱을 out 에 쓰고, 우측 누적곱을 되돌아오며 곱한다.
}

/* ---------------------------------------------------------------------------
 * 4. range_sum — prefix-sum 구간합
 *   KO: nums[l..r] 합(양끝 포함). prefix[k]=nums[0..k-1]합 로 O(1) 질의.
 *       잘못된 구간(l>r, r>=n, NULL, 빈) -> 0.
 *   EN: Inclusive sum of nums[l..r] via prefix sums.
 *   ex: range_sum({1,2,3,4,5},1,3) -> 9
 * ------------------------------------------------------------------------- */
long range_sum(const int *nums, size_t n, size_t l, size_t r) {
    (void)nums; (void)n; (void)l; (void)r;
    // TODO: prefix[r+1]-prefix[l].
    return 0;
}

/* ---------------------------------------------------------------------------
 * 5. max_subarray — Kadane
 *   KO: 연속 부분배열의 최대합. 전부 음수면 최댓값(단일). 빈 -> 0.
 *   EN: Maximum contiguous subarray sum (Kadane).
 *   ex: max_subarray({-2,1,-3,4,-1,2,1,-5,4}) -> 6
 * ------------------------------------------------------------------------- */
long max_subarray(const int *nums, size_t n) {
    (void)nums; (void)n;
    // TODO: cur=max(x, cur+x); best=max(best,cur).
    return 0;
}

/* ---------------------------------------------------------------------------
 * 6. max_sum_window_k — 고정크기 sliding window
 *   KO: 크기 k 인 연속 창의 최대합. k==0 또는 k>n -> 0.
 *   EN: Max sum of any contiguous window of size k.
 *   ex: max_sum_window_k({2,1,5,1,3,2},3) -> 9
 * ------------------------------------------------------------------------- */
long max_sum_window_k(const int *nums, size_t n, size_t k) {
    (void)nums; (void)n; (void)k;
    // TODO: 첫 창 합 후 sum += nums[i]-nums[i-k] 로 슬라이드.
    return 0;
}

/* ---------------------------------------------------------------------------
 * 7. pair_sum_sorted — two-pointer (정렬 배열)
 *   KO: 오름차순 nums 에서 합이 target 인 두 인덱스(out_i<out_j) -> true.
 *       없음/NULL/원소<2 -> false.
 *   EN: On a sorted array, find two indices summing to target via two-pointer.
 *   ex: pair_sum_sorted({1,2,3,4,6},6) -> (1,3), true
 * ------------------------------------------------------------------------- */
bool pair_sum_sorted(const int *nums, size_t n, int target, int *out_i, int *out_j) {
    (void)nums; (void)n; (void)target; (void)out_i; (void)out_j;
    // TODO: lo=0,hi=n-1. 합 비교로 lo++/hi--.
    return false;
}

/* ---------------------------------------------------------------------------
 * 8. move_zeroes — 제자리(in-place) 0 뒤로
 *   KO: 모든 0 을 배열 끝으로 밀되 비-0 의 상대순서 유지. NULL/빈 -> no-op.
 *   EN: Move all zeroes to the end in place, keeping non-zero order.
 *   ex: move_zeroes({0,1,0,3,12}) -> {1,3,12,0,0}
 * ------------------------------------------------------------------------- */
void move_zeroes(int *nums, size_t n) {
    (void)nums; (void)n;
    // TODO: write 포인터로 비-0 압축 후 나머지 0 채움.
}

/* ---------------------------------------------------------------------------
 * 9. majority_element — Boyer-Moore 다수결
 *   KO: 등장>n/2 인 다수 원소 반환(존재 가정). O(n) 시간, O(1) 공간.
 *   EN: Return the majority element (appears > n/2), assumed to exist.
 *   ex: majority_element({2,2,1,1,1,2,2}) -> 2
 * ------------------------------------------------------------------------- */
int majority_element(const int *nums, size_t n) {
    (void)nums; (void)n;
    // TODO: 후보+카운트. count==0 이면 후보 교체.
    return 0;
}

/* ---------------------------------------------------------------------------
 * 10. top_k_frequent — 해시맵 카운트 + 정렬
 *   KO: 가장 자주 나온 상위 k 개 값을 out 에 채운다. 정렬 기준: 빈도 내림차순,
 *       동률은 값 오름차순. NULL/빈/k==0 -> no-op.
 *   EN: Fill out with the k most frequent values (freq desc, tie: value asc).
 *   ex: top_k_frequent({1,1,1,2,2,3},2) -> {1,2}
 * ------------------------------------------------------------------------- */
void top_k_frequent(const int *nums, size_t n, size_t k, int *out) {
    (void)nums; (void)n; (void)k; (void)out;
    // TODO: 해시맵으로 빈도 집계 -> (값,빈도) 배열 qsort -> 앞 k 개.
}

// ============================================================================
// ---- Test harness (건드리지 말 것: 구현을 채우면 FAIL -> PASS) ----
// ============================================================================
static int g_pass = 0, g_fail = 0;

static void check_int(const char *call, long got, long want) {
    bool ok = (got == want);
    printf("%-52s -> %ld  %s", call, got, ok ? "[PASS]" : "[FAIL]");
    if (!ok) printf(" (expected %ld)", want);
    printf("\n");
    ok ? g_pass++ : g_fail++;
}
static void check_cond(const char *call, bool ok, const char *note) {
    printf("%-52s -> %s  %s\n", call, note, ok ? "[PASS]" : "[FAIL]");
    ok ? g_pass++ : g_fail++;
}
static void check_arr(const char *call, const long *got, const long *want, size_t n) {
    bool ok = true;
    for (size_t i = 0; i < n; i++) if (got[i] != want[i]) ok = false;
    printf("%-52s -> [", call);
    for (size_t i = 0; i < n; i++) printf("%s%ld", i ? "," : "", got[i]);
    printf("]  %s", ok ? "[PASS]" : "[FAIL]");
    if (!ok) { printf(" (expected ["); for (size_t i = 0; i < n; i++) printf("%s%ld", i ? "," : "", want[i]); printf("])"); }
    printf("\n");
    ok ? g_pass++ : g_fail++;
}
static void check_iarr(const char *call, const int *got, const int *want, size_t n) {
    bool ok = true;
    for (size_t i = 0; i < n; i++) if (got[i] != want[i]) ok = false;
    printf("%-52s -> [", call);
    for (size_t i = 0; i < n; i++) printf("%s%d", i ? "," : "", got[i]);
    printf("]  %s", ok ? "[PASS]" : "[FAIL]");
    if (!ok) { printf(" (expected ["); for (size_t i = 0; i < n; i++) printf("%s%d", i ? "," : "", want[i]); printf("])"); }
    printf("\n");
    ok ? g_pass++ : g_fail++;
}

int main(void) {
    printf("== 1. two_sum (hashmap) ==\n");
    {
        int a[] = {2, 7, 11, 15}; int i = -1, j = -1;
        check_cond("two_sum({2,7,11,15},9) found", two_sum(a, 4, 9, &i, &j), "true");
        check_int("  index i", i, 0); check_int("  index j", j, 1);
        int b[] = {3, 2, 4}; two_sum(b, 3, 6, &i, &j);
        check_int("two_sum({3,2,4},6) i", i, 1); check_int("  j", j, 2);
        int c[] = {3, 3}; two_sum(c, 2, 6, &i, &j);
        check_int("two_sum({3,3},6) dup i", i, 0); check_int("  j", j, 1);
        int d[] = {1, 2, 3};
        check_cond("two_sum(no solution)", two_sum(d, 3, 100, &i, &j) == false, "false");
        check_cond("two_sum(empty)", two_sum(NULL, 0, 0, &i, &j) == false, "false");
        int e[] = {5};
        check_cond("two_sum(single)", two_sum(e, 1, 5, &i, &j) == false, "false");
    }

    printf("\n== 2. contains_duplicate (hashset) ==\n");
    {
        int a[] = {1, 2, 3, 1}; int b[] = {1, 2, 3, 4}; int c[] = {7}; int d[] = {2, 2};
        check_cond("contains_dup({1,2,3,1})", contains_duplicate(a, 4) == true, "true");
        check_cond("contains_dup({1,2,3,4})", contains_duplicate(b, 4) == false, "false");
        check_cond("contains_dup(empty)", contains_duplicate(NULL, 0) == false, "false");
        check_cond("contains_dup(single)", contains_duplicate(c, 1) == false, "false");
        check_cond("contains_dup({2,2})", contains_duplicate(d, 2) == true, "true");
    }

    printf("\n== 3. product_except_self ==\n");
    {
        int a[] = {1, 2, 3, 4}; long out[8]; long e1[] = {24, 12, 8, 6};
        product_except_self(a, 4, out); check_arr("product({1,2,3,4})", out, e1, 4);
        int b[] = {-1, 1, 0, -3, 3}; long e2[] = {0, 0, 9, 0, 0};
        product_except_self(b, 5, out); check_arr("product({-1,1,0,-3,3})", out, e2, 5);
        int c[] = {2, 3}; long e3[] = {3, 2};
        product_except_self(c, 2, out); check_arr("product({2,3})", out, e3, 2);
        int d[] = {5}; long e4[] = {1};
        product_except_self(d, 1, out); check_arr("product(single)->{1}", out, e4, 1);
    }

    printf("\n== 4. range_sum (prefix-sum) ==\n");
    {
        int a[] = {1, 2, 3, 4, 5};
        check_int("range_sum([0,2])", range_sum(a, 5, 0, 2), 6);
        check_int("range_sum([1,3])", range_sum(a, 5, 1, 3), 9);
        check_int("range_sum([0,4]) full", range_sum(a, 5, 0, 4), 15);
        check_int("range_sum([2,2]) single", range_sum(a, 5, 2, 2), 3);
        check_int("range_sum(l>r) invalid", range_sum(a, 5, 3, 1), 0);
        check_int("range_sum(r>=n) invalid", range_sum(a, 5, 0, 9), 0);
    }

    printf("\n== 5. max_subarray (Kadane) ==\n");
    {
        int a[] = {-2, 1, -3, 4, -1, 2, 1, -5, 4};
        check_int("max_subarray(classic)", max_subarray(a, 9), 6);
        int b[] = {5, 4, -1, 7, 8};
        check_int("max_subarray({5,4,-1,7,8})", max_subarray(b, 5), 23);
        int c[] = {-1, -2, -3};
        check_int("max_subarray(all neg)", max_subarray(c, 3), -1);
        int d[] = {3};
        check_int("max_subarray(single)", max_subarray(d, 1), 3);
        check_int("max_subarray(empty)", max_subarray(NULL, 0), 0);
    }

    printf("\n== 6. max_sum_window_k (sliding window) ==\n");
    {
        int a[] = {1, 2, 3, 4, 5};
        check_int("max_win({1..5},k=2)", max_sum_window_k(a, 5, 2), 9);
        int b[] = {2, 1, 5, 1, 3, 2};
        check_int("max_win({2,1,5,1,3,2},k=3)", max_sum_window_k(b, 6, 3), 9);
        check_int("max_win(k=0)", max_sum_window_k(a, 5, 0), 0);
        check_int("max_win(k>n)", max_sum_window_k(a, 5, 9), 0);
        int c[] = {1, 2, 3};
        check_int("max_win(k==n)", max_sum_window_k(c, 3, 3), 6);
    }

    printf("\n== 7. pair_sum_sorted (two-pointer) ==\n");
    {
        int a[] = {1, 2, 3, 4, 6}; int i = -1, j = -1;
        check_cond("pair_sum({1,2,3,4,6},6)", pair_sum_sorted(a, 5, 6, &i, &j), "true");
        check_int("  i", i, 1); check_int("  j", j, 3);
        int b[] = {2, 3, 4}; pair_sum_sorted(b, 3, 6, &i, &j);
        check_int("pair_sum({2,3,4},6) i", i, 0); check_int("  j", j, 2);
        int c[] = {1, 2, 3};
        check_cond("pair_sum(no solution)", pair_sum_sorted(c, 3, 100, &i, &j) == false, "false");
        check_cond("pair_sum(empty)", pair_sum_sorted(NULL, 0, 0, &i, &j) == false, "false");
        int d[] = {5};
        check_cond("pair_sum(single)", pair_sum_sorted(d, 1, 10, &i, &j) == false, "false");
    }

    printf("\n== 8. move_zeroes (in-place) ==\n");
    {
        int a[] = {0, 1, 0, 3, 12}; int e1[] = {1, 3, 12, 0, 0};
        move_zeroes(a, 5); check_iarr("move_zeroes({0,1,0,3,12})", a, e1, 5);
        int b[] = {0, 0, 1}; int e2[] = {1, 0, 0};
        move_zeroes(b, 3); check_iarr("move_zeroes({0,0,1})", b, e2, 3);
        int c[] = {1, 2, 3}; int e3[] = {1, 2, 3};
        move_zeroes(c, 3); check_iarr("move_zeroes(no zeros)", c, e3, 3);
        int d[] = {0}; int e4[] = {0};
        move_zeroes(d, 1); check_iarr("move_zeroes({0})", d, e4, 1);
    }

    printf("\n== 9. majority_element (Boyer-Moore) ==\n");
    {
        int a[] = {3, 2, 3};
        check_int("majority({3,2,3})", majority_element(a, 3), 3);
        int b[] = {2, 2, 1, 1, 1, 2, 2};
        check_int("majority({2,2,1,1,1,2,2})", majority_element(b, 7), 2);
        int c[] = {1};
        check_int("majority(single)", majority_element(c, 1), 1);
        int d[] = {6, 6, 6, 7, 7};
        check_int("majority({6,6,6,7,7})", majority_element(d, 5), 6);
    }

    printf("\n== 10. top_k_frequent (hashmap + sort) ==\n");
    {
        int a[] = {1, 1, 1, 2, 2, 3}; int out[8]; int e1[] = {1, 2};
        top_k_frequent(a, 6, 2, out); check_iarr("top_k({1,1,1,2,2,3},k=2)", out, e1, 2);
        int b[] = {1}; int e2[] = {1};
        top_k_frequent(b, 1, 1, out); check_iarr("top_k({1},k=1)", out, e2, 1);
        int c[] = {4, 4, 4, 5, 5, 6}; int e3[] = {4};
        top_k_frequent(c, 6, 1, out); check_iarr("top_k({4,4,4,5,5,6},k=1)", out, e3, 1);
        int d[] = {1, 1, 2, 2, 3}; int e4[] = {1, 2};
        top_k_frequent(d, 5, 2, out); check_iarr("top_k(tie->value asc)", out, e4, 2);
        int e[] = {5, 5, 4, 4, 3, 3}; int e5[] = {3, 4, 5};
        top_k_frequent(e, 6, 3, out); check_iarr("top_k(all tie,k=3)", out, e5, 3);
    }

    printf("\n==================================================\n");
    printf("RESULT: %d passed, %d failed\n", g_pass, g_fail);
    printf("==================================================\n");
    return g_fail ? 1 : 0;
}
