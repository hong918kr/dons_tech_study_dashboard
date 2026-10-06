"""07 · Ring buffer — head · tail · count
prompt: RingBuffer(capacity): push(x) → bool (가득 차면 False), pop() → 가장 오래된 값 (비었으면 IndexError), peek(), len(), is_full().
note: head = 다음에 쓸 칸, tail = 다음에 읽을 칸, count = 들어 있는 개수. head == tail 이 '빈 것'인지 '가득 찬 것'인지 count 가 구분해 준다
note: 다른 방법 — count 없이 '한 칸 비우기' (용량 하나 손해), C 에서는 free-running index (C 문제 06)
note: 테스트 포인트 — 정확히 가득 찼을 때, 한 바퀴 돈 뒤(wrap)의 순서, 빈 상태에서 pop
"""
# --- solution ---
class RingBuffer:
    def __init__(self, capacity):
        self.buf = [None] * capacity
        self.cap = capacity
        self.head = 0                  # 다음에 쓸 칸
        self.tail = 0                  # 다음에 읽을 칸
        self.count = 0

    def __len__(self):
        return self.count

    def is_full(self):
        return self.count == self.cap

    def push(self, x):
        if self.is_full():
            return False
        self.buf[self.head] = x
        self.head = (self.head + 1) % self.cap
        self.count += 1
        return True

    def peek(self):
        if self.count == 0:
            raise IndexError("empty")
        return self.buf[self.tail]

    def pop(self):
        x = self.peek()
        self.buf[self.tail] = None
        self.tail = (self.tail + 1) % self.cap
        self.count -= 1
        return x


def test_ring_buffer():
    r = RingBuffer(3)
    assert r.push(1) and r.push(2) and r.push(3) and not r.push(4) and r.is_full()
    assert r.pop() == 1 and r.push(4)                 # wrap: 4 는 index 0 으로
    assert [r.pop(), r.pop(), r.pop()] == [2, 3, 4] and len(r) == 0


def test_pop_empty_raises():
    try:
        RingBuffer(1).pop()
    except IndexError:
        return
    raise AssertionError("pop on empty must raise")


if __name__ == "__main__":
    test_ring_buffer()
    test_pop_empty_raises()
    print("PASS")
# --- end ---
