# Module 3 — Two Pointers (투 포인터·슬라이딩 윈도우·Prefix Sum)

> 출처: AlgoMonster Coding Patterns, Module 3 (레슨 31개) · 한국어 개념 정리 노트
> 목표: 선형 구조(배열·문자열·연결 리스트)에서 **불변식(invariant)을 지키며 포인터를 한 방향으로만 움직여** O(n²) 브루트포스를 O(n)으로 줄이는 판단력과 템플릿을 몸에 익히기.

## 한눈에 보기 (5분 복습용)

| 패턴 | 언제 쓰나 (키워드) | 핵심 아이디어 | 복잡도 |
|---|---|---|---|
| Same direction (read/write) | in-place, remove/move, 상대 순서 유지 | slow 앞은 이미 정답 prefix, fast가 읽고 조건 맞으면 복사 | O(n) / O(1) |
| Fast/slow (속도 2:1) | 리스트 중간, 사이클, O(1) space | fast 끝 도달 시 slow = 중간 / 사이클이면 반드시 만남 | O(n) / O(1) |
| Fixed gap | 뒤에서 n번째, one pass | dummy + fast를 n칸 먼저 → 함께 전진 | O(n) / O(1) |
| Opposite direction | 정렬 + 쌍 target, 회문, 최대 넓이 | 이동마다 불가능한 후보 제거 (단조성·대칭·bottleneck) | O(n) / O(1) |
| Fixed sliding window | 길이 k 연속 구간, anagram | 들어온 것 +, 나간 것 − | O(n) / O(1)~O(Σ) |
| Longest window | longest ... such that (단조 조건) | `while invalid` 축소, for 끝에서 max | O(n) |
| Shortest window | minimum length ... ≥ target, 모든 문자 포함 | `while valid` 안에서 min 후 축소 | O(n) |
| Prefix sum | 반복 구간 합, immutable | `prefix[j]-prefix[i]`, 길이 n+1, 오른쪽 exclusive | 전처리 O(n), 질의 O(1) |
| Prefix + hash map | subarray sum = k, **음수 포함**, 개수 | `cur-target`이 이전에 나왔나 (`{0:0}`/`{0:1}` 초기화) | O(n) / O(n) |
| Prefix × suffix | except self, no division | 왼쪽 곱 패스 → 오른쪽 곱 패스 | O(n) / O(1) |
| Merge 투 포인터 (Teleporter) | 두 정렬 배열, 공통 값에서 갈아타기 | 공통 값으로 구간 분할, 구간별 max 합 | O(n+m) / O(1) |

**포인터 이동에는 항상 증명이 붙는다:** "이 이동이 어떤 후보를 배제했고, 왜 안전한가?"를 못 말하면 추측이다.

**분류 순서:** ① 연속 구간 유지? → sliding window ② 한쪽을 안전하게 제거? → opposite ③ in-place 재작성? → same direction ④ 리스트 상대 위치? → fast/slow.

**Sliding window는 원소가 비음수(단조)일 때만.** 음수가 섞인 subarray sum은 prefix sum + 해시맵.

**경계가 두 개라고 sliding window가 아니다** — Container의 구간은 탐색 공간, sliding window의 구간은 답 그 자체.

---

## 1. Two Pointers Introduction

> 섹션: Intro

### 핵심 개념
- 배열·문자열처럼 **순회 가능한 구조** 위에서 위치 두 개를 유지하며 움직이는 기법. 두 번째 위치가 첫 번째에서 계산되면(예: `i`와 `i+k`) 변수 하나여도 "투 포인터"다.
- 정해진 구현이 하나 있는 게 아니라 **공통 뼈대**가 있다:
  1. 두 포인터가 움직인다 (같은 방향 / 반대 방향, 함께 또는 따로)
  2. 두 포인터가 가리키는 값을 **검사**하고, 그게 답과 연결된다
  3. **다음에 어느 포인터를 움직일지** 정하는 단순한 규칙
  4. 포인터가 움직일 때마다 답을 갱신하는 방법

### 세 가지 대표 모양
| 모양 | 미니 예제 | 동작 |
|---|---|---|
| **Same direction** | "ace"가 "abcde"의 subsequence인가? | 긴 문자열 포인터는 매 스텝 전진, 글자가 일치할 때만 짧은 쪽 포인터 전진. 짧은 쪽이 끝에 닿으면 True |
| **Opposite direction** | 회문(palindrome)인가? | 양 끝에서 비교 → 같으면 둘 다 안쪽으로. 만나면 True |
| **Sliding window** | 연속 k개 합의 최댓값 | 첫 k개 합 → 오른쪽 하나 더하고 왼쪽 하나 빼며 O(1) 갱신 |

- Sliding window는 두 끝점만이 아니라 **사이 구간 전체**의 상태(합, 빈도)를 다룬다는 점이 일반 투 포인터와 다름. 크기가 고정이 아니라 늘었다 줄었다 하면 "중복 없는 최장 부분문자열" 같은 문제로 확장.
- 연결 리스트에도 적용: **Floyd cycle 탐지**(fast/slow)도 투 포인터.

```python
def is_subsequence(s: str, t: str) -> bool:   # same direction
    i = 0
    for ch in t:
        if i < len(s) and s[i] == ch:
            i += 1
    return i == len(s)
```

### 왜 쓰나
- 모든 쌍을 보는 이중 루프 O(n²) → 한 번 훑는 O(n). 핵심은 **각 포인터가 뒤로 돌아가지 않는다**는 것.

- 🎤 "투 포인터를 어떻게 알아보나?" → 선형 구조 + 쌍/구간을 봐야 하는데 브루트포스가 O(n²)이고, 포인터를 **한 방향으로만** 움직여도 답을 놓치지 않는다는 근거(정렬, 단조성)가 있을 때.

---

## 2. Understanding Invariants

> 섹션: Core Concepts

### 핵심 개념
- **Invariant(불변식)** = 루프가 도는 동안 "매 반복의 시작/끝 시점에" 항상 참이어야 하는 명제. 반복 **도중에는 잠깐 깨져도 되지만**, 다음 반복 전에는 복구돼야 한다.
- invariant를 말로 못 하면 패턴을 기계적으로 따라하고 있을 가능성이 높다.

### 예: 상자 A → B로 하나씩 옮기기
- invariant: `len(A) + len(B) == total`
- `A.pop()` 직후엔 손에 든 하나 때문에 깨짐 → `B.append()`로 복구.

```python
total = len(A) + len(B)
while A:
    item = A.pop()      # 잠깐 깨짐
    B.append(item)      # 반복이 끝나기 전 복구
    assert len(A) + len(B) == total
```

### 정확성 증명의 3단계 (귀납법)
1. **초기화:** 첫 반복 전에 참이다.
2. **유지:** 시작 시 참이면 루프 본문이 끝날 때도 참이다.
3. **종료:** 루프가 끝나는 이유(종료 조건) + invariant ⇒ 원하는 결과(**postcondition**).
   - 예: A가 비어서 종료 + `len(A)+len(B)==total` ⇒ `len(B)==total`, 모든 아이템이 B에.
- 추가로 루프가 **반드시 종료**해야 한다 (각 반복마다 뭔가 단조 감소).

### 투 포인터에 적용
- 포인터 이동을 외우지 말고 **"포인터를 움직인 뒤에도 무엇이 참이어야 하나?"**를 물을 것.
- 🐛 invariant 점검으로 잡히는 버그: 원소 건너뛰기, 후보를 잃어버림, 너무 일찍 멈춤.
- 🎤 "이 루프가 맞다는 걸 어떻게 아나?" → invariant를 명시하고 초기화·유지·종료로 설명.

---

## 3. Common Invariant Shapes

### 네 가지 자주 나오는 invariant 모양
| # | 모양 | invariant | 갱신 | 대표 예 |
|---|---|---|---|---|
| 1 | **완료 영역이 정확** | 이미 만든 영역은 최종 답의 올바른 앞부분 | 안전한 한 칸 확장 | merge(두 정렬 배열 병합), remove duplicates |
| 2 | **남은 후보 안에 답이 있다** | 답이 있다면 현재 범위 안에 있다 | 답일 수 없는 쪽을 버림 | binary search, two sum sorted |
| 3 | **활성 상태가 유효** | 지금 쓰는 상태(윈도우)가 규칙을 만족 | 바꾸고 → 깨지면 **수리** 후에만 사용 | 중복 없는 sliding window |
| 4 | **변수 간 관계 유지** | 포인터 사이의 간격/속도비/순서가 유지 | 협조적으로 이동 | fast/slow (중간점, cycle) |

- 1번: merge에서 다음 후보가 5와 4면 4를 붙인다 — 다음에 올 수 있는 가장 작은 값이므로 출력은 여전히 정렬 상태.
- 2번: target 11, mid=9 → 9 이하 전부 버려도 안전.
- 3번: right 확장으로 중복 생기면 left를 당겨 중복 제거 **후에** 길이 측정.
- 4번: slow 1칸, fast 2칸 → 관계가 매 반복 의미를 유지하므로 중간점·사이클 추론 가능.

- ✅ 체크포인트: 새 루프를 보면 "네 모양 중 뭐에 가까운가?" 먼저 분류 → invariant 진술이 쉬워진다.
- 🎤 투 포인터 문제 설명 시 "left 앞은 완료 영역", "[l, r] 밖에는 답이 없다", "윈도우는 항상 유효" 같은 문장 하나로 시작하면 설득력이 크게 오른다.

---

## 4. Same Direction Family Map

> 섹션: Same Direction

### 핵심 개념
- 둘 다 왼→오른쪽으로 움직이지만 **역할이 다르다**: 하나는 입력에서 다음 값을 **읽고(read/fast)**, 하나는 다음으로 남길 값이 들어갈 자리를 **표시(write/slow)**.
- 또는 둘이 함께 전진하되 한쪽이 뒤처져 **간격·관계**를 유지.

### 언제 쓰나
- 배열을 **in-place로 재작성**, 추가 메모리 없이 원소 제거/압축, 조건 맞는 값만 남기기, 두 전진 포인터 사이 **고정 간격** 유지.

### 하위 유형과 invariant
| 유형 | 예 | invariant |
|---|---|---|
| Compaction / overwrite | Remove Duplicates, Move Zeros | **write 포인터 앞은 이미 최종 형태** (prefix) |
| Relative speed | Middle of Linked List | fast는 slow의 **2배 속도** (소비한 양의 비율) |
| Fixed gap | Remove N-th from End | 두 포인터 사이 **거리 n 유지** |

- 공통 아이디어: "포인터 **왼쪽 구조는 이미 알려진 의미를 가진다**."

```python
def compact(arr, keep):          # 일반 compaction 템플릿
    w = 0
    for r in range(len(arr)):
        if keep(arr[r]):
            arr[w] = arr[r]
            w += 1
    return w                      # arr[:w]가 결과
```

### 흔한 실수
- 🐛 현재 값을 복사하기 **전에** write 포인터를 올림 (순서 혼동).
- 🐛 어느 영역이 이미 유효한지 잊음.
- 🐛 invariant가 prefix / gap / 속도 중 무엇인지 헷갈림.
- ✅ 체크포인트: 매 문제마다 "지금 slow 앞 구간은 무슨 의미인가?"를 자문.

---

## 5. Remove Duplicates

**문제:** 정렬된 배열(길이 ≥ 1)에서 중복을 **in-place, O(1) 추가 메모리**로 제거하고 새 길이 반환. 예: `[0,0,1,1,1,2,2]` → `3` (앞 3칸이 `0,1,2`).

**핵심 관찰:** 해시셋은 금지. 하지만 **정렬**돼 있으므로 같은 값은 연속으로 붙어 있고, B를 본 뒤엔 A가 다시 나오지 않는다 → "직전에 남긴 값과 다르면 새 값".

**풀이**
1. `slow = 0` : `arr[0..slow]`는 중복 없는 결과 (invariant).
2. `fast`로 전체 스캔. `arr[fast] != arr[slow]`이면 `slow += 1` 후 `arr[slow] = arr[fast]`.
3. 길이는 `slow + 1`.

```python
def remove_duplicates(arr: list[int]) -> int:
    slow = 0
    for fast in range(len(arr)):
        if arr[fast] != arr[slow]:
            slow += 1
            arr[slow] = arr[fast]
    return slow + 1
```

**복잡도:** 시간 O(n), 공간 O(1).

- 🐛 여기서 slow는 "마지막으로 남긴 원소의 인덱스"(포함). compaction 템플릿의 `w`(다음 쓸 자리)와 정의가 1 차이 → 반환값 `slow+1` 주의.
- 🐛 모두 같은 값 / 중복 없음 케이스로 검증.
- 🔑 "sorted" + "in-place" + "remove duplicates / return new length" → same-direction compaction.

---

## 6. Middle of a Linked List

**문제:** 연결 리스트의 중간 노드 값 반환. 짝수 길이면 **두 번째** 중간. 예: `0 1 2 3 4` → `2`, `0 1 2 3 4 5` → `3`.

**핵심 관찰:** 배열이면 길이/2로 바로 접근하지만, 리스트는 길이를 알려면 한 번 순회해야 함(2-pass). **한 번에** 하려면 fast(2칸)·slow(1칸) — fast가 끝에 닿을 때 slow는 정확히 절반을 소비.

**풀이**
1. `slow = fast = head`
2. `while fast and fast.next:` fast 2칸, slow 1칸.
3. 종료 시 slow가 중간 (짝수면 두 번째 중간).

```python
def middle_of_linked_list(head: Node) -> int:
    slow = fast = head
    while fast and fast.next:
        fast = fast.next.next
        slow = slow.next
    return slow.val
```

**복잡도:** 시간 O(n) (정확히는 n/2 스텝), 공간 O(1).

- 🐛 루프 조건에서 `fast`와 `fast.next` **둘 다** 검사: 홀수 길이면 fast가 마지막 노드에서 멈추고(`fast.next is None`), 짝수 길이면 fast가 `None`에 떨어진다.
- 🐛 첫 번째 중간이 필요하면 `while fast.next and fast.next.next` 로 바꾼다 (merge sort on list 분할 시).
- 🔑 "linked list" + "middle / 한 번의 순회로" → fast/slow (relative speed).

---

## 7. Move Zeros

**문제:** 0을 모두 뒤로 보내되 0이 아닌 원소의 **상대 순서 유지**, in-place·O(1) 공간. 예: `[1,0,2,0,0,7]` → `[1,2,7,0,0,0]`.

**핵심 관찰:** 새 배열에 non-zero를 모은 뒤 복사해 오는 O(n) 공간 풀이에서 출발. 스캔 위치는 **절대 새 배열 길이보다 뒤처지지 않으므로**(k개 봤으면 non-zero는 최대 k개) 새 배열을 원본의 **이미 스캔한 앞부분**에 겹쳐 둘 수 있다 → in-place compaction.

**풀이 (swap 버전)**
1. `slow` = 다음 non-zero가 들어갈 자리 (= 있으면 가장 앞의 0 위치).
2. `fast`로 스캔, `nums[fast] != 0`이면 `nums[slow]`와 swap 후 `slow += 1`.
3. invariant: `nums[:slow]`는 non-zero들이 원래 순서로, `nums[slow:fast]`는 전부 0.

```python
def move_zeros(nums: list[int]) -> None:
    slow = 0
    for fast in range(len(nums)):
        if nums[fast] != 0:
            nums[slow], nums[fast] = nums[fast], nums[slow]
            slow += 1
```
- 대안: non-zero를 `nums[i]`에 덮어쓰며 모은 뒤 나머지를 0으로 채우기 (역시 O(1) 공간, 쓰기 횟수는 더 많을 수 있음).

**복잡도:** 시간 O(n), 공간 O(1).

- 🐛 순서 유지가 핵심 — 양끝 투 포인터로 0과 뒤쪽 원소를 바꾸면 순서가 깨짐.
- 🐛 전부 0 / 0 없음 케이스.
- 🔑 "in-place" + "relative order 유지" + "특정 값 뒤로/제거" → same-direction compaction (stable partition).

---

## 8. Remove N-th Node from End of Linked List

**문제:** 단일 연결 리스트에서 **뒤에서 n번째** 노드(1-indexed)를 제거하고 head 반환. 예: `[1,2,3,4], n=2` → `[1,2,4]`; `n=4` → `[2,3,4]` (head 제거).

**핵심 관찰:** 단일 리스트는 앞으로만 갈 수 있는데 "뒤에서" 세야 한다 → 두 포인터 사이에 **간격 n을 유지**하면, 앞 포인터가 끝에 닿을 때 뒤 포인터가 삭제 대상 **바로 앞**에 선다.

**풀이**
1. `dummy → head` 만들고 fast, slow 모두 dummy에서 시작 (head 삭제를 특별 처리하지 않기 위해).
2. fast를 n칸 전진.
3. `fast.next`가 None이 될 때까지 둘 다 한 칸씩 → slow는 대상의 이전 노드.
4. `slow.next = slow.next.next`, **`dummy.next` 반환** (원래 `head` 변수는 지워진 노드를 가리킬 수 있음).

```python
def remove_nth_from_end(head, n):
    dummy = Node(0, head)
    fast = slow = dummy
    for _ in range(n):
        fast = fast.next
    while fast.next:
        fast, slow = fast.next, slow.next
    slow.next = slow.next.next
    return dummy.next
```

**복잡도:** 시간 O(L), 공간 O(1).

- 🐛 왜 n+1이 아니라 n칸? dummy에서 출발 + 종료 조건 `fast.next is None` 조합이라 slow가 대상의 **직전**에서 멈춘다. 종료 조건을 `fast is None`으로 바꾸면 n+1칸 먼저 보내야 함.
- 🐛 head 삭제(n == 길이), 노드 1개 → dummy가 해결.
- 🔑 "n-th from the end" + "one pass" → fixed-gap 투 포인터 + dummy node.

---

## 9. Opposite Direction Family Map

> 섹션: Opposite Direction

### 핵심 개념
- 왼쪽 끝과 오른쪽 끝에서 시작해 **안쪽으로** 이동. 루프 자체는 쉽고, 어려운 건 **"어느 포인터를 움직여도 안전한가"를 정당화**하는 것.

### 언제 쓰나
- 입력이 **정렬**됐거나 정렬로 단조성을 만들 수 있을 때
- 왼쪽 선택과 오른쪽 선택을 비교할 때
- **대칭성**이 있을 때 (회문)
- 한쪽을 움직이면 불가능한 답 집합을 **통째로 제거**할 수 있을 때

### Core invariant
- **"현재 [left, right] 밖의 후보 답은 이미 모두 배제됐다."** (Invariant shape #2: 남은 후보 안에 답이 있다)
- 매 이동마다 "이 이동이 **어떤 후보 집합**을 제거했나?"에 대한 증명이 있어야 한다. 없으면 추측일 뿐.

### 세 하위 유형
| 유형 | 예 | 이동 규칙 |
|---|---|---|
| Monotonic sum elimination | Two Sum Sorted | 합이 작으면 l++, 크면 r-- (정렬이 단조 규칙을 준다) |
| Symmetry checking | Valid Palindrome | 대칭 위치 비교 후 둘 다 이동 (구두점 skip 예외) |
| Bottleneck elimination | Container With Most Water | **짧은 벽**이 넓이를 제한 → 짧은 벽 쪽을 버려도 안전 |

### Sliding window와 다른 점
- "윈도우가 유효하다" 같은 구간 성질을 유지하지 않는다. 구간은 그냥 **남은 탐색 공간**일 뿐.

### 흔한 실수
- 🐛 정렬 안 된 배열의 쌍 문제에 바로 적용 (단조성 먼저 만들 것).
- 🐛 Container 문제에서 **높은 벽**을 움직임 (제거 증명 없음).
- 🐛 경계 두 개가 있다고 sliding window라고 부름.

---

## 10. Two Sum Sorted

**문제:** 오름차순 정렬된 (유일 원소) 배열에서 합이 target인 두 수의 인덱스를 오름차순으로 반환. 해는 정확히 하나, O(n) 시간·O(1) 공간. 예: `[2,3,4,5,8,11,18], 8` → `[1,3]` (3+5).

**핵심 관찰:** 브루트포스는 모든 쌍 O(n²). 안 쓴 조건 = **정렬**. 최소+최대(2+18)부터 보면 어느 쪽을 바꿔야 할지 **명확**:
- 합 > target → 줄여야 하는데 2는 이미 최소 → **18을 버린다** (18은 어떤 짝과도 너무 큼).
- 합 < target → 18은 이미 최대 → **2를 버린다**.

**풀이**
1. `l, r = 0, n-1`
2. `while l < r`: 합 비교 → 같으면 반환, 작으면 `l += 1`, 크면 `r -= 1`.

```python
def two_sum_sorted(arr: list[int], target: int) -> list[int]:
    l, r = 0, len(arr) - 1
    while l < r:
        s = arr[l] + arr[r]
        if s == target:
            return [l, r]
        if s < target:
            l += 1
        else:
            r -= 1
    return []
```

**복잡도:** 시간 O(n) (각 원소 최대 1회 방문), 공간 O(1).

- 🐛 `l < r` (같은 원소 두 번 사용 금지).
- 🐛 정렬 안 된 입력이면 해시맵 O(n) 또는 정렬 후(인덱스 보존 필요) 투 포인터.
- 🎤 "왜 r을 줄여도 답을 놓치지 않나?" → arr[r]은 가장 작은 arr[l]과도 합이 넘치므로 **어떤 짝으로도 답이 될 수 없다**.
- 🔑 "sorted array" + "pair sum = target" + "O(1) space" → opposite-direction.

---

## 11. Valid Palindrome

**문제:** 영숫자가 아닌 문자와 대소문자를 무시하고 회문인지 판별. 예: `"Do geese see God?"` → True, `"A brown fox jumping over"` → False.

**핵심 관찰:** 대칭 위치 비교 = opposite direction. 영숫자가 아닌 문자는 **해당 포인터만** 한 칸씩 건너뛴다.

**풀이**
1. `l, r = 0, n-1`
2. `while l < r`: 영숫자 아닌 동안 l 전진 / r 후진 (**내부 while에도 `l < r` 경계 검사**).
3. 소문자로 비교, 다르면 False. 같으면 `l += 1; r -= 1`.

```python
def is_palindrome(s: str) -> bool:
    l, r = 0, len(s) - 1
    while l < r:
        while l < r and not s[l].isalnum():
            l += 1
        while l < r and not s[r].isalnum():
            r -= 1
        if s[l].lower() != s[r].lower():
            return False
        l, r = l + 1, r - 1
    return True
```

**복잡도:** 시간 O(n), 공간 O(1). (먼저 필터링한 리스트를 만들면 더 단순하지만 공간 O(n).)

- 🐛 skip을 `if`로 하면 연속된 구두점(`", "`)을 한 번에 못 넘김 → **while**.
- 🐛 내부 while에 경계 검사가 없으면 `"?!"` 같은 입력에서 인덱스가 교차/범위 이탈.
- 🐛 `isalpha()`만 쓰면 숫자를 버리는 버그 — 조건은 **alphanumeric**.
- 🔑 "palindrome" + "ignore non-alphanumeric / case" → opposite-direction symmetry check.

---

## 12. Container With Most Water

**문제:** `height[i]` 막대들 중 두 개를 골라 담을 수 있는 물의 최대량 `(j-i) × min(h[i], h[j])`. 예: `[1,8,6,2,5,4,8,3,7]` → `49` (인덱스 1, 8).

**핵심 관찰 (bottleneck elimination):** 넓이의 높이는 **짧은 막대**가 결정. **높은 쪽**을 안으로 옮기면 폭은 줄고 높이 상한은 여전히 짧은 막대 이하 → 절대 개선 불가. 따라서 현재 짧은 막대와 이룰 수 있는 나머지 모든 쌍은 **현재 값보다 나을 수 없으므로** 짧은 막대를 통째로 버려도 안전.

**풀이**
1. `left=0, right=n-1`, 넓이 계산 후 최댓값 갱신.
2. 짧은 쪽 포인터를 안으로 이동 (같으면 아무 쪽이나).
3. 만날 때까지 최대 n-1번 이동.

```python
def container_with_most_water(height: list[int]) -> int:
    l, r, best = 0, len(height) - 1, 0
    while l < r:
        best = max(best, (r - l) * min(height[l], height[r]))
        if height[l] < height[r]:
            l += 1
        else:
            r -= 1
    return best
```

**복잡도:** 시간 O(n), 공간 O(1). (브루트포스 n(n-1)/2쌍 O(n²))

- 🐛 높은 벽을 움직이면 틀림 — 이동마다 "무엇을 배제했나" 증명 필요.
- 🎤 "왜 짧은 쪽을 움직이나?" → 짧은 막대를 고정한 채 폭을 줄이는 모든 쌍은 현재보다 작거나 같다. 한 번에 한 막대씩 증명과 함께 제거.
- 🔑 "두 경계 + 폭 × min(높이)" / "최대 넓이 쌍" → opposite-direction bottleneck.

---

## 13. Sliding Window Family Map

> 섹션: Sliding Window

### 핵심 개념
- 포인터 두 개가 **연속 구간(window)**을 정의하고, 그 **구간 전체에 대한 정보**(합, 빈도 맵 등)를 이동하면서 유지하는 투 포인터 특화 가족. invariant가 가장 중요한 가족.

### 해당 조건
- 답이 left~right 사이의 **subarray/substring**에 의존
- 원소가 들어오고 나갈 때 윈도우 상태를 **증분(incremental) 갱신** 가능
- 윈도우가 **유효한지** 알려주는 조건이 있음

### 전형적 invariant
- "현재 윈도우는 유효하다" / "중복 없음" / "합 ≤ target" / "빈도 맵 = 윈도우 안 문자들"

### 세 하위 유형
| 유형 | 예 | 포인트 |
|---|---|---|
| Fixed-size | Largest Subarray Sum | 하나 들어오고 하나 나갈 때 O(1) 갱신 |
| Flexible + validity rule | Longest Subarray Sum ≤ target, Least Consecutive Cards | 무효가 될 때까지 확장 → 유효해질 때까지 축소 |
| Frequency-tracking | Find All Anagrams, Longest Substring w/o Repeat, Minimum Window Substring | 문자가 들고 날 때 bookkeeping 정확성 |

### 다른 가족과 비교
- vs Container: sliding window는 **구간 자체가 의미**를 가짐. Container의 구간은 그냥 남은 탐색 공간.
- vs Remove Duplicates: sliding window는 두 포인터가 **살아 있는 윈도우**를 묘사. compaction은 왼쪽이 **완성된 답 prefix**.

### 가변 윈도우 템플릿
```python
def variable_window(arr):
    state, left, best = init(), 0, 0
    for right, x in enumerate(arr):
        add(state, x)                       # 1. 확장
        while broken(state):                # 2. invariant 깨지면
            remove(state, arr[left]); left += 1   #    왼쪽에서 축소
        best = max(best, right - left + 1)  # 3. 유효할 때만 답 갱신
    return best
```
- ✅ 체크포인트: "내 윈도우가 유지하는 invariant는? 답을 갱신해도 안전한 **시점**은?"
- 🐛 invariant 없이 축소하면 언제 멈출지/언제 답을 갱신할지 근거가 없어진다.

---

## 14. Subarray Sum - Fixed

### Sliding window 도입
- same-direction 투 포인터의 변형이지만, 두 위치가 아니라 **사이 구간 전체**에 대한 함수를 다룬다. 윈도우 결과를 유지하다가 한 칸 밀 때 **들어온 것 더하고 나간 것 빼기** → 겹치는 구간을 다시 계산하지 않음. 이중 루프를 포인터당 한 번씩의 두 패스로.

**문제:** 비음수 배열에서 길이 k인 subarray의 최대 합. 예: `[1,2,3,7,4,1], k=3` → `14` (`[3,7,4]`).

**풀이**
1. 첫 k개 합 = `window_sum`, `largest = window_sum`.
2. `right`를 k..n-1로, `left = right - k`: `window_sum += nums[right] - nums[left]`, 최대 갱신.

```python
def subarray_sum_fixed(nums: list[int], k: int) -> int:
    window = sum(nums[:k])
    best = window
    for right in range(k, len(nums)):
        window += nums[right] - nums[right - k]
        best = max(best, window)
    return best
```

**복잡도:** 시간 O(n) (브루트포스 O(nk)), 공간 O(1).

### 고정 윈도우 템플릿
```python
def sliding_window_fixed(arr, k):
    window = build(arr[:k]); ans = score(window)
    for right in range(k, len(arr)):
        remove(window, arr[right - k])
        add(window, arr[right])
        ans = optimal(ans, score(window))   # max/min 등 문제에 맞게
    return ans
```
- 🐛 `left = right - k` (나가는 원소)의 off-by-one. `len(nums) == k`면 루프 0회로 첫 윈도우가 답.
- 🔑 "length k / size k 연속 구간" + "최대/최소/평균" → 고정 sliding window.

---

## 15. Find All Anagrams in a String

**문제:** `original`의 부분문자열 중 `check`의 anagram인 것의 **시작 인덱스**를 모두 오름차순으로. 예: `"cbaebabacd", "abc"` → `[0, 6]`; `"abab", "ab"` → `[0,1,2]`. 소문자만.

**핵심 관찰:** anagram ⇔ 문자 **빈도가 같음**, 길이는 항상 `len(check)` → **고정 크기 윈도우 + 빈도 배열**.

**풀이**
1. `check` 빈도 `need[26]`, 첫 윈도우 빈도 `win[26]`. 같으면 0 추가.
2. `i`를 m..n-1로: `win[original[i-m]] -= 1`, `win[original[i]] += 1`, 같으면 `i-m+1` 추가.

```python
def find_all_anagrams(original: str, check: str) -> list[int]:
    n, m = len(original), len(check)
    if n < m:
        return []
    a = ord('a')
    need, win = [0] * 26, [0] * 26
    for i in range(m):
        need[ord(check[i]) - a] += 1
        win[ord(original[i]) - a] += 1
    res = [0] if win == need else []
    for i in range(m, n):
        win[ord(original[i - m]) - a] -= 1
        win[ord(original[i]) - a] += 1
        if win == need:
            res.append(i - m + 1)
    return res
```

**복잡도:** 시간 O(n · 26) = O(n), 공간 O(1) (크기 26 고정 배열).

- 🐛 `len(original) < len(check)` 먼저 처리.
- 🐛 시작 인덱스는 `i - m + 1` (방금 들어온 i가 윈도우의 끝).
- 💡 26 비교도 없애려면 "일치하는 문자 종류 수(matches)" 카운터를 증분 관리.
- 🔑 "anagram / permutation of pattern in string" + "모든 시작 위치" → 고정 윈도우 + 빈도 카운트.

---

## 16. Sliding Window - Longest

**문제:** 비음수 배열에서 **합 ≤ target**인 가장 긴 subarray 길이. 예: `[1,6,3,1,2,4,5], target=10` → `4` (`[3,1,2,4]`).

**핵심 관찰:** 빈 윈도우는 유효. 오른쪽으로 확장하다 **무효가 되면 왼쪽을 유효해질 때까지만** 줄인다. 원소가 비음수라 합은 확장 시 증가·축소 시 감소 → **단조성** 덕분에 left를 되돌릴 일이 없다.

**풀이**
1. `right`로 확장, `window_sum += nums[right]`.
2. `while window_sum > target`: 왼쪽 제거, `left += 1`.
3. while을 빠져나오면 윈도우는 **반드시 유효** → 길이 갱신.

```python
def subarray_sum_longest(nums: list[int], target: int) -> int:
    window_sum = left = best = 0
    for right, x in enumerate(nums):
        window_sum += x
        while window_sum > target:
            window_sum -= nums[left]
            left += 1
        best = max(best, right - left + 1)
    return best
```

**복잡도:** 시간 O(n) (각 원소 들어오고 나가기 한 번씩, amortized), 공간 O(1).

### Longest 템플릿의 invariant
- 매 for 반복 **시작과 끝**에 윈도우는 유효. while에는 무효 윈도우만 들어가므로 빠져나오면 유효. → 답 갱신은 **for 끝에서**.
- 최장을 찾으므로 left는 **가능한 한 적게** 움직인다.

```python
def sliding_window_flexible_longest(arr):
    left, ans = 0, 0
    for right in range(len(arr)):
        add(arr[right])
        while invalid():
            remove(arr[left]); left += 1
        ans = max(ans, right - left + 1)   # 여기선 항상 유효
    return ans
```
- 🐛 **음수가 섞이면** 단조성이 깨져 sliding window가 틀린다 → prefix sum + 해시맵 / 정렬 구조 필요.
- 🔑 "longest subarray/substring such that (조건)" + 조건이 확장에 대해 단조 → flexible longest window.

---

## 17. Longest Substring without Repeating Characters

**문제:** 중복 문자가 없는 가장 긴 부분문자열의 길이. 예: `"abccabcabcc"` → `3` (`abc`, `cab`); `"aaaabaaa"` → `2`.

**핵심 관찰:** 브루트포스는 모든 (start, end) 검사 O(n²)~O(n³). 어떤 start에서 중복이 생기면 그 start로 더 긴 걸 볼 필요가 없다 → start 증가 = **longest 가변 윈도우**. invariant: **윈도우 안 문자는 모두 유일**.

**풀이**
1. `window = set()`, `l = 0`.
2. `r`마다: `s[r]`이 이미 윈도우에 있으면 **있는 동안** `s[l]` 제거하고 `l += 1`.
3. `s[r]` 추가, 길이 갱신.

```python
def longest_substring_without_repeating_characters(s: str) -> int:
    window, l, best = set(), 0, 0
    for r, ch in enumerate(s):
        while ch in window:
            window.remove(s[l])
            l += 1
        window.add(ch)
        best = max(best, r - l + 1)
    return best
```

**복잡도:** 시간 O(n), 공간 O(min(n, Σ)).

- 💡 변형: `last_seen[ch]` 인덱스 맵으로 `l = max(l, last_seen[ch] + 1)` 점프 → while 없이 한 번에 (`max` 빠뜨리면 l이 **뒤로** 가는 버그).
- 🐛 여기선 추가 **전에** 수리(while) — "추가 후 수리" 템플릿과 순서만 다를 뿐 invariant는 같다.
- 🔑 "longest substring" + "without repeating / distinct / at most K distinct" → 빈도·집합 가변 윈도우.

---

## 18. Sliding Window - Shortest

**문제:** 양수 배열에서 **합 ≥ target**인 가장 짧은 subarray 길이 (없으면 0). 예: `[1,4,1,7,3,0,2,5], target=10` → `2` (`[7,3]`).

**핵심 관찰:** Longest와 반대. 빈 윈도우는 **무효**에서 출발. 유효해질 때까지 확장하고, 유효한 **동안** 왼쪽을 최대한 줄이며 매번 답 갱신. (유효한 윈도우에 원소를 더해도 여전히 유효 → 단조.)

**풀이**
1. `right`로 확장해 합 누적.
2. `while window_sum >= target`: 길이 갱신 → 왼쪽 제거 → `left += 1`.
3. 한 번도 갱신 안 됐으면 0.

```python
def subarray_sum_shortest(nums: list[int], target: int) -> int:
    window_sum = left = 0
    best = len(nums) + 1
    for right, x in enumerate(nums):
        window_sum += x
        while window_sum >= target:
            best = min(best, right - left + 1)
            window_sum -= nums[left]
            left += 1
    return best if best <= len(nums) else 0
```

**복잡도:** 시간 O(n), 공간 O(1).

### Longest vs Shortest 템플릿
| | Longest | Shortest |
|---|---|---|
| 초기 빈 윈도우 | 유효 | 무효 |
| while 조건 | `while invalid` → 축소 | `while valid` → 답 갱신 후 축소 |
| 답 갱신 위치 | while **밖**(for 끝) | while **안**(제거 전) |
| left 이동 | 최소한 | 최대한 |

- 🐛 답 갱신을 **제거 전에** 해야 윈도우가 비지 않은 상태(left ≤ right)에서 길이를 읽는다. 한 원소만으로 target 이상이면 left가 right+1까지 가도 OK.
- 🐛 "없음" sentinel(`n+1`) 처리.
- 🔑 "shortest/minimum length subarray with sum ≥ target" (양수) → shortest 가변 윈도우.

---

## 19. Least Consecutive Cards to Match

**문제:** 카드 값 배열에서 **연속으로** 집어서 같은 값 한 쌍을 만들 때 최소 장수, 없으면 -1. 예: `[3,4,2,3,4,7]` → `4`.

**핵심 관찰:** = "중복을 포함하는 **가장 짧은** subarray". 방금 넣은 `cards[right]`의 개수가 2가 되면 윈도우 **유효** → shortest 템플릿.

**풀이**
1. `Counter` 윈도우. `right`로 추가.
2. `while window[cards[right]] == 2`: 길이 갱신, `cards[left]` 빼고 `left += 1`.

```python
from collections import Counter

def least_consecutive_cards_to_match(cards: list[int]) -> int:
    window, left, best = Counter(), 0, len(cards) + 1
    for right, c in enumerate(cards):
        window[c] += 1
        while window[c] == 2:
            best = min(best, right - left + 1)
            window[cards[left]] -= 1
            left += 1
    return best if best <= len(cards) else -1
```

**복잡도:** 시간 O(n), 공간 O(n).

- 💡 더 단순한 대안: `last_index[c]`를 기억해 `i - last_index[c] + 1`의 최소값 (윈도우 불필요, 같은 O(n)).
- 🐛 왜 `cards[right]`만 보면 되나? invariant상 추가 전 윈도우엔 중복이 없으므로 새 중복은 방금 들어온 값뿐.
- 🔑 "minimum consecutive / contiguous ... to get a pair/duplicate" → shortest 윈도우 (또는 last-seen 해시).

---

## 20. Prefix Sum — Introduction

> 섹션: Prefix Sum

### 핵심 개념
- 같은 배열에 구간 합 질의가 **여러 번** 오거나, 모든 subarray 합을 탐색해야 할 때 매번 O(n)씩 다시 더하는 반복을 없앤다. **O(n) 선계산 → 질의당 O(1) 뺄셈 한 번.**
- 길이 n+1 배열: `prefix[0] = 0`, `prefix[k] = arr[0] + … + arr[k-1]` = `prefix[k-1] + arr[k-1]`.
- `prefix[0] = 0`(원소 0개의 합) 한 칸 덕분에 경계 계산이 딱 맞는다.

### 구간 합 공식
- `sum(arr[i..j-1]) = prefix[j] - prefix[i]` (**오른쪽 exclusive**). "j 이전 전부" − "i 이전 전부" = 가운데.
- 문제가 inclusive `r`을 주면 → `prefix[r+1] - prefix[i]`.

```python
from itertools import accumulate
prefix = [0, *accumulate(arr)]          # len n+1
range_sum = lambda i, j: prefix[j] - prefix[i]   # arr[i:j]
```

### 언제 쓰나
- 고정(immutable) 배열 + 반복 구간 합, 또는 subarray 경계를 탐색하는 알고리즘. 일회성 합 하나면 그냥 루프.
- 일반화: **prefix product, prefix XOR, 2D prefix sum**(사각형 합).
- 강력한 업그레이드: **running sum + 해시맵** — "보완값(complement)이 앞에서 나왔나?"로 subarray 탐색을 O(1) 조회로.

### 흔한 실수
- 🐛 off-by-one 3종 세트 기억: 길이 **n+1**, `prefix[0]=0`, 오른쪽 **exclusive**.
- 🐛 원소가 수정되는 배열이면 prefix는 부적합 → Fenwick/segment tree.

---

## 21. Subarray Sum Equals Target

**문제:** 합이 target인 subarray를 `[start, end)`로 반환 (여러 개면 end가 작은 것). **음수 포함**. 예: `[1,-20,-3,30,5,4], 7` → `[1,4]` (`-20-3+30`).

**핵심 관찰:** `sum(arr[i:j]) = prefix[j] - prefix[i] = target` ⇔ **`prefix[i] = prefix[j] - target`**. j를 고정하면 "그 값이 앞에서 나온 적 있나?"만 물으면 됨 → 해시맵 (Two Sum과 같은 구조). 음수가 있어 sliding window는 불가.

**풀이**
1. `seen = {0: 0}` (빈 prefix — index 0에서 시작하는 subarray를 위해 필수).
2. `cur += arr[idx]`, `j = idx + 1`. `cur - target`이 seen에 있으면 `[seen[cur-target], j]`.
3. 없으면 `seen[cur] = j` (처음 나온 위치만 저장).

```python
def subarray_sum(arr: list[int], target: int) -> list[int]:
    seen = {0: 0}
    cur = 0
    for idx, x in enumerate(arr):
        cur += x
        if cur - target in seen:
            return [seen[cur - target], idx + 1]
        seen.setdefault(cur, idx + 1)
    return []
```

### 후속: 개수 세기 (LeetCode 560)
- 해시맵에 인덱스 대신 **빈도** 저장. `freq = {0: 1}`, 매 위치에서 `count += freq[S - target]` 후 `freq[S] += 1`.

```python
from collections import Counter
def subarray_sum_total(arr: list[int], target: int) -> int:
    freq, cur, count = Counter({0: 1}), 0, 0
    for x in arr:
        cur += x
        count += freq[cur - target]
        freq[cur] += 1
    return count
```

**복잡도:** 시간 O(n) (평균 해시 O(1)), 공간 O(n).

- 🐛 `{0: 0}` / `{0: 1}` 초기화 누락 → 맨 앞에서 시작하는 subarray를 놓침.
- 🐛 조회 **먼저**, 저장 **나중** (순서 바꾸면 target=0일 때 빈 subarray를 셈).
- 🔑 "subarray sum equals k" + **음수 가능** / "개수" → prefix sum + 해시맵.

---

## 22. Range Sum Query - Immutable

**문제:** 배열 `nums`에 대해 `sumRange(left, right)` (inclusive) 질의 다수를 **O(1)**로. 예: `[1,2,3,4]`, `(1,3)` → `9`.

**핵심 관찰:** 전형적인 "한 번 선계산, 여러 번 질의". inclusive right → `prefix[right+1] - prefix[left]`.

```python
class NumArray:
    def __init__(self, nums: list[int]):
        self.prefix = [0]
        for x in nums:
            self.prefix.append(self.prefix[-1] + x)

    def sum_range(self, left: int, right: int) -> int:
        return self.prefix[right + 1] - self.prefix[left]
```

**복잡도:** 전처리 O(n), 질의 O(1), 공간 O(n).

- 🐛 질의마다 prefix를 다시 만들면 의미 없음 — 생성자에서 한 번.
- 🎤 "배열이 자주 바뀌면?" → Fenwick tree(BIT) / segment tree로 update·query 모두 O(log n).
- 🔑 "immutable array" + "many range sum queries" → prefix sum.

---

## 23. Product of Array Except Self

**문제:** `answer[i]` = `nums[i]`를 제외한 모든 원소의 곱. 예: `[1,2,3,4]` → `[24,12,8,6]`.

**핵심 관찰:** 각 곱 = **왼쪽 전체 곱 × 오른쪽 전체 곱**. prefix 아이디어를 곱셈에, **양방향**으로 적용. 나눗셈(전체 곱 / nums[i])은 **0이 있으면 깨진다**.

**풀이**
1. 왼→오 패스: `result[i] = left` (i 이전 곱), 그 뒤 `left *= nums[i]` → `[1,1,2,6]`.
2. 오→왼 패스: `result[i] *= right` (i 이후 곱), 그 뒤 `right *= nums[i]` → `[24,12,8,6]`.

```python
def product_of_array_except_self(nums: list[int]) -> list[int]:
    n = len(nums)
    res = [1] * n
    left = 1
    for i in range(n):
        res[i] = left
        left *= nums[i]
    right = 1
    for i in range(n - 1, -1, -1):
        res[i] *= right
        right *= nums[i]
    return res
```

**복잡도:** 시간 O(n) (두 패스), 공간 O(1) (출력 배열 제외).

- 🐛 "먼저 기록, 나중에 곱하기" 순서 — 바꾸면 자기 자신이 포함됨.
- 🐛 0이 하나/둘 이상인 경우도 곱셈만 쓰므로 자동 처리.
- 🔑 "except self" / "without division" / "왼쪽과 오른쪽 전체" → prefix × suffix 두 패스.

---

## 24. Fast and Slow Family Map

> 섹션: Cycle Finding

### 핵심 개념
- Same Direction에서의 fast/slow(중간점 2:1, n번째 끝 고정 간격)는 **목표 위치 도달**이 목적이었고, fast가 끝에 닿으면 종료.
- Cycle finding은 **순회가 스스로 끝나지 않을 수도** 있는 상황. 목표가 "위치 찾기" → "끝(terminal state)이 존재하긴 하나?"로 바뀐다.

### 언제 쓰나
- 연결 리스트(또는 `f(x)` 반복 함수)가 이미 방문한 노드로 **되돌아갈 수 있을 때**. fast가 null에 닿는 것에 기대어 멈출 수 없다는 게 신호.

### Core invariant — 상대적 진척
- 둘 다 사이클 안에 들어오면 매 라운드 fast가 slow에게 **정확히 1칸씩** 따라붙는다. 사이클 길이는 유한 정수 → 간격이 1씩 줄어 **반드시 0** → 만남.
- 사이클이 없으면 fast가 먼저 null에 도달 → 정상 종료.
- **만남 자체가 신호** (Same Direction은 slow의 최종 **위치**가 답).

### 흔한 실수
- 🐛 `fast`와 `fast.next`가 null인지 확인하기 **전에** `fast.next.next`를 읽음 → 비순환 리스트에서 NPE. 종료 조건은 항상 **두 가드: 동등성 + null**.
- ✅ 체크포인트: "사이클이 있으면 왜 반드시 만나고, 없으면 왜 절대 못 만나나?"를 설명할 수 있어야 함.

---

## 25. Linked List Cycle

**문제:** 연결 리스트에 사이클이 있는지 판별, 보너스로 O(1) 공간.

**핵심 관찰:** 방문 노드를 set에 넣으면 O(n) 공간. **Floyd (tortoise & hare):** slow 1칸, fast 2칸. 사이클 안에서는 거리 차가 매번 1씩 줄어 만남. 사이클 없으면 fast가 끝에 도달.

**풀이**
1. `slow = fast = head`
2. `while fast and fast.next`: slow 1칸, fast 2칸, 같으면 True.
3. 루프 종료 = 끝에 도달 = False.

```python
def has_cycle(head: Node) -> bool:
    slow = fast = head
    while fast and fast.next:
        slow = slow.next
        fast = fast.next.next
        if slow is fast:
            return True
    return False
```

**복잡도:** 시간 O(n) (slow가 사이클을 한 바퀴 돌기 전에 만남, 최악 ~2n), 공간 O(1).

- 🐛 **이동 후에** 비교 (시작점에서 둘이 같으니 이동 전 비교하면 바로 True).
- 🐛 값(`val`) 비교 말고 **노드 identity** (`is`) 비교.
- 💡 확장: 만난 뒤 한 포인터를 head로 보내고 둘 다 1칸씩 → 다시 만나는 곳이 **사이클 시작점** (LC 142). Find Duplicate Number(LC 287)도 같은 원리.
- 🔑 "cycle in linked list" / "O(1) space" / "반복 함수가 루프에 빠지나(happy number)" → Floyd fast/slow.

---

## 26. Two Pointers Decision Rule

> 섹션: Decision Making

### 핵심 개념
- 목표: 다음 문제를 푸는 게 아니라 **코딩 전에 올바르게 분류**하는 것.

### 네 가족
1. **Same direction** — 하나는 읽고 하나는 쓰기, 또는 뒤따르며 스캔
2. **Opposite direction** — 양끝에서 시작, 이동마다 불가능한 답 제거
3. **Sliding window** — 연속 구간을 정의하고 그 구간의 성질을 유지
4. **Fast / slow** — 연결 구조에서 속도차 또는 고정 간격

### 순서대로 던질 질문
| # | 질문 | 신호 | 가족 |
|---|---|---|---|
| 1 | **연속 구간**을 유지하나? | longest/shortest valid substring, 이동 구간의 합·빈도·개수 | Sliding window |
| 2 | 한쪽을 **안전하게 제거**할 수 있나? | 정렬 + 쌍 target, 회문/거울 비교, bottleneck | Opposite direction |
| 3 | **in-place로 재작성/필터**하나? | remove duplicates, 특정 원소 앞으로, stable partition | Same direction |
| 4 | 연결 리스트에서 **상대 위치**를 추론하나? | 중간, 사이클, 목표 직전 노드 | Fast/slow, fixed gap |

### 빠른 비교표
| 신호 | 첫 추측 | 이유 |
|---|---|---|
| 정렬 배열, 쌍 target | Opposite | 한쪽 이동이 답을 예측 가능하게 바꿈 |
| 회문/거울 | Opposite | 대칭 위치 비교 |
| 최장/최단 유효 부분문자열 | Sliding window | 구간 성질 유지 |
| in-place 필터·압축 | Same direction | 한 포인터가 남길 값을 씀 |
| 리스트 중간·사이클 | Fast/slow | 상대 속도가 구조를 드러냄 |

### 흔한 분류 실수
- 🐛 경계가 두 개 움직인다고 sliding window라 부름 — sliding window는 **구간 전체 정보**를 유지할 때만.
- 🐛 한쪽 이동이 안전하다는 증명 없이 opposite direction 사용.
- 🐛 연결 리스트 문제를 배열 스캔처럼 취급 — 뒤로 가거나 인덱싱 불가, 그래서 **상대 이동**이 핵심.
- 🎤 문제를 받으면 먼저 "이건 ○○ 가족이고 invariant는 △△" 라고 말한 뒤 코딩.

---

## 27. Minimum Window Substring

> 섹션: Advanced

**문제:** `original`에서 `check`의 모든 문자(**중복 포함**)를 포함하는 가장 짧은 부분문자열. 길이 동률이면 사전순 최소. 없으면 `""`. 예: `"cdbaebaecd", "abc"` → `"baec"` (`cdba`와 동률, `b < c`).

**핵심 관찰:** shortest 가변 윈도우 + 빈도. 매번 26/52개 비교 대신 **`satisfied` 카운터**: 필요한 distinct 문자 중 윈도우 빈도가 요구량에 도달한 개수. `satisfied == required` ⇔ 유효 → O(1) 판정.

**풀이**
1. `need = Counter(check)`, `required = len(need)`, `have = defaultdict(int)`.
2. 확장: `ch = original[r]`가 need에 있으면 `have[ch] += 1`, **정확히** `need[ch]`에 도달한 순간 `satisfied += 1`.
3. `while satisfied == required`: 답 갱신(길이 → 동률이면 사전순) → `original[l]` 제거, 요구량 **미만**으로 떨어지면 `satisfied -= 1` → `l += 1`.

```python
from collections import Counter, defaultdict

def get_minimum_window(original: str, check: str) -> str:
    need = Counter(check)
    required, satisfied = len(need), 0
    have = defaultdict(int)
    best_start, best_len = -1, len(original) + 1
    l = 0
    for r, ch in enumerate(original):
        if ch in need:
            have[ch] += 1
            if have[ch] == need[ch]:
                satisfied += 1
        while satisfied == required:
            cur = r - l + 1
            if cur < best_len or (cur == best_len and
                    original[l:r + 1] < original[best_start:best_start + best_len]):
                best_start, best_len = l, cur
            out = original[l]
            if out in need:
                have[out] -= 1
                if have[out] < need[out]:
                    satisfied -= 1
            l += 1
    return original[best_start:best_start + best_len] if best_start >= 0 else ""
```

**복잡도:** 포인터 이동만 보면 O(n + m). 사전순 tie-break 비교(슬라이스)가 윈도우 길이만큼 들어 최악 O(n·m). 공간 O(k) (check의 distinct 문자 수).

- 🐛 `==`일 때만 satisfied 증가 (`>=`로 하면 초과분마다 중복 증가).
- 🐛 감소 판정은 `<` — 여분이 있던 문자를 빼도 여전히 만족.
- 🐛 `check`에 없는 문자는 추적하지 않아도 됨. 대소문자 구분.
- 🔑 "minimum window containing all characters (with duplicates)" → shortest 윈도우 + need/have + satisfied 카운터.

---

## 28. Teleporter Arrays (Get Maximum Score)

**문제:** 두 정렬·원소 유일 배열 `arr1`, `arr2`. 한 배열의 시작에서 출발해 어느 배열이든 끝까지 간다. 한 칸 전진하거나, **같은 값**이 있는 칸이면 다른 배열로 순간이동 가능. 점수 = 밟은 **서로 다른** 수의 합의 최댓값 (mod 1e9+7). 예: `[2,4,5,8,10]`, `[4,6,8,9]` → `30` (2+4+6+8+10).

**핵심 관찰:** 공통 값(4, 8)이 **결정 지점**이 되어 문제를 **구간(segment)**으로 쪼갠다. 구간 사이에선 한 배열에 머물러야 하고, 공통 값에 도착하면 어느 쪽이든 다음 구간을 고를 수 있으므로 **구간별 선택이 독립** → 각 구간에서 합이 큰 배열 + 공통 값 전부.

**풀이 (merge식 투 포인터)**
1. `i, j`, 구간 합 `s1, s2`.
2. `arr1[i] < arr2[j]` → `s1 += arr1[i]; i++` / 반대면 `s2`, `j++`.
3. 같으면 `res += max(s1, s2) + 값`, `s1 = s2 = 0`, 둘 다 전진.
4. 끝나면 남은 구간 `res += max(s1, s2)`.

```python
MOD = 10**9 + 7

def maximum_score(arr1: list[int], arr2: list[int]) -> int:
    i = j = 0
    s1 = s2 = res = 0
    while i < len(arr1) or j < len(arr2):
        if i < len(arr1) and j < len(arr2) and arr1[i] == arr2[j]:
            res += max(s1, s2) + arr1[i]
            s1 = s2 = 0
            i += 1; j += 1
        elif j == len(arr2) or (i < len(arr1) and arr1[i] < arr2[j]):
            s1 += arr1[i]; i += 1
        else:
            s2 += arr2[j]; j += 1
    return (res + max(s1, s2)) % MOD
```

**복잡도:** 시간 O(n + m), 공간 O(1).

- 🐛 **mod는 마지막(또는 max 비교 이후)에만** — 구간 합을 mod한 뒤 `max`로 비교하면 대소가 뒤집힐 수 있다. (Python은 큰 정수 OK, C/C++에선 64-bit 누적.)
- 🐛 한 배열이 먼저 끝난 뒤 나머지 처리 (경계 조건).
- 💡 그래프 관점: 각 배열이 경로, 같은 값끼리 연결된 DAG의 최장 경로를 선형으로 푼 것.
- 🔑 "두 정렬 배열" + "공통 원소에서 갈아타기" + "최대 합 경로" → merge 투 포인터 + 구간 합.

---

## 29. Two Pointers Synthesis

### 진짜 질문
- "어떤 템플릿을 기억하지?"가 아니라 **"포인터가 무엇을 나타내고, 어떤 invariant가 포인터 이동을 안전하게 만드나?"**

### 네 멘탈 모델 + invariant
| 가족 | 멘탈 모델 | invariant | 대표 문제 |
|---|---|---|---|
| Same direction | 스캔하며 답을 **쌓는다** | slow 앞 prefix는 이미 정답 | Remove Duplicates, Move Zeros |
| Opposite direction | 불가능한 경계 선택을 **제거** | 현재 범위 밖은 모두 배제됨 | Two Sum Sorted, Valid Palindrome, Container |
| Sliding window | **유효한 구간**을 유지 | 답을 갱신하는 순간 구간은 유효 | Largest/Longest Subarray, Longest Substring, Min Window |
| Fast/slow | 인덱스 없이 **상대 위치** 인코딩 | 포인터 간 거리/속도 관계가 고정된 의미 | Middle, Cycle, N-th from End |

### 흔한 혼동
- "경계가 둘이니 sliding window" ✗ — Container는 유지되는 유효성 조건이 없고 구간은 탐색 공간일 뿐.
- "연결 리스트 문제는 다른 토픽" ✗ — fast/slow도 투 포인터, 인덱싱 대신 상대 이동을 쓸 뿐.
- **"코드 패턴을 외웠으니 이해했다" ✗ (가장 큰 함정)** — invariant 없이 외우면 이동 규칙이 조금만 바뀌어도 무너진다.

### 코딩 전 체크리스트
1. left와 right는 **무엇을 나타내나?**
2. 각 이동 후에 **무엇이 참으로 남나?**
3. 이 포인터를 움직이는 게 **왜 안전한가?**
4. 답을 갱신해도 되는 **시점**은 언제인가?
- 🎤 Speedrun은 "풀기"가 아니라 **분류 드릴**: 먼저 가족과 기대 invariant를 말하고 나서 풀 것.

---

## 30. Two Pointers Speedrun

> 섹션: Speedrun

- 형식: 문제 하나 + "올바른 의사코드/단계 고르기" 객관식 9문항. **풀기 전에 가족과 invariant를 먼저 말하는 분류 드릴**로 쓸 것.

| # | 문제 (LeetCode) | 가족 | 핵심 포인트 / 함정 |
|---|---|---|---|
| 1 | Reverse Vowels of a String | Opposite | l이 모음 아니면 l++, **elif** r이 모음 아니면 r--, 둘 다 모음일 때만 swap 후 **둘 다 이동**. `if/if`로 쓰거나 swap 후 이동 누락이 오답 |
| 2 | Valid Palindrome II (최대 1글자 삭제) | Opposite | 불일치 시 `s[l+1..r]` 또는 `s[l..r-1]`만 검사. **매 스텝 문자열 복사하면 O(n²)** — 불일치 때만 한 번 |
| 3 | String Compression | Same (read/write) | read가 같은 문자 run을 끝까지 훑고, write가 문자+개수(>1이면 자릿수별)를 in-place로 기록 |
| 4 | Container With Most Water | Opposite (bottleneck) | **짧은 막대를 버리고 높은 막대를 유지** — 반대로 움직이는 단계가 오답 |
| 5 | Boats to Save People | Opposite (정렬 후 greedy) | 정렬 → 가장 가벼운+가장 무거운 합 ≤ limit이면 l++, **r--는 항상**. `<`(strict)나 매번 l++은 오답 |
| 6 | 3Sum | 정렬 + Opposite | i 고정 후 나머지에서 two sum sorted, **i와 l/r 모두 중복 skip** |
| 7 | Longest Substring with At Most Two Distinct | Sliding window (longest) | 빈도 맵 크기 > 2이면 축소 |
| 8 | Minimum Swaps to Group All 1's | Sliding window (fixed) | 윈도우 크기 = 1의 총 개수, 답 = `total − max(윈도우 안 1의 수)` |
| 9 | Minimum Size Subarray Sum | Sliding window (shortest) | 합 ≥ target 동안 축소하며 최소 길이. 시작점마다 브루트포스/이진탐색은 느리거나 과함 |

```python
def three_sum(nums):                       # 6번 대표 코드
    nums.sort(); res = []
    for i in range(len(nums) - 2):
        if i and nums[i] == nums[i - 1]:
            continue
        l, r = i + 1, len(nums) - 1
        while l < r:
            s = nums[i] + nums[l] + nums[r]
            if s < 0: l += 1
            elif s > 0: r -= 1
            else:
                res.append([nums[i], nums[l], nums[r]])
                l += 1
                while l < r and nums[l] == nums[l - 1]:
                    l += 1
                r -= 1
    return res
```
- 🎤 객관식 오답 패턴은 대부분 **이동 규칙의 증명 누락**(잘못된 쪽 이동, 이동 누락, strict 비교)이다.

---

## 31. Monster Breakdown | Two Pointers

- 투 포인터 문제들의 **3분짜리 영상 워크스루** 모음(페이지는 영상 임베드만 있음). 빠르게 지나가므로 **포인터가 움직일 때마다 멈추고** 해당 레슨으로 돌아가 확인하는 용도.
- ✅ 복습법: 영상 보기 전 문제 제목만 보고 (1) 가족 (2) invariant (3) 이동 규칙의 안전성 근거 (4) 답 갱신 시점을 말해 본 뒤 영상으로 채점.

---

## 용어·패턴 사전

| 용어 | 뜻 |
|---|---|
| Invariant (불변식) | 루프 매 반복의 시작/끝에서 항상 참인 명제. 초기화·유지·종료로 정확성 증명 |
| Postcondition | 루프 종료 조건 + invariant로부터 얻는 최종 결론 |
| Same direction | 두 포인터가 같은 방향으로 전진 (read/write, 속도차, 고정 간격) |
| Read / write pointer | fast가 입력을 읽고 slow가 남길 값을 쓸 위치를 가리킴 (compaction) |
| Compaction / stable partition | 조건 맞는 원소만 앞으로 모으되 상대 순서 유지 |
| Opposite direction | 양끝에서 안쪽으로 이동, 이동마다 후보 집합 제거 |
| Monotonic elimination | 정렬로 생긴 단조성으로 한쪽 후보 전체를 버리는 논리 (Two Sum Sorted) |
| Bottleneck elimination | 짧은 쪽이 상한을 결정하므로 짧은 쪽을 버리는 논리 (Container) |
| Sliding window | 연속 구간의 상태(합·빈도)를 증분 갱신하며 유지하는 투 포인터 |
| Fixed window | 크기 k 고정, 하나 들어오고 하나 나감 |
| Flexible window (longest) | `while invalid` 축소, for 끝에서 답 갱신, left 최소 이동 |
| Flexible window (shortest) | `while valid` 안에서 답 갱신 후 축소, left 최대 이동 |
| satisfied / required | Minimum Window에서 요구량을 채운 distinct 문자 수 / 필요한 distinct 문자 수 — O(1) 유효성 판정 |
| Prefix sum | `prefix[0]=0`, `prefix[k]=arr[0..k-1]` 합. `sum(arr[i:j]) = prefix[j]-prefix[i]` |
| Prefix + hash map | `prefix[j]-target`이 이전에 나왔는지 조회 → 음수 포함 subarray sum O(n) |
| Prefix × suffix | 양방향 누적곱으로 "자신 제외" 계산 (나눗셈 없이) |
| Fast / slow (tortoise & hare) | 1칸/2칸 이동. 중간점, 사이클 탐지(Floyd) |
| Fixed gap | 두 포인터 간격 n 유지 → 뒤에서 n번째 |
| Dummy node | head 앞의 가짜 노드로 head 삭제 등 경계 케이스 제거 |
| Floyd cycle detection | 사이클 안에서 fast가 매 라운드 1칸씩 따라잡아 반드시 만남 |
| Decision point (teleporter) | 두 정렬 배열의 공통 값 — 구간을 나눠 구간별 max 선택 |
