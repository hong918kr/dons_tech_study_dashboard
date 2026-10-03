"""M8 · 용량 검증 누락: capacity=0 이나 음수를 받아들임."""
from lib.ringbuf import RingBuffer as _Base


class RingBuffer(_Base):
    def __init__(self, capacity, overwrite=False):
        self.capacity = capacity
        self.overwrite = overwrite
        self._buf = [None] * max(capacity, 0)
        self._head = self._count = 0
        self.dropped = 0
