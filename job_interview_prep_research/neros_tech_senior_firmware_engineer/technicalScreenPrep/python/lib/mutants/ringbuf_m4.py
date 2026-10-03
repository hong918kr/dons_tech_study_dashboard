"""M4 · iter() 가 저장 배열 순서로 돈다 — wraparound 뒤에는 순서가 틀림."""
from lib.ringbuf import RingBuffer as _Base


class RingBuffer(_Base):
    def __iter__(self):
        return iter([x for x in self._buf if x is not None])
