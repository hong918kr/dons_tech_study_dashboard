"""모의 C · 고친 버전 (버그 3개 수정, 각 줄 옆 주석)."""
# --- fixed ---
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
        if self.count == self.cap:                      # 버그 1: cap - 1 → 한 칸을 못 씀
            return False
        self.buf[self.head] = x
        self.head = (self.head + 1) % self.cap
        self.count += 1
        return True

    def pop(self):
        if self.count == 0:
            raise IndexError("empty")
        x = self.buf[self.tail]
        self.tail = (self.tail + 1) % self.cap           # 버그 2: % 빠짐 → 한 바퀴 뒤 IndexError
        self.count -= 1
        return x

    def peek(self):
        if self.count == 0:
            raise IndexError("empty")
        return self.buf[self.tail]                       # 버그 3: head - 1 = 가장 '최신'
# --- end ---
