# --- solution ---
class MinStack:
    def __init__(self):
        self.stack = []                    # (값, 그 시점까지의 최솟값)

    def push(self, val: int) -> None:
        cur_min = min(val, self.stack[-1][1]) if self.stack else val
        self.stack.append((val, cur_min))

    def pop(self) -> None:
        self.stack.pop()

    def top(self) -> int:
        return self.stack[-1][0]

    def getMin(self) -> int:
        return self.stack[-1][1]
# --- end ---


def test_lc155():
    s = MinStack()
    s.push(-2); s.push(0); s.push(-3)
    assert s.getMin() == -3
    s.pop()
    assert s.top() == 0 and s.getMin() == -2
