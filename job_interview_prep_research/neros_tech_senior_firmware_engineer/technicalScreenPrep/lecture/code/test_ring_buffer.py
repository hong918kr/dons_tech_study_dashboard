# --- tests ---
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
# --- end ---


@pytest.mark.parametrize("chunk", [1, 3, 7])
def test_bytering_streams_in_order(chunk):
    br, data, out = ByteRing(8), bytes(range(200)), b""
    i = 0
    while len(out) < len(data):
        i += br.write(data[i:i + chunk])
        out += br.read(5)
    assert out == data
