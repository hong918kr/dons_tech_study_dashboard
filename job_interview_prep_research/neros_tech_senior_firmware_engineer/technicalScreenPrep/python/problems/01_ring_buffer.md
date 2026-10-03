# 🔁 01 · Python ring buffer — 고정 용량 · drop-new / overwrite · iter

> 리크루터가 주제로 **ring buffer**를 직접 짚었다. Python이든 C든 고를 수 있다고 했으니, 1시간짜리 스크린에서는 **Python으로 15분 안에 깔끔하게 짜고, C 버전과 동시성으로 꼬리 질문을 받는** 흐름이 가장 가능성이 높다 [추정]. 이 문제는 그 첫 15분이다.

## 왜 나오나

- 리크루터 안내 주제: pytest · **ring buffer** · Python/C · system design [확인됨 2026-10-02 리크루터]
- 온사이트 1:1에도 "ring buffer 구현"이 예고돼 있다 [확인됨 2026-09-16 Devin] → 스크린에서 미리 보는 셈일 수 있다 [추정]
- Full Stack 인터뷰어 [추정] 관점에서는 **임베디드 지식**보다 **API 설계 · 경계 조건 · 테스트를 스스로 꺼내는지**를 볼 가능성이 크다 → 구현 직후 테스트를 직접 쓰는 것이 차별점

## 문제

> "Implement a fixed-capacity ring buffer in Python. Push, pop, peek, length. When it's full, either reject the new item or overwrite the oldest — make that configurable. And let me iterate over the contents without consuming them."

- `RingBuffer(capacity, overwrite=False)` — capacity가 양의 `int`가 아니면 `ValueError`
- `push(x) -> bool`: 가득 찼을 때 `overwrite=False`면 `False`(버림), `True`면 가장 오래된 것을 덮고 `dropped += 1`, `True` 반환
- `pop()` / `peek()`: 비었으면 `IndexError`
- `len()`, `empty()`, `full()`, `clear()`, `iter()` (오래된 → 최신, 소비하지 않음)

## 예시

```python
rb = RingBuffer(3, overwrite=True)
for i in range(5):
    rb.push(i)
list(rb)      # [2, 3, 4]
rb.dropped    # 2
rb.pop()      # 2
```

## 엣지 케이스

- capacity 1 — head와 tail이 항상 같은 칸
- 정확히 가득 참 — **capacity개를 전부 쓸 수 있어야** 한다 (C의 "한 칸 비우기" 방식과 다르다는 걸 말로 짚기)
- 여러 바퀴 wraparound 뒤의 순서
- overwrite 모드에서 pop 순서 — 가장 오래 **남은** 것부터
- `clear()` 뒤 재사용

## 힌트

1. 저장소는 `[None] * capacity`, 상태는 `head`(다음에 쓸 칸)와 `count`. tail은 `(head - count) % capacity`로 계산 → full/empty 구분이 `count` 하나로 끝난다
2. overwrite: 가득 찼으면 `count -= 1; dropped += 1` 한 뒤 평소처럼 쓴다. 덮어쓰는 칸이 정확히 가장 오래된 칸이다
3. `__iter__`는 generator로: `for i in range(count): yield buf[(tail + i) % capacity]`

## 풀이 해설

- **왜 `collections.deque(maxlen=n)`를 안 쓰나**: 면접관이 "deque로 되지 않나?"라고 물을 수 있다. 답: "In production Python I'd use `deque(maxlen=n)` — it's exactly overwrite-oldest. Here I'm showing the mechanics, and I need drop-new plus a drop counter, which deque doesn't give me." 이 말을 **먼저** 해 두면 실무 감각 점수
- **count vs free-running index**: C에서는 head/tail을 마스킹 없이 증가시키고 `head - tail`로 개수를 구했다 ([onsitePrep ring_buffer.c](../../../onsitePrep/site/code/ring_buffer.c.html)). Python int는 무한 정밀도라 2^32 wrap이 없으니 `count`가 더 읽기 쉽다. 두 방식의 차이를 말할 수 있으면 Python/C 둘 다 아는 신호
- **pop 후 `None` 대입**: 큰 객체(프레임 bytes 등)의 참조를 놓아 메모리를 빨리 돌려준다 — C에는 없는 Python 고유의 디테일
- **흔한 실수**: `full()`을 `count == capacity - 1`로 쓰기(한 칸 비우기와 섞임), iter를 저장 배열 순서로 돌기, overwrite에서 head만 옮기고 count를 안 고침

## 말하면서 풀기

- "Let me pin down the policy first: when it's full, do we drop the new item or overwrite the oldest? I'll make it a constructor flag and keep a drop counter so loss is visible."
- "State is a fixed list, a head index for the next write, and a count. Tail is derived, so full versus empty is unambiguous."
- "Before calling it done, I'd like to write a few tests: empty, exactly full, many wraparounds, and overwrite order."

## 꼬리 질문

- "Make it thread-safe." → [문제 05 blocking ring](05_blocking_ring.md)
- "Now write it in C for an ISR producer." → [onsitePrep N05](../../../onsitePrep/site/notes/2026-10-01_N05_ring_buffer_onsite.html) 4.2절 SPSC + [ring_buffer_spsc.c](../../../onsitePrep/site/code/ring_buffer_spsc.c.html)
- "How would you test someone else's ring buffer?" → [문제 02](02_pytest_ring_buffer.md)
- "Bytes from a UART come in random chunks — how do you get frames out?" → [문제 06](06_stream_framer.md)
- "Complexity?" → push/pop/peek O(1), 메모리 O(capacity) 고정, iter O(n)

## 파일

- [starter](../starters/01_ring_buffer.py) · [모범답안](../solutions/01_ring_buffer.py)
- 채점: `python3 python/run.py 01` · `python3 python/run.py 01 --sol`
