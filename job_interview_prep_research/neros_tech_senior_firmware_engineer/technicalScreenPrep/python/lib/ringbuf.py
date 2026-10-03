"""고정 용량 ring buffer — 문제 02(pytest로 테스트 쓰기)의 피검 코드. 문제 01 모범답안과 같은 스펙.

RingBuffer(capacity, overwrite=False)
  push(x) -> bool   가득 찼을 때: overwrite=False 면 False(버림), True 면 가장 오래된 것을 덮고 dropped += 1
  pop()             가장 오래된 것을 꺼냄. 비었으면 IndexError
  peek()            꺼내지 않고 보기. 비었으면 IndexError
  len(), empty(), full(), clear(), iter() (오래된 → 최신, 소비하지 않음)
  dropped           덮어써서 잃은 개수
"""


class RingBuffer:
    def __init__(self, capacity, overwrite=False):
        if not isinstance(capacity, int) or capacity <= 0:
            raise ValueError("capacity must be a positive int")
        self.capacity = capacity
        self.overwrite = overwrite
        self._buf = [None] * capacity
        self._head = 0          # 다음에 쓸 칸
        self._count = 0
        self.dropped = 0

    def __len__(self):
        return self._count

    def empty(self):
        return self._count == 0

    def full(self):
        return self._count == self.capacity

    def _tail(self):            # 가장 오래된 칸
        return (self._head - self._count) % self.capacity

    def push(self, item):
        if self.full():
            if not self.overwrite:
                return False
            self._count -= 1    # 가장 오래된 것 하나를 버린 셈
            self.dropped += 1
        self._buf[self._head] = item
        self._head = (self._head + 1) % self.capacity
        self._count += 1
        return True

    def pop(self):
        if self.empty():
            raise IndexError("pop from empty RingBuffer")
        t = self._tail()
        item, self._buf[t] = self._buf[t], None      # 참조를 놓아 GC 가능하게
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
