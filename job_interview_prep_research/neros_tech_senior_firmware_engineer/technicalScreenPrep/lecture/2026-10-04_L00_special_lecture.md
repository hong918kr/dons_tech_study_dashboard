# 🎓 특별강의 · 10-08 스크린에 가장 나올 것만 — Ring buffer · pytest · Python LeetCode 15

> 리크루터가 짚은 주제(**pytest · ring buffer · Python/C**)를 맨 위에 두고, 그다음 1시간 코딩 스크린에서 흔히 쓰이는 **기본 Python LeetCode 15문제**를 빈도 순으로 모았다. 빈도는 리크루터 주제와 폰스크린에서 일반적으로 쓰이는 문제 유형으로 매긴 **추정**이다(공개된 Neros 출제 목록은 없다). 이 페이지의 코드는 전부 `lecture/code/`의 실제 파일에서 그대로 가져왔고 **pytest 37개로 통과**한 것이다 — `python3 lecture/build_lecture.py --test`.

## 0. 이 강의 쓰는 법

| 순서 | 무엇 | 시간 |
|---|---|---|
| 1 | Part 1 ring buffer — 개념 → 코드 → 손 시뮬레이션 → 꼬리 질문 | 40분 |
| 2 | Part 2 pytest — 6개 도구 → 완성 예제 → mock → 면접 대사 | 40분 |
| 3 | Part 3 LeetCode — **Tier S 4개** 먼저, 그다음 A, B | 문제당 10~15분 |
| 4 | 각 문제를 [playground](../playground/README.md)의 **빈 파일**에서 다시 → `make test` | 매일 |

- 빈도 표기: **Tier S** = 리크루터 주제와 직결 / **Tier A** = 1시간 스크린 단골 / **Tier B** = 워밍업 · 꼬리 질문용 [추정]
- 각 문제의 "말하면서" 한 줄은 그대로 영어로 말할 문장이다

---

## Part 1 · Ring buffer — Python으로 개념부터

### 1.1 무엇이고 왜 펌웨어에서 늘 나오나

- **고정 크기 배열을 원형으로 쓰는 FIFO.** 끝에 닿으면 처음으로 돌아간다. 할당이 없고(한 번만), 넣기 · 빼기가 O(1)
- 펌웨어에서: UART RX 바이트(ISR이 넣고 main이 뺀다), 로그 · 블랙박스(최근 N개만 보관), 센서 이동 평균, 텔레메트리 큐
- 면접관이 보는 것: **경계 조건**(full vs empty), **정책**(가득 차면?), **동시성**(누가 넣고 누가 빼나), 그리고 **스스로 테스트하나**

```text
capacity = 4,  head = 다음에 쓸 칸,  count = 들어 있는 개수,  tail = (head - count) % capacity

 index:    0    1    2    3
         [ C ][ D ][ A ][ B ]       push A,B,C,D 를 index 2 부터 썼다면
                     ↑ tail=2       가장 오래된 것 = A
           ↑ head=0 (다음 쓸 칸)    count = 4 → full
 pop() → A,  tail 은 3 으로, count = 3
 push(E) → index 0 에 E, head = 1, count = 4
```

### 1.2 full과 empty를 어떻게 구분하나 — 세 가지 방법

| 방법 | 상태 | full / empty | 장단점 | 어디서 쓰나 |
|---|---|---|---|---|
| **count 유지** | head, count | `count == cap` / `count == 0` | 가장 읽기 쉽다. 동시성에선 count를 양쪽이 건드림 | **Python 면접 답** |
| 한 칸 비우기 | head, tail | `(head+1)%cap == tail` / `head == tail` | count 없음. 대신 용량 하나를 못 씀 | 오래된 C 코드 |
| free-running index | head, tail (마스크 없이 증가) | `head - tail == cap` / `head == tail` | 전부 사용 + SPSC lock-free 가능. **2의 거듭제곱 크기 + unsigned wrap** 필요 | **C / MCU 답** ([onsitePrep C 코드](../../onsitePrep/site/code/ring_buffer.c.html)) |

> "In Python I track a count, because integers don't overflow and full versus empty becomes trivial. In C I'd use free-running unsigned indices with a power-of-two mask — same invariant: zero ≤ count ≤ capacity."

### 1.3 코드 — 면접에서 15분 안에 쓸 버전

```python
class RingBuffer:
    """고정 용량 FIFO. 가득 차면 overwrite=False → push 가 False (drop-new),
    overwrite=True → 가장 오래된 것을 버리고 dropped += 1."""

    def __init__(self, capacity, overwrite=False):
        if not isinstance(capacity, int) or capacity <= 0:
            raise ValueError("capacity must be a positive int")
        self.capacity = capacity
        self.overwrite = overwrite
        self._buf = [None] * capacity   # 미리 한 번만 할당 (C 의 static 배열처럼)
        self._head = 0                  # 다음에 쓸 칸
        self._count = 0                 # 들어 있는 개수 → full/empty 구분
        self.dropped = 0                # 덮어써서 잃은 개수 (손실을 보이게)

    def __len__(self):
        return self._count

    def empty(self):
        return self._count == 0

    def full(self):
        return self._count == self.capacity

    def _tail(self):                    # 가장 오래된 칸 = head 에서 count 만큼 뒤
        return (self._head - self._count) % self.capacity

    def push(self, item):
        if self.full():
            if not self.overwrite:
                return False            # drop-new: 호출자가 알 수 있게 False
            self._count -= 1            # 가장 오래된 것 하나를 버린 셈
            self.dropped += 1
        self._buf[self._head] = item
        self._head = (self._head + 1) % self.capacity
        self._count += 1
        return True

    def pop(self):
        if self.empty():
            raise IndexError("pop from empty RingBuffer")
        t = self._tail()
        item, self._buf[t] = self._buf[t], None   # 참조를 놓아 GC 가능하게
        self._count -= 1
        return item

    def peek(self):
        if self.empty():
            raise IndexError("peek from empty RingBuffer")
        return self._buf[self._tail()]

    def __iter__(self):                 # 오래된 → 최신, 소비하지 않음
        t = self._tail()
        for i in range(self._count):
            yield self._buf[(t + i) % self.capacity]
```

| 결정 | 이유 (말할 것) |
|---|---|
| `[None] * capacity` 미리 할당 | "No allocation after construction — same property I'd want on an MCU." |
| `count`로 상태 표현, `tail`은 계산 | "Full versus empty is unambiguous." |
| 정책을 플래그로, `dropped` 카운터 | "Loss should be visible, not silent." |
| 빈 버퍼에서 `IndexError` | "Raising is more Pythonic than returning None, and None could be a valid item." |
| pop 후 `None` 대입 | "Release the reference so a big object can be garbage-collected." |

### 1.4 손으로 돌려 보기 (capacity 3) — 화이트보드에서 꼭 할 것

| 동작 | buf | head | count | tail | 반환 |
|---|---|---|---|---|---|
| 생성 | [_, _, _] | 0 | 0 | 0 | |
| push 1 | [1, _, _] | 1 | 1 | 0 | True |
| push 2 | [1, 2, _] | 2 | 2 | 0 | True |
| push 3 | [1, 2, 3] | 0 | 3 | 0 | True (full) |
| push 4 | [1, 2, 3] | 0 | 3 | 0 | **False** (drop-new) |
| pop | [_, 2, 3] | 0 | 2 | 1 | 1 |
| push 4 | [4, 2, 3] | 1 | 3 | 1 | True — **배열 순서와 논리 순서가 다르다** |
| list(rb) | | | | | [2, 3, 4] |

### 1.5 변형 — 꼬리 질문으로 나오는 것

**(a) 실무 답: `collections.deque(maxlen=n)`** — overwrite-oldest가 그대로 내장. "In production I'd use deque; I wrote it out to show the mechanics and to support drop-new with a counter."

**(b) 바이트 링 (UART RX, bulk read/write)** — wrap 지점에서 두 조각. C의 `memcpy` 두 번과 같은 생각:

```python
class ByteRing:
    """UART RX 용 바이트 링. write/read 는 wrap 지점에서 최대 두 조각으로 나뉜다 (C 의 memcpy 두 번)."""

    def __init__(self, capacity):
        self._buf = bytearray(capacity)
        self._cap = capacity
        self._head = self._count = 0

    def __len__(self):
        return self._count

    def write(self, data):
        n = min(len(data), self._cap - self._count)        # 들어갈 만큼만 (drop-new)
        first = min(n, self._cap - self._head)               # 배열 끝까지 연속 공간
        self._buf[self._head:self._head + first] = data[:first]
        self._buf[0:n - first] = data[first:n]               # 나머지는 앞에서부터
        self._head = (self._head + n) % self._cap
        self._count += n
        return n

    def read(self, size):
        n = min(size, self._count)
        tail = (self._head - self._count) % self._cap
        first = min(n, self._cap - tail)
        out = bytes(self._buf[tail:tail + first]) + bytes(self._buf[0:n - first])
        self._count -= n
        return out
```

**(c) 스레드 안전** — Python 스레드는 GIL 아래에서도 바이트코드 사이에서 전환되므로 `count += 1`이 깨진다 → `Lock` + `Condition`(not_empty / not_full), 실무는 `queue.Queue`. 자세히: [문제 05](../python/problems/05_blocking_ring.md) · [N06 Python 스레드 vs C](../notes/2026-10-03_N06_python_threads_vs_c.md)

**(d) UART 조각 → 프레임** — 링 위에서 sync · length · CRC로 프레임 뽑기: [문제 06](../python/problems/06_stream_framer.md)

### 1.6 꼬리 질문 TOP 6 (영어 한 줄 답)

| 질문 | 답 |
|---|---|
| Complexity? | "O(1) push, pop and peek; O(n) iteration; memory fixed at capacity." |
| Why not a list with `pop(0)`? | "`pop(0)` shifts every element — O(n). The ring is O(1)." |
| Is it thread-safe? | "No — `count += 1` is several bytecodes. Lock plus two conditions, or `queue.Queue`." |
| Drop-new or overwrite? | "Commands: drop and count, never lose silently. Logs and telemetry: overwrite oldest — latest state matters." |
| How would you test it? | "Empty, one, exactly full, one past full, many wraparounds, both policies, then a random test against deque." (→ Part 2) |
| C version? | "Static array, power-of-two size, free-running unsigned head/tail, mask on access; for ISR producer, release/acquire on the indices." |

---

## Part 2 · pytest — 개념부터

### 2.1 pytest가 하는 일 (30초)

- `test_`로 시작하는 함수를 찾아 실행한다(discovery). 클래스 · 상속 · `self.assertEqual` 없이 **그냥 `assert`**
- `assert` 실패 시 양쪽 값을 펼쳐 보여 준다(assert rewriting) → 실패 메시지를 따로 안 써도 된다
- 준비물은 **fixture**로 이름만 적어 받는다. 같은 테스트를 여러 입력으로는 **parametrize**
- 실행: `pytest -q` · `-x`(첫 실패에서 멈춤) · `-k 이름` · `-v` · `--lf`(지난번 실패만)

### 2.2 꼭 아는 6개

| 도구 | 한 줄 | 문법 |
|---|---|---|
| `assert` | 그냥 파이썬 assert | `assert rb.pop() == 1` |
| `pytest.raises` | 예외가 나야 통과 | `with pytest.raises(IndexError, match="empty"):` |
| `@pytest.mark.parametrize` | 입력만 바꿔 여러 번, 각각 따로 pass/fail | `@pytest.mark.parametrize("cap", [1, 2, 5])` |
| `@pytest.fixture` | 준비 + (yield 뒤) 정리, 이름으로 요청, scope 선택 | `def rb(): return RingBuffer(3)` |
| `unittest.mock.Mock` | 하드웨어 · 시간 · 네트워크를 가짜로, 호출 기록 검사 | `port.readline.side_effect = [b"", b"OK"]` |
| 내장 fixture | `tmp_path` 임시 폴더, `monkeypatch` 환경 교체, `capsys` 출력 | `def test_x(tmp_path):` |

```python
@pytest.fixture
def port():
    p = open_port()      # 준비
    yield p              # 테스트가 여기서 실행된다
    p.close()            # 정리 — 테스트가 실패해도 실행

@pytest.fixture(scope="session")   # 비싼 것(리그 연결)은 세션당 한 번
def rig(): ...
```

### 2.3 완성 예제 — Part 1의 RingBuffer를 테스트하기

fixture 2개(빈 버퍼 → 가득 찬 버퍼), parametrize 3곳, raises, 그리고 **deque를 정답 모델로 둔 무작위 테스트**까지:

```python
import random
from collections import deque

import pytest

from ring_buffer import ByteRing, RingBuffer


@pytest.fixture
def rb():                                   # 테스트마다 새 버퍼
    return RingBuffer(3)


@pytest.fixture
def full_rb(rb):                            # fixture 가 fixture 를 받는다
    for x in (1, 2, 3):
        rb.push(x)
    return rb


def test_new_is_empty(rb):
    assert len(rb) == 0 and rb.empty() and not rb.full()


def test_fifo_order(rb):
    for x in "abc":
        rb.push(x)
    assert [rb.pop() for _ in range(3)] == ["a", "b", "c"]


def test_all_slots_usable(full_rb):
    assert full_rb.full() and len(full_rb) == 3


def test_drop_new_when_full(full_rb):
    assert full_rb.push(4) is False
    assert list(full_rb) == [1, 2, 3]


def test_overwrite_drops_oldest():
    r = RingBuffer(3, overwrite=True)
    for x in range(5):
        r.push(x)
    assert list(r) == [2, 3, 4] and r.dropped == 2


@pytest.mark.parametrize("method", ["pop", "peek"])
def test_empty_access_raises(rb, method):
    with pytest.raises(IndexError, match="empty"):
        getattr(rb, method)()


def test_wraparound_order(full_rb):
    full_rb.pop()
    full_rb.push(4)                         # 배열은 [4, 2, 3], 논리 순서는 2, 3, 4
    assert list(full_rb) == [2, 3, 4]


@pytest.mark.parametrize("bad", [0, -1, 2.5, None])
def test_bad_capacity(bad):
    with pytest.raises(ValueError):
        RingBuffer(bad)


@pytest.mark.parametrize("capacity", [1, 2, 5])
def test_matches_deque_model(capacity):
    rnd, r, model = random.Random(42), RingBuffer(capacity), deque()
    for step in range(500):
        if rnd.random() < 0.6:
            ok = r.push(step)
            assert ok == (len(model) < capacity)
            if ok:
                model.append(step)
        elif model:
            assert r.pop() == model.popleft()
        assert list(r) == list(model), f"step {step}"
```

| 테스트 | 무엇을 잡나 |
|---|---|
| `test_all_slots_usable` | "한 칸 비우기"와 섞인 off-by-one |
| `test_drop_new_when_full` | 가득 찼는데 True 반환 · 기존 값 덮음 |
| `test_overwrite_drops_oldest` | 최신을 덮는 버그 · `dropped` 누락 |
| `test_wraparound_order` | 배열 순서로 iterate 하는 버그 |
| `test_matches_deque_model` | 손으로 못 찾은 경계 (seed 고정 → 재현 가능) |

### 2.4 mock — 하드웨어 없이 드라이버 테스트

피검 코드 (재시도 + 백오프 있는 센서 읽기):

```python
import time


class SensorTimeout(Exception):
    pass


def read_temperature(port, retries=2, sleep=time.sleep):
    """b"TEMP?\\n" 를 보내고 b"TEMP=23.5\\r\\n" 을 받는다. 빈 응답(타임아웃)이면 0.1, 0.2 초 쉬고 재시도."""
    for attempt in range(retries + 1):
        if attempt:
            sleep(0.1 * 2 ** (attempt - 1))
        port.write(b"TEMP?\n")
        line = port.readline()
        if line:
            return float(line.strip().split(b"=")[1])
    raise SensorTimeout("no response")
```

테스트 (포트와 sleep을 가짜로 바꿔서 0초에 끝나고, 나간 바이트 · 재시도 횟수 · 백오프 값까지 검사):

```python
from unittest.mock import Mock, call

import pytest

from sensor_demo import SensorTimeout, read_temperature


@pytest.fixture
def port():
    return Mock(spec=["write", "readline"])   # 하드웨어 대신: 호출을 기록하는 가짜 포트


def test_reads_value(port):
    port.readline.return_value = b"TEMP=23.5\r\n"
    assert read_temperature(port, sleep=Mock()) == 23.5
    port.write.assert_called_once_with(b"TEMP?\n")       # 선로에 나간 바이트까지 검사


def test_retries_then_succeeds(port):
    port.readline.side_effect = [b"", b"", b"TEMP=21.0\r\n"]   # 호출마다 다음 응답
    sleep = Mock()
    assert read_temperature(port, sleep=sleep) == 21.0
    assert port.write.call_count == 3
    assert sleep.call_args_list == [call(0.1), call(0.2)]       # 백오프 값까지


def test_gives_up(port):
    port.readline.return_value = b""
    with pytest.raises(SensorTimeout):
        read_temperature(port, sleep=Mock())
```

- 원칙: **경계(포트 · 시계 · 네트워크)만 가짜로.** 내부 메서드를 mock 하면 리팩터링마다 깨진다
- `sleep`을 인자로 받은 것(의존성 주입)이 핵심 — 아니면 `patch("time.sleep")`이 필요하고, 패치 경로 함정이 생긴다
- 더 깊게: [문제 03](../python/problems/03_mock_serial_driver.md) (mutant 9개) · [N02 pytest 실전](../notes/2026-10-02_N02_pytest_in_practice.md)

### 2.5 "Write tests for this" — 면접 진행 순서

1. 스펙을 소리 내 요약 → 2. **테스트 이름부터** 6~8개 → 3. fixture로 상태 준비 → 4. 경계부터 채우기 → 5. 비슷한 것 3개면 parametrize → 6. 안 한 것(동시성 · 성능 · 실제 HW)을 말로

> 경계 체크리스트: **0 · 1 · N · N+1 · 경계 ± 1 · 잘못된 입력 · 예외 경로 · 정리(cleanup) · 순서**

### 2.6 pytest 질문 TOP 5 (영어 한 줄 답)

| 질문 | 답 |
|---|---|
| Fixture vs setUp? | "Requested by name, composable, scoped, and teardown sits right after yield." |
| Fixture scope? | "Expensive and stateless — session. Anything with state — function, or reset it per test." |
| parametrize vs a loop? | "Each case passes or fails on its own and shows up by name in the report." |
| When do you mock? | "At the boundaries — port, time, network. And keep one real-hardware test so mocks can't drift." |
| Flaky tests? | "Measure flakiness per test, find the cause, quarantine with a ticket — no blind reruns." |

---

## Part 3 · Python LeetCode 15 — 빈도 순

### Tier 표

| Tier | # | 문제 | 패턴 | 왜 이 스크린에 [추정] |
|---|---|---|---|---|
| **S** | 622 | Design Circular Queue | ring buffer | 리크루터 주제 그 자체 |
| **S** | 346 | Moving Average from Data Stream | deque · 슬라이딩 합 | 센서 스트림, ring buffer 변형 |
| **S** | 359 | Logger Rate Limiter | dict · 타임스탬프 | 로그 · 텔레메트리 스로틀 |
| **S** | 146 | LRU Cache | OrderedDict · 설계 | "클래스 설계" 대표, Full Stack 단골 |
| A | 1 | Two Sum | dict | 가장 흔한 워밍업 |
| A | 20 | Valid Parentheses | stack | 파서 · 프로토콜 감각 |
| A | 56 | Merge Intervals | 정렬 | 로그 시간 구간 합치기 |
| A | 3 | Longest Substring Without Repeating | 슬라이딩 윈도우 | 스트림 처리 패턴 |
| A | 347 | Top K Frequent Elements | Counter · heap | "가장 많이 난 에러 K개" |
| A | 49 | Group Anagrams | dict + 키 설계 | dict 활용 |
| B | 704 | Binary Search | 이진 탐색 | 경계 조건 |
| B | 191 · 190 | Number of 1 Bits · Reverse Bits | 비트 연산 | 펌웨어 면접 단골 |
| B | 21 | Merge Two Sorted Lists | 연결 리스트 | 포인터 감각 (C 출신 강점) |
| B | 155 | Min Stack | 보조 상태 | 설계 워밍업 |
| B | 125 | Valid Palindrome | 투 포인터 | 문자열 워밍업 |

- 346 · 359는 LeetCode Premium 문제다. 문제 자체는 아래 설명으로 충분하다
- 모든 풀이: 시간 · 공간 복잡도를 **먼저** 말하고, 끝나면 테스트 케이스 2~3개를 손으로 돌린다

### ★ S1 · LC 622 Design Circular Queue (Medium)

> `MyCircularQueue(k)`: `enQueue(v)`/`deQueue()` → bool, `Front()`/`Rear()` → 값 또는 -1, `isEmpty()`, `isFull()`.

```python
class MyCircularQueue:
    def __init__(self, k: int):
        self.q = [0] * k
        self.k = k
        self.head = 0          # 가장 오래된 칸 (Front)
        self.count = 0

    def enQueue(self, value: int) -> bool:
        if self.isFull():
            return False
        self.q[(self.head + self.count) % self.k] = value
        self.count += 1
        return True

    def deQueue(self) -> bool:
        if self.isEmpty():
            return False
        self.head = (self.head + 1) % self.k
        self.count -= 1
        return True

    def Front(self) -> int:
        return -1 if self.isEmpty() else self.q[self.head]

    def Rear(self) -> int:
        return -1 if self.isEmpty() else self.q[(self.head + self.count - 1) % self.k]

    def isEmpty(self) -> bool:
        return self.count == 0

    def isFull(self) -> bool:
        return self.count == self.k
```

- 복잡도: 모든 연산 O(1), 공간 O(k)
- 말하면서: "Head points at the front, count separates full from empty, and the rear is head plus count minus one, modulo k."
- 함정: `Rear()`를 `head - 1`로 계산(빈 큐 · wrap에서 틀림). Part 1의 RingBuffer와 같은 구조라는 걸 **말로 연결**하기

### ★ S2 · LC 346 Moving Average from Data Stream (Easy)

> `MovingAverage(size)`, `next(val)` → 최근 size개의 평균.

```python
from collections import deque


class MovingAverage:
    def __init__(self, size: int):
        self.window = deque()
        self.size = size
        self.total = 0

    def next(self, val: int) -> float:
        self.window.append(val)
        self.total += val
        if len(self.window) > self.size:
            self.total -= self.window.popleft()   # 합을 O(1)로 갱신
        return self.total / len(self.window)
```

- 복잡도: `next` O(1), 공간 O(size). 매번 `sum(window)` 하면 O(size) — **합을 유지**하는 게 포인트
- 말하면서: "I keep a running total, so each update is constant time — the same trick as a sensor low-pass filter on an MCU."
- 꼬리: 정수만 쓰는 MCU라면? → 크기를 2의 거듭제곱으로 하고 나눗셈 대신 shift, 오버플로 대비해 합은 넓은 타입

### ★ S3 · LC 359 Logger Rate Limiter (Easy)

> `shouldPrintMessage(timestamp, message)`: 같은 메시지는 10초에 한 번만 True. timestamp는 증가 순.

```python
class Logger:
    def __init__(self):
        self.next_ok = {}                  # message → 다시 찍어도 되는 가장 이른 시각

    def shouldPrintMessage(self, timestamp: int, message: str) -> bool:
        if timestamp < self.next_ok.get(message, 0):
            return False
        self.next_ok[message] = timestamp + 10
        return True
```

- 복잡도: O(1), 공간 O(메시지 종류)
- 말하면서: "For each message I store the earliest time it may print again."
- 꼬리: 메모리가 무한히 커지면? → 오래된 키를 주기적으로 정리하거나, 최근 10초만 deque + set으로 유지

### ★ S4 · LC 146 LRU Cache (Medium)

> `get(key)` → 값 또는 -1, `put(key, value)`. 용량을 넘으면 가장 오래 안 쓴 것을 제거. 둘 다 O(1).

```python
from collections import OrderedDict


class LRUCache:
    def __init__(self, capacity: int):
        self.cap = capacity
        self.d = OrderedDict()             # 순서 = 최근 사용 순 (끝이 가장 최근)

    def get(self, key: int) -> int:
        if key not in self.d:
            return -1
        self.d.move_to_end(key)
        return self.d[key]

    def put(self, key: int, value: int) -> None:
        if key in self.d:
            self.d.move_to_end(key)
        self.d[key] = value
        if len(self.d) > self.cap:
            self.d.popitem(last=False)     # 가장 오래 안 쓴 것 제거
```

- 복잡도: O(1) (OrderedDict = 해시 + 이중 연결 리스트)
- 말하면서: "I need O(1) lookup and O(1) recency updates, so a hash map plus a doubly linked list — which is exactly what OrderedDict is."
- 꼬리: "OrderedDict 없이?" → dict + 직접 만든 이중 연결 리스트(dummy head/tail). C 출신이라 포인터 조작을 자신 있게 말할 것

### A1 · LC 1 Two Sum (Easy)

```python
def twoSum(nums: list[int], target: int) -> list[int]:
    seen = {}                              # 값 → 인덱스
    for i, x in enumerate(nums):
        if target - x in seen:
            return [seen[target - x], i]
        seen[x] = i
    return []
```

- O(n) 시간 · O(n) 공간. "For each number I check whether its complement is already in the map."
- 함정: 같은 원소를 두 번 쓰기 — **확인한 다음에** 넣는다

### A2 · LC 20 Valid Parentheses (Easy)

```python
def isValid(s: str) -> bool:
    pairs = {")": "(", "]": "[", "}": "{"}
    stack = []
    for ch in s:
        if ch in pairs:
            if not stack or stack.pop() != pairs[ch]:
                return False
        else:
            stack.append(ch)
    return not stack
```

- O(n). "A stack: push openers, and every closer must match the most recent opener."
- 함정: 닫는 괄호가 먼저 올 때(빈 스택에서 pop), 끝에 스택이 남을 때

### A3 · LC 56 Merge Intervals (Medium)

```python
def merge(intervals: list[list[int]]) -> list[list[int]]:
    out = []
    for start, end in sorted(intervals):
        if out and start <= out[-1][1]:
            out[-1][1] = max(out[-1][1], end)
        else:
            out.append([start, end])
    return out
```

- O(n log n) 정렬 + O(n). "Sort by start; then each interval either extends the last one or starts a new one."
- 함정: `[1,4],[2,3]`처럼 안에 포함된 경우 → `max`로 끝 갱신

### A4 · LC 3 Longest Substring Without Repeating Characters (Medium)

```python
def lengthOfLongestSubstring(s: str) -> int:
    last = {}                              # 문자 → 마지막 위치
    left = best = 0
    for right, ch in enumerate(s):
        if ch in last and last[ch] >= left:
            left = last[ch] + 1            # 중복 바로 뒤로 창을 민다
        last[ch] = right
        best = max(best, right - left + 1)
    return best
```

- O(n). "A sliding window; when I see a repeat inside the window, I move the left edge just past its last position."
- 함정: `"abba"` — 마지막 위치가 이미 창 밖이면 left를 **뒤로 돌리면 안 된다** (`last[ch] >= left` 조건)

### A5 · LC 347 Top K Frequent Elements (Medium)

```python
from collections import Counter
import heapq


def topKFrequent(nums: list[int], k: int) -> list[int]:
    counts = Counter(nums)
    return [x for x, _ in counts.most_common(k)]          # 내부적으로 heap: O(n log k)


def topKFrequent_heap(nums: list[int], k: int) -> list[int]:
    counts = Counter(nums)
    return heapq.nlargest(k, counts, key=counts.get)
```

- Counter O(n) + heap O(n log k). "Count, then a size-k heap — or bucket sort for O(n)."
- 실무 연결: "Top K error codes from a test log" 같은 질문으로 바뀌어 나올 수 있다

### A6 · LC 49 Group Anagrams (Medium)

```python
from collections import defaultdict


def groupAnagrams(strs: list[str]) -> list[list[str]]:
    groups = defaultdict(list)
    for s in strs:
        groups["".join(sorted(s))].append(s)   # 정렬한 문자열이 같으면 같은 그룹
    return list(groups.values())
```

- O(n · k log k). "The key is the sorted string; a 26-count tuple makes it O(n · k)."

### B1 · LC 704 Binary Search (Easy)

```python
def search(nums: list[int], target: int) -> int:
    lo, hi = 0, len(nums) - 1
    while lo <= hi:
        mid = (lo + hi) // 2               # Python int 는 오버플로 없음 (C 에선 lo + (hi - lo) / 2)
        if nums[mid] == target:
            return mid
        if nums[mid] < target:
            lo = mid + 1
        else:
            hi = mid - 1
    return -1
```

- O(log n). 루프 불변식을 말로: "target, if present, is always within lo..hi." 실무에선 `bisect` 모듈

### B2 · LC 191 · 190 Number of 1 Bits · Reverse Bits (Easy)

```python
def hammingWeight(n: int) -> int:          # LC 191 Number of 1 Bits
    count = 0
    while n:
        n &= n - 1                          # 가장 낮은 1 비트 하나 지우기
        count += 1
    return count


def reverseBits(n: int) -> int:            # LC 190 Reverse Bits (32비트)
    out = 0
    for _ in range(32):
        out = (out << 1) | (n & 1)
        n >>= 1
    return out
```

- `n & (n - 1)`은 가장 낮은 1비트를 지운다 → 1의 개수만큼만 반복
- Python int는 크기 제한이 없다 → C처럼 32비트로 보려면 `& 0xFFFFFFFF`를 의식할 것 (음수 입력이면 특히)

### B3 · LC 21 Merge Two Sorted Lists (Easy)

```python
class ListNode:
    def __init__(self, val=0, next=None):
        self.val = val
        self.next = next


def mergeTwoLists(a, b):
    dummy = tail = ListNode()              # dummy 노드로 head 특수 처리 제거
    while a and b:
        if a.val <= b.val:
            tail.next, a = a, a.next
        else:
            tail.next, b = b, b.next
        tail = tail.next
    tail.next = a or b
    return dummy.next
```

- O(n + m), 추가 공간 O(1). "A dummy head removes the special case for the first node."

### B4 · LC 155 Min Stack (Medium)

```python
class MinStack:
    def __init__(self):
        self.stack = []                    # (값, 그 시점까지의 최솟값)

    def push(self, val: int) -> None:
        cur_min = min(val, self.stack[-1][1]) if self.stack else val
        self.stack.append((val, cur_min))

    def pop(self) -> None:
        self.stack.pop()

    def top(self) -> int:
        return self.stack[-1][0]

    def getMin(self) -> int:
        return self.stack[-1][1]
```

- 모든 연산 O(1). "Each entry stores the minimum so far, so pop never needs a rescan."

### B5 · LC 125 Valid Palindrome (Easy)

```python
def isPalindrome(s: str) -> bool:
    i, j = 0, len(s) - 1
    while i < j:
        if not s[i].isalnum():
            i += 1
        elif not s[j].isalnum():
            j -= 1
        elif s[i].lower() != s[j].lower():
            return False
        else:
            i, j = i + 1, j - 1
    return True
```

- O(n), O(1) 공간. 한 줄 버전 `s2 = [c.lower() for c in s if c.isalnum()]; return s2 == s2[::-1]`도 말할 수 있으면 좋다 (O(n) 공간)

---

## Part 4 · 문제를 받았을 때 매번 같은 5단계

| 단계 | 할 것 | 대사 |
|---|---|---|
| 1 | 다시 말하고 입력 · 출력 · 경계 확인 | "Can the input be empty? Duplicates? Negative numbers?" |
| 2 | 단순한 방법 + 복잡도 | "Brute force is O(n²); I can get O(n) with a hash map." |
| 3 | 코딩 — 이름을 의미 있게, 작은 함수 | "I'll write it, then walk through an example." |
| 4 | 예시로 손 시뮬레이션 | "With [2, 7, 11, 15] and 9: at index 1, 9 − 7 = 2 is in the map…" |
| 5 | 테스트 · 경계 · 개선 | "Edge cases: empty, one element, no answer. Let me add asserts." |

## 체크

- [ ] Part 1 RingBuffer를 빈 파일에서 12분 안에 → [playground](../playground/README.md)에서 `make test`
- [ ] Part 2 테스트 파일을 보지 않고 fixture 2개 + parametrize + raises로 다시 쓰기
- [ ] Tier S 4개(622 · 346 · 359 · 146)를 각 15분 타이머로
- [ ] Tier A 6개를 하루 3개씩, 복잡도부터 말하며
- [ ] 이 페이지 코드 검증: `python3 lecture/build_lecture.py --test` → 37 passed
