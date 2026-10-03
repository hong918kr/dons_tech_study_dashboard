"""05 · 스레드 안전 blocking ring buffer — starter

problems/05_blocking_ring.md 를 읽고 TODO 를 채운다. 채점: python3 python/run.py 05
힌트: Lock 하나 + Condition 두 개(not_empty, not_full). wait 는 while 안에서.
"""
import threading
import time

import pytest


class Closed(Exception):
    pass


class BlockingRing:
    def __init__(self, capacity):
        # TODO: 저장소, head, count, closed, lock, not_empty / not_full Condition
        raise NotImplementedError

    def __len__(self):
        raise NotImplementedError

    def put(self, item, timeout=None):
        """자리가 날 때까지 대기. timeout 초과 → TimeoutError, 닫혔으면 → Closed."""
        raise NotImplementedError

    def get(self, timeout=None):
        """항목이 올 때까지 대기. timeout 초과 → TimeoutError. 닫혔고 비었으면 → Closed."""
        raise NotImplementedError

    def close(self):
        """대기 중인 모두를 깨운다."""
        raise NotImplementedError


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
