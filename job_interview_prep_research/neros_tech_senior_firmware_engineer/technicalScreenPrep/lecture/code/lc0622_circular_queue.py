# --- solution ---
class MyCircularQueue:
    def __init__(self, k: int):
        self.q = [0] * k
        self.k = k
        self.head = 0          # 가장 오래된 칸 (Front)
        self.count = 0

    def enQueue(self, value: int) -> bool:
        if self.isFull():
            return False
        self.q[(self.head + self.count) % self.k] = value
        self.count += 1
        return True

    def deQueue(self) -> bool:
        if self.isEmpty():
            return False
        self.head = (self.head + 1) % self.k
        self.count -= 1
        return True

    def Front(self) -> int:
        return -1 if self.isEmpty() else self.q[self.head]

    def Rear(self) -> int:
        return -1 if self.isEmpty() else self.q[(self.head + self.count - 1) % self.k]

    def isEmpty(self) -> bool:
        return self.count == 0

    def isFull(self) -> bool:
        return self.count == self.k
# --- end ---


def test_lc622_example():
    q = MyCircularQueue(3)
    assert [q.enQueue(1), q.enQueue(2), q.enQueue(3), q.enQueue(4)] == [True, True, True, False]
    assert q.Rear() == 3 and q.isFull()
    assert q.deQueue() and q.enQueue(4) and q.Rear() == 4 and q.Front() == 2


def test_lc622_empty():
    q = MyCircularQueue(1)
    assert q.Front() == -1 and q.Rear() == -1 and not q.deQueue()
