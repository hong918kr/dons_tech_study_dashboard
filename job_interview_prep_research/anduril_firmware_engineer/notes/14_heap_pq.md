# 14. 힙 & 우선순위 큐 (Heaps & Priority Queue) — P1~P8 ⛰️

> **배열 이진 힙**은 임베디드 코딩 인터뷰의 단골이자, RTOS 우선순위 스케줄러의
> 실제 뼈대다. Anduril 류 방산/로보틱스 펌웨어에서 **힙/우선순위 큐**와
> **"로봇 스웜 우선순위 태스크"** 시나리오는 실제로 보고된 인터뷰 질문이다.

---

## 왜 Anduril 펌웨어 인터뷰에 나오나

- **RTOS 우선순위 스케줄링**: "가장 급한 태스크 먼저"는 곧 max-heap 이다. 태스크 큐,
  타이머 만료 큐, 이벤트 디스패치가 우선순위 큐로 구현된다.
- **로봇 스웜 태스크(P7)**: pop-highest -> 실행 -> 우선순위 재평가 -> 재삽입. 실시간
  스케줄러/미션 플래너의 핵심 루프. (보고된 실제 질문 시나리오)
- **top-k(P4/P5)**: 레이더/센서에서 **가장 가까운 표적 K개**, 가장 빈번한 이벤트 K개.
- **k-way merge(P6)**: 여러 정렬된 **멀티센서 스트림을 시간순 병합**.
- **스트림 중앙값(P8)**: 온라인 통계/노이즈 강건 필터(중앙값)를 O(log n) 갱신으로.
- 인터뷰어는 STL 없이 **배열 인덱스 산술(parent/child)** 과 sift 로직을 정확히
  짜는지로 C 실력을 가늠한다.

---

## 배열 이진 힙 핵심 (0-indexed)

```
        인덱스:   0
                / \
               1   2
              / \ / \
             3  4 5  6

  parent(i) = (i-1)/2
  left(i)   = 2i+1
  right(i)  = 2i+2
```

- **완전 이진 트리**라서 포인터 없이 배열 하나로 표현 -> 캐시 친화적, 힙 할당 0.
- 모든 연산은 두 원자로 환원된다:
  - **sift_up(상향 조정)**: 새로 넣은 잎을 부모와 비교하며 위로. (push)
  - **sift_down(하향 조정)**: 루트를 큰/작은 자식과 교환하며 아래로. (pop, heapify)
- max-heap: 부모 >= 자식. min-heap: 부모 <= 자식. sift 비교 방향만 뒤집으면 된다.

---

## 핵심 개념 치트시트

| 개념 | 한 줄 요약 |
|------|-----------|
| 배열 인덱싱 | parent=(i-1)/2, left=2i+1, right=2i+2. 포인터 불필요. |
| sift_up / sift_down | 힙의 두 원자. push=sift_up, pop/heapify=sift_down. |
| heapify (bottom-up) | i=n/2-1..0 sift_down -> **O(n)** (삽입 n번 O(n log n) 보다 빠름). |
| heap_sort | max-heap 후 루트↔끝 교환 + 힙 축소. in-place O(n log n). |
| 크기 k 힙 | K번째 최대/top-k 빈도를 **O(n log k)** 로 (전체 정렬 불필요). |
| k-way merge | 각 배열 선두를 min-heap 에, pop 후 다음 원소 push. 힙 크기 <= k. |
| 두 힙 중앙값 | lo(max-heap)+hi(min-heap), 크기 차 <= 1 유지. |

---

## P1. heapify — 왜 bottom-up 이 O(n)인가

```c
void heap_build_max(int *a, int n) {
    for (int i = n / 2 - 1; i >= 0; --i)  // 잎이 아닌 마지막 노드부터
        sift_down_max(a, n, i);
}
```

- 인덱스 `n/2 .. n-1` 은 전부 잎(자식 없음) -> 이미 힙. 그래서 `n/2-1` 부터 시작.
- 낮은 레벨 노드가 대부분(잎이 절반)이고 이동 거리가 짧다. 레벨별 비용 합이
  수렴해 **O(n)** (겉보기 O(n log n) 이지만 실제는 선형). 인터뷰 단골 팔로업.

---

## P2. push / pop

```c
void max_heap_push(int *a, int *n, int val) {
    a[*n] = val; sift_up_max(a, *n); (*n)++;   // 끝에 넣고 위로
}
int max_heap_pop(int *a, int *n) {
    int top = a[0];
    a[0] = a[--(*n)];                          // 마지막 원소를 루트로
    sift_down_max(a, *n, 0);                    // 아래로 정렬
    return top;
}
```

- **가드 순서**: sift_down 에서 `l < n && a[l] > a[i]` — 인덱스 범위를 **먼저** 검사
  (short-circuit)해야 배열 밖 접근 UB 를 막는다.

---

## P3. heap_sort — in-place, O(1) 추가 공간

```c
void heap_sort(int *a, int n) {
    heap_build_max(a, n);                   // O(n)
    for (int end = n - 1; end > 0; --end) {
        swap(&a[0], &a[end]);               // 최댓값을 뒤에 고정
        sift_down_max(a, end, 0);           // 줄어든 힙 재정비
    }
}
```

- max-heap 이라야 **오름차순**이 나온다(최댓값을 뒤로 밀어냄). 안정 정렬은 아님.

---

## P4/P5. 크기 k 힙 — top-k 의 정석 (O(n log k))

- **K번째 최대(P4)**: 크기 k **min-heap** 유지. 루트 = 지금까지 상위 k개 중 최소 =
  현재 K번째 최대. 새 값 > 루트면 루트 교체 후 sift_down.
- **top-k 빈도(P5)**: (값,빈도) 쌍을 **count 기준 min-heap** 으로 k개 유지.

```c
// P4 핵심 루프
if (hn < k) { heap[hn]=a[i]; sift_up_min(heap,hn); hn++; }
else if (a[i] > heap[0]) { heap[0]=a[i]; sift_down_min(heap,k,0); }
```

- 전체 정렬 O(n log n) 보다 메모리 O(k), 시간 O(n log k). **스트리밍**에서 유리.

---

## P6. k-way merge — min-heap 으로 (O(N log k))

```c
// (값, 배열idx, 원소idx) 를 min-heap 으로
push 각 배열의 첫 원소;
while (heap 비지 않음) {
    top = pop();  out[idx++] = top.val;
    if (top.ei+1 < lens[top.ai]) push(arrs[top.ai][top.ei+1]);
}
```

- 힙 크기 항상 <= k -> 총 N개에 대해 O(N log k). 배열이 매우 많을 때 pairwise merge
  보다 효율적. 빈 배열/서로 다른 길이 경계 처리에 주의.

---

## P7. 로봇 스웜 우선순위 태스크 (Anduril 시나리오 ★)

```c
// max-heap(우선순위)
초기 태스크 heapify;
while (heap 비지 않음 && steps < cap) {
    top = pop();                 // 가장 급한 태스크
    order[steps++] = top.id;     // 한 스텝 실행
    top.priority -= decay;       // 실행 후 우선순위 재평가(감쇠)
    if (top.priority > 0)        // 남았으면 갱신값으로 재삽입
        push(top);
}
```

- **패턴**: pop-highest -> 실행 -> 우선순위 갱신 -> 재삽입. aging/decay 로 기아
  (starvation)를 방지하고, 동적으로 우선순위가 바뀌는 미션을 처리한다.
- 실전 확장: 새 태스크 도착 시 push, 취소 시 lazy-deletion(무효 플래그) 또는
  decrease-key. 인터뷰에서 "우선순위가 바뀌면?"이 대표 팔로업.

---

## P8. 스트림 중앙값 — 두 힙 (add O(log n), get O(1))

```
   lo (max-heap, 작은 절반)        hi (min-heap, 큰 절반)
        [ .. , lo루트=중앙 근처 ]      [ hi루트=중앙 근처, .. ]

  불변식: 모든 lo <= 모든 hi,  lo_n == hi_n  또는  lo_n == hi_n + 1
  중앙값: 크기 같으면 (lo루트+hi루트)/2, lo 가 하나 크면 lo루트
```

```c
void median_add(median_t *m, int v) {
    if (m->lo_n == 0 || v <= m->lo[0]) push_max(lo, v);
    else                                push_min(hi, v);
    // 재균형: 크기 차가 1 을 넘으면 큰 쪽 루트를 반대편으로 이동
    if (lo_n > hi_n + 1) move lo->hi;
    else if (hi_n > lo_n) move hi->lo;
}
```

- 중앙값은 노이즈에 강건한 온라인 필터. 정렬 배열 재삽입 O(n) 대신 O(log n).

---

## 인터뷰 팔로업 & 임베디드 함정

- **Q. heapify 가 왜 O(n)?** -> 잎이 절반이고 이동거리가 레벨에 반비례, 합이 수렴.
- **Q. push n번 vs heapify?** -> 둘 다 힙을 만들지만 heapify 가 O(n) 으로 더 빠름.
- **Q. top-k 에 왜 min-heap?** -> 루트(최소)만 밀어내면 상위 k개가 유지됨. max-heap 이면
  전부 넣고 k번 pop -> O(n log n).
- **함정: 인덱스 산술** -> `2i+1/2i+2`, `(i-1)/2`. 1-indexed 와 헷갈리면 전부 깨진다.
- **함정: sift 가드 순서** -> 범위검사(`l<n`)를 비교보다 먼저. 아니면 배열 밖 접근.
- **함정: pop 후 크기 갱신** -> 마지막 원소를 루트로 옮기고 **크기를 먼저 줄인 뒤**
  sift_down. 순서 틀리면 방금 뺀 원소를 다시 본다.
- **함정: 우선순위 큐 decrease-key** -> 배열 힙은 임의 원소 위치를 모른다. 위치
  인덱스(handle)를 따로 유지하거나 lazy-deletion 필요.
- **함정(임베디드): 동적 할당** -> 위 예시는 malloc 을 쓰지만, 방산 펌웨어는 보통
  **정적 배열 + 고정 용량 힙**으로 만든다(P8 처럼 struct 내 고정 배열). 고갈 시
  우아하게 실패.
- **함정: ISR 공유** -> 스케줄러 힙을 ISR 와 main 이 함께 만지면 경쟁. 임계구역
  보호(인터럽트 마스킹) 필요.

---

## 복잡도 요약

| 연산 | 시간 | 공간 |
|------|------|------|
| heapify (build) | O(n) | O(1) |
| push / pop | O(log n) | O(1) |
| heap_sort | O(n log n) | O(1) (in-place) |
| kth_largest / top-k | O(n log k) | O(k) |
| merge k arrays | O(N log k) | O(k) |
| robot swarm step | O(log n) / step | O(n) |
| median add / get | O(log n) / O(1) | O(n) |
