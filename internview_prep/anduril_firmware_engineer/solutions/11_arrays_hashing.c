// 11_arrays_hashing.c — Arrays & Hashing (배열 & 해싱) · DSA
//                       · Arrays/Hash · REFERENCE SOLUTION
// ---------------------------------------------------------------------------
// 해시맵/해시셋(개방주소법)을 C로 직접 구현 + prefix-sum + two-pointer +
// sliding window. STL 없음 — 모든 자료구조를 파일 안에서 정의한다.
// Anduril 펌웨어 인터뷰의 "배열/해시로 O(n) 풀어라 (STL 없이 C로)" 유형.
//
// 빌드:  cc -std=c11 -Wall -Wextra solutions/11_arrays_hashing.c -o /tmp/andb_11
// 실행:  /tmp/andb_11
// ---------------------------------------------------------------------------

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>      // malloc/free/qsort
#include <stdio.h>
#include <string.h>      // memcpy (테스트 검증)

// ============================================================================
// 인라인 해시맵/해시셋 (int 키, 개방주소법 = open addressing + linear probing)
//   - STL 없이 C로 직접 구현하는 것이 이 토픽의 핵심.
//   - 용량은 2의 거듭제곱 → 마스킹(& (cap-1))으로 모듈로 대체.
//   - 부하율(load factor) 관리를 위해 필요 원소수의 2배를 잡는다.
// ============================================================================
static size_t next_pow2(size_t n) {          // n 이상인 최소 2의 거듭제곱
    size_t p = 1;
    while (p < n) p <<= 1;
    return p;
}
static size_t hash_int(int key, size_t cap) {  // Knuth 곱셈 해시, 음수 키 안전
    uint32_t x = (uint32_t)key;
    x *= 2654435761u;                          // 2^32 * (황금비)
    return (size_t)x & (cap - 1);              // cap 은 2의 거듭제곱
}

// --- 해시맵: key(int) -> val(int) ---
typedef struct { int key, val; bool used; } HMSlot;
typedef struct { HMSlot *s; size_t cap; } HashMap;

static void hm_init(HashMap *m, size_t expected) {
    m->cap = next_pow2(expected < 4 ? 4 : expected * 2);
    m->s = calloc(m->cap, sizeof(HMSlot));     // used=false 로 0-초기화
}
static void hm_free(HashMap *m) { free(m->s); m->s = NULL; m->cap = 0; }

// key 존재 시 val 을 *out 에 넣고 true. 개방주소법 선형 탐사.
static bool hm_get(const HashMap *m, int key, int *out) {
    size_t i = hash_int(key, m->cap);
    for (size_t probe = 0; probe < m->cap; probe++) {
        size_t j = (i + probe) & (m->cap - 1);
        if (!m->s[j].used) return false;       // 빈 슬롯 = 더 볼 필요 없음
        if (m->s[j].key == key) { if (out) *out = m->s[j].val; return true; }
    }
    return false;
}
// key 를 val 로 삽입/갱신.
static void hm_put(HashMap *m, int key, int val) {
    size_t i = hash_int(key, m->cap);
    for (size_t probe = 0; probe < m->cap; probe++) {
        size_t j = (i + probe) & (m->cap - 1);
        if (!m->s[j].used) { m->s[j].used = true; m->s[j].key = key; m->s[j].val = val; return; }
        if (m->s[j].key == key) { m->s[j].val = val; return; }
    }
}

// --- 해시셋: int 원소 집합 (해시맵의 val 없는 버전) ---
typedef struct { int key; bool used; } HSSlot;
typedef struct { HSSlot *s; size_t cap; } HashSet;

static void hs_init(HashSet *set, size_t expected) {
    set->cap = next_pow2(expected < 4 ? 4 : expected * 2);
    set->s = calloc(set->cap, sizeof(HSSlot));
}
static void hs_free(HashSet *set) { free(set->s); set->s = NULL; set->cap = 0; }
// 이미 있으면 true(변경 없음), 없으면 삽입하고 false.
static bool hs_add(HashSet *set, int key) {
    size_t i = hash_int(key, set->cap);
    for (size_t probe = 0; probe < set->cap; probe++) {
        size_t j = (i + probe) & (set->cap - 1);
        if (!set->s[j].used) { set->s[j].used = true; set->s[j].key = key; return false; }
        if (set->s[j].key == key) return true;
    }
    return false;
}

// ============================================================================
// 1. two_sum — 해시맵으로 O(n)
//    값->인덱스 맵을 채우며 target-nums[i] 를 조회. 찾으면 (더 이른 인덱스, i).
//    NULL/빈 배열/해 없음 -> false.
// ============================================================================
bool two_sum(const int *nums, size_t n, int target, int *out_i, int *out_j) {
    if (!nums || n < 2) return false;
    HashMap m; hm_init(&m, n);
    bool found = false;
    for (size_t i = 0; i < n; i++) {
        int need = target - nums[i];
        int idx;
        if (hm_get(&m, need, &idx)) {          // 이전에 본 값 중에 짝이 있다
            if (out_i) *out_i = idx;
            if (out_j) *out_j = (int)i;
            found = true;
            break;
        }
        hm_put(&m, nums[i], (int)i);           // 뒤에서 조회되도록 저장
    }
    hm_free(&m);
    return found;
}

// ============================================================================
// 2. contains_duplicate — 해시셋으로 O(n)
//    삽입 중 이미 존재하면 즉시 true. NULL/빈/단일 -> false.
// ============================================================================
bool contains_duplicate(const int *nums, size_t n) {
    if (!nums || n < 2) return false;
    HashSet set; hs_init(&set, n);
    bool dup = false;
    for (size_t i = 0; i < n; i++) {
        if (hs_add(&set, nums[i])) { dup = true; break; }
    }
    hs_free(&set);
    return dup;
}

// ============================================================================
// 3. product_except_self — 나눗셈 없이 prefix/suffix 곱
//    out[i] = (좌측 누적곱) * (우측 누적곱). 0 이 있어도 정상 동작.
//    (Anduril 리포트 문제) NULL/빈은 no-op, 단일은 {1}.
// ============================================================================
void product_except_self(const int *nums, size_t n, long *out) {
    if (!nums || !out || n == 0) return;
    long prefix = 1;                           // 나눗셈 회피: 좌측곱 먼저
    for (size_t i = 0; i < n; i++) { out[i] = prefix; prefix *= nums[i]; }
    long suffix = 1;                           // 우측곱을 되돌아오며 곱함
    for (size_t i = n; i-- > 0; ) { out[i] *= suffix; suffix *= nums[i]; }
}

// ============================================================================
// 4. range_sum — prefix-sum 으로 구간합 질의
//    prefix[k] = nums[0..k-1] 합. sum[l..r] = prefix[r+1] - prefix[l].
//    질의 여러 번이면 전처리 O(n) 후 질의당 O(1). 잘못된 구간 -> 0.
// ============================================================================
long range_sum(const int *nums, size_t n, size_t l, size_t r) {
    if (!nums || n == 0 || l > r || r >= n) return 0;
    long *prefix = malloc((n + 1) * sizeof(long));
    if (!prefix) return 0;
    prefix[0] = 0;
    for (size_t i = 0; i < n; i++) prefix[i + 1] = prefix[i] + nums[i];
    long result = prefix[r + 1] - prefix[l];   // 구간합 = 두 접두합의 차
    free(prefix);
    return result;
}

// ============================================================================
// 5. max_subarray — Kadane 알고리즘 O(n)
//    cur = max(nums[i], cur+nums[i]); best = max(best, cur).
//    전부 음수면 최댓값(단일 원소) 반환. 빈 -> 0.
// ============================================================================
long max_subarray(const int *nums, size_t n) {
    if (!nums || n == 0) return 0;
    long best = nums[0], cur = nums[0];
    for (size_t i = 1; i < n; i++) {
        long v = nums[i];
        cur = (v > cur + v) ? v : cur + v;     // 새로 시작 vs 이어가기
        if (cur > best) best = cur;
    }
    return best;
}

// ============================================================================
// 6. max_sum_window_k — 고정크기 sliding window O(n)
//    창을 한 칸씩 밀며 sum += 새 원소 - 나간 원소. k==0 또는 k>n -> 0.
// ============================================================================
long max_sum_window_k(const int *nums, size_t n, size_t k) {
    if (!nums || k == 0 || k > n) return 0;
    long sum = 0;
    for (size_t i = 0; i < k; i++) sum += nums[i];  // 첫 창
    long best = sum;
    for (size_t i = k; i < n; i++) {
        sum += nums[i] - nums[i - k];          // 슬라이드: 들어오고 나가고
        if (sum > best) best = sum;
    }
    return best;
}

// ============================================================================
// 7. pair_sum_sorted — 정렬 배열에서 two-pointer O(n)
//    양끝에서 합 비교: 크면 우측 --, 작으면 좌측 ++, 같으면 발견.
//    해 없음/NULL/원소<2 -> false.
// ============================================================================
bool pair_sum_sorted(const int *nums, size_t n, int target, int *out_i, int *out_j) {
    if (!nums || n < 2) return false;
    size_t lo = 0, hi = n - 1;
    while (lo < hi) {
        long s = (long)nums[lo] + nums[hi];
        if (s == target) {
            if (out_i) *out_i = (int)lo;
            if (out_j) *out_j = (int)hi;
            return true;
        }
        if (s < target) lo++; else hi--;
    }
    return false;
}

// ============================================================================
// 8. move_zeroes — 제자리(in-place) 0 뒤로 밀기, 비-0 순서 유지 O(n)
//    write 포인터로 비-0 을 앞으로 압축, 나머지 0 채움. NULL/빈 -> no-op.
// ============================================================================
void move_zeroes(int *nums, size_t n) {
    if (!nums || n == 0) return;
    size_t w = 0;
    for (size_t i = 0; i < n; i++)
        if (nums[i] != 0) nums[w++] = nums[i];  // 비-0 을 앞으로
    while (w < n) nums[w++] = 0;                 // 남은 자리 0
}

// ============================================================================
// 9. majority_element — Boyer-Moore 다수결 투표 O(n), O(1) 공간
//    후보 하나와 카운트만 유지. count==0 이면 후보 교체.
//    (다수 원소가 존재한다고 가정: 등장>n/2)
// ============================================================================
int majority_element(const int *nums, size_t n) {
    int cand = 0, count = 0;
    for (size_t i = 0; i < n; i++) {
        if (count == 0) cand = nums[i];
        count += (nums[i] == cand) ? 1 : -1;
    }
    return cand;
}

// ============================================================================
// 10. top_k_frequent — 해시맵 카운트 + 정렬 O(n log n)
//     맵으로 빈도 집계 -> (값,빈도) 배열 -> 빈도 내림차순(동률은 값 오름차순)
//     정렬 -> 앞 k 개. (Anduril 리포트 문제)
// ============================================================================
typedef struct { int val; int freq; } VF;
static int vf_cmp(const void *a, const void *b) {
    const VF *x = a, *y = b;
    if (x->freq != y->freq) return y->freq - x->freq;  // 빈도 내림차순
    return x->val - y->val;                             // 동률: 값 오름차순
}
void top_k_frequent(const int *nums, size_t n, size_t k, int *out) {
    if (!nums || !out || k == 0 || n == 0) return;
    HashMap m; hm_init(&m, n);
    VF *uniq = malloc(n * sizeof(VF));         // 유일값은 최대 n 개
    size_t u = 0;
    for (size_t i = 0; i < n; i++) {
        int c;
        if (hm_get(&m, nums[i], &c)) hm_put(&m, nums[i], c + 1);
        else { hm_put(&m, nums[i], 1); uniq[u++].val = nums[i]; }
    }
    for (size_t i = 0; i < u; i++) { int c; hm_get(&m, uniq[i].val, &c); uniq[i].freq = c; }
    qsort(uniq, u, sizeof(VF), vf_cmp);
    size_t take = (k < u) ? k : u;
    for (size_t i = 0; i < take; i++) out[i] = uniq[i].val;
    free(uniq);
    hm_free(&m);
}

// ============================================================================
// ---- Test harness (구현을 채우면 FAIL -> PASS) ----
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
        int d[] = {1, 1, 2, 2, 3}; int e4[] = {1, 2};       // 동률은 값 오름차순
        top_k_frequent(d, 5, 2, out); check_iarr("top_k(tie->value asc)", out, e4, 2);
        int e[] = {5, 5, 4, 4, 3, 3}; int e5[] = {3, 4, 5}; // 전부 동률 -> 값 오름차순
        top_k_frequent(e, 6, 3, out); check_iarr("top_k(all tie,k=3)", out, e5, 3);
    }

    printf("\n==================================================\n");
    printf("RESULT: %d passed, %d failed\n", g_pass, g_fail);
    printf("==================================================\n");
    return g_fail ? 1 : 0;
}
