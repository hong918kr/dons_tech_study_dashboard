// 14_heap_pq.c  —  REFERENCE SOLUTION
// 힙 & 우선순위 큐 (Heaps & Priority Queue, 배열 이진 힙 인라인 구현)  —  P1~P8
// ---------------------------------------------------------------------------
// 빌드: cc -std=c11 -Wall -Wextra 14_heap_pq.c -o /tmp/andb_14 && /tmp/andb_14
//
// 핵심 (STL/라이브러리 없이 전부 직접 구현):
//   - 배열 이진 힙(0-indexed): parent=(i-1)/2, left=2i+1, right=2i+2.
//   - sift_up(상향 조정) / sift_down(하향 조정) 이 모든 힙 연산의 두 원자.
//   - heapify: 잎이 아닌 노드(n/2-1..0)를 sift_down -> O(n) 로 힙 구성.
//   - 힙정렬: max-heap 구성 후 top 을 끝과 교환하며 힙 크기 축소.
//   - 크기 k 힙: k번째 최대(min-heap) / top-k 빈도(min-heap) 로 O(n log k).
//   - k-way merge / 스트림 중앙값(두 힙) 은 우선순위 큐의 대표 응용.
//
// 임베디드/로보틱스 관점:
//   - 힙 = RTOS 우선순위 스케줄러/태스크 큐의 뼈대 (가장 급한 일 먼저).
//   - top-k = 최근접 표적 K개, merge = 멀티센서 스트림 시간순 병합,
//     로봇 스웜 태스크 = pop-highest 후 갱신 우선순위로 재삽입.
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

static void swap_int(int *x, int *y) { int t = *x; *x = *y; *y = t; }

// ===========================================================================
// (공용) max-heap / min-heap 의 두 원자 연산 (int 배열, 0-indexed)
//   parent=(i-1)/2, left=2i+1, right=2i+2 를 계속 재사용한다.
// ===========================================================================

// max-heap: 부모 >= 자식.  새로 넣은 원소 i 를 위로 밀어올린다.
static void sift_up_max(int *a, int i) {
    while (i > 0) {
        int p = (i - 1) / 2;
        if (a[p] >= a[i]) break;        // 부모가 크거나 같으면 정지
        swap_int(&a[p], &a[i]);
        i = p;
    }
}
// max-heap: 루트(또는 i)를 두 자식 중 큰 쪽과 교환하며 아래로 내린다.
static void sift_down_max(int *a, int n, int i) {
    for (;;) {
        int l = 2 * i + 1, r = 2 * i + 2, largest = i;
        if (l < n && a[l] > a[largest]) largest = l;
        if (r < n && a[r] > a[largest]) largest = r;
        if (largest == i) break;        // 자식보다 크면 제자리
        swap_int(&a[i], &a[largest]);
        i = largest;
    }
}
// min-heap: 부모 <= 자식.  (크기 k 힙 문제에서 사용)
static void sift_up_min(int *a, int i) {
    while (i > 0) {
        int p = (i - 1) / 2;
        if (a[p] <= a[i]) break;
        swap_int(&a[p], &a[i]);
        i = p;
    }
}
static void sift_down_min(int *a, int n, int i) {
    for (;;) {
        int l = 2 * i + 1, r = 2 * i + 2, smallest = i;
        if (l < n && a[l] < a[smallest]) smallest = l;
        if (r < n && a[r] < a[smallest]) smallest = r;
        if (smallest == i) break;
        swap_int(&a[i], &a[smallest]);
        i = smallest;
    }
}

// ===========================================================================
// P1. heap_build_sift_down  —  배열을 in-place 로 max-heap 화 (heapify)
// ---------------------------------------------------------------------------
// 왜 n/2-1 부터 아래로?  인덱스 n/2..n-1 은 전부 잎(자식 없음)이라 이미 힙.
// 잎 바로 위부터 sift_down 하면 자식 서브트리가 이미 힙임이 보장된다.
// 삽입 n번(각 O(log n)) 보다 빠른 O(n).  [PATTERN: bottom-up heapify]
// ===========================================================================
void heap_build_max(int *a, int n) {
    for (int i = n / 2 - 1; i >= 0; --i)
        sift_down_max(a, n, i);
}

// ===========================================================================
// P2. heap_push / heap_pop  —  sift_up 삽입, sift_down 삭제 (max-heap)
// ---------------------------------------------------------------------------
// n 은 현재 크기 포인터.  push: 끝에 넣고 sift_up.  pop: 루트 저장 ->
// 마지막 원소를 루트로 옮기고 크기 감소 후 sift_down.  각 O(log n).
// ===========================================================================
void max_heap_push(int *a, int *n, int val) {
    a[*n] = val;
    sift_up_max(a, *n);
    (*n)++;
}
int max_heap_pop(int *a, int *n) {          // 호출자는 *n>0 을 보장
    int top = a[0];
    a[0] = a[--(*n)];                        // 마지막 원소를 루트로
    sift_down_max(a, *n, 0);
    return top;
}

// ===========================================================================
// P3. heap_sort  —  오름차순 정렬 (max-heap 이용, in-place O(n log n))
// ---------------------------------------------------------------------------
// max-heap 구성 후 루트(최댓값)를 끝과 교환하고 힙 크기를 1 줄여 sift_down.
// 매 반복 최댓값이 뒤쪽에 확정된다.  추가 메모리 O(1) (제자리 정렬).
// ===========================================================================
void heap_sort(int *a, int n) {
    heap_build_max(a, n);                    // O(n)
    for (int end = n - 1; end > 0; --end) {
        swap_int(&a[0], &a[end]);            // 현재 최댓값을 뒤에 고정
        sift_down_max(a, end, 0);            // 줄어든 힙 재정비
    }
}

// ===========================================================================
// P4. kth_largest_element  —  크기 k 의 min-heap 으로 K번째 최대 (O(n log k))
// ---------------------------------------------------------------------------
// 지금까지 본 "가장 큰 k개"를 min-heap 에 유지.  루트=그 중 최솟값=현재 K번째.
// 새 값이 루트보다 크면 루트를 교체(replace) -> 항상 상위 k개만 남는다.
// 전체 정렬 O(n log n) 보다 메모리/시간 유리 (스트리밍 가능).
// ===========================================================================
int kth_largest(const int *a, int n, int k) {
    int *heap = (int *)malloc((size_t)k * sizeof *heap);
    int hn = 0;
    for (int i = 0; i < n; ++i) {
        if (hn < k) {                        // 아직 k개 미만 -> 그냥 삽입
            heap[hn] = a[i];
            sift_up_min(heap, hn);
            hn++;
        } else if (a[i] > heap[0]) {         // 최솟값보다 크면 교체
            heap[0] = a[i];
            sift_down_min(heap, k, 0);
        }
    }
    int res = heap[0];                        // 루트 = K번째 최대
    free(heap);
    return res;
}

// ===========================================================================
// P5. top_k_frequent  —  빈도 상위 k개 (크기 k min-heap, count 기준)
// ---------------------------------------------------------------------------
// (값,빈도) 쌍을 count 로 비교하는 min-heap 을 크기 k로 유지.  루트=최소 빈도.
// 결과는 빈도 내림차순으로 out[] 에 채운다.  O(m log k) (m=고유값 수).
// ===========================================================================
typedef struct { int val, cnt; } vc_t;

static void vc_sift_up(vc_t *h, int i) {      // count 기준 min-heap
    while (i > 0) {
        int p = (i - 1) / 2;
        if (h[p].cnt <= h[i].cnt) break;
        vc_t t = h[p]; h[p] = h[i]; h[i] = t;
        i = p;
    }
}
static void vc_sift_down(vc_t *h, int n, int i) {
    for (;;) {
        int l = 2 * i + 1, r = 2 * i + 2, s = i;
        if (l < n && h[l].cnt < h[s].cnt) s = l;
        if (r < n && h[r].cnt < h[s].cnt) s = r;
        if (s == i) break;
        vc_t t = h[i]; h[i] = h[s]; h[s] = t;
        i = s;
    }
}
// out[] 에 상위 k개 값을 빈도 내림차순으로 채우고, 실제 채운 개수 반환.
int top_k_frequent(const int *a, int n, int k, int *out) {
    // 1) 빈도 집계 (선형 스캔, 고유값 배열 구성)
    vc_t *freq = (vc_t *)malloc((size_t)(n > 0 ? n : 1) * sizeof *freq);
    int m = 0;
    for (int i = 0; i < n; ++i) {
        int j = 0;
        for (; j < m; ++j) if (freq[j].val == a[i]) { freq[j].cnt++; break; }
        if (j == m) { freq[m].val = a[i]; freq[m].cnt = 1; m++; }
    }
    if (k > m) k = m;
    // 2) 크기 k min-heap 에 상위 빈도 유지
    vc_t *heap = (vc_t *)malloc((size_t)(k > 0 ? k : 1) * sizeof *heap);
    int hn = 0;
    for (int i = 0; i < m; ++i) {
        if (hn < k) { heap[hn] = freq[i]; vc_sift_up(heap, hn); hn++; }
        else if (freq[i].cnt > heap[0].cnt) { heap[0] = freq[i]; vc_sift_down(heap, k, 0); }
    }
    // 3) heap 을 비우며 out 뒤에서부터 채우면 빈도 내림차순
    for (int i = k - 1; i >= 0; --i) {
        out[i] = heap[0].val;
        heap[0] = heap[hn - 1];
        hn--;
        vc_sift_down(heap, hn, 0);
    }
    int ret = k;
    free(heap);
    free(freq);
    return ret;
}

// ===========================================================================
// P6. merge_k_sorted_arrays  —  min-heap 으로 k-way merge (O(N log k))
// ---------------------------------------------------------------------------
// 각 배열의 현재 선두를 (값,배열idx,원소idx) 로 min-heap 에 넣는다.  루트 pop ->
// out 에 추가 -> 그 배열의 다음 원소를 push.  힙 크기는 항상 <= k.
// N=전체 원소 수.  멀티센서 정렬 스트림 병합의 정석.
// ===========================================================================
typedef struct { int val, ai, ei; } m6_t;

static void m6_sift_up(m6_t *h, int i) {
    while (i > 0) {
        int p = (i - 1) / 2;
        if (h[p].val <= h[i].val) break;
        m6_t t = h[p]; h[p] = h[i]; h[i] = t;
        i = p;
    }
}
static void m6_sift_down(m6_t *h, int n, int i) {
    for (;;) {
        int l = 2 * i + 1, r = 2 * i + 2, s = i;
        if (l < n && h[l].val < h[s].val) s = l;
        if (r < n && h[r].val < h[s].val) s = r;
        if (s == i) break;
        m6_t t = h[i]; h[i] = h[s]; h[s] = t;
        i = s;
    }
}
// arrs[k] 개의 정렬 배열(길이 lens[])을 out[] 에 오름차순 병합, 총 개수 반환.
int merge_k_sorted(const int *const *arrs, const int *lens, int k, int *out) {
    m6_t *heap = (m6_t *)malloc((size_t)(k > 0 ? k : 1) * sizeof *heap);
    int hn = 0;
    for (int i = 0; i < k; ++i)              // 각 배열의 첫 원소 삽입
        if (lens[i] > 0) {
            heap[hn].val = arrs[i][0]; heap[hn].ai = i; heap[hn].ei = 0;
            m6_sift_up(heap, hn);
            hn++;
        }
    int idx = 0;
    while (hn > 0) {
        m6_t top = heap[0];                  // 현재 최솟값 pop
        out[idx++] = top.val;
        int ne = top.ei + 1;                 // 그 배열의 다음 원소
        if (ne < lens[top.ai]) {             // 있으면 루트를 대체(replace)
            heap[0].val = arrs[top.ai][ne];
            heap[0].ai = top.ai; heap[0].ei = ne;
        } else {                             // 없으면 마지막 원소를 루트로
            heap[0] = heap[hn - 1];
            hn--;
        }
        m6_sift_down(heap, hn, 0);
    }
    free(heap);
    return idx;
}

// ===========================================================================
// P7. task_scheduler_by_priority  —  로봇 스웜 우선순위 태스크 (Anduril 시나리오)
// ---------------------------------------------------------------------------
// max-heap(우선순위) 에서 가장 급한 태스크를 pop -> 한 스텝 실행 ->
// 우선순위를 decay 만큼 낮춰 다시 push(재삽입).  0 이하가 되면 완료(재삽입 안 함).
// "가장 급한 일 먼저 + 실행 후 우선순위 재평가" 라는 실시간 스케줄러의 핵심 패턴.
// order[] 에 실행 id 순서를 기록하고 총 실행 스텝 수를 반환.
// ===========================================================================
typedef struct { int id, priority; } task_t;

static void task_sift_up(task_t *h, int i) {  // priority 기준 max-heap
    while (i > 0) {
        int p = (i - 1) / 2;
        if (h[p].priority >= h[i].priority) break;
        task_t t = h[p]; h[p] = h[i]; h[i] = t;
        i = p;
    }
}
static void task_sift_down(task_t *h, int n, int i) {
    for (;;) {
        int l = 2 * i + 1, r = 2 * i + 2, big = i;
        if (l < n && h[l].priority > h[big].priority) big = l;
        if (r < n && h[r].priority > h[big].priority) big = r;
        if (big == i) break;
        task_t t = h[i]; h[i] = h[big]; h[big] = t;
        i = big;
    }
}
int robot_swarm_schedule(const task_t *tasks, int n, int decay,
                         int *order, int cap) {
    task_t *heap = (task_t *)malloc((size_t)(n > 0 ? n : 1) * sizeof *heap);
    int hn = 0;
    for (int i = 0; i < n; ++i) {            // 초기 태스크들을 힙에 적재
        heap[hn] = tasks[i];
        task_sift_up(heap, hn);
        hn++;
    }
    int steps = 0;
    while (hn > 0 && steps < cap) {
        task_t top = heap[0];                // 가장 급한 태스크 pop
        heap[0] = heap[hn - 1];
        hn--;
        task_sift_down(heap, hn, 0);
        order[steps++] = top.id;             // 이번 스텝 실행 기록
        top.priority -= decay;               // 실행 후 우선순위 재평가
        if (top.priority > 0) {              // 아직 남았으면 갱신값으로 재삽입
            heap[hn] = top;
            task_sift_up(heap, hn);
            hn++;
        }
    }
    free(heap);
    return steps;
}

// ===========================================================================
// P8. median_from_stream  —  두 힙으로 실행 중 중앙값 (O(log n) add, O(1) 조회)
// ---------------------------------------------------------------------------
// lo = max-heap(작은 절반), hi = min-heap(큰 절반).  불변식:
//   모든 lo <= 모든 hi,  그리고  lo_n == hi_n  또는  lo_n == hi_n + 1.
// 중앙값: 크기 같으면 (lo루트+hi루트)/2, lo 가 하나 더 크면 lo 루트.
// ===========================================================================
#define MED_CAP 512
typedef struct {
    int lo[MED_CAP]; int lo_n;               // max-heap
    int hi[MED_CAP]; int hi_n;               // min-heap
} median_t;

void median_init(median_t *m) { m->lo_n = 0; m->hi_n = 0; }

void median_add(median_t *m, int v) {
    // 1) 어느 절반으로? lo 가 비었거나 v<=lo루트면 lo, 아니면 hi
    if (m->lo_n == 0 || v <= m->lo[0]) {
        m->lo[m->lo_n] = v; sift_up_max(m->lo, m->lo_n); m->lo_n++;
    } else {
        m->hi[m->hi_n] = v; sift_up_min(m->hi, m->hi_n); m->hi_n++;
    }
    // 2) 재균형: 크기 차이가 1 을 넘지 않게, lo_n >= hi_n 유지
    if (m->lo_n > m->hi_n + 1) {
        int top = m->lo[0]; m->lo[0] = m->lo[--m->lo_n];
        sift_down_max(m->lo, m->lo_n, 0);
        m->hi[m->hi_n] = top; sift_up_min(m->hi, m->hi_n); m->hi_n++;
    } else if (m->hi_n > m->lo_n) {
        int top = m->hi[0]; m->hi[0] = m->hi[--m->hi_n];
        sift_down_min(m->hi, m->hi_n, 0);
        m->lo[m->lo_n] = top; sift_up_max(m->lo, m->lo_n); m->lo_n++;
    }
}
double median_get(const median_t *m) {
    if (m->lo_n == 0) return 0.0;            // 빈 스트림
    if (m->lo_n == m->hi_n)
        return (m->lo[0] + m->hi[0]) / 2.0;  // 짝수개: 두 루트 평균
    return (double)m->lo[0];                  // 홀수개: lo 루트
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
        heap_build_max(empty, 0);              // n=0 안전
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
        int expect_desc[] = {9, 8, 5, 3, 2, 1};   // pop 순서 = 내림차순
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
        int a[] = {1, 1, 1, 2, 2, 3};         // freq: 1->3, 2->2, 3->1
        int out[8];
        int cnt = top_k_frequent(a, 6, 2, out);
        int exp[] = {1, 2};                    // 빈도 내림차순
        T("top-2 frequent -> [1,2]", cnt == 2 && arr_eq(out, exp, 2));
        int b[] = {4, 4, 4, 5, 5, 6, 6, 6, 6}; // 6->4, 4->3, 5->2
        int out2[8];
        int cnt2 = top_k_frequent(b, 9, 3, out2);
        int exp2[] = {6, 4, 5};
        T("top-3 frequent -> [6,4,5]", cnt2 == 3 && arr_eq(out2, exp2, 3));
        int out3[8];
        int cnt3 = top_k_frequent(a, 6, 5, out3);  // k > 고유값 수(3)
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
        int b1[] = {0};                        // 서로 다른 길이 + 빈 배열
        int b2[] = {0};
        (void)b2;
        const int *arrs2[] = {b0, b1, NULL};
        int lens2[] = {3, 1, 0};               // 세 번째는 빈 배열
        int out2[5];
        int total2 = merge_k_sorted(arrs2, lens2, 3, out2);
        int exp2[] = {0, 1, 2, 3};
        T("merge uneven+empty -> [0,1,2,3]", total2 == 4 && arr_eq(out2, exp2, 4));
    }

    // -------- P7 robot_swarm_schedule --------
    printf("== P7. task_scheduler_by_priority (robot swarm) ==\n");
    {
        // 태스크 3개, 실행마다 우선순위 -3, 0 이하면 완료
        task_t tasks[] = {{10, 5}, {20, 9}, {30, 2}};
        int order[32];
        int steps = robot_swarm_schedule(tasks, 3, 3, order, 32);
        // 진행: p=9(id20),9-3=6 재삽입; 이제 [6(20),5(10),2(30)]
        //       6(20)->3 재삽입; [5(10),3(20),2(30)]
        //       5(10)->2 재삽입; [3(20),2(10),2(30)]
        //       3(20)->0 완료;   [2,2]
        //       2(10)->-1 완료;  [2(30)]
        //       2(30)->-1 완료;  []
        int exp_order[] = {20, 20, 10, 20, 10, 30};
        T("swarm executes highest-priority first", steps == 6 && arr_eq(order, exp_order, 6));
        T("id20 ran 3x (9->6->3->done)",
          order[0] == 20 && order[1] == 20 && order[3] == 20);

        // cap 으로 스텝 제한
        task_t big[] = {{1, 100}};
        int o2[4];
        int s2 = robot_swarm_schedule(big, 1, 1, o2, 4);
        T("cap limits steps to 4", s2 == 4);

        // 빈 태스크셋
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

        median_t m2; median_init(&m2);         // 단조 증가 스트림
        for (int i = 1; i <= 7; ++i) median_add(&m2, i);
        T("median 1..7 == 4", median_get(&m2) == 4.0);

        median_t m3; median_init(&m3);         // 역순 입력
        int seq[] = {6, 10, 2, 6, 4, 8};       // 정렬: 2,4,6,6,8,10 -> (6+6)/2
        for (int i = 0; i < 6; ++i) median_add(&m3, seq[i]);
        T("median of 6 elems == 6", median_get(&m3) == 6.0);
    }

    // -------- 결과 --------
    printf("\n==== %d passed, %d failed ====\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
