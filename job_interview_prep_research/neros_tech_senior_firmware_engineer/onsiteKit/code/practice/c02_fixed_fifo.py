"""C02 fixed-capacity FIFO (ring buffer without saying so): full policy = reject or overwrite oldest, counters.

Interviewer wording (no hint): "An interrupt hands us one byte at a time and the main
loop drains them later. Memory is fixed at startup -- you can't grow anything.
Give me put / get, and tell me what happens when the main loop falls behind."

Run: python3 c02_fixed_fifo.py
"""


class Fifo:
    def __init__(self, capacity, overwrite=False):
        if capacity <= 0:
            raise ValueError("capacity must be > 0")
        self.cap = capacity
        self.overwrite = overwrite
        self.data = [0] * capacity        # allocated once
        self.head = 0                     # next write slot
        self.tail = 0                     # next read slot
        self.count = 0
        self.dropped = 0                  # rejected new items (overwrite=False)
        self.overwritten = 0              # lost old items (overwrite=True)

    def __len__(self):
        return self.count

    def is_empty(self):
        return self.count == 0

    def is_full(self):
        return self.count == self.cap

    def put(self, item):
        """True if stored. When full: reject (False) or overwrite the oldest (True)."""
        raise NotImplementedError("TODO")

    def get(self):
        """Oldest item, or None when empty."""
        raise NotImplementedError("TODO")

    def peek(self, offset=0):
        """Item `offset` places after the oldest, without removing it. None if out of range."""
        raise NotImplementedError("TODO")

    def put_all(self, items):
        """All-or-nothing: store every item or none of them (reject mode semantics)."""
        raise NotImplementedError("TODO")


# ----------------------------------------------------------------- tests
def _tests():
    try:
        Fifo(0)
        assert False, "capacity 0 must raise"
    except ValueError:
        pass

    f = Fifo(3)
    assert f.get() is None and f.is_empty()
    assert f.put(1) and f.put(2) and f.put(3)
    assert f.is_full()
    assert f.put(4) is False and f.dropped == 1        # THE overflow case: full -> reject, count it
    assert [f.get(), f.get(), f.get(), f.get()] == [1, 2, 3, None]

    f = Fifo(3, overwrite=True)
    for x in range(1, 6):
        assert f.put(x)
    assert f.overwritten == 2 and len(f) == 3
    assert [f.get(), f.get(), f.get()] == [3, 4, 5]     # oldest were replaced, order kept

    f = Fifo(1)                                          # smallest capacity
    assert f.put("a") and not f.put("b")
    assert f.get() == "a" and f.put("c") and f.get() == "c"

    f = Fifo(4)                                          # wrap many times, compare to a model
    from collections import deque
    model = deque()
    import random
    rnd = random.Random(7)
    for step in range(5000):
        if rnd.random() < 0.55:
            ok = f.put(step)
            if len(model) < 4:
                model.append(step)
                assert ok
            else:
                assert not ok
        else:
            assert f.get() == (model.popleft() if model else None)
        assert len(f) == len(model)
        assert f.peek(0) == (model[0] if model else None)

    f = Fifo(5)
    f.put_all([1, 2, 3])
    assert f.put_all([4, 5, 6]) is False and len(f) == 3   # nothing partially written
    assert f.put_all([4, 5]) and f.is_full()
    assert [f.peek(i) for i in range(6)] == [1, 2, 3, 4, 5, None]
    print("c02 OK")


if __name__ == "__main__":
    _tests()
