# Module 11 — Miscellaneous (기타: Interval · Stack · Monotonic Stack · Divide & Conquer · Line Sweep · Greedy · Math · Matrix)

> 출처: AlgoMonster Coding Patterns, Module 11 (레슨 26개) · 한국어 개념 정리 노트
> 목표: 앞 모듈에 안 들어간 "자주 나오는 잡패턴"(구간 정렬, 스택/단조 스택, 분할 정복, 라인 스윕, 그리디, 소수 체, 희소 행렬)을 문제 문구만 보고 바로 떠올리기.

## 한눈에 보기 (5분 복습용)

| 패턴 | 언제 쓰나(키워드) | 핵심 아이디어 | 복잡도 |
|---|---|---|---|
| Merge / Insert Interval | "merge overlapping", "insert into sorted intervals" | start 정렬 → 마지막 유지 구간과만 비교, `max(end)`로 확장 / 왼·겹침·오 3구역 | O(n log n) / O(n) |
| Meeting Rooms I · II | "attend all?", "min rooms", "max concurrent" | 정렬 후 인접 비교 / end min-heap(또는 ±1 이벤트 스윕) | O(n log n) |
| Non-overlapping · Arrows | "min removals", "min points to stab all" | **end 정렬 그리디** — 가장 먼저 끝나는 것 유지 | O(n log n) |
| Partition Labels | "each letter in one part, max parts" | 문자별 last index, `end=max(end,last)`, `i==end`면 절단 | O(n) |
| Min Stack · Basic Calculator | "O(1) getMin", "evaluate + - ( )" | 보조 min 스택(`<=`) / (result, sign) 괄호 스택 | O(1) / O(n) |
| Monotonic stack | "next greater", "days until warmer", "circular" | 인덱스 감소 스택, pop 순간 답 확정, 원형은 2n + `i%n` | O(n) |
| Monotonic deque | "max of each window of size k" | 앞=최대, 뒤에서 작은 값 pop, 앞에서 윈도우 밖 제거 | O(n) |
| Largest Rectangle | "largest rectangle in histogram" | 막대별 좌·우 nearest smaller → `h*(R-L-1)` | O(n) |
| Divide & Conquer | "skyline", "count smaller after self", "inversions" | split → recurse → merge에서 정렬된 절반 이용 | O(n log n) |
| Line Sweep | "union area of rectangles", "overlap count" | 이벤트 정렬 + 활성 집합 + slab 누적, 좌표 압축 | O(n² log n)~O(n log n) |
| Iterative in-order | "k closest in BST", "BST iterator" | 명시적 스택 중위 순회 + 크기 k 윈도우, 조기 break | O(n) |
| Greedy | "gas station", "coin change(canonical)" | 정렬 키 + 한 패스, 교환 논증으로 정당화 / 음수면 리셋 | O(n)~O(n log n) |
| Prime Sieve | "count/n-th prime" | 소수의 배수 지우기 (`i*i`부터, √n까지) | O(n log log n) |
| Sparse Matrix | "mostly zeros" matrix multiply | i-j-l 루프 순서, `A[i][j]==0`이면 스킵 | O(nnz(A)·p) |

**구간 문제의 첫 질문: start 정렬(병합·충돌) vs end 정렬(최대 선택·최소 제거·찌르기).**

**단조 스택/deque: 각 원소는 한 번 push, 한 번 pop → O(n). pop되는 순간이 그 원소의 답이 정해지는 순간.**

**그리디는 교환 논증으로 증명하거나 작은 반례로 깨보고, 깨지면 DP로.**

**라인 스윕 = "이벤트 정렬 → 누적 먼저, 이벤트 적용 나중" + 좌표 압축 시 거리는 원 좌표로.**

---

## 1. Intervals
> 섹션: Interval

**핵심 개념:** interval = `[start, end]` 쌍 (회의 시간, 작업 구간 등). 구간 문제는 결국 **"두 구간이 겹치나? 겹치면 공통 부분은?"** 하나의 원시 연산으로 환원된다. 나머지는 bookkeeping.

### 겹침 판정 — 안 겹치는 조건부터
- `a`가 `b` 완전히 앞: `a.end < b.start`. 대칭으로 `b.end < a.start`.
- 둘 다 아니면 겹침 → De Morgan으로 부정: **`a.start <= b.end and b.start <= a.end`**.

### 공통 구간
- `[max(a.start, b.start), min(a.end, b.end)]` — 둘 다 시작한 뒤부터, 하나라도 끝나면 종료.
- `max_start <= min_end` 이면 겹침 → **공통 구간 계산 = 겹침 판정**이 같은 연산.

### 왜 start로 정렬하나
- 정렬 안 하면 모든 쌍 비교 O(n²). start 정렬 후엔 각 구간은 **뒤에 오는 구간하고만** 겹칠 수 있음 → 한 번 스윕.
- 템플릿: **정렬 → 한 번 스윕 → 마지막으로 유지한 구간과 비교.**

```python
def interval_pattern(intervals):
    intervals.sort(key=lambda x: x[0])
    res = []
    for s, e in intervals:
        if res and s <= res[-1][1]:          # 마지막 유지 구간과 겹침
            res[-1][1] = max(res[-1][1], e)  # 병합: 끝 확장
        else:
            res.append([s, e])
    return res
```
- **복잡도:** O(n log n) 정렬 + O(n) 스윕.
- 🐛 흔한 실수: 병합 시 `res[-1][1] = e`로 덮어쓰기 → 안쪽에 포함된 구간(`[1,10]`,`[2,3]`)에서 끝이 줄어듦. 반드시 `max`.
- 🐛 경계: 끝점이 닿는 `[1,2],[2,3]`을 겹침으로 볼지(`<=`) 문제마다 확인.
- 🔑 이 섹션의 문제들: Merge(병합), Insert(삽입 후 재병합), Non-overlapping/Arrows(그리디로 유지/제거), Meeting Rooms II(동시 활성 개수).

---

## 2. Merge Intervals

**문제:** 구간 목록에서 겹치는 구간을 모두 병합. `[[1,3],[2,6],[8,10],[15,18]] → [[1,6],[8,10],[15,18]]`. 끝점이 닿는 `[1,4],[4,5]`도 겹침 → `[1,5]`.

**핵심 관찰:** start로 정렬하면 새 구간은 **결과의 마지막 구간하고만** 비교하면 된다. 만약 뒤에서 두 번째 구간과 겹친다면 새 구간의 start가 마지막 구간 start보다 앞서야 하는데, 이는 정렬 순서에 모순.

**풀이**
1. start 기준 정렬.
2. 결과가 비었거나 마지막 구간과 안 겹치면 append.
3. 겹치면 마지막 구간의 end = `max(end, 새 end)`.

```python
def merge_intervals(intervals):
    intervals.sort()
    res = []
    for s, e in intervals:
        if not res or res[-1][1] < s:   # 안 겹침
            res.append([s, e])
        else:
            res[-1][1] = max(res[-1][1], e)
    return res
```
**복잡도:** 시간 O(n log n) (정렬), 추가 공간 O(1) (출력 제외, 정렬 제외).

- 🐛 함정: 포함 관계 `[1,10],[2,3]` → `max` 안 쓰면 end가 3으로 줄어듦. 닿는 경계는 겹침(`<` 비교로 "안 겹침" 판정).
- 🐛 입력 리스트를 그대로 결과에 넣으면 원본이 변형됨 — 필요 시 복사.
- 🔑 "merge overlapping", "합쳐진 타임라인", "union of ranges" → **정렬 + 마지막과 비교**.
- 🎤 "왜 마지막만 보나?" → start 정렬 불변식 때문에 앞 구간과 겹치면 반드시 마지막과도 겹친다(이미 병합됨).

---

## 3. Insert Interval

**문제:** start로 정렬된, 서로 안 겹치는 구간 목록에 새 구간을 넣고 필요하면 병합. `[[1,2],[3,5],[6,7],[8,10],[12,16]] + [4,8] → [[1,2],[3,10],[12,16]]`.

**핵심 관찰:** 가장 쉬운 길은 새 구간을 뒤에 붙이고 **Merge Intervals 그대로** (O(n log n)). 하지만 입력이 이미 정렬돼 있으니 **세 구역으로 나눠 한 번 스캔**하면 O(n).

**풀이 (O(n))**
1. `end < new.start` 인 구간 → 새 구간보다 완전히 왼쪽 → 그대로 결과에.
2. `start <= new.end` 인 동안 → 겹침 → 새 구간을 `min(start)`, `max(end)`로 확장.
3. 확장된 새 구간 추가.
4. 나머지(완전히 오른쪽) 그대로 추가.

```python
def insert_interval(intervals, new):
    res, i, n = [], 0, len(intervals)
    s, e = new
    while i < n and intervals[i][1] < s:      # 왼쪽
        res.append(intervals[i]); i += 1
    while i < n and intervals[i][0] <= e:     # 겹침 → 흡수
        s = min(s, intervals[i][0]); e = max(e, intervals[i][1]); i += 1
    res.append([s, e])
    res.extend(intervals[i:])                 # 오른쪽
    return res
```
**복잡도:** O(n) 시간, 출력 외 O(1).

- 🐛 엣지: 빈 목록 → `[new]`. 새 구간이 기존에 포함(`[1,5]+[2,3]`) → `[1,5]`. 새 구간이 여러 개를 삼킴(`[4,8]`).
- 🐛 두 번째 while 조건은 `start <= e`(갱신되는 e!) — 처음 new_end로 고정하면 연쇄 병합을 놓침. 사실 e는 확장돼도 다음 구간 start 판정엔 문제없지만, 루프 안에서 e를 갱신해 두는 게 안전.
- 🔑 "already sorted, non-overlapping + insert one" → **왼쪽 / 겹침 / 오른쪽 3구역 선형 스캔**.
- 🎤 "정렬 다시 하면 O(n log n)인데 더 빠르게?" → 3-phase 선형 풀이.

---

## 4. Meeting Rooms

**문제:** 회의 구간 배열이 주어질 때 한 사람이 모든 회의에 참석 가능한지(= 어떤 두 회의도 겹치지 않는지). `[[0,30],[5,10],[15,20]] → false`, `[[7,10],[2,4]] → true`.

**핵심 관찰:** start 정렬 후엔 **인접한 쌍만** 확인하면 된다. 어떤 두 회의가 겹치면 정렬된 순서상 이웃한 쌍 중 하나도 반드시 겹친다.

**풀이**
1. start로 정렬.
2. `i = 1..n-1`에서 `prev.end > cur.start`면 충돌 → `False`.
3. 끝까지 통과하면 `True`.

```python
def meeting_rooms(intervals):
    intervals.sort()
    return all(intervals[i-1][1] <= intervals[i][0] for i in range(1, len(intervals)))
```
**복잡도:** O(n log n) 시간, O(1) 추가 공간.

- 🐛 경계: `[1,5],[5,8]`처럼 끝나는 순간 시작 → 참석 가능 (`>` 로 비교, `>=` 아님).
- 🐛 정렬 안 하고 인접 비교만 하면 틀림 (`[[7,10],[2,4]]`).
- 🔑 "can attend all", "any conflict/overlap?" → **정렬 + 인접 비교**. "몇 개 방이 필요?"로 바뀌면 → Meeting Rooms II.

---

## 5. Meeting Rooms II

**문제:** 겹치는 회의가 같은 방을 못 쓸 때 필요한 **최소 회의실 수**. `[[0,30],[5,10],[15,20]] → 2`, `[[0,30],[5,10],[15,20],[5,15]] → 3`.

**핵심 관찰:** 필요한 방 수 = **동시에 진행 중인 회의 수의 최댓값**(가장 바쁜 순간). start 순으로 훑으면서 "지금 비어 있는 방이 있나?"만 판단하면 된다. 방은 **가장 빨리 끝나는 회의**가 먼저 비므로 end 시각 **min-heap**으로 관리.

**풀이**
1. start로 정렬.
2. 각 회의에 대해 heap top(가장 이른 end) `<=` 현재 start이면 그 방 재사용 → pop.
3. 현재 회의 end push (재사용이든 새 방이든).
4. 최종 heap 크기 = 필요한 방 수 (heap은 줄지 않는 한 방식이라 크기 = 피크).

```python
import heapq

def min_meeting_rooms(intervals):
    intervals.sort()
    ends = []                                # 사용 중인 방들의 end
    for s, e in intervals:
        if ends and ends[0] <= s:
            heapq.heapreplace(ends, e)       # 빈 방 재사용 (pop+push)
        else:
            heapq.heappush(ends, e)          # 새 방
    return len(ends)
```
**복잡도:** O(n log n) 시간, O(n) 공간.

### 대안: 이벤트 스윕 (라인 스윕)
```python
def min_meeting_rooms_sweep(intervals):
    starts = sorted(s for s, _ in intervals); ends = sorted(e for _, e in intervals)
    rooms = best = j = 0
    for s in starts:
        while ends[j] <= s: rooms -= 1; j += 1   # 끝난 회의 반납
        rooms += 1; best = max(best, rooms)
    return best
```
- 🐛 `[5,10]`이 끝나는 10에 시작하는 `[10,15]`는 같은 방 사용 가능 → `<=`.
- 🐛 heap에 pop만 하고 push를 빼먹는 실수. 재사용이든 아니든 현재 end는 항상 push.
- 🔑 "minimum rooms/platforms/servers", "max concurrent", "동시에 몇 개" → **end min-heap** 또는 **+1/-1 이벤트 스윕**.
- 🎤 "왜 heap 크기가 답?" → pop은 재사용일 때만 일어나므로 heap 크기는 지금까지 연 방 수 = 동시성 피크.

---

## 6. Non-overlapping Intervals

**문제:** 나머지가 서로 겹치지 않도록 제거해야 하는 **최소 구간 수**. (끝점이 닿는 건 겹침 아님.) `[[1,2],[2,3],[3,4],[1,3]] → 1` (`[1,3]` 제거), `[[1,2],[1,2],[1,2]] → 2`.

**핵심 관찰:** "최소 제거" = n − "**겹치지 않게 최대한 많이 남기기**" (activity selection). 충돌하는 두 구간 중 **먼저 끝나는 쪽**을 남기는 게 이후에 더 많은 공간을 남기므로 절대 손해가 아님 → **end 기준 정렬** 그리디.

**풀이**
1. end 기준 정렬, `last_end = -inf`.
2. `start >= last_end` → 남김, `last_end = end`.
3. 아니면 이미 남긴 구간과 겹침 → 제거 카운트 (last_end 유지).

```python
def non_overlapping_intervals(intervals):
    intervals.sort(key=lambda x: x[1])
    removed, last_end = 0, float('-inf')
    for s, e in intervals:
        if s >= last_end:
            last_end = e
        else:
            removed += 1
    return removed
```
**복잡도:** O(n log n) 시간, O(1) 추가 공간.

- 🐛 **start로 정렬하면 틀린다** (긴 구간 `[1,100]`이 먼저 잡혀 여럿을 막음). start 정렬로 하려면 겹칠 때 end가 더 큰 쪽을 버리는 보정이 필요.
- 🐛 닿는 경계 `[1,2],[2,3]`은 유지 → `>=`.
- 🔑 "minimum removals to make non-overlapping", "최대 몇 개 겹치지 않게 선택" → **end 정렬 greedy (interval scheduling)**.
- 🎤 교환 논증: 최적해의 첫 구간을 가장 먼저 끝나는 구간으로 바꿔도 여전히 유효하고 개수는 같다.

---

## 7. Minimum Number of Arrows to Burst Balloons

**문제:** 풍선 = x축 구간 `[start, end]`. x에서 수직으로 쏜 화살은 `start <= x <= end`인 풍선을 모두 터뜨림. 전부 터뜨리는 **최소 화살 수**. `[[10,16],[2,8],[1,6],[7,12]] → 2` (x=6, x=12).

**핵심 관찰:** 화살 하나 = 한 점을 공유하는 풍선 그룹 하나. Non-overlapping Intervals와 같은 **end 정렬 그리디**. 첫 풍선의 **end**에 쏘는 게 그 풍선을 맞추면서 가장 오른쪽 → 뒤 풍선을 최대한 많이 같이 맞춤.

**풀이**
1. end 기준 정렬, `arrow = -inf`.
2. `start > arrow`이면 기존 화살로 못 맞춤 → 새 화살을 이 풍선의 end에 발사, count++.
3. 아니면 이미 터짐.

```python
def find_min_arrow_shots(points):
    arrows, pos = 0, float('-inf')
    for s, e in sorted(points, key=lambda p: p[1]):
        if s > pos:          # 마지막 화살 오른쪽에서 시작 → 새 화살
            arrows += 1
            pos = e
    return arrows
```
**복잡도:** O(n log n) 시간, O(1) 추가 공간(정렬 제외).

- 🐛 Non-overlapping과의 차이: 여기는 **닿는 경계도 같이 터짐**(`[1,2],[2,3]` → 화살 1개) → `s > pos` (strict).
- 🐛 LeetCode 버전은 좌표가 int32 극값 → `end - start` 같은 뺄셈 비교자 오버플로 주의(Java/C++).
- 🔑 "한 점으로 모든 구간 찌르기(stabbing)", "minimum points covering all intervals" → **end 정렬 + 끝점에 찍기**.
- 🔑 답 = n − (Non-overlapping 제거 수) 관계(경계 규칙만 다름).

---

## 8. Partition Labels

**문제:** 소문자 문자열을 **가능한 한 많은 조각**으로 자르되 각 문자는 한 조각에만 등장해야 함. 각 조각 길이 리스트 반환. `"ababcbacadefegdehijhklij" → [9,7,8]`, `"eccbbbbdec" → [10]`.

**핵심 관찰:** 각 문자 = 첫 등장~마지막 등장의 **구간(span)**. 조각은 어떤 문자의 span 중간에서 끝날 수 없다 → 조각은 포함한 모든 문자의 마지막 위치까지 뻗어야 한다. 구간 병합을 문자 span에 적용한 것.

**풀이**
1. 각 문자의 마지막 인덱스 `last[c]` 기록.
2. 왼→오 스캔, `end = max(end, last[s[i]])`.
3. `i == end`가 되는 순간 현재 조각의 모든 문자가 닫힘 → 길이 `i - start + 1` 기록, `start = i + 1`.

```python
def partition_labels(s):
    last = {c: i for i, c in enumerate(s)}
    res, start, end = [], 0, 0
    for i, c in enumerate(s):
        end = max(end, last[c])
        if i == end:
            res.append(i - start + 1)
            start = i + 1
    return res
```
**복잡도:** O(n) 시간, O(1) 공간(알파벳 26개 고정).

- 🐛 `last`를 dict comprehension으로 만들면 뒤 인덱스가 덮어써서 자동으로 "마지막" — 편리.
- 🐛 `i == end`에서 자르는 것이 **가장 이른 절단점** → 조각 수 최대화(그리디).
- 🔑 "각 문자/원소가 한 그룹에만", "as many parts as possible" → **last occurrence + 확장하는 end**.

---

## 9. Min Stack
> 섹션: Stack

**문제:** `push`, `pop`, `top`, `getMin`을 **모두 O(1)**로 지원하는 스택 설계. 예: `push -2, push 0, push -3, getMin→-3, pop, top→0, getMin→-2`.

**핵심 관찰:** push/pop/top은 평범한 스택. 어려운 건 getMin — 매번 스캔하면 O(n). **보조 스택(min stack)**이 "지금까지의 최솟값"을 그림자처럼 따라가게 하면 top만 읽으면 된다.

**풀이**
1. `push(v)`: 본 스택에 push. min 스택이 비었거나 `v <= min_top`이면 min 스택에도 push.
2. `pop()`: 본 스택에서 뺀 값이 `min_top`과 같으면 min 스택도 pop.
3. `getMin()` = min 스택 top.

```python
class MinStack:
    def __init__(self):
        self.st, self.mins = [], []
    def push(self, v):
        self.st.append(v)
        if not self.mins or v <= self.mins[-1]:
            self.mins.append(v)
    def pop(self):
        if self.st.pop() == self.mins[-1]:
            self.mins.pop()
    def top(self):    return self.st[-1]
    def getMin(self): return self.mins[-1]
```
**복잡도:** 모든 연산 O(1), 공간 O(n).

- 🐛 **`<=` 필수**: 같은 최솟값이 두 번 push될 때 `<`로 하면 첫 pop에서 min이 사라져 틀림.
- 🐛 대안: 본 스택에 `(val, cur_min)` 쌍을 저장 — 더 단순하지만 항상 2배 메모리.
- 🔑 "O(1) getMin/getMax", "스택인데 최솟값도" → **보조 min 스택 / (값, 누적 최소) 쌍**.
- 🎤 확장: Max Stack(popMax까지)이면 이중 연결 리스트 + 정렬 맵 필요.

---

## 10. Basic Calculator

**문제:** 음이 아닌 정수, `+`, `-`, 괄호, 공백으로 된 유효한 수식을 eval 없이 계산. `"1 - (2 + 3)" → -4`, `"(1+(4+5+2)-3)+(6+8)" → 23`.

**핵심 관찰:** `*`/`/`가 없으니 **한 번의 왼→오 스캔**으로 충분. 상태는 두 개: 누적 `result`, 다음 숫자에 곱할 `sign(±1)`. 유일한 변수는 괄호 → **스택에 (바깥 result, 괄호 앞 sign) 저장**. `-(...)`처럼 괄호 앞 부호가 그룹 전체에 적용되는 게 괄호가 의미 있는 이유.

**풀이**
1. 숫자: 여러 자리 끝까지 읽어 `result += sign * num`.
2. `+`/`-`: sign 설정.
3. `(`: `result`, `sign` push → `result=0, sign=1`로 새로 시작.
4. `)`: `sign_prev, res_prev` pop → `result = res_prev + sign_prev * result`.
5. 공백 무시.

```python
def calculate(s):
    stack, res, sign, i, n = [], 0, 1, 0, len(s)
    while i < n:
        c = s[i]
        if c.isdigit():
            num = 0
            while i < n and s[i].isdigit():
                num = num * 10 + int(s[i]); i += 1
            res += sign * num
            continue
        if c == '+': sign = 1
        elif c == '-': sign = -1
        elif c == '(':
            stack.append((res, sign)); res, sign = 0, 1
        elif c == ')':
            prev, psign = stack.pop(); res = prev + psign * res
        i += 1
    return res
```
**복잡도:** O(n) 시간, O(괄호 깊이) 공간.

- 🐛 여러 자리 숫자 읽은 뒤 `continue`로 `i += 1` 건너뛰기 — 안 하면 숫자 다음 문자를 놓침.
- 🐛 `(` 후 sign 리셋 안 하면 괄호 안 첫 숫자에 바깥 부호가 두 번 적용됨.
- 🐛 단항 마이너스 `-(3)`/`"-2+1"`: res=0에서 sign=-1로 시작하니 자연스럽게 처리됨.
- 🔑 "evaluate expression", "parentheses", "no eval" → **sign + result + 괄호 스택**. `*`/`/`까지 있으면 → "마지막 항" 스택(Basic Calculator II) 또는 shunting-yard.

---

## 11. Monotonic Stack Intro
> 섹션: Monotonic Stack

**핵심 개념:** 단조 스택 = push할 때마다 **정렬 상태(증가 또는 감소)를 유지**하는 스택. 가장 흔한 건 **감소 스택**: 새 값 `x`를 넣기 전에 top이 `x` 이하인 동안 pop → 그 뒤 push하면 bottom→top 감소 유지. **단조 deque**는 같은 규칙 + 앞쪽에서 오래된 원소 제거 가능(슬라이딩 윈도우용).

### 핵심 성질
- 각 원소는 **한 번 push, 최대 한 번 pop** → 전체 O(n) (amortized).
- pop은 낭비가 아니라 **정보**: index `j`가 index `i` 처리 중 pop되면, `i`는 `j` 오른쪽에서 조건을 만족하는 **첫 번째** 원소. (감소 스택이면 `nums[i]`가 `nums[j]`의 next greater.)
- 실전에선 값 대신 **인덱스**를 저장 → 값은 배열에서 읽고, 거리(며칠 뒤?)도 계산 가능.

```python
def next_greater(nums):
    res = [-1] * len(nums)
    st = []                              # 인덱스, 값 기준 감소
    for i, x in enumerate(nums):
        while st and nums[st[-1]] < x:   # x가 이들의 next greater
            res[st.pop()] = x
        st.append(i)
    return res                           # [3,1,6,2] → [6,6,-1,-1]
```

### 언제 쓰나
| 질문 | 도구 |
|---|---|
| 다음/이전 더 큰(작은) 원소, 며칠 기다려야? | 단조 **스택** (Daily Temperatures) |
| 윈도우 내 최대/최소 | 단조 **deque**, front = 현재 최대, 범위 밖 index는 front에서 제거 (Sliding Window Maximum) |
| 막대가 양쪽으로 얼마나 뻗나 (히스토그램) | 증가 스택 — pop될 때 좌우 경계 확정 |

- 🐛 흔한 실수: `<` vs `<=` 선택 → 중복 값 처리(같은 값이 pop되나 남나)가 달라짐. "strictly greater"면 `<`로 pop.
- 🐛 값만 저장하면 거리/위치 정보를 잃음 → 인덱스 저장.
- ✅ 체크포인트: `[3,1,6,2]` 감소 스택에서 6이 들어오면 1, 3이 pop → 둘의 next greater = 6.
- 🎤 "왜 O(n)인가?" → 이중 while처럼 보여도 원소당 push/pop 각 1회.

---

## 12. Sliding Window Maximum

**문제:** 크기 k 윈도우가 한 칸씩 오른쪽으로 이동할 때 매 윈도우의 최댓값. `arr=[1,3,2,5,8,7], k=3 → [3,5,8,8]`.

**핵심 관찰**
- Brute force: 윈도우마다 k개 스캔 → O(nk). 바깥 루프는 줄일 수 없으니 **안쪽(최대 찾기)**을 줄여야 함.
- Max heap + **lazy deletion**(top의 index가 윈도우 밖이면 그때 pop) → O(n log n), 오래된 원소가 쌓여 공간 O(n).
- 결정적 관찰: `l < r`이고 `arr[r] >= arr[l]`이면 `arr[l]`은 **먼저 나가면서 더 작으니 절대 최대가 될 수 없다** → 버려도 됨. 남는 것은 앞→뒤 **감소하는 deque**.

**풀이 (단조 deque, 인덱스 저장)**
1. 새 원소 `cur`: back에서 `nums[q[-1]] <= cur`인 동안 pop, 그리고 push.
2. front 인덱스가 `i - k`(윈도우 밖)이면 popleft.
3. `i >= k-1`부터 `nums[q[0]]`을 결과에.

```python
from collections import deque

def max_sliding_window(nums, k):
    q, res = deque(), []                   # 인덱스, 값 기준 감소
    for i, x in enumerate(nums):
        while q and nums[q[-1]] <= x:
            q.pop()
        q.append(i)
        if q[0] <= i - k:                  # 윈도우 밖
            q.popleft()
        if i >= k - 1:
            res.append(nums[q[0]])
    return res
```
**복잡도:** O(n) 시간 (각 인덱스 push/pop 1회), O(k) 공간.

- 🐛 값이 아니라 **인덱스** 저장 — 윈도우 이탈 판단에 필요.
- 🐛 첫 결과는 `i == k-1`부터. 그 전엔 윈도우가 덜 참.
- 🐛 deque는 "앞은 크고 오래됨, 뒤는 작고 최신" — 크기와 위치 정보를 동시에 담는다.
- 🔑 "max/min of every window of size k", "최근 k개 중 최대" → **단조 deque**. (min이면 증가 deque.)
- 🎤 heap(O(n log n)) → deque(O(n)) 발전 과정을 설명하면 좋은 인상.

---

## 13. Daily Temperatures

**문제:** 일별 기온 배열에서 각 날마다 **더 따뜻한 날까지 며칠 기다려야 하는지**(없으면 0). `[73,74,75,71,69,72,76,73] → [1,1,4,2,1,1,0,0]`.

**핵심 관찰:** 날마다 앞으로 스캔하면 O(n²). 한 번만 훑으려면 "아직 답을 못 찾은 날들"을 스택에 보관하다가, 더 따뜻한 날이 오면 그 날들이 한꺼번에 해결된다. 스택의 기온은 bottom→top **감소**(단조 스택).

**풀이**
1. 스택엔 답이 없는 날의 **인덱스**.
2. 오늘 `i`: `t[i] > t[top]`인 동안 pop → `res[top] = i - top`.
3. `i` push.
4. 끝까지 스택에 남은 날은 0 (초기값).

```python
def daily_temperatures(t):
    res, st = [0] * len(t), []
    for i, x in enumerate(t):
        while st and t[st[-1]] < x:
            j = st.pop()
            res[j] = i - j
        st.append(i)
    return res
```
**복잡도:** O(n) 시간, O(n) 공간.

- 🐛 "warmer" = strictly → 같은 기온에서는 pop 안 함(`<`).
- 🐛 값이 아니라 인덱스를 넣어야 거리 계산 가능.
- 🔑 "how many days until …", "next greater element", "다음에 더 큰 값까지 거리" → **감소 단조 스택(인덱스)**.
- ✅ 체크포인트: 75(idx2)는 71, 69, 72를 지나 76(idx6)에서 pop → 4.

---

## 14. Next Greater Element II

**문제:** **원형 배열**(마지막 다음 = 첫 원소)에서 각 원소의 다음 더 큰 수, 없으면 -1. `[1,2,1] → [2,-1,2]` (두 번째 1은 한 바퀴 돌아 2를 찾음).

**핵심 관찰:** Daily Temperatures와 동일한 감소 단조 스택. 원형이라는 점만 다름 → **두 번째 패스**를 돌며 스택에 남은(아직 답 없는) 원소들을 해결하되 **새로 push하지는 않는다**(이미 한 번 들어갔으므로).

**풀이**
1. 1차 패스: 표준 next-greater (인덱스 스택).
2. 2차 패스: 같은 배열을 다시 훑으며 pop만 수행.
3. 끝까지 남은 원소(최댓값들) = -1.

```python
def next_greater_elements(nums):
    n = len(nums)
    res, st = [-1] * n, []
    for i in range(2 * n):              # 인덱스 모듈러로 두 바퀴
        x = nums[i % n]
        while st and nums[st[-1]] < x:
            res[st.pop()] = x
        if i < n:                        # 첫 바퀴에서만 push
            st.append(i)
    return res
```
**복잡도:** O(n) 시간 (2n 스캔, 각 원소 push/pop 1회), O(n) 공간.

- 🐛 두 번째 바퀴에서도 push하면 중복 처리·잘못된 답.
- 🐛 최댓값(여러 개일 수 있음)은 끝까지 -1 — strict `<` 유지.
- 🔑 "circular array", "wrap around" + next greater → **2n 순회 + `i % n`**.

---

## 15. Largest Rectangle in Histogram

**문제:** 폭 1인 막대 높이 배열에서 히스토그램 안에 들어가는 **최대 직사각형 넓이**. 직사각형 높이는 범위 내 최소 막대에 제한됨. `[2,1,5,6,2,3] → 10` (높이 5 × 폭 2).

**핵심 관찰:** 직사각형을 직접 찾지 말고, **각 막대를 높이로 삼았을 때 얼마나 넓힐 수 있나**를 묻는다. 좌우로 **자기보다 strictly 낮은 막대**를 만날 때까지 확장 → "양쪽 nearest smaller element" = 단조 스택 문제.

**풀이**
1. 왼→오 패스: 증가 스택 유지. 현재 막대가 top보다 낮으면 top의 `right[top] = i`로 확정하고 pop.
2. 오→왼 패스: 대칭으로 `left[]` 채움.
3. 각 막대 넓이 `h[i] * (right[i] - left[i] - 1)`의 최댓값.

```python
def largest_rectangle(h):
    n = len(h)
    left, right, st = [-1] * n, [n] * n, []
    for i in range(n):
        while st and h[st[-1]] > h[i]:
            right[st.pop()] = i
        st.append(i)
    st = []
    for i in reversed(range(n)):
        while st and h[st[-1]] > h[i]:
            left[st.pop()] = i
        st.append(i)
    return max((h[i] * (right[i] - left[i] - 1) for i in range(n)), default=0)
```
**복잡도:** O(n) 시간, O(n) 공간.

### 한 패스 버전 (sentinel)
```python
def largest_rectangle_one_pass(h):
    st, best = [], 0
    for i, x in enumerate(h + [0]):          # 끝에 0 → 전부 flush
        while st and h[st[-1]] > x:
            height = h[st.pop()]
            left = st[-1] if st else -1        # pop 후 새 top = 왼쪽 경계
            best = max(best, height * (i - left - 1))
        st.append(i)
    return best
```
- 🐛 폭 = `right - left - 1` (양쪽 경계는 exclusive).
- 🐛 기본값 `left=-1`, `right=n` 잊으면 끝까지 뻗는 막대 계산 오류.
- 🐛 한 패스 버전은 `h + [0]` sentinel 없으면 스택에 남은 막대를 놓침.
- 🔑 "largest rectangle", "maximal area bounded by minimum", Maximal Rectangle(행렬 → 행마다 히스토그램) → **양방향 nearest smaller + 단조 스택**.

---

## 16. Divide and Conquer Intro
> 섹션: Divide and Conquer

**핵심 개념:** 큰 문제를 **같은 종류의 더 작은 부분 문제**로 반복 분할 → 각각 재귀로 풀기 → 답을 **합치기(combine)**. 템플릿 = **split → recurse → merge**.

### 예: Merge Sort
- `[7,2,5,1]` → `[7,2]`,`[5,1]` → 원소 1개까지 분할(1개는 이미 정렬 = base case).
- 병합: `[7]+[2]→[2,7]`, `[5]+[1]→[1,5]`, `[2,7]+[1,5]→[1,2,5,7]`.
- 핵심: 각 재귀 호출은 **더 작은 같은 작업**, merge가 "두 풀린 절반 → 전체 답"을 만든다.

```python
def merge_sort(a):
    if len(a) <= 1:                 # base case
        return a
    mid = len(a) // 2
    L, R = merge_sort(a[:mid]), merge_sort(a[mid:])   # divide
    out, i, j = [], 0, 0                               # conquer (merge)
    while i < len(L) and j < len(R):
        if L[i] <= R[j]: out.append(L[i]); i += 1
        else:            out.append(R[j]); j += 1
    return out + L[i:] + R[j:]
```
- **복잡도:** 레벨마다 전체 n개를 한 번씩 처리 × log n 레벨 → **O(n log n)**. (Master theorem: `T(n)=2T(n/2)+O(n)`.)

### 왜/언제
- 합치기 단계에서 **양쪽 절반의 정렬 상태 등 구조를 이용**할 수 있을 때 강력 — 예: 역순쌍 개수, Count of Smaller After Self, Skyline 병합, closest pair.
- 🐛 흔한 실수: base case 누락/잘못(빈 배열), 슬라이싱으로 인한 추가 O(n log n) 메모리(인덱스 범위로 넘기면 절약), 안정 정렬을 위해 `<=` 사용.
- 🎤 "D&C를 merge sort로 설명" → 재귀는 크기 ≤1에서 멈추고, merge는 두 정렬된 절반을 선형 시간에 하나로 — 이것이 없으면 분할만 하고 답이 없다.

---

## 17. The Skyline Problem

**문제:** 건물 `[L, R, H]` 목록(모두 높이 0 바닥에 섬)으로 만든 **스카이라인의 key point**(높이가 바뀌는 지점) `[[x,y],...]` 반환, 마지막은 높이 0. `[[2,9,10],[3,7,15],[5,12,12],[15,20,10],[19,24,8]] → [[2,10],[3,15],[7,12],[12,0],[15,10],[20,8],[24,0]]`.

**핵심 관찰 (D&C):** 부분 문제의 답 자체가 스카이라인. 건물을 반으로 나눠 각각 스카이라인을 구하면 남은 일은 **두 스카이라인 병합** 하나뿐. 두 스카이라인 모두 x로 정렬 → **merge sorted arrays처럼 두 포인터**.

**풀이**
1. base: 건물 1개 → `[[L, H], [R, 0]]`.
2. 분할 → 재귀 → merge.
3. merge: x가 작은 점을 소비하며 해당 쪽 현재 높이(`ly` 또는 `ry`) 갱신. 보이는 높이 = `max(ly, ry)`. **높이가 바뀔 때만** key point 추가. 같은 x면 양쪽 동시 소비.

```python
def get_skyline(b):
    if not b: return []
    if len(b) == 1:
        L, R, H = b[0]
        return [[L, H], [R, 0]]
    m = len(b) // 2
    return merge(get_skyline(b[:m]), get_skyline(b[m:]))

def merge(A, B):
    res, i, j, ha, hb = [], 0, 0, 0, 0
    while i < len(A) or j < len(B):
        xa = A[i][0] if i < len(A) else float('inf')
        xb = B[j][0] if j < len(B) else float('inf')
        x = min(xa, xb)
        if xa == x: ha = A[i][1]; i += 1
        if xb == x: hb = B[j][1]; j += 1     # 같은 x면 둘 다 소비
        h = max(ha, hb)
        if not res or res[-1][1] != h:        # 높이 변화만 기록
            res.append([x, h])
    return res
```
**복잡도:** 레벨당 O(n) 병합 × log n 레벨 → **O(n log n)** 시간, O(n) 공간.

- 🐛 같은 x에서 두 점을 따로 처리하면 불필요한 중간 key point가 생김 → 동시 소비 또는 마지막 점 높이 덮어쓰기.
- 🐛 연속된 같은 높이 점 금지(`[2,3],[4,3]` ✗) → "높이 변할 때만 append".
- 🐛 대안: **라인 스윕 + max-heap**(시작 이벤트 push, 끝난 건물 lazy pop) — 역시 O(n log n), 면접에서 더 흔함.
- 🔑 "skyline", "outline of rectangles", "height changes along x" → **D&C 병합 또는 스윕+heap**.

---

## 18. Count of Smaller Numbers After Self

**문제:** `counts[i]` = `nums[i]` 오른쪽에 있는 더 작은 원소 개수. `[5,2,6,1] → [2,1,1,0]`. 변형: **정렬에 필요한 인접 swap 수 = 역순쌍(inversion) 총수** = `sum(counts)` (예: 4).

**핵심 관찰:** 쌍을 전부 보면 O(n²). **merge sort를 인덱스 기준으로 분할**하면 왼쪽 절반의 모든 원소는 원래 오른쪽 절반의 모든 원소보다 앞에 있었다 → merge 중 `left_val > right_val`인 쌍은 전부 유효한 "오른쪽의 더 작은 수". 두 절반이 정렬돼 있으니 한꺼번에 셀 수 있다.

**풀이**
1. `(원래 인덱스, 값)` 쌍으로 merge sort.
2. merge에서 왼쪽 원소를 결과에 놓을 때, **그때까지 앞질러 나간 오른쪽 원소 수 `r`**을 `counts[원래 인덱스]`에 더함.
3. 동률은 왼쪽 먼저(`<=`) → 같은 값은 "더 작은"으로 안 셈.

```python
def count_smaller(nums):
    cnt = [0] * len(nums)
    def sort(arr):
        if len(arr) <= 1: return arr
        m = len(arr) // 2
        L, R = sort(arr[:m]), sort(arr[m:])
        out, r = [], 0
        for item in L:
            while r < len(R) and R[r][1] < item[1]:   # 더 작은 오른쪽 원소가 먼저 나감
                out.append(R[r]); r += 1
            cnt[item[0]] += r
            out.append(item)
        out.extend(R[r:])
        return out
    sort(list(enumerate(nums)))
    return cnt
```
**복잡도:** `T(n)=2T(n/2)+O(n)` → O(n log n) 시간, O(n) 공간.

- 🐛 값만 정렬하면 원래 위치를 잃음 → **enumerate로 인덱스 동반**.
- 🐛 동률 처리: strict smaller이므로 같은 값의 오른쪽 원소는 왼쪽보다 먼저 나가면 안 됨(`<` 비교).
- 🐛 대안: 오른쪽부터 **BIT/Fenwick**(좌표 압축) 또는 정렬 리스트 + bisect (O(n²) worst insert).
- 🔑 "number of elements smaller to the right", "count inversions", "minimum adjacent swaps to sort" → **merge sort 카운팅 / BIT**.

---

## 19. Line-Sweep Introduction
> 섹션: Line Sweep

**핵심 개념:** 2D 문제를 **정렬된 1D 이벤트 처리**로 바꾸는 기법. 수직선을 왼→오로 움직인다고 상상 — 도형의 왼쪽/오른쪽 변에 닿을 때만 상태가 바뀐다. **두 이벤트 사이 구간(slab)은 상태가 동일**하므로 한 덩어리로 계산.

### 예: 두 직사각형 합집합 넓이
- A = `[1,4]×[1,3]`, B = `[2,5]×[2,4]`. x 이벤트: 1(A 시작), 2(B 시작), 4(A 끝), 5(B 끝).
- `[1,2)`: A만 → y 길이 2. `[2,4)`: A∪B의 y = `[1,4)` → 3. `[4,5)`: B만 → 2.
- 넓이 = 2·1 + 3·2 + 2·1 = **10**.
- 패턴: **이벤트 정렬 → 활성 집합 유지 → 이벤트 사이 기여 누적**.

### 좌표 압축 (Coordinate Compression)
- 좌표가 10⁹여도 **이벤트가 일어나는 좌표만** 중요 → 고유 좌표를 정렬해 인덱스 0..k-1로 매핑.
- ⚠️ **인덱스 거리 ≠ 실제 거리**. 넓이/길이 계산은 반드시 `xs[i+1] - xs[i]` 원래 좌표 차로.

```python
def compress(values):
    xs = sorted(set(values))
    idx = {v: i for i, v in enumerate(xs)}
    return xs, idx          # xs[i]: 원래 좌표, idx[v]: 압축 인덱스

# 1D 스윕 템플릿: 최대 동시 겹침 수
def max_overlap(intervals):
    events = []
    for s, e in intervals:
        events += [(s, +1), (e, -1)]
    events.sort()           # 같은 좌표면 -1이 먼저 → 닿는 구간은 안 겹침
    cur = best = 0
    for _, d in events:
        cur += d; best = max(best, cur)
    return best
```
- **언제 쓰나:** 한 축으로 이벤트 정렬 가능 + 각 이벤트가 **다른 축의 활성 집합**을 갱신 → 직사각형 합집합 넓이, 선분 교차 개수, 구간 겹침 수, skyline.
- 🐛 흔한 실수: 같은 좌표의 시작/끝 이벤트 순서(경계 포함 여부), 압축 인덱스로 넓이 계산, 끝 이벤트 누락.
- 🎤 "좌표가 10⁹인데 배열로?" → 이벤트 좌표만 압축, 거리는 원 좌표로.

---

## 20. Union Area of Rectangles

**문제:** 축 정렬 직사각형 `[x1,y1,x2,y2]`(반열린 영역)들이 덮는 **합집합 넓이**(겹침은 한 번만). `[[1,1,4,3],[2,2,5,4]] → 10` (6+6−2). 닿기만 하면 겹침 아님, 포함된 사각형은 추가 기여 0.

**핵심 관찰:** 라인 스윕 그대로. 각 사각형 = 열림 이벤트 `(x1, +1, i)` + 닫힘 이벤트 `(x2, -1, i)`. 연속 이벤트 x 사이 slab에서는 활성 사각형이 안 바뀜 → slab 넓이 = **덮인 y 길이 × Δx**. 덮인 y 길이 = 활성 사각형 y구간들의 **Merge Intervals**.

**풀이**
1. 이벤트 정렬.
2. 같은 x의 이벤트 그룹마다: **먼저** 이전 활성 집합으로 `[prev_x, x)` slab 넓이 누적 → **그다음** 이벤트 적용(추가/제거).
3. y 길이: 활성 y구간 정렬 후 병합하며 길이 합.

```python
def rectangle_area(rects):
    events = sorted([(x1, 1, i) for i, (x1, _, x2, _) in enumerate(rects)] +
                    [(x2, -1, i) for i, (x1, _, x2, _) in enumerate(rects)])
    active, total, prev_x, k = set(), 0, events[0][0], 0
    while k < len(events):
        x = events[k][0]
        covered, cur_s, cur_e = 0, None, None          # 활성 y구간 병합 길이
        for y1, y2 in sorted((rects[i][1], rects[i][3]) for i in active):
            if cur_e is None or y1 > cur_e:
                if cur_e is not None: covered += cur_e - cur_s
                cur_s, cur_e = y1, y2
            else:
                cur_e = max(cur_e, y2)
        if cur_e is not None: covered += cur_e - cur_s
        total += covered * (x - prev_x)                 # 이벤트 적용 전 누적!
        while k < len(events) and events[k][0] == x:    # 같은 x 이벤트 일괄 적용
            _, d, i = events[k]
            active.add(i) if d == 1 else active.discard(i)
            k += 1
        prev_x = x
    return total                                        # (LeetCode 850은 % 1e9+7)
```
**복잡도:** 이벤트 O(n)개 × 매번 활성 y구간 정렬 O(n log n) → **O(n² log n)** 시간, O(n) 공간. (segment tree + 좌표 압축이면 O(n log n).)

- 🐛 **누적 → 적용 순서**가 핵심: x에서의 활성 집합은 아직 `[prev_x, x)`를 설명한다.
- 🐛 같은 x 이벤트를 한꺼번에 처리 — x에서 끝나는 사각형은 오른쪽을 안 덮고, 시작하는 건 덮는다.
- 🐛 활성 집합엔 **인덱스** 저장(사각형 좌표 복사 X).
- 🔑 "union area of rectangles", "total covered area" → **x 스윕 + y 구간 병합** (고급: segment tree).

---

## 21. Closest BST Values II
> 섹션: Tree Traversal without Recursion

**문제:** BST에서 `x`에 가장 가까운 값 k개를 **값 순으로** 반환. BST를 리스트로 변환 금지. 예: `x=7, k=4 → [5,6,8,10]`.

**핵심 관찰:** 정렬 배열이라면 크기 k **슬라이딩 윈도우**를 앞에서부터 밀면 된다 — 윈도우 맨 앞 원소보다 새 원소가 x에 더 가까우면 앞을 빼고 새 걸 넣고, 아니면 이후는 더 멀어지기만 하므로 **중단**. BST에선 **중위 순회 = 정렬 순서**, 이를 **반복문(명시적 스택)**으로 돌리면 중간에 break 가능.
- 다음 노드(in-order successor): 오른쪽 서브트리가 있으면 그 서브트리의 최左 노드, 없으면 아직 오른쪽을 안 본 가장 가까운 조상(= 스택 top).

**풀이**
1. 반복 중위 순회: 왼쪽 끝까지 push → pop → 방문 → 오른쪽으로.
2. 방문 값: deque 크기 < k면 append.
3. 가득 찼으면 `|front - x| > |val - x|`일 때 popleft + append, 아니면 break.

```python
from collections import deque

def closest_values(root, x, k):
    win, st, cur = deque(), [], root
    while st or cur:
        while cur:                          # 왼쪽 끝까지
            st.append(cur); cur = cur.left
        cur = st.pop()                      # 정렬 순서로 방문
        if len(win) < k:
            win.append(cur.val)
        elif abs(win[0] - x) > abs(cur.val - x):
            win.popleft(); win.append(cur.val)
        else:
            break                           # 이후 값은 더 멀어짐
        cur = cur.right
    return list(win)
```
**복잡도:** 최악 O(n) 시간, O(h + k) 공간 (h = 트리 높이).

- 🐛 재귀 중위 순회로 하면 조기 종료가 번거로움 → **명시적 스택 순회**가 이 섹션의 요점.
- 🐛 동률(거리 같음)일 때 규칙 확인 — 위 코드는 앞(작은 값)을 유지.
- 🔑 "k closest in BST", "BST iterator", "next/previous in sorted order without list" → **반복 중위 순회 + 스택**. (O(h + k) 최적화: predecessor/successor 두 스택.)

---

## 22. Greedy Introduction
> 섹션: Greedy

**핵심 개념:** 그리디 = 매 단계 **지금 가장 좋아 보이는 선택**을 하고 되돌리지 않음(백트래킹·미리보기 없음). "국소 최선의 연속 = 전역 최선"이라는 베팅.
- 예: 동전 {1,5,10,50}로 69센트 → 50, 10, 5, 1×4 = 7개. "들어가는 가장 큰 동전 먼저" 규칙 하나가 알고리즘 전부.

### Greedy-choice rule
- 각 단계에서 선택지를 **비교 가능한 순서로 정렬**하는 규칙: 동전 값, "가장 먼저 끝나는 구간", "가장 가까운 마감" 등.
- 장점: 짧고 빠름 — 보통 정렬 O(n log n) 또는 한 패스 O(n). 단점: **정확성 증명**이 진짜 일.

### 정당화: 교환 논증 (Exchange Argument)
- 임의의 최적해를 **그리디 해 쪽으로 한 번씩 교환**해도 절대 나빠지지 않음을 보이면 그리디 ≥ 최적.
- {1,5,10,50}: 큰 동전 값이 작은 동전의 배수 → 작은 동전 묶음을 50 하나로 바꾸면 개수가 줄어듦 → "최적해가 그리디와 다르다"는 가정에 모순.

### 그리디가 실패할 때
- 동전 {1,20,30}, 40 만들기: 그리디 30 + 1×10 = **11개** vs 최적 20×2 = **2개**. 30이 20의 배수가 아니라 교환 논증 붕괴.
- 실전 검증법: **작은 반례 찾기** — 국소 최선과 전역 최선이 갈리는 입력을 손으로 돌려본다. 깨지면 → **DP**로.

```python
def greedy(items):
    items.sort(key=greedy_key)          # 1) 그리디 키로 정렬 (마감, 끝시각, 크기, 비율…)
    state, result = init(), 0
    for it in items:
        if feasible(it, state):         # 2) 국소 최선이 유효하면 채택
            result += take(it, state)
    return result                       # 3) 정당성은 테스트가 아니라 교환 논증으로

def make_change(coins, amount):         # canonical 동전계에서만 최적
    count = 0
    for c in sorted(coins, reverse=True):
        count += amount // c; amount %= c
    return count if amount == 0 else -1
```
- 🐛 흔한 실수: 예제 몇 개 통과했다고 그리디를 믿음 → 반례 탐색/교환 논증 필수.
- 🎤 "그리디가 맞다는 걸 어떻게 보이나?" → 교환 논증(또는 "greedy stays ahead"). 반례가 있으면 DP.
- 🔑 이 모듈의 그리디: Non-overlapping(끝시각), Arrows(끝점), Partition Labels(가장 이른 절단), Gas Station(시작점 리셋).

---

## 23. Gas Station

**문제:** 원형 경로의 n개 주유소, `gas[i]` 주유 가능, `dist[i]` = i→i+1 거리(연료 1당 거리 1). 빈 탱크로 어느 주유소에서 출발해야 한 바퀴 돌 수 있나? 불가능하면 -1 (답은 유일). `gas=[1,2,3,4,5], dist=[3,4,5,1,2] → 3`.

**핵심 관찰 (그리디):** `start`에서 출발해 `i`에서 탱크가 음수가 되면 `i+1`에 못 간다. 게다가 **start~i 사이 어느 역에서 출발해도 실패** — 중간에서 출발하면 그 앞 구간에서 모은 (비음수) 잉여 연료조차 없이 i+1에 도달해야 하므로. → 실패 구간 통째로 버리고 **i+1부터 재시작**.

**풀이**
1. `tank += gas[i] - dist[i]`, 음수면 `start = i+1`, `tank = 0`.
2. 전체 합 `sum(gas) - sum(dist) < 0`이면 어디서도 불가 → -1.
3. 아니면 마지막 `start`가 답. (강의 구현은 2n까지 시뮬레이션하며 n칸 연속 성공을 확인하는 방식 — 동일 아이디어.)

```python
def starting_station(gas, dist):
    total = tank = start = 0
    for i in range(len(gas)):
        diff = gas[i] - dist[i]
        total += diff; tank += diff
        if tank < 0:            # start..i 전부 탈락
            start, tank = i + 1, 0
    return start if total >= 0 else -1
```
**복잡도:** O(n) 시간, O(1) 공간.

- 🐛 total 체크 없이 start만 반환하면 불가능한 입력에서 틀림.
- 🐛 왜 total ≥ 0이면 마지막 start가 반드시 성공? — start 이전 구간들의 합은 음수였으므로, 나머지(start~끝)의 합이 전체를 메울 만큼 크다.
- 🔑 "circular route", "can complete the circuit", "start index" → **누적합 음수면 리셋하는 그리디** (Kadane과 닮음).

---

## 24. Prime Sieve Intro
> 섹션: Math

**핵심 개념:** n 이하의 소수를 효율적으로 모두 구하는 **에라토스테네스의 체(Sieve of Eratosthenes)**.

### 단계별 개선
1. **Naive:** 각 수를 자기보다 작은 모든 수로 나눠보기 → 수 하나 O(n), 전체 O(n²).
2. **√n까지만:** 합성수 n의 약수 `d > √n`이면 짝 `n/d < √n`도 약수 → 작은 쪽만 확인. 예: 16 = 2×8, 8의 짝 2가 √16=4보다 작음. 전체 O(n√n).
3. **체:** 2부터 올라가며 소수 p를 만나면 **p의 배수를 전부 합성수로 표시**. 이미 합성수로 표시된 수는 건너뜀. 비용 `n/2 + n/3 + n/5 + …` ≈ **O(n log log n)**, 공간 O(n).

```python
def prime_sieve(n):
    is_p = [True] * (n + 1)
    is_p[0] = is_p[1] = False
    for i in range(2, int(n ** 0.5) + 1):   # √n까지만 돌아도 충분
        if is_p[i]:
            for j in range(i * i, n + 1, i): # i*i부터: 더 작은 배수는 이미 처리됨
                is_p[j] = False
    return is_p                               # [i for i,p in enumerate(is_p) if p]
```
- 🐛 흔한 실수: 0과 1을 소수로 둠, `range` 끝을 `n`으로 해서 n 자체를 빠뜨림(`n+1`).
- 🐛 최적화 두 개: 바깥 루프 `√n`까지, 안쪽은 `i*i`부터 — 둘 다 결과는 같고 상수만 줄임.
- 🔑 "count primes ≤ n", "여러 수의 소수 판정", "n번째 소수" → **체로 전처리** 후 O(1) 조회. 단일 수 판정이면 √n trial division.
- 🎤 "왜 n log log n?" → 소수 p마다 n/p 작업, 소수 역수합 ~ log log n.

---

## 25. N-th prime

**문제:** n번째 소수를 구하라. 답은 100000 미만 보장. `n=3 → 5` (2,3,5), `n=5 → 11`.

**핵심 관찰:** 상한 M(=100001)이 주어졌으니 **체로 M까지 소수를 표시**하면서 소수를 만날 때마다 카운트, n번째에서 멈추면 끝.

**풀이**
1. `is_p[0..M-1] = True`, 0·1은 False.
2. i = 2..M-1: `is_p[i]`면 count++, `count == n`이면 반환. 아니면 i의 배수 지우기.

```python
def nth_prime(n, M=100001):
    is_p = [True] * M
    is_p[0] = is_p[1] = False
    cnt = 0
    for i in range(2, M):
        if is_p[i]:
            cnt += 1
            if cnt == n:
                return i
            for j in range(i * i, M, i):
                is_p[j] = False
    return -1
```
**복잡도:** O(M log log M) 시간, O(M) 공간.

- 🐛 상한이 없다면? → 소수 정리로 추정: n번째 소수 < `n (ln n + ln ln n)` (n ≥ 6) 로 M을 잡는다.
- 🐛 `i*i`가 M을 넘으면 안쪽 루프는 자연히 비어 있음 — 문제 없음.
- 🔑 "n-th prime", "상한이 주어진 소수 질의" → **체 + 카운트**.

---

## 26. Sparse Matrix Multiplication
> 섹션: Matrix

**문제:** 대부분이 0인 희소 행렬 A(n×m), B(m×p)의 곱 AB. `a=[[1,0,3],[0,1,2]], b=[[0,1],[1,3],[0,0]] → [[0,1],[1,3]]`.

**핵심 관찰:** 특별한 자료구조 없이 **루프와 인덱스 연습**. 정의: `C[i][l] = Σ_j A[i][j]·B[j][l]`. 세 루프(A 행 i, 공유 차원 j, B 열 l)의 순서는 자유 → **i-j-l 순서**로 두면 `A[i][j] == 0`일 때 B의 j행 전체를 건너뛸 수 있다.

**풀이**
1. `C = n×p` 영행렬.
2. A의 각 원소 `A[i][j]`가 0이 아니면, B의 j행을 훑으며 `C[i][l] += A[i][j] * B[j][l]`.

```python
def multiply(a, b):
    n, p = len(a), len(b[0])
    c = [[0] * p for _ in range(n)]
    for i, row in enumerate(a):
        for j, av in enumerate(row):
            if av == 0:
                continue                        # 희소성 활용: B의 j행 스킵
            for l, bv in enumerate(b[j]):
                if bv:
                    c[i][l] += av * bv
    return c
```
**복잡도:** 최악 O(n·m·p) 시간(= A 크기 × B 열 수), 0이 많을수록 실제로는 **nnz(A)·p**. 공간 O(n·p).

- 🐛 `[[0]*p]*n`은 같은 행 리스트를 n번 참조 → 한 칸 수정이 모든 행에 반영. **comprehension**으로 생성.
- 🐛 i-l-j 순서(교과서 순서)로 하면 A[i][j]=0 스킵을 루프 밖으로 못 뺌.
- 🐛 더 희소하면 B도 `{row: [(col, val)]}` 비영(nnz) 리스트로 전처리 (CSR 개념).
- 🔑 "sparse matrix", "mostly zeros" → **nonzero만 순회 / 루프 순서 바꾸기 / CSR**.
- 🎤 임베디드 관점: i-j-l 순서는 B와 C를 **행 단위 연속 접근**해 캐시 친화적이기도 하다.

---

## 용어·패턴 사전

| 용어 | 뜻 |
|---|---|
| Interval overlap | `a.start <= b.end and b.start <= a.end`. 공통 구간 = `[max start, min end]` |
| Sort by start | 병합/삽입/충돌 탐지의 표준 첫 줄. 이후 마지막 유지 구간과만 비교 |
| Sort by end (interval scheduling) | 최대 비겹침 선택·최소 제거·화살 문제의 그리디 키 |
| End-time min-heap | 사용 중 자원(방)의 종료 시각 관리 → 동시성 피크 = 필요한 자원 수 |
| Min stack | 보조 스택에 누적 최솟값을 그림자처럼 유지, `<=`로 push |
| Sign + result stack | `+ - ( )` 수식 평가: 괄호에서 (result, sign) 저장/복원 |
| Monotonic stack | push 전 조건 위반 top을 pop해 단조성 유지. pop 순간 = 그 원소의 답 확정 |
| Monotonic deque | 단조 스택 + front에서 윈도우 밖 인덱스 제거 → 윈도우 최대/최소 O(n) |
| Nearest smaller/greater | 좌/우 첫 번째 더 작은/큰 원소. 히스토그램 폭 계산의 기반 |
| Circular array trick | `2n` 순회 + `i % n`, 두 번째 바퀴는 pop만 |
| Divide and conquer | split → recurse → merge. `T(n)=2T(n/2)+O(n)` → O(n log n) |
| Inversion | `i<j, a[i]>a[j]`인 쌍. 개수 = 인접 swap으로 정렬하는 최소 횟수 |
| Line sweep | 한 축으로 이벤트 정렬, 활성 집합 유지, 이벤트 사이 slab 기여 누적 |
| Coordinate compression | 이벤트 좌표만 정렬·인덱싱. 거리 계산은 원 좌표 차로 |
| Iterative in-order | 명시적 스택으로 BST를 정렬 순서로 순회, 중간 break 가능 |
| Greedy-choice rule | 매 단계 선택지를 순위화하는 규칙(최대 동전, 최조 종료 등) |
| Exchange argument | 최적해를 그리디 쪽으로 교환해도 나빠지지 않음을 보여 그리디 정당화 |
| Sieve of Eratosthenes | 소수의 배수를 지워 n 이하 소수 전부: O(n log log n) |
| Sparse matrix | 대부분 0인 행렬. 0 아닌 원소만 순회(nnz), CSR 표현 |
