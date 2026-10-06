# --- solution ---
from collections import deque


class MovingAverage:
    def __init__(self, size: int):
        self.window = deque()
        self.size = size
        self.total = 0

    def next(self, val: int) -> float:
        self.window.append(val)
        self.total += val
        if len(self.window) > self.size:
            self.total -= self.window.popleft()   # 합을 O(1)로 갱신
        return self.total / len(self.window)
# --- end ---


def test_lc346():
    m = MovingAverage(3)
    assert [m.next(1), m.next(10), round(m.next(3), 5), m.next(5)] == [1.0, 5.5, 4.66667, 6.0]
