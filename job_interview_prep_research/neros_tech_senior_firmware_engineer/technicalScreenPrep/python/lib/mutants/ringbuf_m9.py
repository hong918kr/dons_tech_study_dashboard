"""M9 · drop-new 정책에서 가득 찼을 때 False 대신 True 를 돌려줌 (호출자가 손실을 모름)."""
from lib.ringbuf import RingBuffer as _Base


class RingBuffer(_Base):
    def push(self, item):
        super().push(item)
        return True
