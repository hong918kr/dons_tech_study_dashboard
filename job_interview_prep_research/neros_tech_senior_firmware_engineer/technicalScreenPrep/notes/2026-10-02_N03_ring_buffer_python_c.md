# 🔁 N03 · Ring buffer — Python 구현 · C 버전 · 언어 선택 · 꼬리 질문

> 리크루터가 "ring buffer, Python/C 둘 다 가능"이라고 했다. C 쪽 라운드 플레이북(질문 먼저 → 화이트보드 순서 → SPSC → DMA → 테스트)은 이미 [onsitePrep N05](../../onsitePrep/site/notes/2026-10-01_N05_ring_buffer_onsite.html)에 있고 코드 5종이 `make -C code test`로 통과한다. 이 노트는 **이번 스크린용 차이**만 다룬다: Full Stack 면접관 앞에서 어느 언어로 시작할지, Python 구현의 결정들, Python ↔ C 대응표, 그리고 Python 쪽에서 나올 꼬리 질문.

## 0. 결론 먼저

| 결정 | 선택 | 이유 |
|---|---|---|
| 시작 언어 | **Python** (면접관이 C를 원하면 즉시 C) | 면접관이 Full Stack [추정] → 리뷰하기 쉬운 언어로. 테스트(pytest)까지 같은 파일에서 바로 이어짐 |
| 첫 질문 | "Python or C — any preference? And is this single-threaded to start?" | 선택권을 면접관에게 한 번 넘기는 것 자체가 협업 신호 |
| 상태 표현 | `head` + `count` (Python) / free-running `head`·`tail` (C) | Python int는 wrap이 없다 → count가 가장 읽기 쉬움 |
| 가득 찼을 때 | 생성자 플래그 `overwrite` + `dropped` 카운터 | 정책을 숨기지 않고 손실을 보이게 |
| 마무리 | **테스트를 직접 쓴다** → 문제 02 패턴 | 이 라운드에서 가장 큰 차별점 |

## 1. Python — 화이트보드 버전 (15분)

```python
class RingBuffer:
    def __init__(self, capacity, overwrite=False):
        if not isinstance(capacity, int) or capacity <= 0:
            raise ValueError("capacity must be a positive int")
        self.capacity, self.overwrite = capacity, overwrite
        self._buf = [None] * capacity
        self._head = 0            # next write slot
        self._count = 0
        self.dropped = 0

    def _tail(self):              # oldest slot, derived
        return (self._head - self._count) % self.capacity

    def push(self, item):
        if self._count == self.capacity:
            if not self.overwrite:
                return False
            self._count -= 1      # forget the oldest
            self.dropped += 1
        self._buf[self._head] = item
        self._head = (self._head + 1) % self.capacity
        self._count += 1
        return True

    def pop(self):
        if self._count == 0:
            raise IndexError("pop from empty RingBuffer")
        t = self._tail()
        item, self._buf[t] = self._buf[t], None
        self._count -= 1
        return item
```

- 전체 버전(peek, iter, clear, len)과 테스트 18개: [문제 01](../python/problems/01_ring_buffer.md) → [모범답안](../python/solutions/01_ring_buffer.py)
- **말하면서 쓰는 순서**: 상태 3개를 먼저 말로 정의("head is the next write, count disambiguates full from empty") → push → pop → SIZE=3으로 손 시뮬레이션(push 3, pop 1, push 1) → 테스트

## 2. Python ↔ C 대응표 — 둘 다 아는 사람처럼 말하기

| 주제 | Python | C ([onsitePrep ring_buffer.c](../../onsitePrep/site/code/ring_buffer.c.html)) |
|---|---|---|
| 저장소 | `[None] * cap` (참조 배열) / 바이트면 `bytearray(cap)` | `uint8_t buf[RB_SIZE]` 정적 할당 |
| 인덱스 | `% capacity`, int는 무한 정밀도 | free-running `uint32_t`, `& MASK` (2의 거듭제곱) |
| full/empty | `count` | `head - tail` (unsigned wrap 덕분에 2^32 넘어도 정확) |
| 용량 사용 | capacity 전부 | 전부 (한 칸 비우기 방식은 `SIZE-1`) |
| 실패 표현 | 예외(`IndexError`) / `False` | `bool` 반환 + out 포인터 |
| 동시성 | `Lock` + `Condition` ([문제 05](../python/problems/05_blocking_ring.md)), 실무는 `queue.Queue` | SPSC: 단일 writer + C11 acquire/release ([ring_buffer_spsc.c](../../onsitePrep/site/code/ring_buffer_spsc.c.html)) |
| 대기 | `Condition.wait(timeout)` | ISR은 대기 불가 → RTOS 세마포어 / task notification |
| bulk | 슬라이스 두 번 (`buf[i:] + buf[:j]`) | `memcpy` 두 번 (wrap 전후) |
| 덮어쓰기 | `deque(maxlen=n)`가 이미 그 동작 | [ring_buffer_overwrite.c](../../onsitePrep/site/code/ring_buffer_overwrite.c.html) — SPSC에서 producer가 tail을 건드리면 안 되는 함정 |
| 메시지 단위 | 객체를 그대로 넣음 | 고정 크기 struct + `memcpy` ([ring_buffer_generic.c](../../onsitePrep/site/code/ring_buffer_generic.c.html)) |
| 테스트 | pytest + 모델 비교 | `main()` assert + ASan/UBSan, 또는 pytest+ctypes ([문제 07](../python/problems/07_ctypes_c_ring_buffer.md)) |

> "In C on an MCU I'd use free-running unsigned indices with a power-of-two mask, because it's cheap and the 2^32 wrap is harmless. In Python there's no overflow, so a count is simpler and clearer. Same invariant either way: zero ≤ count ≤ capacity."

## 3. C로 써 달라고 하면 — 5분 안에 이 모양

```c
#define RB_SIZE 16u                       /* power of 2 */
#define RB_MASK (RB_SIZE - 1u)
typedef struct { uint8_t buf[RB_SIZE]; uint32_t head, tail; } rb_t;

static uint32_t rb_count(const rb_t *rb) { return rb->head - rb->tail; }

bool rb_push(rb_t *rb, uint8_t b) {
    if (rb_count(rb) == RB_SIZE) return false;      /* drop-new */
    rb->buf[rb->head & RB_MASK] = b;
    rb->head++;                                     /* publish after the data write */
    return true;
}

bool rb_pop(rb_t *rb, uint8_t *out) {
    if (rb_count(rb) == 0) return false;
    *out = rb->buf[rb->tail & RB_MASK];
    rb->tail++;
    return true;
}
```

- 이후 흐름(ISR-safe, `volatile`이 부족한 이유, DMA, 다중 producer)은 [onsitePrep N05](../../onsitePrep/site/notes/2026-10-01_N05_ring_buffer_onsite.html) 1~8절 그대로
- 더 깊게: [링버퍼 완전정복 7레벨 42문제](../../research/ringbuffer_research/html/index.html) · [Neros C 연습 01 ring/logging](../../practice/html/index.html)

## 4. Python 쪽 꼬리 질문과 영어 답

**Q. Why not just use `collections.deque(maxlen=n)`?**

> "In production I would — it's implemented in C and it's exactly overwrite-oldest. I wrote it out to show the mechanics, and because I wanted drop-new as an option plus a drop counter, which deque doesn't provide."

**Q. Is your ring buffer thread-safe?**

> "No — `count += 1` is several bytecodes, so the GIL doesn't make it atomic, and there's no way to wait. For one producer and one consumer I'd add a lock with two conditions, not-empty and not-full, or just use `queue.Queue`, which is that exact design."

**Q. What's the complexity?**

> "Push, pop and peek are O(1). Memory is fixed at capacity. Iteration is O(n). No allocation after construction, which is the property that matters on an MCU."

**Q. Why set the popped slot to None?**

> "So the buffer doesn't keep a reference to a large object after it's been consumed — otherwise it can't be garbage-collected until the slot is overwritten."

**Q. How would you store bytes efficiently, for a serial stream?**

> "A `bytearray` of fixed size, and read with `memoryview` slices to avoid copies. For two regions across the wrap, return both slices, like the C bulk read with two memcpys."

**Q. Why would anyone want a power-of-two size in Python?**

> "In Python it barely matters — modulo is fine. In C it lets me replace modulo with a mask and makes the unsigned index wrap line up with the buffer wrap. I'd mention it, but I wouldn't force it in Python."

**Q. Generic over type?**

> "In Python it already is. If you want type hints, `class RingBuffer(Generic[T])` with `push(self, item: T)`. In C it's either a fixed-size element with memcpy or a macro-generated type."

**Q. How would you test it?**

> "Boundaries first — empty, one, exactly full, one past full, many wraparounds — then both full policies, then a randomized test against deque as a reference model with a fixed seed." → [문제 02](../python/problems/02_pytest_ring_buffer.md)

**Q. Bytes arrive from a UART in random chunks. Now what?**

> "The ring becomes the receive buffer and a framer pulls complete frames out: find sync, wait for length, wait for the body, check CRC, and on a bad CRC drop one byte and resync." → [문제 06](../python/problems/06_stream_framer.md)

## 5. 연결 — Don 경험 (레쥬메 범위)

- Solidigm/SK hynix FW: production FW의 **error reporting/handling scheme**, NVMe **telemetry** 디버그 기능 → 로그·텔레메트리 링(덮어쓰기 정책, 손실 카운터)을 설계할 이유를 실무로 설명. 구체 구조는 (Don: 실제로 쓴 버퍼 구조 — 레쥬메에 없음, 지어내지 말 것)
- SK hynix 챔버 플랫폼: **UART 시퀀스** 자동화 → 시리얼 스트림을 받아 파싱하는 PC 쪽 경험 (Don: 당시 수신 버퍼·파서 방식)

## 체크

- [ ] 1절 Python 버전을 빈 파일에 12분 안에 → `python3 python/run.py 01`
- [ ] 3절 C 버전을 종이에 5분 안에
- [ ] 2절 표를 보고 "Python에선 count, C에선 free-running index인 이유"를 30초 영어로
- [ ] 4절 답 9개 중 deque · thread-safe · test 세 개는 외울 정도로
- [ ] [문제 05](../python/problems/05_blocking_ring.md) Condition 버전을 15분 안에
