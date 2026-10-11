# ✅ L07 · 마지막 체크업 — 오늘 밤 2시간 + 내일 9:00~11:40, 12:00 면접

> **2026-10-08(목) 12:00 · Firmware Test Engineer technical screen · 1시간 · 코딩 위주 · 면접관 Jon Kotowski.** 이 페이지 하나만 보고 순서대로 체크한다. 새로운 걸 배우지 말고 **이미 한 것을 손과 입에 붙이는** 시간이다. 아래 치트시트 코드는 전부 실행해서 통과한 파일에서 그대로 가져왔다.
>
> ⚠️ **시간 확인**: 리크루터 첫 안내(10-02)는 **11:00**, 지금 계획은 **12:00**. 오늘 밤 캘린더 초대 · 메일의 **시간과 시간대(PT)** 를 한 번 더 확인할 것 → 아래 첫 체크박스.

## 0. 내일 기억할 다섯 줄

- **시작은 질문 3개**: "What should happen when it's full? Single-threaded to start? Python is fine?"
- **ring buffer**: `head`(쓸 칸) · `tail`(읽을 칸) · `count`(개수). count가 full/empty를 구분. `% cap`로 감기
- **구현 직후, 묻기 전에** "Let me add tests" → 정상 1개 + 가득 참 + wraparound + 빈 상태 예외
- **mock은 경계만**(포트 · 시간). 생성자로 주입하면 patch가 필요 없다
- **막히면 말로**: "Let me trace capacity 3 by hand." 30초 이상 침묵 금지

## 1. 오늘 밤 — 2시간

| 시간 | 할 것 | 어떻게 | ✓ |
|---|---|---|---|
| 0:00–0:05 | **면접 시간 · 링크 확인** | 초대 메일: 12:00인지 11:00인지, PT인지, Zoom/Teams/CoderPad 링크 | |
| 0:05–0:15 | 큰 그림 다시 보기 | [L06 0절 결론 표 · 2절 공통 전략](2026-10-05_L06_screen_scenarios.md) | |
| 0:15–0:55 | **시나리오 A 리허설 (40분 타이머)** | 빈 파일: `python3 lecture/warmup/new.py py 7` → 구현 → `test_…` 3개 → overwrite 모드 추가 → 테스트 하나 더. **소리 내어** | |
| 0:55–1:15 | **모의 C (20분)**: 버그 있는 코드에 테스트 | [L06 시나리오 C](2026-10-05_L06_screen_scenarios.md) — `ring_buggy.py` 스펙만 보고 테스트 → 버그 3개 | |
| 1:15–1:35 | C 손 풀기 | `new.py c 6` (free-running ring) · `new.py c 1` (reverse str) — 각 PASS까지 | |
| 1:35–1:55 | 영어 소리 내기 | 아래 6절 대사 · 7절 역질문 · [N05 1절 자기소개 45초](../notes/2026-10-02_N05_live_coding_english.md) | |
| 1:55–2:00 | 내일 준비물 · 끝 | 8절 체크리스트의 전날 항목, **그리고 잔다** | |

- 실행: `cd playground && make py F=py/<파일>` · `make c F=c/<파일>` → `PASS` 보면 지우고 다음
- 리허설이 40분을 넘겨도 괜찮다 — **넘긴 지점만 메모**해 두고 내일 아침 그 부분만

## 2. 내일 오전 — 9:00~11:40

| 시간 | 할 것 | 목표 | ✓ |
|---|---|---|---|
| 9:00–9:20 | 몸풀기 Python 3개: 01 Two Sum · 03 투 포인터 · 05 이진 탐색 | 각 5분, `new.py py 1` … PASS | |
| 9:20–9:45 | **ring buffer 빈 파일에서 한 번 더** (07) | **12분 안에 구현 + 테스트 3개** | |
| 9:45–10:10 | pytest 손 풀기 | 빈 파일에 아래 4절(fixture · parametrize · raises · Mock)을 **보지 않고** 다시 쓰고 실행 | |
| 10:10–10:30 | 모의 B 말로만 | [L06 시나리오 B](2026-10-05_L06_screen_scenarios.md) 풀이를 읽고 꼬리 질문 5개 답을 소리 내기 | |
| 10:30–10:50 | 꼬리 질문 사다리 · system design 10분 틀 | [L06 A 사다리 6단계](2026-10-05_L06_screen_scenarios.md) · [N04 0절](../notes/2026-10-02_N04_system_design_test_tooling.md) | |
| 10:50–11:15 | **휴식 · 식사** | 화면 보지 말 것 | |
| 11:15–11:35 | 환경 점검 | 8절 당일 체크리스트 | |
| 11:35–11:55 | 이 페이지 0절 · 6절만 | 자기소개 45초 한 번, 역질문 3개 메모 | |
| 11:55 | 접속 | 물, 펜, 종이 | |

## 3. 치트시트 ① Ring buffer (Python, head · tail · count)

```python
class RingBuffer:
    def __init__(self, capacity):
        self.buf = [None] * capacity
        self.cap = capacity
        self.head = 0                  # 다음에 쓸 칸
        self.tail = 0                  # 다음에 읽을 칸
        self.count = 0

    def __len__(self):
        return self.count

    def is_full(self):
        return self.count == self.cap

    def push(self, x):
        if self.is_full():
            return False
        self.buf[self.head] = x
        self.head = (self.head + 1) % self.cap
        self.count += 1
        return True

    def peek(self):
        if self.count == 0:
            raise IndexError("empty")
        return self.buf[self.tail]

    def pop(self):
        x = self.peek()
        self.buf[self.tail] = None
        self.tail = (self.tail + 1) % self.cap
        self.count -= 1
        return x


def test_ring_buffer():
    r = RingBuffer(3)
    assert r.push(1) and r.push(2) and r.push(3) and not r.push(4) and r.is_full()
    assert r.pop() == 1 and r.push(4)                 # wrap: 4 는 index 0 으로
    assert [r.pop(), r.pop(), r.pop()] == [2, 3, 4] and len(r) == 0


def test_pop_empty_raises():
    try:
        RingBuffer(1).pop()
    except IndexError:
        return
    raise AssertionError("pop on empty must raise")


if __name__ == "__main__":
    test_ring_buffer()
    test_pop_empty_raises()
    print("PASS")
```

| 꼬리 질문 | 한 줄 답 |
|---|---|
| Overwrite mode? | 가득 차면 `tail = (tail + 1) % cap`, `count -= 1`, `dropped += 1` 한 뒤 평소대로 push |
| Thread-safe? | 아니다 — `count += 1`은 바이트코드 여러 개. `Lock` + `Condition` 두 개, 실무는 `queue.Queue` |
| Complexity? | push · pop · peek O(1), 메모리 O(capacity) 고정 |
| Why not `list.pop(0)`? | O(n) — 나머지를 전부 당긴다. `deque.popleft()`는 O(1) |
| C version? | 2의 거듭제곱 크기 + free-running `head`/`tail`, `head - tail`이 개수 (아래 5절) |
| How do you know the tests are good? | 경계 + 무작위(deque를 정답 모델로) + mutation testing |

## 4. 치트시트 ② pytest — fixture · parametrize · raises · Mock

> pytest가 헷갈리면 먼저 [L08 pytest 처음부터](2026-10-08_L08_pytest_from_zero.md) (20분, 단어 6개 + 실제 실행 화면).

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

Mock — 하드웨어 없이 재시도 · 백오프 검증:

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

- pytest가 없는 환경이면: 같은 `def test_…()`를 `if __name__ == "__main__":`에서 직접 호출 + `print("PASS")` ([L05 형식](2026-10-04_L05_warmup.md))

## 5. 치트시트 ③ C ring buffer · 몸풀기 핵심

```c
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define RB_SIZE 8u                               /* 2 의 거듭제곱 */
#define RB_MASK (RB_SIZE - 1u)

typedef struct {
    uint8_t  buf[RB_SIZE];
    uint32_t head, tail;
} rb_t;

bool rb_push(rb_t *rb, uint8_t v)
{
    if (rb->head - rb->tail == RB_SIZE)
        return false;
    rb->buf[rb->head & RB_MASK] = v;
    rb->head++;
    return true;
}

bool rb_pop(rb_t *rb, uint8_t *v)
{
    if (rb->head == rb->tail)
        return false;
    *v = rb->buf[rb->tail & RB_MASK];
    rb->tail++;
    return true;
}

static void test_rb_index_wrap(void)
{
    rb_t rb = {{0}, UINT32_MAX - 3, UINT32_MAX - 3};      /* 32비트 wrap 을 지나가게 */
    uint8_t v;
    for (uint8_t i = 0; i < RB_SIZE; i++)
        assert(rb_push(&rb, i));
    assert(!rb_push(&rb, 99) && rb.head - rb.tail == RB_SIZE);
    for (uint8_t i = 0; i < RB_SIZE; i++)
        assert(rb_pop(&rb, &v) && v == i);
    assert(!rb_pop(&rb, &v));
}

int main(void)
{
    test_rb_index_wrap();
    puts("PASS");
    return 0;
}
```

Two Sum (dict) · 이진 탐색:

```python
def two_sum(nums, target):
    seen = {}                          # 값 → 인덱스
    for i, x in enumerate(nums):
        need = target - x
        if need in seen:               # 짝이 이미 지나갔나?
            return [seen[need], i]
        seen[x] = i                    # 확인한 다음에 저장
    return []


def test_two_sum():
    assert two_sum([2, 7, 11, 15], 9) == [0, 1]
    assert two_sum([3, 3], 6) == [0, 1]       # 같은 값 두 개
    assert two_sum([3], 6) == []              # 자기 자신과 짝 금지


if __name__ == "__main__":
    test_two_sum()
    print("PASS")
```

```python
def search(nums, target):
    left, right = 0, len(nums) - 1
    while left <= right:
        mid = (left + right) // 2
        if nums[mid] == target:
            return mid
        if nums[mid] < target:
            left = mid + 1             # 답은 오른쪽 절반에
        else:
            right = mid - 1            # 답은 왼쪽 절반에
    return -1


def test_search():
    assert search([-1, 0, 3, 5, 9, 12], 9) == 4
    assert search([-1, 0, 3, 5, 9, 12], 2) == -1
    assert search([], 1) == -1


if __name__ == "__main__":
    test_search()
    print("PASS")
```

## 6. 말할 문장 — 순서대로

| 때 | 영어 |
|---|---|
| 문제 받자마자 | "Let me restate it to make sure I've got it right… A few quick questions before I code." |
| 질문 | "What should happen when it's full — drop the new item, overwrite the oldest, or raise? Single-threaded to start? Is Python okay?" |
| 계획 | "I'll pick the data layout first, then push and pop, trace a small example, then write tests." |
| 상태 설명 | "Head is the next write slot, tail is the next read slot, and count tells full from empty." |
| 테스트 꺼내기 | "Before we move on, I'd like to add a few tests — a normal case, exactly full, wraparound, and pop on empty." |
| 막혔을 때 | "Let me trace capacity three by hand… push 1, 2, 3, pop gives 1, push 4 goes to index 0." |
| 확장 받을 때 | "I'll keep the existing tests green while I add this." |
| 시간 부족 | "I'll skip that and make sure the core is correct and tested — I can describe the rest." |
| 마무리 | "Next I'd add a randomized test against a deque, and in our setup run it in CI on every merge request." |
| Python vs C 질문 | "In Python I track a count; in C on the MCU I'd use free-running unsigned indices with a power-of-two mask, so the ISR producer and the main-loop consumer each own one index." |
| GIL 질문 | "CPython threads are real OS threads, but only one runs Python bytecode at a time — so concurrency, not parallelism, and `count += 1` still isn't safe without a lock." |

## 7. 역질문 3개 (Full Stack 면접관용)

1. "How does test data flow to the software side today — is there a results database or dashboard the test team and your team share?"
2. "What would you hope the firmware test team builds first that would make your work easier?"
3. "What does a typical week look like for you, and how often do you work with firmware or test engineers?"

## 8. 체크리스트

### 오늘 밤

- [ ] 초대 메일에서 **시간(12:00 vs 11:00) · 시간대 · 링크 · 플랫폼** 확인
- [ ] 시나리오 A 리허설 40분 (막힌 지점 메모: ________)
- [ ] 모의 C 버그 3개 찾기
- [ ] C ring buffer (free-running) PASS
- [ ] 6절 대사 소리 내어 한 번, 자기소개 45초 한 번
- [ ] 노트북 충전, 이어폰 충전, 알람 8:30
- [ ] 자기 (7시간)

### 내일 아침

- [ ] 9:00 몸풀기 3개 PASS
- [ ] ring buffer 12분 안에 구현 + 테스트
- [ ] pytest 4종 빈 파일에서
- [ ] 11:15 환경 점검: 카메라 · 마이크 · 이어폰 · 인터넷 · 알림 끄기 · 화면 공유 테스트 · 에디터 글자 크게
- [ ] 로컬에서 바로 코딩해야 할 경우 대비: `technicalScreenPrep/.venv/bin/python -m pytest --version` 동작 확인
- [ ] 물 · 종이 · 펜 (손 시뮬레이션용)
- [ ] 끝나고 바로: 받은 문제 원문 · 내 답 · 아쉬운 점 메모 → 컨텍스트 파일 §6

> 마지막 한 줄: **"질문하고, 작게 만들고, 돌려 보고, 테스트를 먼저 꺼낸다."** 이미 이걸 여러 번 했다.
