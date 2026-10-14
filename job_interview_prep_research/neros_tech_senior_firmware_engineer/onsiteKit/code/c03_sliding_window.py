"""C03 sensor stream sliding window: O(1) moving average with warm-up + bad samples, and a K-of-N alarm.

Interviewer wording (no hint): "We get a battery-voltage reading every 10 ms. Give me
a smoothed value over the last N readings, and raise an alarm if at least K of the
last N readings were below a threshold. Sometimes the sensor returns garbage."

Run: python3 c03_sliding_window.py
"""
import math
from collections import deque


def _valid(x):
    return x is not None and not (isinstance(x, float) and math.isnan(x))


class MovingAverage:
    """Average of the last n valid samples. Before n samples: average of what we have."""

    def __init__(self, n):
        if n <= 0:
            raise ValueError("n must be > 0")
        self.n = n
        self.win = deque()
        self.total = 0.0
        self.invalid = 0

    def add(self, x):
        """Return the current average (None if no valid sample yet)."""
        # >>> solution
        if not _valid(x):
            self.invalid += 1                  # skip it, but count it -- a test wants to see this
        else:
            self.win.append(x)
            self.total += x
            if len(self.win) > self.n:
                self.total -= self.win.popleft()
        if not self.win:
            return None
        return self.total / len(self.win)
        # <<< solution


class KofNAlarm:
    """True when at least k of the last n samples were below `low` (invalid counts as bad)."""

    def __init__(self, n, k, low):
        if not 0 < k <= n:
            raise ValueError("need 0 < k <= n")
        self.n, self.k, self.low = n, k, low
        self.win = deque(maxlen=n)             # maxlen drops the oldest for us
        self.bad = 0

    def add(self, x):
        # >>> solution
        is_bad = (not _valid(x)) or x < self.low
        if len(self.win) == self.n and self.win[0]:
            self.bad -= 1                      # the flag about to fall out of the window
        self.win.append(is_bad)
        if is_bad:
            self.bad += 1
        return self.bad >= self.k
        # <<< solution


# ----------------------------------------------------------------- tests
def _tests():
    m = MovingAverage(3)
    assert m.add(None) is None and m.invalid == 1          # nothing valid yet: no divide by zero
    assert m.add(3) == 3                                    # warm-up: average of 1
    assert m.add(6) == 4.5
    assert m.add(9) == 6
    assert m.add(12) == 9                                   # 3 is gone
    assert m.add(float("nan")) == 9 and m.invalid == 2      # bad sample does not move the window

    m = MovingAverage(1)
    assert [m.add(x) for x in (5, 7, None, 2)] == [5, 7, 7, 2]

    import random
    rnd = random.Random(3)                                  # compare to brute force, catches drift
    m, hist = MovingAverage(10), []
    for _ in range(3000):
        x = rnd.uniform(10, 26)
        hist.append(x)
        got = m.add(x)
        want = sum(hist[-10:]) / len(hist[-10:])
        assert abs(got - want) < 1e-9

    a = KofNAlarm(n=5, k=3, low=14.0)
    seq = [15, 13, 15, 13, 15, 13, 15, 15, 15]
    out = [a.add(x) for x in seq]
    assert out == [False, False, False, False, False, True, False, False, False]
    a = KofNAlarm(n=3, k=2, low=14.0)
    assert [a.add(x) for x in (None, 15, float("nan"), 15, 15)] == [False, False, True, False, False]
    a = KofNAlarm(n=4, k=4, low=1.0)                        # k == n: every sample must be bad
    assert [a.add(0) for _ in range(5)] == [False, False, False, True, True]
    print("c03 OK")


if __name__ == "__main__":
    _tests()
