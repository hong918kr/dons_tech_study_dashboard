"""M7 · peek() 가 값을 소비해 버림."""
from lib.ringbuf import RingBuffer as _Base


class RingBuffer(_Base):
    def peek(self):
        return self.pop()
