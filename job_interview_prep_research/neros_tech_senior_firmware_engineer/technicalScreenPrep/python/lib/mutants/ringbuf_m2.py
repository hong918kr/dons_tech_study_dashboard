"""M2 · overwrite 정책이 '가장 오래된 것'이 아니라 '가장 최신 것'을 덮어씀."""
from lib.ringbuf import RingBuffer as _Base


class RingBuffer(_Base):
    def push(self, item):
        if self.full() and self.overwrite:
            self._buf[(self._head - 1) % self.capacity] = item
            self.dropped += 1
            return True
        return super().push(item)
