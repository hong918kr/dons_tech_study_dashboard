"""08 · retry decorator · wait_until · Timer — starter

HIL 테스트에서 매일 쓰는 세 가지 유틸:
  retry(...)    지수 backoff 로 재시도하는 decorator (재시도할 예외만 골라서)
  wait_until()  조건이 참이 될 때까지 polling, timeout 이면 TimeoutError  ← time.sleep(5) 대신
  Timer         with 블록 경과 시간 측정 context manager
시간 의존 코드는 clock/sleep 을 주입받아서 테스트가 즉시 끝난다.

  python3 python/run.py 08          # 내 풀이 채점
  python3 python/run.py 08 --sol    # 모범답안
"""
import functools
import time
from typing import Callable, Optional, Tuple, Type


def retry(max_attempts: int = 3, base_delay: float = 0.1, factor: float = 2.0,
          max_delay: Optional[float] = None,
          exceptions: Tuple[Type[BaseException], ...] = (Exception,),
          sleep: Callable[[float], None] = time.sleep):
    """decorator factory.

    attempt 1 실패 → sleep(base) → attempt 2 실패 → sleep(base*factor) → ... → 마지막 예외를 그대로 raise.
    - sleep 값은 max_delay 로 상한 (None 이면 상한 없음). 마지막 실패 뒤에는 sleep 하지 않는다
    - exceptions 에 없는 예외는 재시도 없이 즉시 전파
    - 원래 함수의 __name__/__doc__ 를 보존하고, wrapper.max_attempts 속성을 붙인다
    """
    # TODO: def decorator(fn): → @functools.wraps(fn) def wrapper(*args, **kwargs): → return wrapper → return decorator
    raise NotImplementedError


def wait_until(predicate: Callable[[], object], timeout: float, poll_interval: float = 0.1,
               clock: Callable[[], float] = time.monotonic,
               sleep: Callable[[float], None] = time.sleep, message: str = ""):
    """predicate() 가 truthy 를 돌려주면 그 값을 반환. timeout 이 지나면 TimeoutError(메시지에 message 포함).
    - 최소 한 번은 확인한다 (timeout=0 이어도)
    - 남은 시간이 poll_interval 보다 짧으면 남은 시간만큼만 sleep 하고 마지막으로 한 번 더 확인
    """
    # TODO: deadline = clock() + timeout → while True: 확인 → 남은 시간 계산 → sleep(min(...))
    raise NotImplementedError


class Timer:
    """with Timer() as t: ...  →  t.elapsed (초). 예외가 나도 elapsed 는 기록되고, 예외는 삼키지 않는다."""

    def __init__(self, clock: Callable[[], float] = time.perf_counter):
        self.clock = clock
        self.start = None
        self.elapsed = None

    def __enter__(self):
        # TODO: 시작 시각 저장, self 반환
        raise NotImplementedError

    def __exit__(self, exc_type, exc, tb):
        # TODO: elapsed 계산. return False (True 를 돌려주면 예외가 사라진다)
        raise NotImplementedError


# ------------------------------------------------------------------ tests
class FakeClock:
    """sleep 하면 시간이 그만큼 흐르는 가짜 시계. 테스트는 0초 만에 끝난다."""

    def __init__(self):
        self.now = 0.0
        self.sleeps = []

    def __call__(self):
        return self.now

    def sleep(self, s):
        self.sleeps.append(round(s, 6))
        self.now += s


def test_retry_succeeds_on_third_attempt_with_backoff():
    clk = FakeClock()
    calls = {"n": 0}

    @retry(max_attempts=5, base_delay=0.1, factor=2, exceptions=(IOError,), sleep=clk.sleep)
    def read_port():
        calls["n"] += 1
        if calls["n"] < 3:
            raise IOError("device busy")
        return b"OK"

    assert read_port() == b"OK"
    assert calls["n"] == 3
    assert clk.sleeps == [0.1, 0.2]


def test_retry_exhausted_reraises_last():
    clk = FakeClock()

    @retry(max_attempts=3, base_delay=1, factor=3, max_delay=2, sleep=clk.sleep)
    def always_fail():
        raise TimeoutError("no ack")

    try:
        always_fail()
        assert False, "should raise"
    except TimeoutError as e:
        assert str(e) == "no ack"
    assert clk.sleeps == [1, 2]                  # 3 이 max_delay 2 로 잘림, 마지막 실패 뒤엔 sleep 없음


def test_retry_does_not_retry_other_exceptions():
    clk = FakeClock()
    calls = {"n": 0}

    @retry(max_attempts=5, exceptions=(IOError,), sleep=clk.sleep)
    def check():
        calls["n"] += 1
        raise AssertionError("motor 3 output 0")

    try:
        check()
    except AssertionError:
        pass
    assert calls["n"] == 1 and clk.sleeps == []


def test_retry_preserves_metadata():
    @retry(sleep=lambda s: None)
    def arm_drone():
        """Send ARM and wait for ack."""
        return 1

    assert arm_drone.__name__ == "arm_drone"
    assert arm_drone.__doc__ == "Send ARM and wait for ack."
    assert arm_drone.max_attempts == 3


def test_wait_until_returns_value():
    clk = FakeClock()
    state = {"link": None}

    def link_up():
        if clk.now >= 0.5:
            state["link"] = "LQ=100"
        return state["link"]

    got = wait_until(link_up, timeout=2.0, poll_interval=0.1, clock=clk, sleep=clk.sleep)
    assert got == "LQ=100"
    assert 0.5 <= clk.now < 0.7


def test_wait_until_timeout_and_boundary():
    clk = FakeClock()
    try:
        wait_until(lambda: False, timeout=1.0, poll_interval=0.3, clock=clk, sleep=clk.sleep,
                   message="waiting for ARMED")
        assert False, "should time out"
    except TimeoutError as e:
        assert "ARMED" in str(e)
    assert abs(clk.now - 1.0) < 1e-9             # 0.3,0.3,0.3,0.1 → deadline 에 딱 맞춰 마지막 확인
    assert clk.sleeps[-1] == 0.1


def test_wait_until_checks_at_least_once():
    clk = FakeClock()
    assert wait_until(lambda: 42, timeout=0, clock=clk, sleep=clk.sleep) == 42


def test_timer_records_even_on_exception():
    clk = FakeClock()
    with Timer(clock=clk) as t:
        clk.sleep(1.5)
    assert t.elapsed == 1.5
    t2 = Timer(clock=clk)
    try:
        with t2:
            clk.sleep(0.25)
            raise RuntimeError("boom")
    except RuntimeError:
        pass
    assert t2.elapsed == 0.25


if __name__ == "__main__":
    for _n, _f in list(globals().items()):
        if _n.startswith("test_") and callable(_f):
            _f()
            print("PASS", _n)
