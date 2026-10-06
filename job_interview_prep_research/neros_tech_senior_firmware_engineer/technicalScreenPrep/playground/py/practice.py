class RingBuffer:
    """고정 용량 FIFO. 가득 차면 overwrite=False → push 가 False (drop-new),
    overwrite=True → 가장 오래된 것을 버리고 dropped += 1."""

    def __init__(self, capacity, overwrite=False):
        if not isinstance(capacity, int) or capacity <= 0:
            raise ValueError("capacity must be a positive int")
        self.capacity = capacity
        self.overwrite = overwrite
        self._buf = [None] * capacity   # 미리 한 번만 할당 (C 의 static 배열처럼)
        self._head = 0                  # 다음에 쓸 칸
        self._count = 0                 # 들어 있는 개수 → full/empty 구분
        self.dropped = 0                # 덮어써서 잃은 개수 (손실을 보이게)

    def __len__(self):
        return self._count

    def empty(self):
        return self._count == 0

    def full(self):
        return self._count == self.capacity

    def _tail(self):                    # 가장 오래된 칸 = head 에서 count 만큼 뒤
        return (self._head - self._count) % self.capacity

    def push(self, item):
        if self.full():
            if not self.overwrite:
                return False            # drop-new: 호출자가 알 수 있게 False
            self._count -= 1            # 가장 오래된 것 하나를 버린 셈
            self.dropped += 1
        self._buf[self._head] = item
        self._head = (self._head + 1) % self.capacity
        self._count += 1
        return True

    def pop(self):
        if self.empty():
            raise IndexError("pop from empty RingBuffer")
        t = self._tail()
        item, self._buf[t] = self._buf[t], None   # 참조를 놓아 GC 가능하게
        self._count -= 1
        return item

    def peek(self):
        if self.empty():
            raise IndexError("peek from empty RingBuffer")
        return self._buf[self._tail()]

    def __iter__(self):                 # 오래된 → 최신, 소비하지 않음
        t = self._tail()
        for i in range(self._count):
            yield self._buf[(t + i) % self.capacity]