"""M6 · clear() 가 저장소만 비우고 개수를 리셋하지 않음."""
from lib.ringbuf import RingBuffer as _Base


class RingBuffer(_Base):
    def clear(self):
        self._buf = [None] * self.capacity
