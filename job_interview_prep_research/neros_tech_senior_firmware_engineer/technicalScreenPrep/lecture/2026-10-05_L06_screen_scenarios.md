# 🎯 L06 · 10-08 technical screen 예상 시나리오 — 1시간 코딩이라면 무엇이 나올까

> **단정할 수 없다.** Neros 출제 목록은 공개된 것이 없고, 아래 확률은 전부 **추정**이다. 근거는 세 가지뿐이다: ① 리크루터가 준 주제(**pytest · ring buffer · Python/C 둘 다 가능 · system design**) [확인됨 2026-10-02], ② 면접관이 Full Stack 엔지니어라는 공개 데이터 [추정], ③ FW Test Engineer 공고 [확인됨 공고 4941340007]. 행동 질문과 경력 이야기는 Adam · Michael 라운드에서 이미 했으므로 **1시간 전부 코딩**일 가능성을 기본으로 둔다 (Don의 판단). 이 강의는 시나리오 6개를 확률 순으로 놓고, 각각 **면접관이 할 말 → 1시간 진행 → 꼬리 질문 사다리 → 준비 자료**를 정리한다. 새로 만든 모의 문제 2개(B · C)는 실행해서 검증했다 (`python3 lecture/build_lecture.py --test`).

## 0. 결론 먼저

| 순위 | 시나리오 | 추정 확률 | 한 줄 | 준비 자료 |
|---|---|---|---|---|
| 1 | **A. Ring buffer 구현 → pytest 테스트 → 확장** | ~40% | 리크루터가 이름을 짚은 두 주제를 한 문제로 | [L00 Part 1 · 2](2026-10-04_L00_special_lecture.md) · [L05 07 · C 05/06](2026-10-04_L05_warmup.md) · [문제 01 · 02 · 05](../python/problems/01_ring_buffer.md) |
| 2 | **B. 로그 · 텔레메트리 처리 + 테스트** | ~20% | Full Stack이 좋아하는 "실무 데이터" 문제 | 아래 **모의 B** · [문제 06 framer](../python/problems/06_stream_framer.md) |
| 3 | **C. 남이 쓴 코드에 테스트를 써서 버그 찾기** | ~15% | 테스트 엔지니어 정체성을 가장 직접 보는 형식 | 아래 **모의 C** · [문제 02 mutant](../python/problems/02_pytest_ring_buffer.md) |
| 4 | D. C로 풀기 (ring buffer · 문자열 · 비트) | ~10% | "Python/C 둘 다 가능"이라 했으니 C를 고를 수도 | [L05 C 6개](2026-10-04_L05_warmup.md) · [L03](2026-10-04_L03_embedded_c_basics.md) |
| 5 | E. 장치 드라이버 + mock (재시도 · 타임아웃) | ~10% | pytest의 mock · fixture를 실전으로 | [문제 03](../python/problems/03_mock_serial_driver.md) · [L00 2.4](2026-10-04_L00_special_lecture.md) |
| 6 | F. 일반 LeetCode 1~2문제 | ~5% | 면접관 개인 문제은행일 경우 | [L00 Part 3](2026-10-04_L00_special_lecture.md) Tier S/A |

- **공통 분모**: 어떤 시나리오든 **"구현 → 테스트를 스스로 → 확장 질문"** 구조일 가능성이 높다 [추정]. 그래서 준비의 80%는 A와 그 테스트다
- system design이 주제 목록에 있었으므로, 코딩만 하더라도 마지막 5~10분에 **"이걸 실제 리그 · CI에서 어떻게 돌리겠나"**를 말로 물을 수 있다 → [N04](../notes/2026-10-02_N04_system_design_test_tooling.md) 10분 틀

## 1. 왜 이렇게 추정하나 — 근거 정리

| 근거 | 내용 | 시사점 [추정] |
|---|---|---|
| 리크루터 주제 | pytest · ring buffer · Python/C · system design [확인됨] | 주제를 미리 알려 줬다 = **문제가 정해져 있고 그 범위**일 가능성이 크다. "pytest + ring buffer"를 한 문제로 묶기 가장 쉽다 |
| 면접관 | Jon Kotowski — 공개 데이터 브로커상 Neros Full Stack Engineer. 이전 이력으로 모바일 앱 개발(Silvertrac, 2019–2022) · 소프트웨어 엔지니어(Hoonigan) 등이 나오지만 **동명이인 가능성** 있음 [추정, Datanyze · ZoomInfo] | 펌웨어 깊이(레지스터, ISR 타이밍)보다 **코드 품질 · 클래스 설계 · 테스트 · 소통**을 본다. 문제는 FW Test 팀(Michael)이 준 것을 진행할 가능성 |
| 이전 라운드 | Adam(Director, 09-18), Michael(FW Test HM, 10-01: 퀴즈형, **Python 미출제**) | 이번 라운드는 비어 있던 **Python 코딩 · pytest**를 채우는 자리 |
| 공고 | "Python", "automated testing frameworks", "HIL", I2C/SPI/UART [확인됨 공고 4941340007] | 실무형 문제(로그 · 프레임 · 드라이버)도 후보 |
| 형식 | 1시간, 코딩 위주 [확인됨 리크루터] | 문제 1개를 단계적으로 키우거나(가능성 높음), 2개 |

## 2. 공통 전략 — 어느 시나리오든

| 시점 | 할 것 | 대사 |
|---|---|---|
| 0–2분 | 문제를 다시 말하고 **질문 3개**: 입력 형태 · 경계 · 정책 | "Before I code — what should happen when it's full? Single-threaded to start? Python is fine?" |
| 2–3분 | 계획을 한 줄로 | "Data structure first, then the operations, then tests." |
| 구현 | 작은 단위로, **자주 실행** | "Let me run what I have so far." |
| 직후 | **묻기 전에** 테스트 | "Before we move on, I'd like to add a few tests — normal case, exactly full, wraparound." |
| 확장 | 다시 질문 → 기존 테스트가 깨지지 않는지 실행 | "I'll keep the old tests green while I add this." |
| 막힘 | 생각을 말로, 예시로 손 시뮬레이션 | "Let me trace capacity 3 by hand." |
| 끝 5분 | 안 한 것 · 다음에 할 것 | "Next I'd add a randomized test against deque, and run this on the rig in CI." |

- 환경 [추정]: CoderPad · HackerRank · 공유 IDE. **pytest가 없으면** L05 방식(`def test_…` + `if __name__ == "__main__":`)으로 같은 테스트를 그대로 실행 — 그래서 L05를 그 형식으로 연습한 것
- 언어: 기본 Python. C를 원하면 바로 C (L05 C 05/06). 판단 기준은 [P00](../plan/2026-10-02_P00_tech_screen_game_plan.md) "10-06 저녁 판단"

---

## 시나리오 A · Ring buffer → pytest → 확장 (~40%)

> 면접관이 할 말 (예상): **"Implement a fixed-size circular buffer in Python. Then write pytest tests for it. We'll extend it from there."**

### 1시간 진행 예상

| 분 | 단계 | Don이 할 것 |
|---|---|---|
| 0–3 | 질문 | 타입? 용량? 가득 차면 drop / overwrite / 예외? 스레드? |
| 3–15 | 구현 | `head · tail · count` 버전 ([L05 07](2026-10-04_L05_warmup.md)) — push · pop · peek · len · is_full |
| 15–28 | 테스트 | fixture(빈 버퍼 · 가득 찬 버퍼) · `pytest.raises` · parametrize(용량) · wraparound ([L00 2.3](2026-10-04_L00_special_lecture.md)) |
| 28–45 | 확장 1~2개 | 아래 사다리 |
| 45–55 | 확장 테스트 + 리팩터 | 기존 테스트 계속 초록 |
| 55–60 | 마무리 · 역질문 | CI · 리그에서 어떻게 돌릴지 한 문단 |

### 꼬리 질문 사다리 (위에서부터 나올 순서 [추정])

| 단계 | 질문 | 답의 핵심 | 자료 |
|---|---|---|---|
| 1 | "Add an overwrite mode." | 가득 차면 tail도 한 칸 전진, `dropped` 카운터 | [L00 1.3](2026-10-04_L00_special_lecture.md) |
| 2 | "How do you test the overwrite?" | 용량 + k개 넣고 남은 내용 = 마지막 용량개, dropped == k | [문제 02](../python/problems/02_pytest_ring_buffer.md) |
| 3 | "Make it thread-safe / one producer thread, one consumer." | Lock + Condition 두 개, 실무는 `queue.Queue` | [문제 05](../python/problems/05_blocking_ring.md) · [N06](../notes/2026-10-03_N06_python_threads_vs_c.md) |
| 4 | "Bytes from a UART come in chunks — read N bytes at once." | bulk read/write, wrap에서 두 조각 | [L00 1.5(b)](2026-10-04_L00_special_lecture.md) |
| 5 | "How would this look in C on the MCU?" | 2의 거듭제곱 + free-running index, ISR = 단일 writer | [L05 C 06](2026-10-04_L05_warmup.md) |
| 6 | "How do you know your tests are good enough?" | 경계 · 무작위(deque 모델) · mutation testing | [문제 02](../python/problems/02_pytest_ring_buffer.md) |

## 시나리오 B · 로그 · 텔레메트리 처리 + 테스트 (~20%)

> 면접관이 할 말 (예상): **"Here's a telemetry log from a drone — one frame per line. Parse it, tell me if any frames were dropped, the longest gap between frames, and the lowest battery voltage. Malformed lines shouldn't crash it. Then test it."**

- 왜 나올 수 있나: Full Stack 엔지니어가 매일 하는 일(데이터 파싱 · 요약 · 에러 처리)이면서, 드론 · 테스트 맥락이 있다 [추정]
- 질문 3개: "Is seq strictly increasing, or can it wrap or arrive out of order?" · "Should malformed lines be skipped and counted?" · "Is the whole log in memory, or streaming?"

### 모의 B 풀이 (실행 검증)

```python
from dataclasses import dataclass, field


@dataclass
class Report:
    frames: int = 0
    malformed: int = 0
    missing: list = field(default_factory=list)     # 빠진 seq 번호들
    max_gap_ms: int = 0                              # 연속 프레임 사이 최대 간격
    min_vbat: float = float("inf")


def parse_line(line):
    """'t=1000 seq=1 vbat=16.4' → {'t': 1000, 'seq': 1, 'vbat': 16.4}. 형식이 틀리면 None."""
    try:
        kv = dict(part.split("=", 1) for part in line.split())
        return {"t": int(kv["t"]), "seq": int(kv["seq"]), "vbat": float(kv["vbat"])}
    except (ValueError, KeyError):
        return None


def check(lines):
    r = Report()
    prev = None
    for line in lines:
        f = parse_line(line)
        if f is None:
            r.malformed += 1
            continue
        r.frames += 1
        r.min_vbat = min(r.min_vbat, f["vbat"])
        if prev is not None:
            r.missing += range(prev["seq"] + 1, f["seq"])      # 1, 2, 5 → 3, 4
            r.max_gap_ms = max(r.max_gap_ms, f["t"] - prev["t"])
        prev = f
    return r
```

### 모의 B 테스트

```python
import pytest

LOG = """t=1000 seq=1 vbat=16.4
t=1020 seq=2 vbat=16.3
garbage line
t=1100 seq=5 vbat=15.9
t=1120 seq=6 vbat=16.0""".splitlines()


def test_summary():
    r = check(LOG)
    assert (r.frames, r.malformed, r.missing) == (4, 1, [3, 4])
    assert r.max_gap_ms == 80 and r.min_vbat == pytest.approx(15.9)


@pytest.mark.parametrize("line", ["", "t=1 seq=2", "t=x seq=1 vbat=1", "t=1 seq=1 vbat"])
def test_malformed_lines_are_counted_not_fatal(line):
    assert parse_line(line) is None
    assert check([line]).malformed == 1


def test_empty_log():
    r = check([])
    assert r.frames == 0 and r.missing == [] and r.max_gap_ms == 0
```

| 설계 결정 | 이유 (말할 것) |
|---|---|
| 파싱(`parse_line`)과 집계(`check`)를 분리 | "Each piece is testable on its own — parser edge cases don't need a whole log." |
| 형식 오류는 **세고 넘어감** | "One bad line from a noisy link shouldn't hide the rest of the flight." |
| 결과를 dataclass로 | "A structured report is easy to assert on and to send to a dashboard." |
| 리스트 대신 이터러블을 받음 | "It works on a file object or a serial stream, line by line, constant memory." |

| 꼬리 질문 | 답의 핵심 |
|---|---|
| "seq wraps at 65535." | 차이를 `(cur - prev) % 65536`으로, 1이면 정상 · 0이면 중복 · 큰 값이면 누락 |
| "Frames can arrive out of order." | 작은 재정렬 창(ring buffer!)에 모았다가 순서대로 방출, 창보다 늦으면 '늦게 도착' 카운트 |
| "It's a 2 GB log." | 지금 구조 그대로 스트리밍 (`for line in open(...)`), `missing`은 범위 `(start, end)`로 저장해 메모리 절약 |
| "Test the malformed handling better." | parametrize로 빈 줄 · 키 누락 · 숫자 아님 · `=` 없음 (위 테스트) |
| "How would CI use this?" | 비행 로그 업로드 → 이 검사 → 기준(누락률, 최대 간격) 넘으면 실패 · 대시보드 ([N04 문제 C](../notes/2026-10-02_N04_system_design_test_tooling.md)) |

## 시나리오 C · 남이 쓴 코드에 테스트를 써서 버그 찾기 (~15%)

> 면접관이 할 말 (예상): **"A teammate wrote this ring buffer. Don't fix it yet — write tests that would catch problems. Then fix what your tests find."**

- 왜 나올 수 있나: "테스트 엔지니어인가"를 가장 직접 보는 형식. 구현 능력 + 테스트 설계 + 디버깅을 한 번에 [추정]
- 진행: **스펙을 소리 내 요약 → 테스트 이름부터 나열 → 하나씩 써서 실행 → 실패를 보고 원인 설명 → 고치기 → 전부 초록**

### 주어진 코드 (버그 3개)

```python
class RingBuffer:
    def __init__(self, capacity):
        self.buf = [None] * capacity
        self.cap = capacity
        self.head = 0
        self.tail = 0
        self.count = 0

    def __len__(self):
        return self.count

    def push(self, x):
        if self.count == self.cap - 1:
            return False
        self.buf[self.head] = x
        self.head = (self.head + 1) % self.cap
        self.count += 1
        return True

    def pop(self):
        if self.count == 0:
            raise IndexError("empty")
        x = self.buf[self.tail]
        self.tail = self.tail + 1
        self.count -= 1
        return x

    def peek(self):
        if self.count == 0:
            raise IndexError("empty")
        return self.buf[self.head - 1]
```

> 먼저 직접 해 보기: 위 코드를 보지 말고 **스펙만으로 테스트를 써서** 3개를 드러내 본다. 그다음 아래를 본다.

### 내가 쓸 테스트 (스펙만 보고)

```python
import importlib
import os

import pytest

RingBuffer = importlib.import_module(os.environ.get("MOCK_RING", "ring_fixed")).RingBuffer


@pytest.fixture
def rb():
    return RingBuffer(3)


def test_can_hold_exactly_capacity(rb):          # → 버그 1
    assert rb.push(1) and rb.push(2) and rb.push(3)
    assert not rb.push(4) and len(rb) == 3


def test_order_survives_wraparound(rb):          # → 버그 2
    for x in (1, 2):
        rb.push(x)
    for _ in range(10):                          # 여러 바퀴: tail 이 끝을 넘어가게
        x = rb.pop()
        rb.push(x + 2)
    assert [rb.pop(), rb.pop()] == [11, 12]        # k 바퀴 뒤 큐는 [k+1, k+2]


def test_peek_returns_oldest_without_consuming(rb):   # → 버그 3
    rb.push("a")
    rb.push("b")
    assert rb.peek() == "a" and len(rb) == 2


@pytest.mark.parametrize("method", ["pop", "peek"])
def test_empty_raises(rb, method):
    with pytest.raises(IndexError):
        getattr(rb, method)()
```

실행 결과 (실제로 돌린 것):

```text
버그 버전  → 3 failed, 2 passed
  FAILED test_can_hold_exactly_capacity               (버그 1)
  FAILED test_order_survives_wraparound   IndexError  (버그 2)
  FAILED test_peek_returns_oldest_without_consuming   (버그 3)
버그를 하나씩만 남기면 → 각각 정확히 그 테스트 하나만 실패
고친 버전  → 5 passed
```

### 고친 코드

```python
class RingBuffer:
    def __init__(self, capacity):
        self.buf = [None] * capacity
        self.cap = capacity
        self.head = 0
        self.tail = 0
        self.count = 0

    def __len__(self):
        return self.count

    def push(self, x):
        if self.count == self.cap:                      # 버그 1: cap - 1 → 한 칸을 못 씀
            return False
        self.buf[self.head] = x
        self.head = (self.head + 1) % self.cap
        self.count += 1
        return True

    def pop(self):
        if self.count == 0:
            raise IndexError("empty")
        x = self.buf[self.tail]
        self.tail = (self.tail + 1) % self.cap           # 버그 2: % 빠짐 → 한 바퀴 뒤 IndexError
        self.count -= 1
        return x

    def peek(self):
        if self.count == 0:
            raise IndexError("empty")
        return self.buf[self.tail]                       # 버그 3: head - 1 = 가장 '최신'
```

| 말할 것 | 대사 |
|---|---|
| 테스트를 먼저 쓰는 이유 | "If I fix by reading the code, I might miss what I didn't notice. A test per spec sentence finds what's actually wrong — and stays as a regression test." |
| 실패를 읽는 법 | "This one fails with IndexError only after the tail passes the end — so it's the wrap on pop, not push." |
| 한 테스트 = 한 사실 | "Each test name is a sentence from the spec, so the failure tells you which promise broke." |

## 시나리오 D · C로 풀기 (~10%)

> 면접관이 할 말 (예상): **"You can use C if you prefer — implement a ring buffer (or reverse a string / parse an integer) and show me how you'd test it."**

| 나올 법한 것 | 자료 (구현 + main 테스트, 실행 검증) |
|---|---|
| ring buffer | [L05 C 05 (count) · 06 (free-running)](2026-10-04_L05_warmup.md) |
| 문자열 뒤집기 · strcpy · atoi | [L05 C 01 · 03 · 04](2026-10-04_L05_warmup.md) |
| 연결 리스트 | [L05 C 02](2026-10-04_L05_warmup.md) |
| 비트 조작 · 엔디언 · CRC | [L03 2 · 6 · 11절](2026-10-04_L03_embedded_c_basics.md) |

- C를 고른다면 테스트도 C로: `static void test_…(void)` + `assert` + main `PASS` (L05 형식). 말로 덧붙이기: "On the real project I'd run these under Unity/Ceedling in CI, with ASan on the host." ([L02](2026-10-04_L02_embedded_test_frameworks.md))
- 면접관이 C를 잘 모를 수 있다 [추정] → **코드보다 설명을 더 친절하게** (`head - tail`이 왜 wrap에도 맞는지)

## 시나리오 E · 장치 드라이버 + mock (~10%)

> 면접관이 할 말 (예상): **"Write a small class that talks to a sensor over serial: send a command, read a reply, retry on timeout. Test it without hardware."**

- 핵심: 포트 · sleep을 **생성자로 주입** → 테스트에서 `Mock`, `side_effect`로 응답 대본, `call_args_list`로 나간 바이트와 백오프 검증
- 자료: [L00 2.4 mock 예제](2026-10-04_L00_special_lecture.md) · [문제 03 (mutant 9개)](../python/problems/03_mock_serial_driver.md) · [FTE 문제 04 시리얼 드라이버](../../firmwareTestEngineerPrep/site/problems/04_serial_dut_driver.html)
- 꼬리: "부작용 있는 명령(arm)도 재시도하나?" → **안 한다**, 상태를 먼저 확인 (멱등성)

## 시나리오 F · 일반 LeetCode (~5%)

- 면접관이 자기 문제은행을 쓸 경우 [추정]. Full Stack 면접에서 흔한 것: **Two Sum · Valid Parentheses · LRU Cache · Merge Intervals · Top K**
- 자료: [L00 Part 3 Tier S/A](2026-10-04_L00_special_lecture.md) · [L05 Python 8개](2026-10-04_L05_warmup.md)
- 이 경우에도 끝에 **테스트 2~3개를 스스로** — 그게 이 포지션의 차별점

---

## 3. 남은 3일 — 시나리오 기준 연습 계획

| 날짜 | 할 것 (타이머 필수) | 시간 |
|---|---|---|
| 10-05 (월) | **시나리오 A 풀 리허설 1회** (빈 파일, 60분, 말하면서) → 막힌 단계만 다시 | 1.5h |
| 10-06 (화) | 모의 B (40분) · 모의 C (30분) — 정답 보기 전에 끝까지 | 1.5h |
| 10-07 (수) | 시나리오 A 다시 (45분 목표) + L05 C 05/06 각 1회 + 역질문 확정 | 1.5h |
| 10-08 (목) 아침 | L05 Python 01 · 07을 한 번씩 (손 풀기), [N05 당일 체크리스트](../notes/2026-10-02_N05_live_coding_english.md) | 40m |

- 리허설은 **녹음**해서 첫 3분(질문)과 "테스트를 먼저 꺼냈나"만 확인
- 빈 파일: [playground](../playground/README.md) · 모의 B는 `lecture/mock/telemetry_check.py`의 문제 설명만 보고, 모의 C는 `ring_buggy.py`를 복사해서

## 4. 면접관이 Full Stack이라서 생기는 것 — 대응 [추정]

| 가능성 | 대응 |
|---|---|
| 임베디드 용어(ISR, DMA)가 안 통할 수 있다 | 한 문장 번역: "an interrupt handler is like a callback that can fire between any two instructions" |
| 코드 스타일 · 이름 · 구조를 꼼꼼히 본다 | 함수 작게, 이름이 스펙 문장처럼, 매직 넘버 상수화, docstring 한 줄 |
| 클래스 설계 질문 ("how would you extend this?") | 정책을 생성자 인자로, 테스트 가능한 경계(주입) |
| 웹식 테스트 감각 (fixture, mock, CI) | 그대로 통한다 — pytest는 공통 언어 |
| "왜 FW Test로 왔나" 같은 짧은 대화 | 30초: "I build test infrastructure around real hardware, and I write the firmware too, so I can read the code under test." |

## 체크

- [ ] 0절 표를 가리고 6개 시나리오와 각 첫 질문을 말하기
- [ ] 시나리오 A를 60분 타이머로 처음부터 끝까지 (테스트까지)
- [ ] 모의 B: 문제 설명만 보고 40분 → 위 풀이와 비교
- [ ] 모의 C: 스펙만 보고 테스트 써서 버그 3개 → 위 결과와 비교
- [ ] 2절 "공통 전략" 대사 7개를 소리 내어 한 번
