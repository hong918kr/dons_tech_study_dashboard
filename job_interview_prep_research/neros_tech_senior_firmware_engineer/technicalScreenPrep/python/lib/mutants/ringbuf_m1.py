"""M1 · 한 칸을 비워 두는 구현을 섞다가 생긴 off-by-one: capacity-1 개에서 이미 full."""
from lib.ringbuf import RingBuffer as _Base


class RingBuffer(_Base):
    def full(self):
        return self._count >= self.capacity - 1
