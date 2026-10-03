# 🧵 05 · 스레드 안전 blocking ring buffer — Lock · Condition · timeout · close

> 문제 01의 꼬리 질문 "Now make it thread-safe — and the consumer should wait when it's empty." 에 대한 답. C의 "ISR producer → main loop consumer"를 Python 스레드로 옮긴 것이다. `queue.Queue`가 내부에서 하는 일을 직접 짠다.

## 왜 나오나

- ring buffer 라운드의 표준 확장 순서: 단일 스레드 → **동시성** → 정책 → 테스트 ([onsitePrep N05 1절](../../../onsitePrep/site/notes/2026-10-01_N05_ring_buffer_onsite.html))
- Full Stack 인터뷰어에게는 producer/consumer가 익숙한 주제다 (job queue, worker pool). 임베디드 쪽 답(SPSC lock-free)과 서버 쪽 답(Condition/Queue)을 **둘 다** 말할 수 있으면 강하다
- 테스트 자동화 실무: 시리얼 reader 스레드가 줄을 넣고, 테스트 스레드가 꺼낸다 → [FTE N03 8절](../../../firmwareTestEngineerPrep/site/notes/2026-09-28_N03_python_for_test_automation.html)

## 문제

> "Make the ring buffer safe for one producer thread and one consumer thread. `get` should block until there's data, with an optional timeout. `put` should block when it's full. And I need a way to shut it down cleanly."

- `put(item, timeout=None)`: 자리가 날 때까지 대기, timeout 초과 → `TimeoutError`, 닫혔으면 → `Closed`
- `get(timeout=None)`: 항목이 올 때까지 대기, timeout 초과 → `TimeoutError`, **닫혔고 비었으면** → `Closed` (남은 건 먼저 비운다)
- `close()`: 대기 중인 모든 스레드를 깨운다

## 엣지 케이스

- 빈 링에서 `get(timeout=0.05)` → 약 0.05초 후 `TimeoutError` (영원히 막히면 안 됨)
- `get`이 대기 중일 때 `put` → 즉시 깨어나 값을 받음
- capacity 1에서 5000개 → 손실 0, 순서 그대로
- `close()` → 대기 중인 consumer가 깨어나 `Closed`, 남은 항목은 먼저 꺼낼 수 있음

## 힌트

1. `Lock` 하나 + `Condition` 두 개 (`not_empty`, `not_full`)가 **같은 lock**을 공유. `queue.Queue` 소스와 같은 구조
2. `wait()`는 반드시 `while` 안에서: spurious wakeup, 그리고 깨어났을 때 조건이 다시 거짓일 수 있다
3. timeout은 **deadline**으로: `deadline = monotonic() + timeout`, 매번 남은 시간만큼 wait. 여러 번 깨어나도 총 대기가 늘지 않는다
4. `notify()`는 lock을 잡은 상태에서. put 후 `not_empty.notify()`, get 후 `not_full.notify()`, close는 둘 다 `notify_all()`

## 풀이 해설

- **GIL이 있는데 왜 lock?**: GIL은 바이트코드 한 개 단위로만 원자적이다. `count += 1`은 읽기·더하기·쓰기 여러 단계라 스레드 전환이 끼어들 수 있다. 그리고 **대기(blocking)**는 GIL과 무관하게 Condition이 필요하다
- **C SPSC와 비교**: C에서는 단일 writer 원칙 + acquire/release로 lock 없이 했다 ([ring_buffer_spsc.c](../../../onsitePrep/site/code/ring_buffer_spsc.c.html)). ISR은 기다릴 수 없으니 blocking은 RTOS 세마포어/task notification이 담당. Python에서는 lock이 싸고, 기다림이 필요하니 Condition
- **실무 답은 `queue.Queue(maxsize=n)`**: "In real code I'd use queue.Queue — it's this exact design. Python 3.13 added `Queue.shutdown()` for the close semantics." [추정: 3.13 shutdown은 문서 기준이며 로컬은 3.9]
- **테스트가 flaky하지 않게**: `time.sleep`으로 순서를 맞추는 테스트는 느린 CI에서 깨질 수 있다. 모든 `join`과 `get`에 timeout을 줘서 **멈추지 않고 실패**하게 한다

## 말하면서 풀기

- "One lock, two conditions sharing it — not_empty for consumers, not_full for producers. Every wait is in a while loop, because waking up doesn't guarantee the condition still holds."
- "Timeouts are computed from a deadline with monotonic time, so spurious wakeups don't extend the total wait."
- "For shutdown, close sets a flag and notifies everyone. Consumers drain what's left, then get Closed."

## 꼬리 질문

- "Multiple producers and consumers?" → 이 구현은 lock 덕분에 MPMC도 안전. C lock-free는 MPSC부터 어려워진다 (CAS + 슬롯별 ready 플래그, 또는 producer별 SPSC 링)
- "asyncio version?" → `asyncio.Queue`, 또는 `asyncio.Condition`. 스레드와 섞지 말 것 (`loop.call_soon_threadsafe`)
- "Producer is much faster than the consumer?" → backpressure(put이 막힘) vs drop(카운터) vs overwrite — 데이터 성격으로 결정 (명령은 drop 금지, 텔레메트리는 overwrite)
- "How do you test for race conditions?" → 많은 반복 + 작은 capacity로 경합 유도, 시퀀스 번호로 손실·순서 검증. C라면 ThreadSanitizer

## 파일

- [starter](../starters/05_blocking_ring.py) · [모범답안](../solutions/05_blocking_ring.py)
- 채점: `python3 python/run.py 05` · `python3 python/run.py 05 --sol`
