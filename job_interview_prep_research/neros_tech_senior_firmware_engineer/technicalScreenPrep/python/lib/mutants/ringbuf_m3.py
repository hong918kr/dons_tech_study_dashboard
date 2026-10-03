"""M3 · 빈 버퍼에서 pop() 이 예외 대신 None 을 돌려줌."""
from lib.ringbuf import RingBuffer as _Base


class RingBuffer(_Base):
    def pop(self):
        if self.empty():
            return None
        return super().pop()
