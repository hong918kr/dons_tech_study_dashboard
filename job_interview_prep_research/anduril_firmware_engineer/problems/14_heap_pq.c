// 14_heap_pq.c  —  PRACTICE STUB (여기 빈칸을 채우세요)
// 힙 & 우선순위 큐 (Heaps & Priority Queue, 배열 이진 힙 인라인 구현)  —  P1~P8
// ---------------------------------------------------------------------------
// 빌드: cc -std=c11 -Wall -Wextra 14_heap_pq.c -o /tmp/andb_14 && /tmp/andb_14
// 각 함수의 // TODO 를 구현하고 다시 빌드/실행해 FAIL -> PASS 로 바꾸세요.
// 지금 상태로도 컴파일/실행은 되며(placeholder 반환), 대부분 [FAIL] 로 나옵니다.
//
// 배열 이진 힙 규칙 (0-indexed, STL 없이 직접):
//   parent = (i-1)/2,  left = 2i+1,  right = 2i+2.
//   sift_up(상향)/sift_down(하향) 두 원자로 push/pop/heapify 를 전부 만든다.
//
// 힌트:
//   - P1 heapify: i=n/2-1 부터 0 까지 sift_down -> O(n).
//   - P4/P5: 크기 k min-heap 유지 -> O(n log k).
//   - P6: 각 배열 선두를 min-heap 에, pop 후 그 배열 다음 원소 push.
//   - P7 로봇 스웜: max-heap 에서 pop -> 우선순위 감쇠 -> 남으면 재삽입.
//   - P8 중앙값: lo(max-heap)/hi(min-heap) 두 힙, 크기 차 <= 1 유지.
// ---------------------------------------------------------------------------
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// ===========================================================================
// 테스트 하네스 (PASS/FAIL)
// ===========================================================================
static int g_pass = 0;
static int g_fail = 0;
#define T(label, cond) do {                                   \
    if (cond) { printf("  [PASS] %s\n", (label)); g_pass++; } \
    else      { printf("  [FAIL] %s\n", (label)); g_fail++; } \
} while (0)

// (팁) 아래 두 원자를 먼저 만들고 재사용하면 편하다.
//   static void sift_up_max(int *a, int i);
//   static void sift_down_max(int *a, int n, int i);
//   (min-heap 버전도 대칭으로)

// ===========================================================================
// P1. heap_build_sift_down  —  배열을 in-place max-heap 화 (heapify, O(n))
//   예: {3,1,6,5,2,4} -> 유효한 max-heap, 루트=6
// ===========================================================================
void heap_build_max(int *a, int n) {
    (void)a; (void)n;
    // TODO: i = n/2-1 .. 0 에 대해 sift_down_max(a, n, i)
}

// ===========================================================================
// P2. heap_push / heap_pop  —  sift_up 삽입 / sift_down 삭제 (max-heap)
//   push: 끝에 넣고 sift_up.  pop: 루트 반환, 마지막을 루트로 옮겨 sift_down.
// ===========================================================================
void max_heap_push(int *a, int *n, int val) {
    (void)a; (void)n; (void)val;
    // TODO: a[*n]=val; sift_up_max(a,*n); (*n)++;
}
int max_heap_pop(int *a, int *n) {
    (void)a; (void)n;
    // TODO: 루트 저장 -> a[0]=a[--(*n)] -> sift_down_max -> 저장값 반환
    return 0;   // placeholder
}

// ===========================================================================
// P3. heap_sort  —  오름차순 정렬 (max-heap, in-place O(n log n))
//   heapify 후 루트를 끝과 교환하며 힙 크기 축소.
// ===========================================================================
void heap_sort(int *a, int n) {
    (void)a; (void)n;
    // TODO: heap_build_max 후 end=n-1..1 에서 swap(a[0],a[end]) + sift_down(end)
}

// ===========================================================================
// P4. kth_largest_element  —  크기 k min-heap 으로 K번째 최대 (O(n log k))
//   min-heap 에 상위 k개 유지, 루트 = K번째 최대.
// ===========================================================================
int kth_largest(const int *a, int n, int k) {
    (void)a; (void)n; (void)k;
    // TODO: 크기 k min-heap; 새 값 > 루트면 루트 교체 후 sift_down
    return 0;   // placeholder
}

// ===========================================================================
// P5. top_k_frequent  —  빈도 상위 k개 (크기 k min-heap, count 기준)
//   out[] 에 빈도 내림차순으로 채우고 개수 반환 (k>고유값수면 clamp).
// ===========================================================================
int top_k_frequent(const int *a, int n, int k, int *out) {
    (void)a; (void)n; (void)k; (void)out;
    // TODO: (값,빈도) 집계 -> count 기준 크기 k min-heap -> out 뒤에서 채움
    return 0;   // placeholder
}

// ===========================================================================
// P6. merge_k_sorted_arrays  —  min-heap 으로 k-way merge (O(N log k))
//   각 배열 선두를 (값,ai,ei) 로 min-heap; pop 후 그 배열 다음 원소 push.
// ===========================================================================
int merge_k_sorted(const int *const *arrs, const int *lens, int k, int *out) {
    (void)arrs; (void)lens; (void)k; (void)out;
    // TODO: 첫 원소들로 힙 초기화 -> pop -> out 추가 -> 다음 원소 push
    return 0;   // placeholder
}

// ===========================================================================
// P7. task_scheduler_by_priority  —  로봇 스웜 우선순위 태스크 (Anduril 시나리오)
//   max-heap(우선순위) pop -> 한 스텝 실행(order 기록) -> 우선순위 -= decay ->
//   0 초과면 갱신값으로 재삽입.  총 실행 스텝 수 반환 (cap 으로 제한).
// ===========================================================================
typedef struct { int id, priority; } task_t;

int robot_swarm_schedule(const task_t *tasks, int n, int decay,
                         int *order, int cap) {
    (void)tasks; (void)n; (void)decay; (void)order; (void)cap;
    // TODO: 초기 태스크 heapify -> pop(max) -> order[steps++] -> decay 후 재삽입
    return 0;   // placeholder
}

// ===========================================================================
// P8. median_from_stream  —  두 힙으로 실행 중 중앙값
//   lo=max-heap(작은 절반), hi=min-heap(큰 절반), 크기 차 <= 1 (lo_n>=hi_n).
// ===========================================================================
#define MED_CAP 512
typedef struct {
    int lo[MED_CAP]; int lo_n;               // max-heap
    int hi[MED_CAP]; int hi_n;               // min-heap
} median_t;

void median_init(median_t *m) {
    m->lo_n = 0; m->hi_n = 0;                 // (초기화는 제공)
}
void median_add(median_t *m, int v) {
    (void)m; (void)v;
    // TODO: v<=lo루트면 lo 삽입 else hi 삽입 -> 크기 차 1 넘으면 재균형
}
double median_get(const median_t *m) {
    (void)m;
    // TODO: 크기 같으면 (lo루트+hi루트)/2.0, lo 가 하나 크면 lo 루트
    return 0.0;   // placeholder
}

// ===========================================================================
// 테스트 헬퍼
// ===========================================================================
static bool is_max_heap(const int *a, int n) {
    for (int i = 0; i < n; ++i) {
        int l = 2 * i + 1, r = 2 * i + 2;
        if (l < n && a[l] > a[i]) return false;
        if (r < n && a[r] > a[i]) return false;
    }
    return true;
}
static bool is_sorted_asc(const int *a, int n) {
    for (int i = 1; i < n; ++i) if (a[i - 1] > a[i]) return false;
    return true;
}
static bool arr_eq(const int *a, const int *b, int n) {
    for (int i = 0; i < n; ++i) if (a[i] != b[i]) return false;
    return true;
}

// ===========================================================================
// main : 모든 케이스 PASS/FAIL
// ===========================================================================
int main(void) {
    // -------- P1 heapify --------
    printf("== P1. heap_build_sift_down (heapify) ==\n");
    {
        int a[] = {3, 1, 6, 5, 2, 4};
        heap_build_max(a, 6);
        T("heapify -> valid max-heap", is_max_heap(a, 6));
        T("heapify root == max(6)", a[0] == 6);
        int one[] = {42};
        heap_build_max(one, 1);
        T("heapify single element", is_max_heap(one, 1) && one[0] == 42);
        int empty[1] = {0};
        heap_build_max(empty, 0);
        T("heapify empty (n=0) 무해", true);
    }

    // -------- P2 push/pop --------
    printf("== P2. heap_push / heap_pop ==\n");
    {
        int a[16]; int n = 0;
        int in[] = {5, 3, 8, 1, 9, 2};
        for (int i = 0; i < 6; ++i) {
            max_heap_push(a, &n, in[i]);
            T("push keeps heap property", is_max_heap(a, n));
        }
        T("size == 6 after 6 pushes", n == 6);
        int expect_desc[] = {9, 8, 5, 3, 2, 1};
        bool ok = true;
        for (int i = 0; i < 6; ++i) if (max_heap_pop(a, &n) != expect_desc[i]) ok = false;
        T("pop yields descending order", ok);
        T("size == 0 after draining", n == 0);
    }

    // -------- P3 heap_sort --------
    printf("== P3. heap_sort ==\n");
    {
        int a[] = {5, 2, 9, 1, 5, 6, 3};
        heap_sort(a, 7);
        int exp[] = {1, 2, 3, 5, 5, 6, 9};
        T("heap_sort ascending", is_sorted_asc(a, 7) && arr_eq(a, exp, 7));
        int dup[] = {4, 4, 4, 4};
        heap_sort(dup, 4);
        T("heap_sort all-equal", is_sorted_asc(dup, 4));
        int rev[] = {5, 4, 3, 2, 1};
        heap_sort(rev, 5);
        T("heap_sort reverse-sorted input", is_sorted_asc(rev, 5));
        int single[] = {7};
        heap_sort(single, 1);
        T("heap_sort single", single[0] == 7);
    }

    // -------- P4 kth_largest --------
    printf("== P4. kth_largest_element ==\n");
    {
        int a[] = {3, 2, 1, 5, 6, 4};
        T("2nd largest of [3,2,1,5,6,4] == 5", kth_largest(a, 6, 2) == 5);
        int b[] = {3, 2, 3, 1, 2, 4, 5, 5, 6};
        T("4th largest == 4", kth_largest(b, 9, 4) == 4);
        T("1st largest == max(6)", kth_largest(a, 6, 1) == 6);
        T("n-th largest == min(1)", kth_largest(a, 6, 6) == 1);
    }

    // -------- P5 top_k_frequent --------
    printf("== P5. top_k_frequent ==\n");
    {
        int a[] = {1, 1, 1, 2, 2, 3};
        int out[8];
        int cnt = top_k_frequent(a, 6, 2, out);
        int exp[] = {1, 2};
        T("top-2 frequent -> [1,2]", cnt == 2 && arr_eq(out, exp, 2));
        int b[] = {4, 4, 4, 5, 5, 6, 6, 6, 6};
        int out2[8];
        int cnt2 = top_k_frequent(b, 9, 3, out2);
        int exp2[] = {6, 4, 5};
        T("top-3 frequent -> [6,4,5]", cnt2 == 3 && arr_eq(out2, exp2, 3));
        int out3[8];
        int cnt3 = top_k_frequent(a, 6, 5, out3);
        T("k>distinct clamps to 3", cnt3 == 3);
    }

    // -------- P6 merge_k_sorted --------
    printf("== P6. merge_k_sorted_arrays ==\n");
    {
        int a0[] = {1, 4, 7};
        int a1[] = {2, 5, 8};
        int a2[] = {3, 6, 9};
        const int *arrs[] = {a0, a1, a2};
        int lens[] = {3, 3, 3};
        int out[9];
        int total = merge_k_sorted(arrs, lens, 3, out);
        int exp[] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
        T("merge 3x3 -> sorted 1..9", total == 9 && arr_eq(out, exp, 9));

        int b0[] = {1, 2, 3};
        int b1[] = {0};
        const int *arrs2[] = {b0, b1, NULL};
        int lens2[] = {3, 1, 0};
        int out2[5];
        int total2 = merge_k_sorted(arrs2, lens2, 3, out2);
        int exp2[] = {0, 1, 2, 3};
        T("merge uneven+empty -> [0,1,2,3]", total2 == 4 && arr_eq(out2, exp2, 4));
    }

    // -------- P7 robot_swarm_schedule --------
    printf("== P7. task_scheduler_by_priority (robot swarm) ==\n");
    {
        task_t tasks[] = {{10, 5}, {20, 9}, {30, 2}};
        int order[32];
        int steps = robot_swarm_schedule(tasks, 3, 3, order, 32);
        int exp_order[] = {20, 20, 10, 20, 10, 30};
        T("swarm executes highest-priority first", steps == 6 && arr_eq(order, exp_order, 6));
        T("id20 ran 3x (9->6->3->done)",
          steps == 6 && order[0] == 20 && order[1] == 20 && order[3] == 20);

        task_t big[] = {{1, 100}};
        int o2[4];
        int s2 = robot_swarm_schedule(big, 1, 1, o2, 4);
        T("cap limits steps to 4", s2 == 4);

        int o3[1];
        T("empty task set -> 0 steps", robot_swarm_schedule(NULL, 0, 1, o3, 1) == 0);
    }

    // -------- P8 median_from_stream --------
    printf("== P8. median_from_stream (two heaps) ==\n");
    {
        median_t m; median_init(&m);
        median_add(&m, 5);
        T("median{5} == 5", median_get(&m) == 5.0);
        median_add(&m, 15);
        T("median{5,15} == 10", median_get(&m) == 10.0);
        median_add(&m, 1);
        T("median{1,5,15} == 5", median_get(&m) == 5.0);
        median_add(&m, 3);
        T("median{1,3,5,15} == 4", median_get(&m) == 4.0);
        median_add(&m, 8);
        T("median{1,3,5,8,15} == 5", median_get(&m) == 5.0);

        median_t m2; median_init(&m2);
        for (int i = 1; i <= 7; ++i) median_add(&m2, i);
        T("median 1..7 == 4", median_get(&m2) == 4.0);

        median_t m3; median_init(&m3);
        int seq[] = {6, 10, 2, 6, 4, 8};
        for (int i = 0; i < 6; ++i) median_add(&m3, seq[i]);
        T("median of 6 elems == 6", median_get(&m3) == 6.0);
    }

    // -------- 결과 --------
    printf("\n==== %d passed, %d failed ====\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
