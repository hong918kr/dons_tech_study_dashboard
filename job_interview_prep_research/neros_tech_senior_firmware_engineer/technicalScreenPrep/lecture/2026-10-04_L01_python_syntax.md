# 📗 L01 · 문제 풀기 전에 반드시 알아야 할 Python 문법 — enqueue/dequeue부터 C와 비교하며

> [L00 특별강의](2026-10-04_L00_special_lecture.md)의 ring buffer · pytest · LeetCode 15문제를 풀 때 **실제로 쓰이는 문법만** 모았다. C 개발자 관점에서 "C로는 이렇게, Python으로는 이렇게"를 나란히 놓고, **함정**(음수 나눗셈, `~`, 리스트 공유, mutable 기본값)을 따로 표시했다. 아래 코드는 전부 `lecture/code/l01_syntax.py` 한 파일에 있고, 각 줄의 `assert`가 실행 결과다 — `python3 lecture/code/l01_syntax.py` → `OK`.

## 0. 한 장 요약 — 무엇을 언제 쓰나

| 하고 싶은 것 | Python | 복잡도 | C로 치면 |
|---|---|---|---|
| 큐 (FIFO): enqueue / dequeue | `deque.append(x)` / `deque.popleft()` | O(1) / O(1) | ring buffer push / pop |
| 스택 (LIFO): push / pop / top | `list.append(x)` / `list.pop()` / `list[-1]` | O(1) | 배열 + top 인덱스 |
| 스레드 사이 큐 | `queue.Queue.put(x)` / `.get(timeout=)` | O(1) | mutex + condvar + ring |
| 우선순위 큐 (최솟값 먼저) | `heapq.heappush(h, x)` / `heapq.heappop(h)` | O(log n) | 배열 기반 heap |
| 키 → 값 찾기 | `dict[key]`, `dict.get(key, 기본)` | 평균 O(1) | 해시 테이블 직접 구현 |
| 있나 없나 | `x in set` | 평균 O(1) | 해시 셋 / 비트맵 |
| 개수 세기 | `Counter(xs)` | O(n) | `int count[256]` |
| 정렬 | `sorted(xs, key=...)` / `xs.sort()` | O(n log n) | `qsort` + 비교 함수 |
| 문자열 만들기 | `"".join(parts)` | O(n) | `snprintf` 누적 |

> 이 표의 첫 줄이 질문의 답이다: **Python에서 큐는 `collections.deque`로 만들고, enqueue = `append`, dequeue = `popleft`.** `list.pop(0)`도 동작하지만 O(n)이다.

## 1. 큐 — enqueue / dequeue

### 1.1 list로 만든 큐 (동작은 하지만 느리다)

```python
q = []
q.append(1)            # enqueue — 뒤에 넣기, O(1)
q.append(2)
q.append(3)
assert q.pop(0) == 1   # dequeue — 앞에서 빼기, 하지만 O(n): 나머지를 전부 한 칸씩 당긴다
assert q == [2, 3]
```

- `list`는 C의 **동적 배열**이다. 뒤에 넣고 빼기(`append` / `pop()`)는 O(1)이지만 **앞에서 빼기(`pop(0)`)는 나머지를 전부 한 칸씩 당기므로 O(n)** — C에서 `memmove(&a[0], &a[1], ...)` 하는 것과 같다
- 면접에서 `pop(0)`로 큐를 쓰면 "복잡도는?"이 바로 나온다 → 처음부터 `deque`

### 1.2 deque — Python의 표준 큐

```python
from collections import deque

q = deque()
q.append(1)            # enqueue (오른쪽에 넣기)   O(1)
q.append(2)
q.appendleft(0)        # 왼쪽에 넣기               O(1)
assert q.popleft() == 0  # dequeue (왼쪽에서 빼기) O(1)  ← 큐는 이것
assert q.pop() == 2      # 오른쪽에서 빼기        O(1)  ← 스택처럼도 쓸 수 있다
assert q[0] == 1 and len(q) == 1        # 맨 앞 보기(peek), 길이
assert not deque()                      # 빈 컨테이너는 False

last3 = deque(maxlen=3)                 # 고정 크기: 넘치면 반대쪽이 자동으로 밀려남 (= overwrite ring)
for x in range(5):
    last3.append(x)
assert list(last3) == [2, 3, 4]
```

| 연산 | deque | list | 비고 |
|---|---|---|---|
| 뒤에 넣기 | `append` O(1) | `append` O(1) | enqueue |
| 앞에서 빼기 | **`popleft` O(1)** | `pop(0)` **O(n)** | dequeue |
| 앞에 넣기 | `appendleft` O(1) | `insert(0, x)` O(n) | |
| 뒤에서 빼기 | `pop` O(1) | `pop` O(1) | 스택 |
| 가운데 인덱스 `q[i]` | O(n) | O(1) | deque는 블록 연결 리스트라 임의 접근이 느리다 |
| 고정 크기 | `deque(maxlen=n)` | 없음 | 넘치면 반대쪽이 밀려남 = overwrite ring |

- 이름 주의: Python에는 `enqueue`/`dequeue`라는 메서드가 **없다**. LeetCode 622가 `enQueue`/`deQueue`라는 이름을 요구하는 것은 문제가 정한 클래스 인터페이스일 뿐 → [L00 S1](2026-10-04_L00_special_lecture.md)
- 빈 deque에서 `popleft()`는 `IndexError` → 먼저 `if q:`로 확인 (빈 컨테이너는 False)
- BFS(너비 우선 탐색), 슬라이딩 윈도우, 이동 평균(LC 346)이 전부 deque

### 1.3 스택

```python
stack = []
stack.append("(")      # push
stack.append("[")
assert stack[-1] == "["  # top (peek) — 음수 인덱스 -1 = 마지막
assert stack.pop() == "["  # pop — 마지막을 빼기, O(1)
assert stack == ["("]
```

- 스택은 그냥 `list`. top은 `stack[-1]` (빈 스택이면 `IndexError`)
- 괄호 검사(LC 20), Min Stack(LC 155), DFS, "이전 것과 비교" 문제

### 1.4 스레드 사이의 큐 — queue.Queue

```python
import queue

jobs = queue.Queue(maxsize=8)      # 스레드 사이 큐: 내부에 Lock + Condition
jobs.put("frame-1")                # enqueue (가득 차면 기다림)
assert jobs.get(timeout=1) == "frame-1"   # dequeue (비어 있으면 기다림, timeout 초과 → queue.Empty)
try:
    jobs.get(timeout=0.01)
except queue.Empty:
    pass
```

- `deque`는 자료구조, `queue.Queue`는 **동기화 도구**(내부에 Lock + Condition). producer/consumer 스레드 사이에는 `queue.Queue`
- `put`/`get`이 Python의 enqueue/dequeue 이름이다. `get()`에 timeout을 주지 않으면 영원히 기다릴 수 있다 → 테스트 코드에선 항상 timeout
- 왜 lock이 필요한지: [N06 Python 스레드 vs C](../notes/2026-10-03_N06_python_threads_vs_c.md)

### 1.5 우선순위 큐 — heapq

```python
import heapq

h = []
for x in (5, 1, 4):
    heapq.heappush(h, x)           # min-heap: 가장 작은 것이 h[0]
assert h[0] == 1
assert heapq.heappop(h) == 1       # O(log n)
assert heapq.nsmallest(2, [5, 1, 4, 2]) == [1, 2]

mx = []                            # max-heap 은 없다 → 음수로 넣는다
for x in (5, 1, 4):
    heapq.heappush(mx, -x)
assert -heapq.heappop(mx) == 5

tasks = []                         # (우선순위, 값) 튜플 — 앞 원소부터 비교
heapq.heappush(tasks, (2, "log"))
heapq.heappush(tasks, (0, "failsafe"))
assert heapq.heappop(tasks) == (0, "failsafe")
```

- `heapq`는 **보통 list를 min-heap으로 다루는 함수 모음**이다 (클래스가 아님). `h[0]`이 항상 최솟값
- max-heap이 필요하면 **음수로 넣고 꺼낼 때 다시 음수** — 단골 트릭
- 튜플 `(우선순위, 값)`을 넣으면 첫 원소로 비교. Top K(LC 347), 스케줄러, 다익스트라

## 2. list — 인덱스 · 슬라이스 · 복사

```python
xs = [10, 20, 30, 40, 50]
assert xs[0] == 10 and xs[-1] == 50          # 음수 인덱스는 뒤에서부터
assert xs[1:3] == [20, 30]                   # 슬라이스 [start, stop) — stop 은 포함 안 함
assert xs[:2] == [10, 20] and xs[3:] == [40, 50]
assert xs[::-1] == [50, 40, 30, 20, 10]      # 뒤집은 '새' 리스트
assert xs[::2] == [10, 30, 50]               # 두 칸씩
assert len(xs) == 5 and 30 in xs             # in 은 리스트에선 O(n)
assert xs.index(30) == 2
assert sum(xs) == 150 and max(xs) == 50 and min(xs) == 10

ys = xs                # 같은 객체를 가리키는 이름이 하나 더 (C 포인터 복사와 비슷)
zs = xs[:]             # 얕은 복사 (list(xs), xs.copy() 도 같음)
ys.append(60)
assert xs[-1] == 60 and len(zs) == 5

grid_bad = [[0] * 3] * 3      # 함정: 같은 행 객체 3개를 공유
grid_bad[0][0] = 1
assert grid_bad[1][0] == 1    # 모든 행이 바뀐다
grid = [[0] * 3 for _ in range(3)]   # 올바른 2차원 리스트
grid[0][0] = 1
assert grid[1][0] == 0
```

| C | Python | 주의 |
|---|---|---|
| `a[n - 1]` | `a[-1]` | 음수 인덱스 = 뒤에서부터 |
| `for (i = 1; i < 3; i++)` 로 부분 복사 | `a[1:3]` | **stop은 포함 안 함** `[start, stop)` |
| 거꾸로 복사 루프 | `a[::-1]` | 새 리스트를 만든다 (O(n) 메모리) |
| `int *b = a;` | `b = a` | 같은 객체. b를 바꾸면 a도 바뀐다 |
| `memcpy(b, a, ...)` | `b = a[:]` / `a.copy()` | **얕은** 복사 — 안쪽 리스트는 공유 |
| `int g[3][3] = {0};` | `[[0] * 3 for _ in range(3)]` | `[[0]*3]*3`은 같은 행 3개를 공유하는 **버그** |

## 3. 정렬 — sorted vs sort, key

```python
words = ["ccc", "a", "bb", "aa"]
assert sorted(words) == ["a", "aa", "bb", "ccc"]               # 새 리스트를 돌려준다
assert sorted(words, key=len) == ["a", "bb", "aa", "ccc"]      # 안정 정렬: 같은 길이는 원래 순서
assert sorted(words, key=lambda w: (len(w), w)) == ["a", "aa", "bb", "ccc"]   # 여러 기준 = 튜플
assert sorted(words, reverse=True)[0] == "ccc"
words.sort()                       # 제자리 정렬, None 을 돌려준다 (x = xs.sort() 는 버그)
assert words[0] == "a"
intervals = [[8, 10], [1, 3], [2, 6]]
intervals.sort(key=lambda iv: iv[0])
assert intervals == [[1, 3], [2, 6], [8, 10]]
```

- `sorted(x)` = 새 리스트 반환, `x.sort()` = 제자리 + **`None` 반환** (`x = x.sort()`는 x를 None으로 만드는 단골 버그)
- C `qsort`의 비교 함수 대신 **key 함수**: "각 원소를 무엇으로 비교할지"만 준다. 여러 기준은 튜플 `(len(w), w)`
- Python 정렬은 **stable**(같은 키는 원래 순서 유지) — Merge Intervals(LC 56), Group Anagrams(LC 49)

## 4. dict · Counter · defaultdict · OrderedDict

```python
d = {"vbat": 16.4, "rssi": -70}
d["state"] = "ARMED"                       # 넣기 / 덮어쓰기, 평균 O(1)
assert d["vbat"] == 16.4
assert d.get("missing") is None            # 없으면 None (d["missing"] 은 KeyError)
assert d.get("missing", 0) == 0            # 기본값
assert "rssi" in d                         # 키 검사 O(1)
assert list(d) == ["vbat", "rssi", "state"]   # 넣은 순서를 기억한다 (3.7+)
for key, value in d.items():               # 키와 값 함께
    assert d[key] == value
del d["rssi"]
assert d.pop("state") == "ARMED" and d == {"vbat": 16.4}
squares = {n: n * n for n in range(4)}     # dict comprehension
assert squares[3] == 9

counts = {}
for ch in "abca":
    counts[ch] = counts.get(ch, 0) + 1     # 세기의 기본형
assert counts == {"a": 2, "b": 1, "c": 1}
```

```python
from collections import Counter, OrderedDict, defaultdict

c = Counter("abracadabra")                 # 세기 전용 dict
assert c["a"] == 5 and c["z"] == 0         # 없는 키는 0 (KeyError 아님)
assert c.most_common(2) == [("a", 5), ("b", 2)]

groups = defaultdict(list)                 # 없는 키에 접근하면 list() 를 만들어 넣는다
for w in ["arm", "abort", "boot"]:
    groups[w[0]].append(w)
assert dict(groups) == {"a": ["arm", "abort"], "b": ["boot"]}

lru = OrderedDict()
lru["a"] = 1
lru["b"] = 2
lru.move_to_end("a")                       # 'a' 를 가장 최근으로
assert list(lru) == ["b", "a"]
assert lru.popitem(last=False) == ("b", 2) # 가장 오래된 것 꺼내기
```

| 상황 | 쓰는 것 | 문제 |
|---|---|---|
| 값 → 인덱스 기억 | `dict` | Two Sum (LC 1) |
| 개수 세기 | `Counter` 또는 `d.get(k, 0) + 1` | Top K (LC 347) |
| 키별로 묶기 | `defaultdict(list)` | Group Anagrams (LC 49) |
| 최근 사용 순서 | `OrderedDict.move_to_end` / `popitem(last=False)` | LRU Cache (LC 146) |
| 마지막 시각 기억 | `dict` | Logger Rate Limiter (LC 359), Longest Substring (LC 3) |

- `d[key]`는 없으면 **KeyError**, `d.get(key)`는 None. 문제를 풀 땐 `get` 또는 `in`으로 먼저 확인
- dict 키가 될 수 있는 것: 바꿀 수 없는(hashable) 값 — int, str, tuple. **list는 키가 못 된다** → tuple로 바꿔서

## 5. set과 tuple

```python
seen = set()
seen.add(3)
seen.add(3)                                # 중복은 무시
assert len(seen) == 1 and 3 in seen        # in 은 O(1) — 리스트(O(n))와 다르다
a, b = {1, 2, 3}, {2, 3, 4}
assert a & b == {2, 3} and a | b == {1, 2, 3, 4} and a - b == {1}

point = (3, 4)                             # 튜플: 바꿀 수 없다 → dict 키 / set 원소로 쓸 수 있다
x, y = point                               # 언패킹
x, y = y, x                                # swap — 임시 변수 없이
assert (x, y) == (4, 3)
visited = {(0, 0), (0, 1)}                 # 좌표 집합 (그리드 문제 단골)
assert (0, 1) in visited
first, *rest = [1, 2, 3]
assert first == 1 and rest == [2, 3]
```

- "봤나?" 검사는 **set** (O(1)). 리스트로 `in` 검사를 반복하면 O(n²)이 된다
- tuple은 바꿀 수 없는 list. 좌표 `(r, c)`, dict 키, 함수의 여러 반환값, `a, b = b, a` swap

## 6. 문자열

```python
s = "  Arm,Disarm ,Land "
parts = [p.strip().lower() for p in s.split(",")]
assert parts == ["arm", "disarm", "land"]
assert "-".join(parts) == "arm-disarm-land"     # 문자열 합치기는 join (+= 반복은 O(n²) 가능)
assert "abc"[::-1] == "cba"
assert "a1!".isalnum() is False and "a1".isalnum() and "7".isdigit()
assert ord("a") == 97 and chr(98) == "b"
assert ord("c") - ord("a") == 2                 # 알파벳 인덱스 (26칸 배열 문제)
assert "".join(sorted("tea")) == "aet"          # sorted(문자열) 은 리스트 → join
assert "TEMP=23.5".split("=")[1] == "23.5"
assert "key = v=1".partition("=") == ("key ", "=", " v=1")
t = "abc"
try:
    t[0] = "z"                                  # 문자열은 바꿀 수 없다 (immutable)
except TypeError:
    t = "z" + t[1:]
assert t == "zbc"
name, ver = "fc", 3
assert f"{name}-v{ver:02d} {0.5:.1%} {255:#04x}" == "fc-v03 50.0% 0xff"
```

- 문자열은 **immutable** — `s[0] = 'x'` 불가. 바꾸려면 새로 만들거나 `list(s)`로 바꿔서 수정 후 `"".join`
- `str`(문자) vs `bytes`(바이트) 구분이 C보다 엄격하다 → 시리얼 데이터는 `bytes` (9절)
- f-string 포맷: `{x:02d}` 0 채움, `{x:.1f}` 소수, `{x:#04x}` hex, `{x:>8}` 정렬

## 7. 반복문 · comprehension

```python
xs = ["a", "b", "c"]
out = []
for i, x in enumerate(xs):                 # 인덱스와 값 함께 (range(len(xs)) 대신)
    out.append(f"{i}{x}")
assert out == ["0a", "1b", "2c"]
assert list(zip([1, 2, 3], "xy")) == [(1, "x"), (2, "y")]   # 짧은 쪽에서 멈춘다
assert list(range(5)) == [0, 1, 2, 3, 4]
assert list(range(2, 10, 3)) == [2, 5, 8]
assert list(range(4, 0, -1)) == [4, 3, 2, 1]                 # 거꾸로
assert list(reversed(xs)) == ["c", "b", "a"]

i, j = 0, len(xs) - 1                      # 투 포인터 while
while i < j:
    i, j = i + 1, j - 1
assert (i, j) == (1, 1)

evens = [x for x in range(10) if x % 2 == 0]     # list comprehension
assert evens == [0, 2, 4, 6, 8]
total = sum(x * x for x in range(4))             # generator — 리스트를 만들지 않는다
assert total == 14
assert any(x > 8 for x in evens) is False and all(x % 2 == 0 for x in evens)
```

| C | Python |
|---|---|
| `for (i = 0; i < n; i++) use(a[i]);` | `for x in a:` |
| `for (i = 0; i < n; i++) use(i, a[i]);` | `for i, x in enumerate(a):` |
| 두 배열 동시에 | `for x, y in zip(a, b):` |
| `for (i = n - 1; i >= 0; i--)` | `for i in range(n - 1, -1, -1):` 또는 `reversed(a)` |
| 결과 배열을 채우는 루프 | `[f(x) for x in a if cond(x)]` |

## 8. 숫자와 비트 — C와 다른 곳이 함정

```python
assert 7 / 2 == 3.5            # / 는 항상 float
assert 7 // 2 == 3             # // 는 내림(floor) 나눗셈
assert -7 // 2 == -4           # ⚠ C 의 -7 / 2 == -3 (0 쪽으로 버림) 과 다르다
assert -7 % 2 == 1             # ⚠ C 의 -7 % 2 == -1. Python 의 % 는 나누는 수의 부호를 따른다
assert int(-7 / 2) == -3       # C 처럼 0 쪽으로 버리려면 int(a / b)
assert divmod(17, 5) == (3, 2)
assert 2 ** 100 > 10 ** 30     # int 는 크기 제한이 없다 → 오버플로 없음
assert (0xFFFFFFFF + 1) & 0xFFFFFFFF == 0       # 32비트처럼 쓰려면 직접 마스크
assert float("inf") > 10 ** 100 and -float("inf") < -1
assert round(2.675, 2) == 2.67                 # float 표현 오차 (2.675 는 정확히 표현 안 됨)
assert abs(-3) == 3 and max(3, 7, 5) == 7
```

```python
x = 0b1011_0000
assert x >> 4 == 0b1011 and (1 << 3) == 8
assert x & 0xF0 == 0xB0 and x | 1 == 0xB1 and x ^ 0xFF == 0x4F
assert bin(5) == "0b101" and hex(255) == "0xff" and int("ff", 16) == 255
assert ~5 == -6                # ⚠ 무한 정밀도 2의 보수: ~x == -x - 1 (C 의 ~5u == 0xFFFFFFFA 와 다름)
assert ~5 & 0xFF == 0xFA       # 8비트로 보려면 마스크
assert (12).bit_length() == 4 and bin(12).count("1") == 2
n = 12
n &= n - 1                     # 가장 낮은 1 비트 지우기
assert n == 8
```

| 연산 | C (int32) | Python | 왜 |
|---|---|---|---|
| `7 / 2` | `3` | **`3.5`** | `/`는 항상 실수 나눗셈 → 정수는 `//` |
| `-7 / 2` (정수) | `-3` (0 쪽으로) | `-7 // 2` = **`-4`** (내림) | floor division |
| `-7 % 2` | `-1` | **`1`** | 나머지가 나누는 수의 부호를 따른다 |
| `INT_MAX + 1` | 오버플로 (UB) | 그냥 큰 수 | int 크기 제한 없음 → 32비트는 `& 0xFFFFFFFF` |
| `~5` | `0xFFFFFFFA` (unsigned) | **`-6`** | `~x == -x - 1` → 비트 폭이 필요하면 마스크 |
| `x >> 1` (음수) | 구현 정의 | 산술 시프트 (부호 유지) | |

- 이진 탐색 mid: Python은 `(lo + hi) // 2`로 충분 (오버플로 없음). C에선 `lo + (hi - lo) / 2`
- 비트 문제(LC 191 · 190)는 Python에서 **32비트를 의식**해야 한다 — 루프를 32번으로 고정하거나 마스크

## 9. 함수 — 기본값 함정 · *args · nonlocal · lambda

```python
def add_item(item, bucket=None):     # ⚠ bucket=[] 로 쓰면 모든 호출이 같은 리스트를 공유한다
    if bucket is None:
        bucket = []
    bucket.append(item)
    return bucket


assert add_item(1) == [1] and add_item(2) == [2]


def stats(*values, scale=1):         # *args = 위치 인자 묶음, 키워드 전용 인자
    return min(values) * scale, max(values) * scale


assert stats(3, 1, 2) == (1, 3) and stats(3, 1, scale=10) == (10, 30)


def make_counter():
    count = 0

    def inc():
        nonlocal count               # 바깥 함수의 변수를 바꾸려면 nonlocal
        count += 1
        return count
    return inc


inc = make_counter()
assert inc() == 1 and inc() == 2
key_fn = lambda pair: pair[1]        # 한 줄 함수 (sort key 에 주로)
assert sorted([("a", 2), ("b", 1)], key=key_fn)[0] == ("b", 1)
```

- **mutable 기본값 함정**: `def f(x, acc=[])`의 `[]`는 함수가 정의될 때 **한 번** 만들어져 모든 호출이 공유한다 → `None`을 기본값으로 두고 안에서 만든다
- 안쪽 함수에서 바깥 변수에 **대입**하려면 `nonlocal` (읽기만 하면 필요 없음)
- 함수는 값이다: `sorted(key=len)`, 콜백, 의존성 주입(`sleep=time.sleep`) — C 함수 포인터와 같은 역할

## 10. 클래스 — 설계 문제(622 · 146 · 155 · 359)의 뼈대

```python
class Ring:
    def __init__(self, capacity):    # 생성자. self = C 의 'this 포인터'를 명시적으로 받는 것
        self.capacity = capacity     # 인스턴스 변수는 self.이름 으로 만든다
        self._items = []             # 앞에 _ = '내부용' 관례 (접근 제한은 없다)

    def push(self, x):
        if len(self._items) == self.capacity:
            raise OverflowError("full")
        self._items.append(x)

    def __len__(self):               # len(r)
        return len(self._items)

    def __iter__(self):              # for x in r
        return iter(self._items)

    def __repr__(self):              # print(r), 디버거 표시
        return f"Ring({self._items}, cap={self.capacity})"

    @property
    def full(self):                  # r.full  (괄호 없이 읽는 계산 속성)
        return len(self) == self.capacity


r = Ring(2)
r.push("a")
r.push("b")
assert len(r) == 2 and list(r) == ["a", "b"] and r.full and repr(r) == "Ring(['a', 'b'], cap=2)"
try:
    r.push("c")
except OverflowError as e:
    assert str(e) == "full"

from dataclasses import dataclass, field


@dataclass
class Frame:                         # __init__, __repr__, __eq__ 자동 생성
    msg_id: int
    payload: bytes = b""
    tags: list = field(default_factory=list)    # mutable 기본값은 default_factory 로


assert Frame(1) == Frame(1, b"") and Frame(2).tags == []
```

| C | Python |
|---|---|
| `typedef struct { ... } ring_t;` + `ring_init(&r, cap)` | `class Ring:` + `def __init__(self, capacity):` |
| `ring_push(&r, x)` | `r.push(x)` — 첫 인자 `self`가 `&r` 역할 |
| `r.capacity` 필드 | `self.capacity` — `__init__`에서 처음 대입할 때 생긴다 |
| `static` 내부 함수 | `_이름` (관례일 뿐, 막지는 않는다) |
| getter 함수 | `@property` |
| 구조체 + 비교 · 출력 함수 | `@dataclass` |

- LeetCode 설계 문제는 **주어진 메서드 이름을 그대로** 써야 한다 (`enQueue`, `getMin`, `shouldPrintMessage`). 면접에서 직접 설계하라면 Python 관례(`snake_case`)로

## 11. 예외

```python
class DeviceError(Exception):
    """모든 장치 오류의 부모 — 호출자는 이것 하나로 잡을 수 있다."""


class DeviceTimeout(DeviceError):
    pass


def read_or_default(fn, default):
    try:
        return fn()
    except DeviceTimeout:            # 좁은 것부터 잡는다
        return default
    finally:
        pass                         # 예외가 나도 반드시 실행 (자원 정리)


def boom():
    raise DeviceTimeout("no reply")


assert read_or_default(boom, -1) == -1
try:
    int("abc")
except ValueError as e:
    assert "invalid literal" in str(e)
```

- C의 에러 코드 반환(`return -1`) 대신 **예외를 raise** — 호출자가 무시할 수 없다
- 예외 계층을 만들면 "장치 오류는 전부" 한 번에 잡거나 "타임아웃만" 골라 잡을 수 있다
- `except Exception: pass`로 전부 삼키지 말 것 — 진짜 버그가 숨는다

## 12. bytes — 시리얼 · 프로토콜 데이터

```python
import struct

raw = bytes([0xAA, 0x02, 0x10, 0x00])
assert raw[0] == 0xAA                         # 인덱스 하나 = int
assert raw[1:3] == b"\x02\x10"                # 슬라이스 = bytes
assert raw.hex() == "aa021000" and bytes.fromhex("aa02") == b"\xaa\x02"
assert int.from_bytes(raw[2:4], "little") == 16
sync, kind, length = struct.unpack("<BBH", raw)   # < little-endian, B u8, H u16
assert (sync, kind, length) == (0xAA, 2, 16)
buf = bytearray(4)                            # 바꿀 수 있는 bytes
buf[0:2] = b"\x01\x02"
assert buf == bytearray(b"\x01\x02\x00\x00")
assert "온도".encode() == b"\xec\x98\xa8\xeb\x8f\x84" and b"OK\r\n".decode().strip() == "OK"
```

- 더 많은 연습: [드릴 Day 3](../drills/2026-10-03_D3_bytes_bits.md) (struct · 비트 필드 · CRC)

## 13. 복잡도 치트시트 (CPython)

| 자료구조 | O(1) | O(n) | 기타 |
|---|---|---|---|
| `list` | `a[i]`, `append`, `pop()`, `len` | `pop(0)`, `insert(0,x)`, `x in a`, `a.index(x)`, 슬라이스 복사 | `sort` O(n log n) |
| `deque` | `append`, `appendleft`, `pop`, `popleft` | `q[i]` (가운데), `x in q` | |
| `dict` / `set` | `d[k]`, `k in d`, 넣기, 지우기 (평균) | 순회 | 최악 O(n) (해시 충돌) |
| `heapq` | `h[0]` | `heapify` O(n) | `heappush` / `heappop` O(log n) |
| `str` | `s[i]`, `len` | `+` 연결, `in`, `split`, `join` | 반복 `+=`는 O(n²)이 될 수 있다 |

## 14. L00 문제별로 쓰이는 문법

| 문제 | 필요한 문법 |
|---|---|
| Ring buffer · LC 622 | `class`, `__init__`, `[None] * k`, `%`, `@property`, `raise IndexError` |
| LC 346 Moving Average | `deque`, `append` / `popleft`, 합 유지 |
| LC 359 Logger | `dict.get(k, 0)` |
| LC 146 LRU | `OrderedDict`, `move_to_end`, `popitem(last=False)` |
| LC 1 Two Sum | `enumerate`, `dict`, `in` |
| LC 20 Parentheses | `list` 스택, `dict` 짝 매핑 |
| LC 56 Merge Intervals | `sorted`, 언패킹 `for s, e in ...`, `out[-1][1]`, `max` |
| LC 3 Longest Substring | `enumerate`, `dict`, 투 포인터 |
| LC 347 Top K | `Counter.most_common`, `heapq.nlargest(k, d, key=d.get)` |
| LC 49 Group Anagrams | `defaultdict(list)`, `"".join(sorted(s))` |
| LC 704 Binary Search | `while lo <= hi`, `//` |
| LC 191 · 190 Bits | `&`, `>>`, `<<`, `n & (n - 1)`, 32비트 고정 루프 |
| LC 21 Merge Lists | 노드 클래스, dummy head, `a or b` |
| LC 155 Min Stack | 튜플 `(값, 최솟값)` 스택 |
| LC 125 Palindrome | `isalnum`, `lower`, 투 포인터 |
| pytest (L00 Part 2) | `def test_`, `assert`, 데코레이터 `@pytest.fixture`, `with pytest.raises` |

## 체크

- [ ] 0절 표를 가리고 "큐 · 스택 · 스레드 큐 · 우선순위 큐"를 Python으로 바로 쓰기
- [ ] [playground](../playground/README.md)에 빈 파일을 만들고 1~5절 예제를 **보지 않고** 다시 써서 `make py`
- [ ] 8절 표의 함정 4개(`/`, `//` 음수, `%` 음수, `~`)를 C와 비교해서 말하기
- [ ] 9절 mutable 기본값 버그를 직접 재현해 보기
- [ ] [드릴](../site/drills.html) Day 1 · 2를 이 문서를 본 뒤에 다시
