"""05 · 스레드 안전 blocking ring buffer (producer/consumer) — 모범답안

C 의 "ISR → main loop" SPSC 링을 Python 스레드로 옮긴 버전. 면접 꼬리 질문
"Now make it thread-safe. What if the consumer should wait?" 에 대한 답.

- Lock 하나 + Condition 두 개(not_empty, not_full). 둘 다 같은 lock 을 공유
- wait 는 반드시 while 루프 안에서 (spurious wakeup, 다른 소비자가 먼저 가져간 경우)
- timeout 은 deadline 으로 계산 (wait 가 여러 번 깨어나도 총 대기 시간이 늘지 않게)
- close(): 대기 중인 모두를 깨우고, 이후 put 은 실패, get 은 남은 것을 비운 뒤 Closed
"""
import threading
import time

import pytest


class Closed(Exception):
    pass


class BlockingRing:
    def __init__(self, capacity):
        if capacity <= 0:
            raise ValueError("capacity must be positive")
        self._buf = [None] * capacity
        self._cap, self._head, self._count = capacity, 0, 0
        self._closed = False
        self._lock = threading.Lock()
        self._not_empty = threading.Condition(self._lock)
        self._not_full = threading.Condition(self._lock)

    def __len__(self):
        with self._lock:
            return self._count

    def put(self, item, timeout=None):
        """자리가 날 때까지 기다린다. timeout 초 안에 못 넣으면 TimeoutError. 닫혔으면 Closed."""
        deadline = None if timeout is None else time.monotonic() + timeout
        with self._not_full:
            while self._count == self._cap and not self._closed:
                if not self._wait(self._not_full, deadline):
                    raise TimeoutError("put timed out")
            if self._closed:
                raise Closed("put on closed ring")
            self._buf[self._head] = item
            self._head = (self._head + 1) % self._cap
            self._count += 1
            self._not_empty.notify()

    def get(self, timeout=None):
        deadline = None if timeout is None else time.monotonic() + timeout
        with self._not_empty:
            while self._count == 0:
                if self._closed:
                    raise Closed("ring closed and drained")
                if not self._wait(self._not_empty, deadline):
                    raise TimeoutError("get timed out")
            t = (self._head - self._count) % self._cap
            item, self._buf[t] = self._buf[t], None
            self._count -= 1
            self._not_full.notify()
            return item

    def close(self):
        with self._lock:
            self._closed = True
            self._not_empty.notify_all()
            self._not_full.notify_all()

    @staticmethod
    def _wait(cond, deadline):
        if deadline is None:
            cond.wait()
            return True
        remaining = deadline - time.monotonic()
        return remaining > 0 and (cond.wait(remaining) or time.monotonic() < deadline)


# ------------------------------------------------------------------ tests
def test_fifo_single_thread():
    r = BlockingRing(3)
    for i in range(3):
        r.put(i)
    assert [r.get() for _ in range(3)] == [0, 1, 2]


def test_get_times_out_on_empty():
    r = BlockingRing(1)
    t0 = time.monotonic()
    with pytest.raises(TimeoutError):
        r.get(timeout=0.05)
    assert time.monotonic() - t0 >= 0.04


def test_put_times_out_on_full():
    r = BlockingRing(1)
    r.put("x")
    with pytest.raises(TimeoutError):
        r.put("y", timeout=0.05)


def test_blocked_get_wakes_on_put():
    r, got = BlockingRing(1), []
    th = threading.Thread(target=lambda: got.append(r.get(timeout=2)))
    th.start()
    time.sleep(0.05)
    r.put(42)
    th.join(timeout=2)
    assert got == [42]


@pytest.mark.parametrize("capacity", [1, 4, 64])
def test_producer_consumer_no_loss_no_reorder(capacity):
    r, n, out = BlockingRing(capacity), 5000, []

    def producer():
        for i in range(n):
            r.put(i, timeout=5)
        r.close()

    def consumer():
        while True:
            try:
                out.append(r.get(timeout=5))
            except Closed:
                return

    ths = [threading.Thread(target=producer), threading.Thread(target=consumer)]
    for t in ths:
        t.start()
    for t in ths:
        t.join(timeout=10)
    assert out == list(range(n))


def test_close_wakes_waiting_consumer():
    r, err = BlockingRing(1), []

    def consumer():
        try:
            r.get()
        except Closed as e:
            err.append(e)

    th = threading.Thread(target=consumer)
    th.start()
    time.sleep(0.05)
    r.close()
    th.join(timeout=2)
    assert not th.is_alive() and len(err) == 1


def test_close_lets_consumer_drain_then_raises():
    r = BlockingRing(4)
    r.put(1), r.put(2)
    r.close()
    assert r.get() == 1 and r.get() == 2
    with pytest.raises(Closed):
        r.get()
    with pytest.raises(Closed):
        r.put(3)
