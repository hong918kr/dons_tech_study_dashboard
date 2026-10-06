"""모의 C · 동료가 쓴 ring buffer — 버그가 3개 있다. 코드를 고치기 전에 테스트로 먼저 드러낸다.
스펙: RingBuffer(capacity). push(x) → bool (가득 차면 False). pop() → 가장 오래된 값 (비었으면 IndexError).
peek() → 가장 오래된 값 (꺼내지 않음). len().
"""
# --- given ---
class RingBuffer:
    def __init__(self, capacity):
        self.buf = [None] * capacity
        self.cap = capacity
        self.head = 0
        self.tail = 0
        self.count = 0

    def __len__(self):
        return self.count

    def push(self, x):
        if self.count == self.cap - 1:
            return False
        self.buf[self.head] = x
        self.head = (self.head + 1) % self.cap
        self.count += 1
        return True

    def pop(self):
        if self.count == 0:
            raise IndexError("empty")
        x = self.buf[self.tail]
        self.tail = self.tail + 1
        self.count -= 1
        return x

    def peek(self):
        if self.count == 0:
            raise IndexError("empty")
        return self.buf[self.head - 1]
# --- end ---
