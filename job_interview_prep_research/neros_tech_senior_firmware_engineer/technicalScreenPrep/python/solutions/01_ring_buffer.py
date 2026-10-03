"""01 · Python ring buffer 구현 — 모범답안

스펙은 problems/01_ring_buffer.md. 핵심 결정:
- 저장소는 고정 길이 list, head(다음 쓸 칸) + count. tail 은 (head - count) % capacity 로 계산
  → full/empty 구분이 count 하나로 끝난다 (C 의 free-running index 와 같은 생각)
- overwrite=True 면 가장 오래된 것을 버리고 dropped 를 올린다. False 면 push 가 False (drop-new)
- pop 한 칸은 None 으로 비워서 큰 객체의 참조를 놓는다
"""
import pytest


class RingBuffer:
    def __init__(self, capacity, overwrite=False):
        if not isinstance(capacity, int) or capacity <= 0:
            raise ValueError("capacity must be a positive int")
        self.capacity = capacity
        self.overwrite = overwrite
        self._buf = [None] * capacity
        self._head = 0
        self._count = 0
        self.dropped = 0

    def __len__(self):
        return self._count

    def empty(self):
        return self._count == 0

    def full(self):
        return self._count == self.capacity

    def _tail(self):
        return (self._head - self._count) % self.capacity

    def push(self, item):
        if self.full():
            if not self.overwrite:
                return False
            self._count -= 1
            self.dropped += 1
        self._buf[self._head] = item
        self._head = (self._head + 1) % self.capacity
        self._count += 1
        return True

    def pop(self):
        if self.empty():
            raise IndexError("pop from empty RingBuffer")
        t = self._tail()
        item, self._buf[t] = self._buf[t], None
        self._count -= 1
        return item

    def peek(self):
        if self.empty():
            raise IndexError("peek from empty RingBuffer")
        return self._buf[self._tail()]

    def clear(self):
        self._buf = [None] * self.capacity
        self._head = self._count = 0

    def __iter__(self):
        t = self._tail()
        for i in range(self._count):
            yield self._buf[(t + i) % self.capacity]


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
