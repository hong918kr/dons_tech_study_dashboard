# Module 2 — Binary Search (이진 탐색)

> 출처: AlgoMonster Coding Patterns, Module 2 (레슨 10개) · 한국어 개념 정리 노트
> 목표: "정렬됨/단조성"을 보고 **탐색 범위를 반으로 자르는 결정**을 설계하고, 경계(`left/right/boundary`)를 실수 없이 다루기.

## 한눈에 보기 (5분 복습용)

| 패턴 | 언제 쓰나(키워드) | 핵심 아이디어 | 복잡도 |
|---|---|---|---|
| Vanilla 이진 탐색 | 정렬 배열에서 값 찾기 | `left <= right`, mid 비교로 절반 버림 | O(log n) / O(1) |
| **First True (경계 찾기)** | F…FT…T, "첫 번째 ~인 위치" | True면 기록 후 `right = mid-1` | O(log n) |
| Lower bound / First occurrence | "target 이상 첫 원소", 중복 첫 위치 | feasible `arr[i] >= t`; 첫 등장은 `==`일 때만 기록 | O(log n) |
| 답 공간 탐색 (sqrt) | 배열 없이 정수 답, "가장 큰 x s.t." | [lo, hi] 값 범위 자체를 정렬 배열로 간주 | O(log V) |
| 암묵적 정렬 (회전/산) | rotated, mountain, peak | 값 대신 `arr[i] <= arr[-1]`, `arr[i] > arr[i+1]` 같은 단조 판정 | O(log n) |
| 최대값 최소화 (Newspapers) | minimize the max, at most k 그룹 | 한도 T 이진 탐색 + O(n) 그리디 검증 | O(n log S) |

**이진 탐색 = 단조 feasible 함수의 첫 True 찾기.** 정렬 배열은 그 특수 경우일 뿐.

**템플릿은 하나만 외운다:** `while l <= r` / True면 `ans = mid; r = mid - 1` / 아니면 `l = mid + 1`. 문제마다 바뀌는 건 **탐색 공간과 feasible**뿐.

**무한 루프 방지:** 매 반복 범위가 반드시 줄어야 한다 (`right = mid` + `<=` 조합 금지).

---

## 1. Binary Search Intro

> 섹션: Basics

### 핵심 개념
- 정렬된 배열에서 **가운데 원소 하나와 비교**하면 절반을 통째로 버릴 수 있다 → 매 스텝 범위 절반 → **O(log n)**.
  - `arr[mid] < target` → mid 포함 왼쪽 전부 버림 (`left = mid + 1`)
  - `arr[mid] > target` → mid 포함 오른쪽 전부 버림 (`right = mid - 1`)
- 이 추론이 성립하는 조건 = **정렬(단조성)**. 정렬이 없으면 "왼쪽은 전부 더 작다"를 보장 못 함.
- 반복형은 O(1) 메모리, 재귀형은 콜스택 O(log n).

### 템플릿 (vanilla)
```python
def binary_search(arr: list[int], target: int) -> int:
    left, right = 0, len(arr) - 1
    while left <= right:              # <= : 원소 1개 남은 경우도 검사
        mid = (left + right) // 2
        if arr[mid] == target:
            return mid
        if arr[mid] < target:
            left = mid + 1
        else:
            right = mid - 1
    return -1
```

### mid 계산
- 짝수 개면 가운데가 둘 → 관례상 **앞쪽**(정수 나눗셈 내림).
- C/C++/Java에선 `left + (right - left) / 2`로 **overflow 방지**. Python int는 무한 정밀이라 상관없음 (🎤 임베디드 C 면접이면 반드시 언급).

### 올바른 이진 탐색을 짜는 3가지 질문
1. **루프 종료 조건** — `<=`를 안 쓰면 원소 1개 배열에서 매치를 놓친다.
2. **어느 쪽을 버리나** — 비교 결과로 left/right 중 무엇을 옮길지.
3. **현재 원소(mid)를 버려도 되나** — vanilla에선 target이 아니면 버려도 됨. 경계 찾기 문제에선 mid가 답 후보일 수 있어 조심 (다음 레슨).

- 🔑 "정렬된", "O(log n)으로", "찾아라" → 이진 탐색. 더 넓게는 **범위를 반으로 줄이는 이분 결정**만 있으면 정렬 배열이 아니어도 쓴다.
- 🐛 `while left < right`로 쓰고 vanilla 로직 유지 → 마지막 한 칸 누락.

---

## 2. First True (Find the First True in a Sorted Boolean Array)

**문제:** `[F, F, ..., F, T, ..., T]` 꼴의 불리언 배열에서 **첫 번째 True의 인덱스**를 반환(없으면 -1). 예: `[F, F, T, T, T]` → `2`.

**핵심 관찰:** mid가 True면 그게 답일 **수도** 있지만 왼쪽에 더 앞선 True가 있을 수 있다 → 무작정 버리면 안 됨. **기록해 두고 버린다.**

**풀이**
1. `boundary = -1`로 시작.
2. `arr[mid]`가 False → mid 포함 왼쪽 버림 (`left = mid + 1`).
3. `arr[mid]`가 True → `boundary = mid` 기록 후 mid 포함 오른쪽 버림 (`right = mid - 1`).
4. 루프 끝나면 `boundary` 반환.

```python
def find_boundary(arr: list[bool]) -> int:
    left, right = 0, len(arr) - 1
    boundary = -1
    while left <= right:
        mid = (left + right) // 2
        if arr[mid]:
            boundary = mid        # 후보 기록
            right = mid - 1       # 더 왼쪽에 True가 있나?
        else:
            left = mid + 1
    return boundary
```

**복잡도:** 시간 O(log n), 공간 O(1).

### 대안: mid를 범위에 남기기 (`right = mid`)
- `right = mid`로 바꾸면 `left == right`일 때 범위가 안 줄어 **무한 루프** → `while left < right`로 바꿔야 하고, 그러면 원소 1개 케이스를 루프 밖에서 따로 확인해야 함 → **수정 3군데**. 기록 변수 방식은 vanilla 루프를 그대로 쓸 수 있어 실수가 적다.
- 무한 루프 피하는 원칙: ① 매 스텝 **반드시 범위가 줄어야** 하고 ② **탈출 조건**이 있어야 한다.

- 🐛 `right = mid` + `while left <= right` 조합 = 무한 루프 단골.
- 🔑 **이 모듈 이후 대부분의 이진 탐색 문제는 "feasible(mid)가 False→True로 바뀌는 경계 찾기"로 환원된다.** 조건 함수만 바꿔 끼우면 됨.
- ✅ 체크포인트: 모든 원소가 False면? → boundary가 한 번도 갱신 안 돼 -1.

---

## 3. Monotonic Function

> 섹션: Sorted Array

### 핵심 개념
- 이진 탐색의 진짜 전제는 "정렬된 배열"이 아니라 **단조(monotonic) 판정 함수**다.
  - 단조 함수: 비감소(`x1 > x2 ⇒ f(x1) ≥ f(x2)`) 또는 비증가.
  - 결과가 True/False뿐인 단조 함수 → 값을 늘어놓으면 `FFFF TTTT` → **First True 문제**로 환원.
- 핵심 질문: **"추측값 x가 통하면, 더 큰 x도 전부 통하나?"** (또는 더 작은 x도?) 예: Koko가 속도 k로 바나나를 다 먹을 수 있으면 k보다 빠른 속도는 당연히 가능 → `F F F T T T`.

### 템플릿 (feasible 버전)
```python
def binary_search(n: int) -> int:
    left, right = 0, n - 1
    first_true = -1
    while left <= right:
        mid = (left + right) // 2
        if feasible(mid):          # 문제마다 이 함수만 설계
            first_true = mid
            right = mid - 1
        else:
            left = mid + 1
    return first_true
```

### 쓰는 법
1. 답이 될 수 있는 **탐색 공간**(인덱스 범위 or 값 범위)을 정한다.
2. `feasible(x)`를 정의하고 **단조성**을 확인한다 (한 번 True면 이후 계속 True).
3. 템플릿에 기계적으로 대입.

- 🐛 feasible이 단조가 아니면(TFTF…) 이진 탐색 결과는 의미 없음 → 먼저 단조성을 말로 증명.
- 🎤 "이 문제에 이진 탐색이 왜 되나요?" → "feasible(x)가 x에 대해 단조라서 F…FT…T 경계를 찾는 문제이기 때문".
- 🔑 "최소 ~로 가능한", "최대 ~ 이하로", "k 이상인 첫 위치" → feasible + First True.

---

## 4. First Element Not Smaller Than Target

**문제:** 오름차순 정렬 배열에서 **target 이상인 첫 원소의 인덱스** (항상 존재한다고 가정). 예: `arr=[1,3,3,5,8,8,10], target=2` → `1`. (= C++ `lower_bound`, Python `bisect_left`)

**핵심 관찰:** 정렬돼 있으니 한 번 `arr[i] >= target`이 되면 이후는 전부 True → `feasible(i) = arr[i] >= target`은 단조. → **First True** 그대로.

**풀이**
1. feasible = `arr[mid] >= target`.
2. True면 후보 기록 + 왼쪽으로, False면 오른쪽으로.

```python
def first_not_smaller(arr: list[int], target: int) -> int:
    left, right = 0, len(arr) - 1
    boundary = -1
    while left <= right:
        mid = (left + right) // 2
        if arr[mid] >= target:
            boundary = mid
            right = mid - 1
        else:
            left = mid + 1
    return boundary
```

**복잡도:** 시간 O(log n), 공간 O(1).

- 🐛 target이 모든 원소보다 크면 답이 없음 → -1 (문제에선 보장되지만 실전 코드에선 처리). `bisect_left`는 이 경우 `len(arr)` 반환 — 관례 차이 주의.
- 🐛 `>`로 쓰면 "target 초과 첫 원소"(= `upper_bound`/`bisect_right`)가 됨. 등호 하나가 문제를 바꾼다.
- 🔑 "~ 이상인 첫 번째", "삽입 위치", "lower bound" → feasible `arr[i] >= target`.

---

## 5. First Occurrence

**문제:** 중복이 있는 정렬 배열에서 target의 **첫 등장 인덱스**, 없으면 -1. 예: `arr=[1,3,3,3,3,6,10,10,10,100], target=3` → `1`; `[2,3,5,7,...], target=6` → `-1`.

**핵심 관찰:** 경계 자체는 `arr[i] >= target` (lower bound)와 같다. 차이는 **답이 target과 "정확히 같아야"** 한다는 것.

**풀이**
1. `arr[mid] == target` → 후보 기록, 더 왼쪽 탐색(`r = mid - 1`).
2. `arr[mid] < target` → 오른쪽(`l = mid + 1`).
3. `arr[mid] > target` → 왼쪽(`r = mid - 1`), **기록은 안 함**.

```python
def find_first_occurrence(arr: list[int], target: int) -> int:
    l, r, ans = 0, len(arr) - 1, -1
    while l <= r:
        mid = (l + r) // 2
        if arr[mid] == target:
            ans = mid
            r = mid - 1
        elif arr[mid] < target:
            l = mid + 1
        else:
            r = mid - 1
    return ans
```

**복잡도:** 시간 O(log n), 공간 O(1).

- 🐛 템플릿 그대로 `>=`일 때 기록하면, target이 없을 때 "target보다 큰 첫 원소"를 답으로 내버림 → **기록 조건을 `==`로 좁혀야** 한다. (또는 lower bound 구한 뒤 `arr[i] == target` 확인.)
- ✅ 변형: **Last occurrence**는 `==`일 때 기록 후 `l = mid + 1`. 두 개를 합치면 target의 개수 = last − first + 1 (LeetCode 34).
- 🔑 "중복 있음", "첫/마지막 위치", "몇 개" → 경계 이진 탐색 2번.

---

## 6. Square Root Estimation

**문제:** 내장 sqrt 없이 정수 n의 제곱근 **정수 부분**(소수점 버림). 예: `16 → 4`, `8 → 2` (2.83…).

**핵심 관찰:** 배열이 없어도 된다 — **답의 공간 [0, n]** 자체가 논리적 정렬 배열. `i*i`는 i에 대해 단조 증가 → `feasible(i) = i*i >= n`은 `F…FT…T`.
- 우리가 원하는 건 "`i*i <= n`인 **가장 큰** i" = 마지막 False(또는 딱 맞는 True).

**풀이**
1. 탐색 범위 `[1, n]`, feasible `mid*mid >= n`으로 First True를 찾는다.
2. 찾은 boundary의 제곱이 n과 같으면 그대로(완전제곱), 아니면 `boundary - 1`.

```python
def square_root(n: int) -> int:
    if n == 0:
        return 0
    left, right, boundary = 1, n, -1
    while left <= right:
        mid = (left + right) // 2
        if mid * mid >= n:
            boundary = mid
            right = mid - 1
        else:
            left = mid + 1
    return boundary if boundary * boundary == n else boundary - 1
```

**복잡도:** 시간 O(log n), 공간 O(1).

- 🐛 C/C++/Java: `mid * mid`가 32-bit overflow → `mid >= n / mid`로 비교하거나 64-bit로 캐스팅. (🎤 펌웨어 면접이면 꼭 짚기)
- 🐛 n=0 처리 누락, n=1 경계.
- ✅ 대안: feasible을 `mid*mid <= n`의 **마지막 True**로 잡아도 된다(기록 후 `left = mid + 1`). 어느 쪽이든 "무엇을 기록하나"만 명확히.
- 🔑 "배열 없이 정수 답을 찾아라", "가장 큰/작은 x such that …" → **답 공간(answer space) 이진 탐색**.

---

## 7. Minimum in Rotated Sorted Array

> 섹션: Implicitly Sorted Array

**문제:** 중복 없는 정렬 배열을 임의의 pivot에서 회전시켰다. **최솟값의 인덱스**를 구하라. 예: `[30,40,50,10,20]` → `3`; `[3,5,7,11,13,17,19,2]` → `7`.

**핵심 관찰:** 전체는 정렬이 아니지만, 그래프로 그리면 두 구간으로 나뉜다.
- 왼쪽 구간: **마지막 원소보다 큰** 값들 / 오른쪽 구간: **마지막 원소 이하** 값들.
- 최솟값 = 두 구간의 경계 = `arr[i] <= arr[-1]`이 처음 True가 되는 곳. → **First True**.

**풀이**
1. feasible = `arr[mid] <= arr[-1]` (mid가 "아래 구간"에 속함).
2. True → 기록, 왼쪽으로. False → 오른쪽으로.

```python
def find_min_rotated(arr: list[int]) -> int:
    left, right = 0, len(arr) - 1
    boundary = -1
    while left <= right:
        mid = (left + right) // 2
        if arr[mid] <= arr[-1]:     # 아래(작은 값) 구간
            boundary = mid
            right = mid - 1
        else:
            left = mid + 1
    return boundary
```

**복잡도:** 시간 O(log n), 공간 O(1).

- 🐛 기준을 `arr[0]`으로 잡으면 **회전이 0번**(이미 정렬)인 경우 모든 원소가 `>= arr[0]` → 경계가 없어 틀림. **`arr[-1]` 기준**은 회전 0번이어도 index 0이 첫 True라 안전.
- 🐛 중복 허용 버전(LC 154)은 `arr[mid] == arr[right]`일 때 판단 불가 → `right -= 1`로 최악 O(n).
- ✅ 확장: 회전 배열에서 target 찾기(LC 33) = 이 경계를 먼저 찾고 한쪽 구간에서 vanilla 탐색.
- 🔑 "rotated sorted", "pivot", "O(log n)" → 마지막 원소 기준 feasible.

---

## 8. Peak of Mountain Array

**문제:** 길이 ≥ 3, 엄격히 증가하다가 peak 이후 엄격히 감소하는 "산 배열"에서 **peak 인덱스**를 구하라 (peak는 양 끝이 아님, 유일). 예: `[0,1,2,3,2,1,0]` → `3`.

**핵심 관찰:** 값 자체는 단조가 아니지만 **"다음 원소보다 큰가?"**는 단조다.
- peak 이전: `arr[i] < arr[i+1]` → False / peak부터 끝까지: `arr[i] > arr[i+1]` → True.
- → feasible `arr[i] > arr[i+1]`의 **First True = peak**.

**풀이**
1. 마지막 원소는 다음이 없으니 오른쪽에 −∞가 있다고 생각 → True로 취급 (실제로 패딩하지 말고 인덱스 체크로).
2. First True 템플릿 적용.

```python
def peak_of_mountain_array(arr: list[int]) -> int:
    n = len(arr)
    left, right, boundary = 0, n - 1, -1
    while left <= right:
        mid = (left + right) // 2
        if mid == n - 1 or arr[mid] > arr[mid + 1]:   # 내리막(또는 끝)
            boundary = mid
            right = mid - 1
        else:
            left = mid + 1
    return boundary
```

**복잡도:** 시간 O(log n), 공간 O(1).

- 🐛 `arr[mid + 1]` 인덱스 범위 초과 — `mid == n-1` 먼저 검사(단락 평가).
- 🐛 배열 패딩(`arr + [-inf]`)은 O(n) 복사라 로그 복잡도가 깨진다.
- ✅ 같은 아이디어로 **Find Peak Element (LC 162)** — 여러 봉우리 중 아무거나: 오르막이면 오른쪽에 반드시 봉우리가 있다.
- 🔑 "mountain", "bitonic", "증가 후 감소", "peak" → 이웃 비교 feasible `arr[i] > arr[i+1]`.

---

## 9. Newspapers

> 섹션: Advanced

**문제:** 순서가 고정된 신문 더미(각각 읽는 시간)를 최대 `num_coworkers`명에게 **연속 구간**으로 나눠 준다. 모두 병렬로 읽을 때 총 시간 = 가장 느린 사람의 시간. **최소 총 시간**은? 예: `[7,2,5,10,8]`, 2명 → `[7,2,5]=14`, `[10,8]=18` → `18`. (= LC 410 Split Array Largest Sum)

**핵심 관찰:** 분배 방식을 직접 탐색하지 말고 질문을 뒤집는다 — **"시간 한도 T로 가능한가?"**
- T로 되면 T+1도 된다 (더 여유) → `feasible(T)`는 단조 → **답 공간에서 First True**.
- 탐색 범위: 하한 = `max(times)` (가장 긴 신문 하나는 누군가 통째로 읽어야), 상한 = `sum(times)` (한 명이 전부).

**풀이**
1. `feasible(T)`: 그리디 시뮬레이션 — 현재 사람에게 계속 얹다가 T를 넘으면 다음 사람으로. 쓴 사람 수 ≤ `num_coworkers`면 True.
2. `[max, sum]`에서 First True 템플릿.

```python
def newspapers_split(times: list[int], num_coworkers: int) -> int:
    def feasible(limit: int) -> bool:
        workers, cur = 1, 0
        for t in times:
            if cur + t > limit:      # 현재 사람 한도 초과 → 다음 사람
                workers += 1
                cur = 0
            cur += t
        return workers <= num_coworkers

    lo, hi, ans = max(times), sum(times), -1
    while lo <= hi:
        mid = (lo + hi) // 2
        if feasible(mid):
            ans = mid
            hi = mid - 1
        else:
            lo = mid + 1
    return ans
```

**복잡도:** 시간 O(n log S) (S = 합계, feasible 1회 O(n) × 탐색 O(log S)), 공간 O(1).

- 🐛 하한을 0이나 1로 잡으면 feasible 안에서 `t > limit`인 신문 하나를 처리 못 하는 버그 여지 → 하한은 `max(times)`.
- 🐛 그리디가 왜 최적 판정인가? 한도 안에서 **최대한 많이 앞사람에게 몰아주는 것**이 뒤에 남는 양을 최소화하므로 필요한 인원을 최소로 센다.
- 🎤 "최솟값을 최대화/최댓값을 최소화"는 거의 항상 **답 이진 탐색 + 그리디 검증**.
- 🔑 "minimize the maximum", "at most k groups/days/workers", "연속 구간 분할", Koko bananas, ship packages within D days → 같은 틀.

---

## 10. Binary Search Speedrun

> 섹션: Speedrun

- 형식: 시간 제한 없는 **객관식 복습**. 풀이 전체를 쓰지 않고 "이 문제의 핵심 기법/템플릿의 빈칸은?"만 빠르게 고르며 **패턴 인식**을 훈련.
- 선행: First True, Monotonic Function + 템플릿, 예제 몇 개.

### 스피드런용 셀프 체크 (feasible 한 줄로 답하기)
| 문제 | 탐색 공간 | feasible(x) → First True |
|---|---|---|
| First True | 인덱스 | `arr[x]` |
| lower bound | 인덱스 | `arr[x] >= target` |
| First occurrence | 인덱스 | `arr[x] >= target` (기록은 `==`일 때만) |
| sqrt | 값 [1, n] | `x*x >= n` (답은 완전제곱 아니면 −1) |
| 회전 배열 최솟값 | 인덱스 | `arr[x] <= arr[-1]` |
| 산 배열 peak | 인덱스 | `x == n-1 or arr[x] > arr[x+1]` |
| Newspapers / Split Array | 값 [max, sum] | 그리디로 센 인원 `<= k` |
| Koko bananas | 속도 [1, max] | `sum(ceil(p/x)) <= h` |

- ✅ 매 문제에서 세 가지를 먼저 말하기: **① 탐색 공간 ② feasible ③ 단조 방향**.
- 🔑 "sorted", "O(log n)", "minimum/maximum that satisfies", "at most k" → 이진 탐색 의심.

---

## 용어·패턴 사전

| 용어 | 뜻 |
|---|---|
| search range (탐색 범위) | 아직 답이 있을 수 있는 `[left, right]` 구간 |
| mid | 탐색 범위의 가운데. `(l+r)//2`, C 계열은 `l + (r-l)/2`로 overflow 방지 |
| monotonic (단조) | 비감소 또는 비증가. 이진 탐색이 가능한 근본 조건 |
| feasible function | 후보 x가 조건을 만족하는지 True/False로 판정하는 단조 함수 |
| boundary / first true | F→T로 바뀌는 첫 인덱스. 거의 모든 변형의 공통 목표 |
| lower bound / upper bound | `>= target` 첫 위치 / `> target` 첫 위치 (`bisect_left` / `bisect_right`) |
| answer space search | 배열 대신 가능한 답의 값 범위를 이진 탐색 |
| rotated sorted array | 정렬 배열을 pivot에서 회전시킨 것. 두 개의 정렬 구간 |
| mountain / bitonic array | 엄격히 증가 후 엄격히 감소하는 배열 |
| greedy check | 한도 T가 주어졌을 때 O(n) 시뮬레이션으로 가능 여부 판정 |
