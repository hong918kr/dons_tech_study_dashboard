# T4 last-N moving average + K-of-N alarm -- "battery reading every 10 ms, sometimes garbage"
import math
from collections import deque


def valid(x):
    return x is not None and not (isinstance(x, float) and math.isnan(x))


class Monitor:
    def __init__(self, n, k, low):
        if not 0 < k <= n:
            raise ValueError("need 0 < k <= n")
        self.n, self.k, self.low = n, k, low
        self.vals = deque()                         # valid values for the average
        self.total = 0.0
        self.flags = deque(maxlen=n)                # last n "bad?" flags for the alarm
        self.bad = 0
        self.invalid = 0

    def add(self, x):
        ok = valid(x)
        if not ok:
            self.invalid += 1
        else:
            self.vals.append(x)
            self.total += x
            if len(self.vals) > self.n:
                self.total -= self.vals.popleft()   # O(1): subtract what falls out
        is_bad = (not ok) or x < self.low           # NaN check FIRST
        if len(self.flags) == self.n and self.flags[0]:
            self.bad -= 1                           # oldest flag is about to fall out
        self.flags.append(is_bad)
        self.bad += is_bad
        avg = self.total / len(self.vals) if self.vals else None
        return avg, self.bad >= self.k


if __name__ == "__main__":
    m = Monitor(n=3, k=2, low=10.0)
    assert m.add(None) == (None, False) and m.invalid == 1     # no divide by zero
    assert m.add(12.0) == (12.0, False)                        # warm-up
    assert m.add(9.0)[1] is True                               # None + 9.0 = 2 bad of last 3
    avg, alarm = m.add(12.0)
    assert avg == 11.0 and alarm is False                      # None fell out -> 1 bad
    assert m.add(float("nan"))[1] is True                      # NaN counts as bad
    for _ in range(3):
        avg, alarm = m.add(12.0)
    assert avg == 12.0 and alarm is False                      # window fully replaced
    print("t4 OK")
