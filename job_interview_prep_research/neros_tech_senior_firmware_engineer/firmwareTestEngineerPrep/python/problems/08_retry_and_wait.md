# 🐍 08 · retry decorator · wait_until · Timer — 테스트 프레임워크 유틸 3종

> 지수 backoff retry decorator, 고정 sleep 대신 쓰는 wait_until polling, 경과 시간을 재는 Timer context manager. decorator·closure·functools.wraps·context manager를 한 번에 설명할 수 있게 만드는 문제.

## 왜 나오나

- JD: "**Build and enhance automated testing frameworks and tools** that facilitate automated testing", "Strong development skills with a scripting language (e.g. Python)"
- "Python 좀 하냐"를 확인하는 가장 흔한 질문이 **"decorator 설명해 봐 / retry decorator 짜 봐"** 다. context manager(`with`)도 세트로 나온다.
- HIL 테스트 코드에서 제일 흔한 악취는 `time.sleep(5)` 다. 느리고, 그래도 가끔 실패한다(flaky). `wait_until(condition, timeout)`으로 바꾸는 게 테스트 엔지니어의 기본기다.

## 문제

> "Write a `retry` decorator with exponential backoff: configurable max attempts, base delay, multiplier, an optional max delay, and a tuple of exception types that should be retried — everything else propagates immediately. Then write `wait_until(predicate, timeout, poll_interval)` that polls until the predicate returns something truthy and raises `TimeoutError` otherwise. Finally a `Timer` context manager. Make them testable without actually sleeping."

한국어로:

- `@retry(max_attempts=5, base_delay=0.1, factor=2, exceptions=(IOError,))` → 실패 시 0.1, 0.2, 0.4 … 초 쉬고 재시도, 다 실패하면 마지막 예외를 그대로 raise
- `wait_until(lambda: fc.state == "ARMED", timeout=2.0)` → 조건 만족 시 값 반환, 아니면 `TimeoutError`
- `with Timer() as t: ...` → `t.elapsed`
- 전부 `clock`/`sleep`을 인자로 주입받는다 → 테스트는 FakeClock으로 0초 만에 끝남

## 예시

```python
@retry(max_attempts=5, base_delay=0.1, exceptions=(IOError,))
def read_port():
    ...                       # 2번 IOError 후 성공 → sleep 0.1, 0.2 후 b"OK"

wait_until(lambda: link.lq, timeout=2.0)     # 0.5초 뒤 링크가 붙으면 그 값 반환
with Timer() as t:
    fc.reboot()
print(f"boot took {t.elapsed:.2f}s")
```

## 먼저 개념: decorator · closure · wraps

- **함수는 객체다.** 인자로 넘기고, 반환하고, 속성을 붙일 수 있다.
- **decorator** = 함수를 받아 함수를 돌려주는 함수. `@deco` 위에 쓰면 `f = deco(f)`와 같다.
- 인자가 있는 decorator(`@retry(max_attempts=5)`)는 **3겹**이다: `retry(...)`가 decorator를 반환 → decorator가 `fn`을 받아 wrapper를 반환 → wrapper가 실제 호출 시 실행.
- **closure**: wrapper는 바깥 함수의 `max_attempts`, `fn`을 기억한다. 바깥 함수가 끝나도 그 변수들은 wrapper의 `__closure__` 셀에 남아 있다.
- **`functools.wraps(fn)`**: 없으면 `read_port.__name__`이 `"wrapper"`가 된다. 그러면 로그, pytest 리포트, 스택 트레이스에 엉뚱한 이름이 찍힌다. `__doc__`, `__module__`, `__wrapped__`도 복사해 준다.
- **context manager** = `__enter__`/`__exit__`를 가진 객체. `__exit__`가 `True`를 반환하면 예외를 삼킨다 → 테스트 유틸에서는 거의 항상 `False`. 간단하게는 `@contextlib.contextmanager` + `yield`로도 만든다.

```python
from contextlib import contextmanager

@contextmanager
def powered(relay):          # HIL: 테스트 동안만 DUT 전원 켜기
    relay.on()
    try:
        yield
    finally:                 # 테스트가 실패해도 반드시 전원 끔
        relay.off()
```

## 엣지 케이스

- 재시도하면 안 되는 예외: `AssertionError`(제품 버그), `KeyboardInterrupt`. 그래서 기본값도 `Exception`까지만, 실제로는 `(IOError, TimeoutError)`처럼 좁혀서 쓴다.
- 마지막 시도 뒤에는 sleep 하지 않는다 (쓸데없이 테스트 시간 증가)
- `raise`만 쓰면 원래 traceback이 보존된다. `raise e`나 새 예외로 감싸면 디버깅이 어려워진다.
- `wait_until(timeout=0)`도 최소 한 번은 확인해야 한다
- deadline 직전: 남은 시간이 poll_interval보다 짧으면 남은 만큼만 자고 **한 번 더 확인** → 경계에서 놓치지 않게
- 시계는 `time.monotonic()`. `time.time()`은 NTP 보정으로 뒤로 갈 수 있다

## 힌트

1. retry: `for attempt in range(1, max_attempts + 1): try: return fn(...) except exceptions: if attempt == max_attempts: raise; sleep(delay); delay *= factor`
2. `except exceptions:`에 **튜플**을 그대로 넣을 수 있다. 목록에 없는 예외는 이 except에 안 걸리니 자동으로 전파된다.
3. wait_until: `deadline = clock() + timeout` → 루프에서 `value = predicate()` → truthy면 반환 → `remaining = deadline - clock()` → `<= 0`이면 raise → `sleep(min(poll, remaining))`.

## 풀이 해설

- **주입(dependency injection)**: `sleep=time.sleep`, `clock=time.monotonic`을 기본값으로 두고 테스트에서 FakeClock을 넘긴다. 이게 "테스트 가능한 테스트 프레임워크"의 핵심이다. mock.patch보다 명시적이다.
- **backoff 이유**: 디바이스가 USB 재열거 중이거나 부팅 중일 때 같은 간격으로 두드리면 계속 실패한다. 간격을 늘리면 회복할 시간을 준다. 실제 네트워크 코드에서는 jitter(랜덤)도 섞는다.
- **wait_until이 sleep보다 나은 이유**: 빠르면 빨리 끝나고(평균 테스트 시간 감소), 느리면 timeout까지 기다린다(flaky 감소). 실패 메시지에 "무엇을 기다렸는지"가 들어간다.
- **복잡도**: 무의미. 대신 "최악의 총 대기 시간 = base × (factor^(n−1) − 1)/(factor − 1)"을 말할 수 있으면 좋다.
- **흔한 실수**: `functools.wraps` 빠뜨림 / 모든 예외 재시도 / 마지막 실패 후 sleep / `except Exception as e: ... raise e` / `__exit__`에서 `True` 반환해 실패를 삼킴.

## 말하면서 풀기

- "A decorator is just a function that takes a function and returns a new one. Because this one takes arguments, it's three levels: the factory, the decorator, and the wrapper."
- "The wrapper is a closure — it keeps a reference to max_attempts and the original function."
- "I use functools.wraps so the test report and stack traces still show the real function name."
- "I only retry the exception types I pass in. Retrying an AssertionError would hide a real product bug."
- "I inject sleep and clock, so the unit tests for the framework run instantly with a fake clock."
- "In HIL tests I replace fixed sleeps with wait_until. A fixed sleep is either too long, which slows the suite, or too short, which makes it flaky."

## 꼬리 질문

- decorator를 클래스로 만들 수 있나? → `__call__`을 가진 클래스. 상태(호출 횟수)를 들고 있을 때 편하다.
- 메서드에 decorator를 붙이면 `self`는? → wrapper가 `*args`로 받으니 그대로 넘어간다.
- async 함수에 retry를 붙이려면? → `async def wrapper` + `await fn(...)` + `await asyncio.sleep(...)`. 동기 버전과 따로 둔다.
- `with` 블록 안에서 예외가 나면 `__exit__`에 뭐가 들어오나? → `(exc_type, exc_value, traceback)`, 정상 종료면 셋 다 None.
- pytest에서는? → `pytest-rerunfailures`의 `@pytest.mark.flaky(reruns=2)`가 있지만, 재시도 후 통과는 따로 집계해야 한다 (07번 문제와 연결).
- Don 경험 연결: 챔버 테스트 SDK에서 UART 기반 테스트 시퀀스를 자동화할 때 응답 대기·타임아웃을 어떻게 처리했는지. (Don: 실제 구현 방식 채우기)

## 파일

- [starter](../starters/08_retry_and_wait.py) · [모범답안](../solutions/08_retry_and_wait.py)
- 채점: `python3 python/run.py 08` (내 풀이) · `python3 python/run.py 08 --sol` (답안)
