# 💬 Q02 지난 스크린에서 놓친 "overflow case" — 정확히 무엇이었을 수 있나

> 어디서: 10-12 Devin 피드백 "ring buffer는 거의 다 했지만 **overflow case를 놓쳤다**" · [M2](../modules/2026-10-12_M2_coding_python.md) 1절 · [C02](../coding/2026-10-12_C02_fixed_fifo.md) · 스크린 재구성 [L09](../../technicalScreenPrep/site/lecture/2026-10-09_L09_screen_recap.html)

## 1. 먼저 — 알 수 있는 것과 없는 것

| | 내용 |
|---|---|
| 확인된 것 | 리크루터가 전한 한 줄: **"missed the overflow case"**. 문제는 C byte ring buffer — `rb_init(rb, uint8_t *buf, uint8_t capacity)`, `size_t count/head/tail`, push/pop byte, **bulk push/pop all-or-nothing**, `rb_peek(rb, uint8_t offset, …)` |
| 모르는 것 | 그때 Don이 쓴 **실제 코드는 남아 있지 않다**. 그래서 "overflow"가 아래 중 무엇이었는지 확정할 수 없다 |
| 그래서 | 이 API에서 "overflow"라고 불릴 수 있는 **4가지를 전부** 막고, 전부 테스트로 보여 주는 것이 온사이트의 답이다 [추정] |

> 스스로 떠올려 볼 질문: 그때 ① 꽉 찬 상태에서 push 테스트를 썼나? ② bulk push에서 자리 검사를 **쓰기 전에** 했나? ③ 그 검사를 `count + len > capacity`로 썼나 `len > capacity - count`로 썼나? ④ 면접관에게 "꽉 차면 어떻게 할까요?"를 물었나? — 기억나는 것이 있으면 알려 주면 이 페이지를 고친다.

## 2. "overflow case" 후보 4가지

| # | 경우 | 틀린 코드 모양 | 결과 | 맞는 처리 |
|---|---|---|---|---|
| ① | **꽉 찬 상태에서 push_byte** (가장 유력 [추정]) | full 검사 없이 쓰기, 또는 검사는 했지만 **테스트에서 안 보여 줌** | head가 tail을 넘어가 가장 오래된 데이터를 조용히 덮어씀, count > capacity | `if count == capacity: return False` + `dropped += 1` (또는 정책이 overwrite면 **tail도 같이 밀기**) |
| ② | **bulk push가 남은 자리보다 클 때** | 바이트를 넣다가 중간에 full을 만남 → 일부만 들어감 | all-or-nothing 위반 — 프레임이 반쪽만 들어감 | **쓰기 전에** `if len > capacity - count: return False` — 아무것도 바꾸지 않음 |
| ③ | **검사식 자체의 정수 overflow** (C) | `if (count + len > capacity)` | len이 아주 크면 `count + len`이 size_t에서 한 바퀴 돌아 작은 수가 됨 → 검사 통과 → 배열 밖 쓰기 | 빼기 쪽으로: `len > capacity - count` (count ≤ capacity가 항상 참이라 빼기는 안전) |
| ④ | **uint8_t 인덱스/카운트 wrap** (C) | head나 새 count를 `uint8_t` 변수에 저장 | 255 다음 0으로 돌아감 → 검사가 틀린 값을 봄 | 인덱스 · count는 `size_t`, 다음 칸은 `% capacity` |

### ③ · ④ 실제로 돌려 본 결과 (C, `-Wall`)

```c
uint8_t capacity = 200, count = 100;
size_t  len = SIZE_MAX - 50;
(count + len > capacity)          /* 0  → 틀림: "자리 있음"으로 통과 */
(len > (size_t)(capacity - count))/* 1  → 맞음: 거절 */

uint8_t len8 = 200;
uint8_t newc = count + len8;      /* 44 → "200 이하"라서 통과처럼 보임 */

uint8_t head = 254; head++; head++; /* 0 → capacity가 255가 아니면 엉뚱한 칸 */
```

> 출력: `A wrong=0 right=1` · `B newc=44` · `C head=0` — 세 가지 모두 **컴파일러 경고 없이** 지나간다. 테스트로만 잡힌다.

## 3. 온사이트(Python)에서 — 코드보다 먼저 말로

Python에는 ③ · ④ 같은 정수 overflow가 없다. 그래서 이번에 보여 줄 것은 **①②의 정책과 테스트**다.

1. 시작할 때: "Last time I missed the full-buffer case, so I'll call out overflow behavior up front."
2. 질문: **"When it's full, should a new byte be rejected, or should it overwrite the oldest?"** — 정책을 면접관이 정하게 한다
3. 질문: "For a bulk write that doesn't fit — all-or-nothing, or write what fits and return the count?"
4. 잃은 데이터는 **센다**: "Losing data silently is the worst bug, so I count what I drop."
5. 마지막 테스트에 overflow 줄이 **반드시** 있다: 꽉 찬 push → 반환값 · 카운터 · **기존 데이터와 순서가 그대로**

## 4. Python 참고 풀이 + 테스트 (실행해서 OK 확인)

```python
class RingBuffer:
    """Fixed-capacity byte FIFO. Full policy: reject + count (never silently lose data)."""

    def __init__(self, capacity):
        if capacity <= 0:
            raise ValueError("capacity must be > 0")
        self._buf = bytearray(capacity)
        self.capacity = capacity
        self.head = self.tail = self.count = 0
        self.dropped = 0                      # how many bytes we refused

    def free(self):
        return self.capacity - self.count

    def push_byte(self, b):
        if not 0 <= b <= 0xFF:
            raise ValueError("not a byte")
        if self.count == self.capacity:       # (1) full -> reject, count it
            self.dropped += 1
            return False
        self._buf[self.head] = b
        self.head = (self.head + 1) % self.capacity   # (3) index wraps
        self.count += 1
        return True

    def pop_byte(self):
        if self.count == 0:
            return None
        b = self._buf[self.tail]
        self.tail = (self.tail + 1) % self.capacity
        self.count -= 1
        return b

    def push(self, data):
        if len(data) > self.free():           # (2) check BEFORE writing anything
            self.dropped += len(data)
            return False
        for b in data:
            self.push_byte(b)
        return True

    def pop(self, n):
        if n > self.count:
            return None
        return bytes(self.pop_byte() for _ in range(n))

    def peek(self, offset):
        if not 0 <= offset < self.count:
            return None
        return self._buf[(self.tail + offset) % self.capacity]


if __name__ == "__main__":
    rb = RingBuffer(3)
    assert rb.pop_byte() is None and rb.peek(0) is None          # empty
    assert rb.push(b"\x01\x02\x03")                               # exactly full: capacity fully usable
    assert rb.push_byte(4) is False and rb.dropped == 1           # (1) full single push
    assert rb.count == 3 and rb.pop(3) == b"\x01\x02\x03"         #     old data untouched, order kept

    rb = RingBuffer(5)
    assert rb.push(b"abc")
    assert rb.push(b"xyz") is False and rb.count == 3             # (2) bulk too big: nothing written
    assert rb.dropped == 3 and rb.pop(3) == b"abc"
    assert rb.push(b"123456") is False and rb.count == 0          #     larger than capacity itself
    assert rb.push(b"") and rb.pop(0) == b""                      #     len 0

    rb = RingBuffer(3)
    for i in range(20):                                           # (3) wrap many laps
        assert rb.push_byte(i) and rb.pop_byte() == i
    rb.push(b"\x0a\x0b"); rb.pop_byte(); rb.push(b"\x0c\x0d")     # wrapped state: 0b 0c 0d
    assert rb.peek(0) == 0x0B and rb.peek(2) == 0x0D and rb.peek(3) is None
    print("OK")
```

| 테스트 줄 | 막는 후보 |
|---|---|
| `push_byte(4) is False and dropped == 1` + 기존 데이터 순서 확인 | ① 꽉 찬 상태 push |
| `push(b"xyz") is False and count == 3` · 용량보다 큰 bulk · len 0 | ② bulk all-or-nothing |
| 20바퀴 push/pop · wrap 상태에서 peek | ④에 해당하는 인덱스 wrap (Python판) |

## 5. 꼬리 질문 대비

| 질문 | 답 |
|---|---|
| "What if it's a UART RX buffer and data keeps coming?" | 거절 + 카운트가 기본. 카운터를 텔레메트리로 내보내면 "main loop가 밀린다"는 것을 필드에서 볼 수 있다. 근본 해결은 버퍼 크기 · 소비 속도 · DMA |
| "When would you overwrite instead?" | 최신 값만 의미 있는 로그 · 텔레메트리 히스토리. 명령 큐에는 절대 안 쓴다 (오래된 명령이 사라짐) |
| "In C, how do you write the free-space check?" | `len > capacity - count` — 더하기 대신 빼기로 써서 정수 overflow를 피한다 |
| "ISR producer, main consumer?" | count를 둘 다 고치면 경쟁. head는 ISR만, tail은 main만 쓰는 SPSC + free-running 인덱스 |

## 6. 관련 페이지

[C02 고정 FIFO](../coding/2026-10-12_C02_fixed_fifo.md) · [M2 1절](../modules/2026-10-12_M2_coding_python.md) · [K02 7.5절](../plan/2026-10-20_K02_final_cram_three_1on1s.md) · [L09 스크린 재구성](../../technicalScreenPrep/site/lecture/2026-10-09_L09_screen_recap.html)
