# T1 fixed FIFO -- "ISR hands bytes, main loop drains, memory fixed, what if main falls behind?"
class Fifo:
    def __init__(self, cap, overwrite=False):
        if cap <= 0:
            raise ValueError("cap must be > 0")
        self.buf = [None] * cap
        self.cap, self.overwrite = cap, overwrite
        self.head = self.tail = self.count = 0     # head: next write, tail: next read
        self.dropped = self.overwritten = 0

    def put(self, x):
        if self.count == self.cap:                  # OVERFLOW: decide, then count
            if not self.overwrite:
                self.dropped += 1
                return False
            self.tail = (self.tail + 1) % self.cap  # drop oldest: move tail too
            self.count -= 1
            self.overwritten += 1
        self.buf[self.head] = x
        self.head = (self.head + 1) % self.cap
        self.count += 1
        return True

    def get(self):
        if self.count == 0:
            return None
        x = self.buf[self.tail]
        self.tail = (self.tail + 1) % self.cap
        self.count -= 1
        return x

    def put_all(self, items):                       # all-or-nothing
        if len(items) > self.cap - self.count:
            self.dropped += len(items)
            return False
        for x in items:
            self.put(x)
        return True


if __name__ == "__main__":
    f = Fifo(3)
    assert f.get() is None                              # empty
    assert f.put(1) and f.put(2) and f.put(3)
    assert f.put(4) is False and f.dropped == 1         # full -> reject + count
    assert [f.get(), f.get(), f.get()] == [1, 2, 3]     # old data and order kept
    g = Fifo(3, overwrite=True)
    for i in range(1, 6):
        g.put(i)
    assert g.overwritten == 2 and [g.get(), g.get(), g.get()] == [3, 4, 5]
    h = Fifo(5)
    assert h.put_all([1, 2, 3]) and not h.put_all([4, 5, 6]) and h.count == 3
    for i in range(20):                                 # wrap many laps
        h.get(); h.put(i)
    print("t1 OK")
