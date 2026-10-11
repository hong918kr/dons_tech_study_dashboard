# 6단계: 다 합치기 — ring buffer 를 fixture · parametrize · raises 로 테스트
import pytest


class RingBuffer:
    def __init__(self, capacity):
        self.buf = [None] * capacity
        self.cap = capacity
        self.head = self.tail = self.count = 0

    def push(self, x):
        if self.count == self.cap:
            return False
        self.buf[self.head] = x
        self.head = (self.head + 1) % self.cap
        self.count += 1
        return True

    def pop(self):
        if self.count == 0:
            raise IndexError("empty")
        x = self.buf[self.tail]
        self.tail = (self.tail + 1) % self.cap
        self.count -= 1
        return x


@pytest.fixture
def rb():                                     # 준비: 용량 3짜리 빈 버퍼
    return RingBuffer(3)


def test_fifo(rb):                            # 넣은 순서대로 나오나
    rb.push(1)
    rb.push(2)
    assert rb.pop() == 1 and rb.pop() == 2


def test_full_rejects(rb):                    # 가득 차면 False
    assert rb.push(1) and rb.push(2) and rb.push(3)
    assert rb.push(4) is False


def test_pop_empty_raises(rb):                # 비었을 때 pop → IndexError
    with pytest.raises(IndexError):
        rb.pop()


@pytest.mark.parametrize("laps", [1, 5, 10])
def test_wraparound(rb, laps):                # 여러 바퀴 돌아도 순서 유지
    rb.push(0)
    for i in range(1, laps + 1):
        rb.push(i)
        assert rb.pop() == i - 1
