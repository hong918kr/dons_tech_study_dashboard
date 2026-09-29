# Module 8 — Priority Queue / Heap (우선순위 큐·힙)

> 출처: AlgoMonster Coding Patterns, Module 8 (레슨 8개) · 한국어 개념 정리 노트
> 목표: "계속 바뀌는 집합에서 최소/최대를 반복해서 꺼낸다"를 보면 heap을 꺼내고, **Top K · K-way merge · 두 개의 힙** 패턴을 `heapq`로 바로 짜기.

## 한눈에 보기 (5분 복습용)

| 패턴/문제 | 언제 쓰나(키워드) | 핵심 아이디어 | 복잡도 |
|---|---|---|---|
| Heap 기본 | 최소/최대를 반복 추출, 집합이 계속 변함 | complete tree + 배열, bubble up/down | push/pop O(log n), heapify O(n) |
| **Top K** (K Closest, Kth Largest) | "k closest/largest/smallest", top k | 크기 k의 **반대 방향** heap, root = 탈락 후보 | O(n log k) / O(k) |
| **K-way merge** (Merge K Lists, Sorted Matrix) | k개 정렬 리스트/행, k번째 작은 | 각 리스트 head만 heap에, pop 후 같은 리스트 다음 원소 push | O(N log k) |
| **Moving best** (Reorganize, Ugly Number) | "가장 많이 남은 것부터", 순서대로 생성 | 매 스텝 현재 최선을 pop → 처리 → 파생값 push (+ seen) | O(n log k) |
| **두 개의 힙** (Median) | running median, 두 절반 | small = max heap, big = min heap, 크기차 ≤ 1 | add O(log n), get O(1) |

**Python `heapq`는 min heap.** max heap은 부호 반전, 복합 키는 `(key, tie_breaker, obj)` tuple.

**"작은 k개 → max heap, 큰 k개 → min heap"** — root는 항상 "다음에 쫓겨날 원소".

**C++ `priority_queue`는 기본 max heap** (`std::greater`로 min), Java `PriorityQueue`는 기본 min heap.

---

## 1. Heap Intro

> 섹션: Introduction

### 핵심 개념
- **Priority Queue** = 추상 자료형(ADT): `insert(key)`, `delete_min`/`delete_max`만 지원 (임의 원소 조회/삭제는 X).
- **Heap** = 그걸 구현하는 구체 자료구조. (예: 응급실 triage — 늦게 와도 중증이면 먼저)
- 배열로 구현하면? 비정렬: insert O(1) / delete_min O(n). 정렬: delete_min O(1) / insert O(n). → **heap은 둘 다 O(log n)**.

### Min heap의 두 성질
1. **거의 완전 트리(complete)**: 마지막 레벨 빼고 꽉 차 있고, 마지막 레벨은 왼쪽부터 채움 → 높이 O(log n).
2. **부모 ≤ 자식** (max heap은 반대).
- BST와 달리 **형제/같은 레벨끼리는 정렬 관계 없음**. 정렬은 root→leaf **세로 경로**에만 → 추가/삭제 시 그 경로만 고치면 됨.

### 연산
| 연산 | 방법 | 복잡도 |
|---|---|---|
| insert | 첫 빈 leaf에 넣고 **bubble up** (부모보다 작으면 swap 반복) | O(log n) |
| delete_min | root 반환 → 마지막 노드를 root로 → **bubble down** (더 작은 자식과 swap 반복) | O(log n) |
| peek | root | O(1) |
| heapify | 배열 전체를 한 번에 heap으로 | **O(n)** |

### 배열 표현 (포인터 불필요)
- 인덱스 i의 자식: `2i+1`, `2i+2` / 부모: `(i-1)//2`. (k-ary: 자식 `ki+1..ki+k`, 부모 `(i-1)//k`)

### Python `heapq` (min heap)
```python
import heapq
h = []
heapq.heappush(h, (5, "write code"))   # tuple은 앞 원소부터 비교
heapq.heappush(h, (1, "write spec"))
heapq.heappop(h)                        # (1, 'write spec')
h[0]                                    # peek

# max heap: 부호 뒤집기
heapq.heappush(h2 := [], -30); -h2[0]   # 30

# Top 3 smallest
def heap_top_3(arr: list[int]) -> list[int]:
    heapq.heapify(arr)                  # O(n)
    return [heapq.heappop(arr) for _ in range(3)]
```

### 다른 언어
- Java `PriorityQueue`: 기본 **min**. max는 `Collections.reverseOrder()`, 객체는 `Comparator.comparingInt(...)`.
- C++ `std::priority_queue`: 기본 **max**! min은 `std::greater<T>`. `pop()`은 void → `top()` 먼저.
- JavaScript: 내장 없음 → 면접에서 직접 구현해야 할 수 있음.

### 흔한 실수
- 🐛 C++ 기본이 max heap인 것 헷갈림 (comparator `a > b` → min heap).
- 🐛 tuple 첫 원소가 같을 때 두 번째 원소가 비교 불가 타입(노드 객체 등)이면 TypeError → `(key, tie_breaker_idx, obj)`.
- 🎤 "heapify가 왜 O(n)?" → 아래 레벨 노드가 대부분이고 그들은 bubble down 거리가 짧다 (합이 급수로 O(n)).
- 🔑 "k번째", "top k", "가장 작은/큰 것을 반복해서", "스트림에서 중앙값", "k개 정렬 리스트 병합" → heap.

---

## 2. K Closest Points

> 섹션: Top K

**문제:** 2D 평면의 점들 중 원점에 **가장 가까운 k개**. 예: `[(1,1),(2,2),(3,3)], k=1` → `[(1,1)]`.

**핵심 관찰:** "closest k" → heap. 비교 키 = 원점까지 거리. **sqrt는 불필요** — `x²+y²`로 비교해도 순서 같고 정수라 정확.

### 풀이 A: min heap에 전부 넣고 k번 pop
- 시간 O(n log n) (또는 heapify로 O(n + k log n)), 공간 O(n).

### 풀이 B: 크기 k의 **max heap** (정석)
1. 현재 후보 k개 중 **가장 먼 점**을 root에 두는 max heap 유지.
2. 새 점이 root보다 가까우면 root를 쫓아내고 새 점 삽입.
3. 끝나면 heap에 남은 k개가 답.

```python
import heapq

def k_closest_points(points: list[list[int]], k: int) -> list[list[int]]:
    max_heap = []                                 # (-dist, x, y)
    for x, y in points:
        d = -(x * x + y * y)                      # 부호 반전 → max heap
        if len(max_heap) < k:
            heapq.heappush(max_heap, (d, x, y))
        elif d > max_heap[0][0]:                  # root(가장 먼 것)보다 가까움
            heapq.heapreplace(max_heap, (d, x, y))
    return [[x, y] for _, x, y in sorted(max_heap, reverse=True)]
```

**복잡도:** B는 시간 O(n log k), 공간 O(k). (n ≫ k거나 스트림일 때 유리)

- 🐛 "가장 작은 k개"에 **max heap**, "가장 큰 k개"에 **min heap** — 직관과 반대. root는 "지금 쫓겨날 후보"다.
- 🐛 `heapreplace`(pop 후 push)와 `heappushpop`(push 후 pop) 의미 차이 주의.
- ✅ 대안: quickselect 평균 O(n), 최악 O(n²).
- 🔑 "k closest", "top k", "k most frequent", "스트림에서 k개 유지" → **크기 k 힙 (반대 방향)**.

---

## 3. Merge K Sorted Lists

**문제:** 정렬된 리스트 k개를 하나의 정렬 리스트로 병합. 예: `[[1,3,5],[2,4,6],[7,10]]` → `[1,2,3,4,5,6,7,10]`.

**핵심 관찰:** 전부 합쳐 정렬하면 O(N log N) — **"각 리스트가 정렬돼 있다"는 조건을 안 쓴 것**.
- 각 리스트의 **현재 head**만 후보 풀(크기 k)에 두면, 풀의 최솟값 = 전체 다음 원소.
- 꺼낸 원소가 속한 리스트의 다음 원소를 풀에 보충 → "k개 스트림 중 최솟값 반복 추출" = **min heap**.

**풀이**
1. 각 리스트의 첫 원소를 `(값, 리스트 번호, 인덱스)`로 push.
2. pop → 결과에 추가 → 같은 리스트에 다음 원소가 있으면 push.
3. heap이 빌 때까지 반복.

```python
import heapq

def merge_k_sorted_lists(lists: list[list[int]]) -> list[int]:
    heap = [(lst[0], i, 0) for i, lst in enumerate(lists) if lst]
    heapq.heapify(heap)
    res = []
    while heap:
        val, i, j = heapq.heappop(heap)
        res.append(val)
        if j + 1 < len(lists[i]):
            heapq.heappush(heap, (lists[i][j + 1], i, j + 1))
    return res
```

**복잡도:** 시간 O(N log k) (N = 전체 원소 수, heap 크기 항상 ≤ k), 공간 O(k) (출력 제외).

- 🐛 빈 리스트의 `lst[0]` 접근 → IndexError. 필터링 필수.
- 🐛 값이 같을 때 tuple 두 번째 원소로 비교가 넘어감 → 리스트/노드 객체를 넣으면 비교 에러 가능. **정수 인덱스 i를 tie-breaker로**.
- ✅ 전부 한 heap에 넣으면 O(N log N) — n ≫ k일 때 느림. 대안: 분할 정복으로 두 개씩 병합해도 O(N log k).
- 🎤 임베디드 연결: 여러 센서 스트림(타임스탬프 정렬)을 하나의 타임라인으로 합치는 **k-way merge**와 동일.
- 🔑 "k sorted lists/arrays/streams", "merge", "smallest range covering k lists" → head만 담는 크기 k min heap.

---

## 4. Kth Largest Element in an Array

**문제:** 비정렬 배열에서 **k번째로 큰 원소** (중복 포함 정렬 순서 기준, distinct 아님). 예: `[3,2,1,5,6,4], k=2` → `5`; `[3,2,3,1,2,4,5,5,6], k=4` → `4`.

**핵심 관찰:** 정렬 후 인덱싱은 O(n log n). **지금까지 본 것 중 큰 k개**만 유지하면 그 중 가장 작은 것 = k번째로 큰 것 → 크기 k **min heap**, 답은 `heap[0]`.

**풀이**
1. 처음 k개로 min heap 구성.
2. 나머지 원소가 root보다 크면 root를 빼고 그 원소를 넣는다.
3. 끝나면 `heap[0]` 반환.

```python
import heapq

def find_kth_largest(nums: list[int], k: int) -> int:
    heap = nums[:k]
    heapq.heapify(heap)                 # O(k)
    for x in nums[k:]:
        if x > heap[0]:
            heapq.heapreplace(heap, x)  # pop + push, O(log k)
    return heap[0]
```

**복잡도:** 시간 O(n log k), 공간 O(k).

### 대안: Quickselect
- quicksort의 partition만 쓰고, pivot 최종 위치가 `k-1`(내림차순 기준)인지 보고 **한쪽으로만** 재귀/반복.
- 평균 O(n), 최악 O(n²) (랜덤 pivot으로 완화). 인덱스 실수가 잦아 **면접에선 heap 풀이가 안전**, quickselect는 후속 질문용.

- 🐛 "k번째 큰"에 max heap을 쓰면 전체를 넣고 k번 pop → O(n + k log n). 틀리진 않지만 메모리 O(n).
- 🐛 distinct 여부 확인 — 이 문제는 중복을 **센다**.
- ✅ 스트림 버전(LC 703 Kth Largest in a Stream)도 동일: 크기 k min heap을 유지하며 매 add 후 `heap[0]`.
- 🔑 "kth largest/smallest", "top k" → 크기 k 반대 방향 heap / quickselect.

---

## 5. Kth Smallest Element in a Sorted Matrix

**문제:** n×n 행렬, 각 행과 각 열이 오름차순. 전체 n² 값 중 **k번째로 작은 값** (중복도 센다). 예: `[[1,5,9],[10,11,13],[12,13,15]], k=8` → `13`.

**핵심 관찰:** 펼쳐서 정렬하면 O(n² log n) — 행/열 정렬 구조를 버린 것. **각 행 = 정렬 리스트 n개** → Merge K Sorted Lists처럼 "여러 후보 중 다음 최솟값" = min heap.

### 풀이 A: 행 포인터 min heap (기본, 추천)
1. 각 행의 첫 원소 `(값, r, 0)`을 heap에.
2. k−1번: pop → 같은 행의 다음 열 `(r, c+1)` push.
3. k번째 pop(= 그 후 `heap[0]`)이 답.

```python
import heapq

def kth_smallest(matrix: list[list[int]], k: int) -> int:
    n = len(matrix)
    heap = [(matrix[r][0], r, 0) for r in range(min(n, k))]  # k보다 많은 행은 불필요
    heapq.heapify(heap)
    for _ in range(k - 1):
        _, r, c = heapq.heappop(heap)
        if c + 1 < n:
            heapq.heappush(heap, (matrix[r][c + 1], r, c + 1))
    return heap[0][0]
```

**복잡도:** O(n + k log n) 시간, O(n) 공간. (행 수를 `min(n,k)`로 자르면 O(k log min(n,k)))

### 풀이 B: 대각선 파동(frontier) 확장 (강의의 최적화)
- `(0,0)`만 넣고 시작. `(r,c)`를 pop하면 오른쪽 `(r,c+1)`과 아래 `(r+1,c)`가 후보.
- 단, **그보다 먼저 나와야 할 칸이 다 처리됐을 때만** push → `row_first[r]`(행 r의 미처리 첫 열), `column_top[c]`(열 c의 미처리 첫 행)로 추적. 오른쪽은 `column_top[c+1] == r`, 아래는 `row_first[r+1] == c`일 때만.
- heap 크기 ≤ min(k, n) → O(k log min(k, n)). k가 작을 때 유리하지만 구현이 까다롭다.

- 🐛 방문 체크 없이 오른쪽/아래를 모두 push하면 같은 칸이 **중복 삽입** → visited set 또는 위 frontier 규칙 필요.
- ✅ 또 다른 대안: **값 범위 이진 탐색** — `count(≤ mid)`를 좌하단에서 O(n) 계단식으로 세고 First True (Module 2와 연결). O(n log(max−min)).
- 🔑 "행·열 정렬 행렬", "k번째 작은", "k smallest pairs/sums" → k-way merge heap 또는 값 이진 탐색.

---

## 6. Reorganize String

> 섹션: Moving Best

**문제:** 문자열 s의 문자를 재배열해 **인접한 두 문자가 같지 않게** 만들 수 있으면 아무 결과나, 불가능하면 `""`. 예: `"aab"` → `"aba"`; `"aaab"` → `""`.

**핵심 관찰:**
- 인덱스를 **짝수 칸 / 홀수 칸**으로 나누면 짝수 칸끼리는 절대 인접하지 않는다. 짝수 칸 수 = `(n+1)//2` (항상 홀수 칸 이상).
- 가장 많은 문자 빈도 > `(n+1)//2` → **불가능**. 아니면 가장 많은 문자부터 짝수 칸 0,2,4,…를 채우고, 넘치면 홀수 칸 1,3,5,…로 이어 채우면 항상 성공.
- "가장 많은 것부터" 처리 → 빈도 max heap.

**풀이**
1. `Counter`로 빈도, `(-count, ch)`로 max heap.
2. 최대 빈도 > `(n+1)//2`면 `""`.
3. heap에서 빈도 큰 순으로 꺼내 pointer 0부터 2칸씩 채우고, `pointer >= n`이면 1로 리셋.

```python
from collections import Counter
import heapq

def reorganize_string(s: str) -> str:
    n = len(s)
    pq = [(-cnt, ch) for ch, cnt in Counter(s).items()]
    heapq.heapify(pq)
    if -pq[0][0] > (n + 1) // 2:
        return ""
    res, p = [""] * n, 0
    while pq:
        cnt, ch = heapq.heappop(pq)
        for _ in range(-cnt):
            res[p] = ch
            p += 2
            if p >= n:          # 짝수 칸 소진 → 홀수 칸
                p = 1
    return "".join(res)
```

**복잡도:** 시간 O(n + k log k) (k = 서로 다른 문자 수 ≤ 26), 공간 O(k).

- 🐛 **최빈 문자를 반드시 먼저** 짝수 칸에 넣어야 한다 — 그렇지 않으면 최빈 문자가 짝수→홀수 경계를 넘으며 인접할 수 있다. (나머지 순서는 무관, 정렬로 대체 가능)
- ✅ 다른 흔한 풀이(greedy heap): 매 스텝 직전 문자와 다른 **가장 많이 남은 문자**를 pop해 붙이고, 직전 문자는 한 턴 쉬었다 다시 push → O(n log k). Task Scheduler(LC 621)와 같은 "moving best" 패턴.
- 🔑 "인접한 같은 문자 없이 재배열", "cooldown", "task scheduler", "가장 많이 남은 것부터 배치" → 빈도 max heap.

---

## 7. Ugly Number

**문제:** 소인수가 2, 3, 5뿐인 양수(1 포함)를 ugly number라 할 때 **n번째** ugly number. 예: `n=10` → `12` (1,2,3,4,5,6,8,9,10,12).

**핵심 관찰:**
- 브루트포스: 범위 내 `2^i·3^j·5^k`를 전부 만들고 정렬 → 가능은 하지만 낭비.
- **순서대로 생성**: 모든 ugly number는 더 작은 ugly number × {2,3,5}. 지금까지 만든 것 중 **가장 작은 미처리 수**를 꺼내 ×2, ×3, ×5를 후보에 추가 → 꺼내는 순서가 곧 오름차순. "현재 최솟값 반복 추출" = min heap.
- 같은 수가 여러 경로로 생성됨 (6 = 2×3 = 3×2) → **hash set으로 중복 제거**.

**풀이**
1. heap = [1], seen = {1}.
2. n−1번: pop한 값 v에 대해 `v*2, v*3, v*5` 중 처음 보는 것만 push + seen 추가.
3. `heap[0]`이 n번째.

```python
import heapq

def nth_ugly_number(n: int) -> int:
    heap, seen = [1], {1}
    for _ in range(n - 1):
        v = heapq.heappop(heap)
        for p in (2, 3, 5):
            nxt = v * p
            if nxt not in seen:
                seen.add(nxt)
                heapq.heappush(heap, nxt)
    return heap[0]
```

**복잡도:** 시간 O(n log n), 공간 O(n) (heap에 최대 ~3n).

- 🐛 seen 없이 push → 중복이 쌓여 같은 수를 여러 번 세게 됨.
- ✅ 최적: **세 포인터 DP** — `ugly[i] = min(ugly[i2]*2, ugly[i3]*3, ugly[i5]*5)`, 선택된 포인터(동률이면 모두) 전진 → O(n) 시간, heap 불필요. (k-way merge를 포인터로 푼 것)
- 🎤 C/C++에선 곱셈 overflow 주의 (n ≤ 1690이면 int32 범위 내).
- 🔑 "n번째 수열 원소", "소인수가 ~뿐인", "작은 것부터 생성" → min heap + seen, 또는 다중 포인터 병합.

---

## 8. Median of Data Stream

> 섹션: Multiple Heaps

**문제:** 숫자가 스트림으로 들어올 때 언제든 **현재 중앙값**을 반환 (`add_number`, `get_median`). 예: 1,2,3 추가 → `2.0`; 4 추가 → `2.5`.

**핵심 관찰:**
- 매번 정렬 O(n log n), 이진 탐색 삽입도 shift 때문에 O(n).
- 중앙값 정의로 돌아가면: **절반은 작고 절반은 크다.** 전체 정렬은 필요 없고, 두 절반의 **경계값**만 빨리 알면 된다.
- `small` = 작은 절반 (**max heap**, top = 작은 쪽 최댓값), `big` = 큰 절반 (**min heap**, top = 큰 쪽 최솟값).

**불변식 (삽입마다 유지)**
1. **순서:** `small`의 모든 값 ≤ `big`의 모든 값.
2. **크기:** `len(small) == len(big)` 또는 `len(small) == len(big) + 1`.
- 홀수 개면 `small` top이 중앙값, 짝수 개면 두 top의 평균.

**풀이**
1. `big`이 비었거나 `num < big[0]` → `small`에, 아니면 `big`에 push (순서 불변식).
2. `big`이 더 크면 `big`의 min을 `small`로, `small`이 2개 이상 많으면 `small`의 max를 `big`으로 (크기 불변식).

```python
import heapq

class MedianOfStream:
    def __init__(self):
        self.small = []   # max heap (음수 저장)
        self.big = []     # min heap

    def add_number(self, num: float) -> None:
        if not self.big or num < self.big[0]:
            heapq.heappush(self.small, -num)
        else:
            heapq.heappush(self.big, num)
        # 크기 재조정
        if len(self.small) < len(self.big):
            heapq.heappush(self.small, -heapq.heappop(self.big))
        elif len(self.small) > len(self.big) + 1:
            heapq.heappush(self.big, -heapq.heappop(self.small))

    def get_median(self) -> float:
        if len(self.small) == len(self.big):
            return (-self.small[0] + self.big[0]) / 2
        return -self.small[0]
```

**복잡도:** `add_number` O(log n), `get_median` O(1), 공간 O(n).

- 🐛 max heap 부호 반전을 꺼낼 때 **다시 뒤집는 것** 잊기 (`-self.small[0]`).
- 🐛 크기 불변식만 맞추고 순서 불변식을 깨면 틀린 중앙값 — 삽입 위치를 top과 비교해 정하거나, "일단 small에 넣고 small의 max를 big으로 옮긴 뒤 크기 조정"하는 방식으로 둘 다 보장.
- ✅ 후속: 슬라이딩 윈도우 중앙값(LC 480)은 삭제가 필요 → lazy deletion 또는 정렬 컨테이너.
- 🎤 임베디드 연결: 센서 노이즈 제거용 **running median filter**. 윈도우가 작으면 정렬 배열 삽입이 캐시상 더 빠를 수 있다는 점까지 언급하면 좋다.
- 🔑 "stream", "running median", "언제든 중간값", "두 절반" → **두 개의 힙(max + min)**.

---

## 용어·패턴 사전

| 용어 | 뜻 |
|---|---|
| priority queue | insert와 min/max 삭제만 지원하는 추상 자료형(ADT) |
| heap | priority queue를 구현하는 complete tree 기반 자료구조 |
| min heap / max heap | 부모 ≤ 자식 / 부모 ≥ 자식. root가 최소 / 최대 |
| complete tree | 마지막 레벨 외엔 꽉 차고 마지막 레벨은 왼쪽부터 채운 트리 → 높이 O(log n) |
| bubble up (sift up) | 삽입 후 부모와 비교·swap하며 올라가 성질 복구 |
| bubble down (sift down) | root 교체 후 더 작은 자식과 swap하며 내려가 성질 복구 |
| heapify | 배열 전체를 O(n)에 heap으로 만드는 연산 |
| `heapreplace` / `heappushpop` | pop 후 push / push 후 pop을 한 번의 O(log n)으로 |
| Top K | 크기 k heap을 유지하며 상위 k개를 고르는 패턴 |
| k-way merge | k개 정렬 시퀀스의 head만 heap에 두고 병합하는 패턴 |
| quickselect | partition으로 k번째 원소를 평균 O(n)에 찾는 선택 알고리즘 |
| two heaps | max heap(작은 절반) + min heap(큰 절반)으로 중앙값 유지 |
| lazy deletion | heap에서 바로 지우지 않고 표시만 한 뒤 top에 올 때 버리는 기법 |
