# --- solution ---
from collections import OrderedDict


class LRUCache:
    def __init__(self, capacity: int):
        self.cap = capacity
        self.d = OrderedDict()             # 순서 = 최근 사용 순 (끝이 가장 최근)

    def get(self, key: int) -> int:
        if key not in self.d:
            return -1
        self.d.move_to_end(key)
        return self.d[key]

    def put(self, key: int, value: int) -> None:
        if key in self.d:
            self.d.move_to_end(key)
        self.d[key] = value
        if len(self.d) > self.cap:
            self.d.popitem(last=False)     # 가장 오래 안 쓴 것 제거
# --- end ---


def test_lc146():
    c = LRUCache(2)
    c.put(1, 1); c.put(2, 2)
    assert c.get(1) == 1
    c.put(3, 3)                            # 2 가 밀려남
    assert c.get(2) == -1
    c.put(4, 4)                            # 1 이 밀려남
    assert (c.get(1), c.get(3), c.get(4)) == (-1, 3, 4)
