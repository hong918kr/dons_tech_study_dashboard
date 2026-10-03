"""01 · Python ring buffer 구현 — starter

problems/01_ring_buffer.md 를 읽고 TODO 를 채운다. 채점: python3 python/run.py 01
목표 시간 15분. 말하면서: "head is the next write slot, count tells full from empty".
"""
import pytest


class RingBuffer:
    def __init__(self, capacity, overwrite=False):
        # TODO: capacity 검증(양의 int 아니면 ValueError), 저장소, head, count, dropped
        raise NotImplementedError

    def __len__(self):
        raise NotImplementedError

    def empty(self):
        raise NotImplementedError

    def full(self):
        raise NotImplementedError

    def push(self, item):
        """가득 찼을 때: overwrite=False → False, True → 가장 오래된 것을 버리고 dropped += 1."""
        raise NotImplementedError

    def pop(self):
        """가장 오래된 것. 비었으면 IndexError."""
        raise NotImplementedError

    def peek(self):
        raise NotImplementedError

    def clear(self):
        raise NotImplementedError

    def __iter__(self):
        """오래된 → 최신, 소비하지 않음."""
        raise NotImplementedError


# ------------------------------------------------------------------ tests
def test_empty_on_create():
    rb = RingBuffer(3)
    assert len(rb) == 0 and rb.empty() and not rb.full()


def test_fifo_order():
    rb = RingBuffer(3)
    for x in "abc":
        assert rb.push(x)
    assert [rb.pop(), rb.pop(), rb.pop()] == ["a", "b", "c"]


def test_all_slots_usable():
    rb = RingBuffer(4)
    assert all(rb.push(i) for i in range(4))
    assert rb.full() and len(rb) == 4


def test_drop_new_when_full():
    rb = RingBuffer(2)
    rb.push(1), rb.push(2)
    assert rb.push(3) is False
    assert list(rb) == [1, 2] and rb.dropped == 0


def test_overwrite_oldest_when_full():
    rb = RingBuffer(3, overwrite=True)
    for i in range(5):
        assert rb.push(i)
    assert list(rb) == [2, 3, 4]
    assert rb.dropped == 2 and len(rb) == 3


def test_pop_and_peek_empty_raise():
    rb = RingBuffer(1)
    with pytest.raises(IndexError):
        rb.pop()
    with pytest.raises(IndexError):
        rb.peek()


def test_peek_does_not_consume():
    rb = RingBuffer(2)
    rb.push("x")
    assert rb.peek() == "x" and len(rb) == 1 and rb.pop() == "x"


@pytest.mark.parametrize("capacity", [1, 2, 3, 7, 8])
def test_wraparound_many_laps(capacity):
    rb, nxt_in, nxt_out = RingBuffer(capacity), 0, 0
    for lap in range(50):
        while rb.push(nxt_in):
            nxt_in += 1
        for _ in range(lap % capacity + 1):
            assert rb.pop() == nxt_out
            nxt_out += 1
    assert list(rb) == list(range(nxt_out, nxt_in))


def test_iter_order_after_wrap():
    rb = RingBuffer(3)
    for x in (1, 2, 3):
        rb.push(x)
    rb.pop()
    rb.push(4)                       # 저장소는 [4, 2, 3] 이지만 순서는 2, 3, 4
    assert list(rb) == [2, 3, 4]


def test_clear():
    rb = RingBuffer(2)
    rb.push(1), rb.push(2)
    rb.clear()
    assert len(rb) == 0 and rb.empty() and rb.push(9) and list(rb) == [9]


@pytest.mark.parametrize("bad", [0, -1, 2.5, "4"])
def test_bad_capacity(bad):
    with pytest.raises(ValueError):
        RingBuffer(bad)
