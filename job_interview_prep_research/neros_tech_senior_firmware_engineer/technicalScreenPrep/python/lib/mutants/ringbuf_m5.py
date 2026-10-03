"""M5 · 덮어쓸 때 dropped 카운터를 올리지 않음 (손실이 안 보임)."""
from lib.ringbuf import RingBuffer as _Base


class RingBuffer(_Base):
    def push(self, item):
        before = self.dropped
        ok = super().push(item)
        self.dropped = before
        return ok
