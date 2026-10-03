# ⌨️ D4 · 10-06 (화) 드릴 — ring buffer · 스레드 · 상태 머신 · retry / C ring · atomics · 타이머

> 매일 아침 20분 + 저녁 20분. **정답을 보지 않고** 빈 파일에 친다. 브라우저: [드릴 트레이너](../site/drills.html) (Day 4 탭) · 에디터: `python3 drills/drill.py new 4` → `python3 drills/drill.py check 4`. 틀린 항목은 다음 날 다시.

## Python 10개

| ID | 주제 | 칠 것 |
|---|---|---|
| P4-01 | RingBuffer 핵심 (drop-new) | RingBuffer(capacity): push(x) → bool (가득 차면 False), pop() (비면 IndexError), len(). head + count 방식. |
| P4-02 | deque(maxlen) | 스트림의 마지막 n개만 리스트로 돌려주는 last_n(stream, n). deque 사용. |
| P4-03 | Lock | 여러 스레드에서 안전한 SafeCounter: increment(), value 속성. with self._lock. |
| P4-04 | queue.Queue producer/consumer | producer 스레드가 items를 넣고 끝에 None(sentinel), consumer가 모아서 돌려주는 run_pipeline(items). |
| P4-05 | Condition + while wait | 한 칸짜리 Mailbox: put(x), get(timeout) — 비어 있으면 기다리고, 시간 초과면 TimeoutError. |
| P4-06 | dict 상태 머신 | TRANSITIONS {("IDLE","arm"):"ARMED", ("ARMED","takeoff"):"FLYING", ("ARMED","disarm"):"IDLE", ("FLYING","land"):"ARMED"}. step(state, event), 없는 전이는 ValueError. |
| P4-07 | Enum | State(Enum) IDLE=0, ARMED=1, FLYING=2. 이름 문자열로 찾는 from_name(s) (대소문자 무시). |
| P4-08 | deadline polling (monotonic) | pred()가 True가 될 때까지 interval마다 확인, timeout 넘으면 False. wait_until(pred, timeout, interval=0.01). |
| P4-09 | retry 데코레이터 | 예외가 나면 최대 times번까지 다시 부르는 데코레이터 retry(times). functools.wraps 사용. |
| P4-10 | generator로 구분자 프레이밍 | 바이트 chunk 이터러블에서 0x00 구분자로 끝나는 프레임을 yield 하는 frames(chunks). 마지막 미완성은 버린다. |

### P4-01 · RingBuffer 핵심 (drop-new)

RingBuffer(capacity): push(x) → bool (가득 차면 False), pop() (비면 IndexError), len(). head + count 방식.

정답:

```python
class RingBuffer:
    def __init__(self, capacity):
        self._buf = [None] * capacity
        self._cap = capacity
        self._head = 0
        self._count = 0

    def __len__(self):
        return self._count

    def push(self, item):
        if self._count == self._cap:
            return False
        self._buf[self._head] = item
        self._head = (self._head + 1) % self._cap
        self._count += 1
        return True

    def pop(self):
        if self._count == 0:
            raise IndexError("empty")
        tail = (self._head - self._count) % self._cap
        item, self._buf[tail] = self._buf[tail], None
        self._count -= 1
        return item
```

### P4-02 · deque(maxlen)

스트림의 마지막 n개만 리스트로 돌려주는 last_n(stream, n). deque 사용.

정답:

```python
from collections import deque


def last_n(stream, n):
    return list(deque(stream, maxlen=n))
```

### P4-03 · Lock

여러 스레드에서 안전한 SafeCounter: increment(), value 속성. with self._lock.

정답:

```python
import threading


class SafeCounter:
    def __init__(self):
        self._lock = threading.Lock()
        self.value = 0

    def increment(self):
        with self._lock:
            self.value += 1
```

### P4-04 · queue.Queue producer/consumer

producer 스레드가 items를 넣고 끝에 None(sentinel), consumer가 모아서 돌려주는 run_pipeline(items).

정답:

```python
import queue
import threading


def run_pipeline(items):
    q = queue.Queue(maxsize=4)
    out = []

    def producer():
        for x in items:
            q.put(x)
        q.put(None)

    t = threading.Thread(target=producer)
    t.start()
    while True:
        x = q.get(timeout=2)
        if x is None:
            break
        out.append(x)
    t.join()
    return out
```

### P4-05 · Condition + while wait

한 칸짜리 Mailbox: put(x), get(timeout) — 비어 있으면 기다리고, 시간 초과면 TimeoutError.

정답:

```python
import threading


class Mailbox:
    def __init__(self):
        self._cond = threading.Condition()
        self._item = None
        self._full = False

    def put(self, x):
        with self._cond:
            self._item, self._full = x, True
            self._cond.notify()

    def get(self, timeout):
        with self._cond:
            if not self._cond.wait_for(lambda: self._full, timeout):
                raise TimeoutError("mailbox empty")
            self._full = False
            return self._item
```

### P4-06 · dict 상태 머신

TRANSITIONS {("IDLE","arm"):"ARMED", ("ARMED","takeoff"):"FLYING", ("ARMED","disarm"):"IDLE", ("FLYING","land"):"ARMED"}. step(state, event), 없는 전이는 ValueError.

정답:

```python
TRANSITIONS = {
    ("IDLE", "arm"): "ARMED",
    ("ARMED", "takeoff"): "FLYING",
    ("ARMED", "disarm"): "IDLE",
    ("FLYING", "land"): "ARMED",
}


def step(state, event):
    try:
        return TRANSITIONS[(state, event)]
    except KeyError:
        raise ValueError(f"{event} not allowed in {state}") from None
```

### P4-07 · Enum

State(Enum) IDLE=0, ARMED=1, FLYING=2. 이름 문자열로 찾는 from_name(s) (대소문자 무시).

정답:

```python
from enum import Enum


class State(Enum):
    IDLE = 0
    ARMED = 1
    FLYING = 2


def from_name(s):
    return State[s.upper()]
```

### P4-08 · deadline polling (monotonic)

pred()가 True가 될 때까지 interval마다 확인, timeout 넘으면 False. wait_until(pred, timeout, interval=0.01).

정답:

```python
import time


def wait_until(pred, timeout, interval=0.01):
    deadline = time.monotonic() + timeout
    while True:
        if pred():
            return True
        if time.monotonic() >= deadline:
            return False
        time.sleep(interval)
```

### P4-09 · retry 데코레이터

예외가 나면 최대 times번까지 다시 부르는 데코레이터 retry(times). functools.wraps 사용.

정답:

```python
import functools


def retry(times):
    def deco(fn):
        @functools.wraps(fn)
        def wrapper(*args, **kwargs):
            last = None
            for _ in range(times):
                try:
                    return fn(*args, **kwargs)
                except Exception as e:
                    last = e
            raise last
        return wrapper
    return deco
```

### P4-10 · generator로 구분자 프레이밍

바이트 chunk 이터러블에서 0x00 구분자로 끝나는 프레임을 yield 하는 frames(chunks). 마지막 미완성은 버린다.

정답:

```python
def frames(chunks):
    buf = bytearray()
    for chunk in chunks:
        buf += chunk
        while True:
            i = buf.find(0)
            if i < 0:
                break
            yield bytes(buf[:i])
            del buf[:i + 1]
```

## C 10개

| ID | 주제 | 칠 것 |
|---|---|---|
| C4-01 | ring buffer push / pop | RB_SIZE 16(2의 거듭제곱), free-running head/tail, rb_push / rb_pop (drop-new). |
| C4-02 | ring count / space / empty / full | rb_t(위와 같은 모양, given)에 대해 rb_count, rb_space, rb_empty, rb_full. |
| C4-03 | SPSC push (C11 atomics) | ISR producer용 spsc_push: tail은 acquire로 읽고, 데이터를 쓴 뒤 head를 release로 publish. |
| C4-04 | ISR 플래그 패턴 | ISR이 세우는 volatile 플래그 g_rx_ready와 main loop에서 읽고 내리는 take_rx_ready(void). (단일 코어, 플래그 하나) |
| C4-05 | switch 상태 머신 | state_t {IDLE, ARMED, FLYING}, event_t {EV_ARM, EV_DISARM, EV_TAKEOFF, EV_LAND}. 허용 안 되는 이벤트는 상태 유지. sm_step(s, e). |
| C4-06 | 테이블 기반 상태 머신 | transition_t {from, ev, to} 배열을 순회하는 sm_lookup(s, e). 없으면 s 그대로. (state_t/event_t는 given) |
| C4-07 | 디바운스 | debounce_t {stable, count}. raw가 stable과 다른 값으로 N(=3)번 연속이면 stable을 바꾼다. debounce(d, raw) → stable. |
| C4-08 | 이동 평균 (링 + 합) | avg_t {buf[8], idx, n, sum}. 새 값을 넣고 지금까지(최대 8개)의 평균을 돌려주는 avg_push(a, v). 합은 O(1)로 갱신. |
| C4-09 | tick wrap 안전한 timeout | 32비트 tick이 wrap 해도 맞는 timed_out(now, start, timeout). |
| C4-10 | bulk write (memcpy 두 번) | rb_t(given, RB_SIZE 16)에 최대 len바이트를 wrap 지점에서 두 번 memcpy로 쓰고 쓴 개수를 돌려주는 rb_write(rb, src, len). |

### C4-01 · ring buffer push / pop

RB_SIZE 16(2의 거듭제곱), free-running head/tail, rb_push / rb_pop (drop-new).

정답:

```c
#define RB_SIZE 16u
#define RB_MASK (RB_SIZE - 1u)

typedef struct {
    uint8_t  buf[RB_SIZE];
    uint32_t head, tail;
} rb_t;

bool rb_push(rb_t *rb, uint8_t b)
{
    if (rb->head - rb->tail == RB_SIZE)
        return false;
    rb->buf[rb->head & RB_MASK] = b;
    rb->head++;
    return true;
}

bool rb_pop(rb_t *rb, uint8_t *out)
{
    if (rb->head == rb->tail)
        return false;
    *out = rb->buf[rb->tail & RB_MASK];
    rb->tail++;
    return true;
}
```

### C4-02 · ring count / space / empty / full

rb_t(위와 같은 모양, given)에 대해 rb_count, rb_space, rb_empty, rb_full.

주어진 코드 (치지 않음):

```c
#define RB_SIZE 16u
typedef struct { uint8_t buf[RB_SIZE]; uint32_t head, tail; } rb_t;
```

정답:

```c
static inline uint32_t rb_count(const rb_t *rb) { return rb->head - rb->tail; }
static inline uint32_t rb_space(const rb_t *rb) { return RB_SIZE - rb_count(rb); }
static inline bool rb_empty(const rb_t *rb)     { return rb_count(rb) == 0; }
static inline bool rb_full(const rb_t *rb)      { return rb_count(rb) == RB_SIZE; }
```

### C4-03 · SPSC push (C11 atomics)

ISR producer용 spsc_push: tail은 acquire로 읽고, 데이터를 쓴 뒤 head를 release로 publish.

정답:

```c
#define Q_SIZE 8u

typedef struct {
    uint8_t buf[Q_SIZE];
    _Atomic uint32_t head, tail;
} spsc_t;

bool spsc_push(spsc_t *q, uint8_t b)
{
    uint32_t head = atomic_load_explicit(&q->head, memory_order_relaxed);
    uint32_t tail = atomic_load_explicit(&q->tail, memory_order_acquire);
    if (head - tail == Q_SIZE)
        return false;
    q->buf[head & (Q_SIZE - 1u)] = b;
    atomic_store_explicit(&q->head, head + 1, memory_order_release);
    return true;
}
```

### C4-04 · ISR 플래그 패턴

ISR이 세우는 volatile 플래그 g_rx_ready와 main loop에서 읽고 내리는 take_rx_ready(void). (단일 코어, 플래그 하나)

정답:

```c
static volatile bool g_rx_ready;

void uart_rx_isr(void)
{
    g_rx_ready = true;
}

bool take_rx_ready(void)
{
    if (!g_rx_ready)
        return false;
    g_rx_ready = false;
    return true;
}
```

### C4-05 · switch 상태 머신

state_t {IDLE, ARMED, FLYING}, event_t {EV_ARM, EV_DISARM, EV_TAKEOFF, EV_LAND}. 허용 안 되는 이벤트는 상태 유지. sm_step(s, e).

정답:

```c
typedef enum { IDLE, ARMED, FLYING } state_t;
typedef enum { EV_ARM, EV_DISARM, EV_TAKEOFF, EV_LAND } event_t;

state_t sm_step(state_t s, event_t e)
{
    switch (s) {
    case IDLE:   return e == EV_ARM ? ARMED : s;
    case ARMED:  return e == EV_TAKEOFF ? FLYING : e == EV_DISARM ? IDLE : s;
    case FLYING: return e == EV_LAND ? ARMED : s;
    }
    return s;
}
```

### C4-06 · 테이블 기반 상태 머신

transition_t {from, ev, to} 배열을 순회하는 sm_lookup(s, e). 없으면 s 그대로. (state_t/event_t는 given)

주어진 코드 (치지 않음):

```c
typedef enum { IDLE, ARMED, FLYING } state_t;
typedef enum { EV_ARM, EV_DISARM, EV_TAKEOFF, EV_LAND } event_t;
```

정답:

```c
typedef struct {
    state_t from;
    event_t ev;
    state_t to;
} transition_t;

static const transition_t TABLE[] = {
    {IDLE, EV_ARM, ARMED},
    {ARMED, EV_TAKEOFF, FLYING},
    {ARMED, EV_DISARM, IDLE},
    {FLYING, EV_LAND, ARMED},
};

state_t sm_lookup(state_t s, event_t e)
{
    for (size_t i = 0; i < sizeof TABLE / sizeof TABLE[0]; i++)
        if (TABLE[i].from == s && TABLE[i].ev == e)
            return TABLE[i].to;
    return s;
}
```

### C4-07 · 디바운스

debounce_t {stable, count}. raw가 stable과 다른 값으로 N(=3)번 연속이면 stable을 바꾼다. debounce(d, raw) → stable.

정답:

```c
#define DEBOUNCE_N 3

typedef struct {
    bool    stable;
    uint8_t count;
} debounce_t;

bool debounce(debounce_t *d, bool raw)
{
    if (raw == d->stable) {
        d->count = 0;
    } else if (++d->count >= DEBOUNCE_N) {
        d->stable = raw;
        d->count = 0;
    }
    return d->stable;
}
```

### C4-08 · 이동 평균 (링 + 합)

avg_t {buf[8], idx, n, sum}. 새 값을 넣고 지금까지(최대 8개)의 평균을 돌려주는 avg_push(a, v). 합은 O(1)로 갱신.

정답:

```c
#define AVG_N 8

typedef struct {
    int32_t buf[AVG_N];
    uint8_t idx, n;
    int32_t sum;
} avg_t;

int32_t avg_push(avg_t *a, int32_t v)
{
    if (a->n == AVG_N)
        a->sum -= a->buf[a->idx];
    else
        a->n++;
    a->buf[a->idx] = v;
    a->sum += v;
    a->idx = (uint8_t)((a->idx + 1) % AVG_N);
    return a->sum / a->n;
}
```

### C4-09 · tick wrap 안전한 timeout

32비트 tick이 wrap 해도 맞는 timed_out(now, start, timeout).

정답:

```c
bool timed_out(uint32_t now, uint32_t start, uint32_t timeout)
{
    return (uint32_t)(now - start) >= timeout;
}
```

### C4-10 · bulk write (memcpy 두 번)

rb_t(given, RB_SIZE 16)에 최대 len바이트를 wrap 지점에서 두 번 memcpy로 쓰고 쓴 개수를 돌려주는 rb_write(rb, src, len).

주어진 코드 (치지 않음):

```c
#define RB_SIZE 16u
#define RB_MASK (RB_SIZE - 1u)
typedef struct { uint8_t buf[RB_SIZE]; uint32_t head, tail; } rb_t;
```

정답:

```c
size_t rb_write(rb_t *rb, const uint8_t *src, size_t len)
{
    uint32_t space = RB_SIZE - (rb->head - rb->tail);
    uint32_t n = len < space ? (uint32_t)len : space;
    uint32_t idx = rb->head & RB_MASK;
    uint32_t first = RB_SIZE - idx;
    if (first > n)
        first = n;
    memcpy(&rb->buf[idx], src, first);
    memcpy(&rb->buf[0], src + first, n - first);
    rb->head += n;
    return n;
}
```

## 체크

- [ ] Python 10개를 정답 안 보고 → `python3 drills/drill.py check 4` 에서 ✓ 10
- [ ] C 10개를 정답 안 보고 → ✓ 10
- [ ] 틀린 것 ID를 적어 두고 다음 날 아침 첫 5분에 다시
