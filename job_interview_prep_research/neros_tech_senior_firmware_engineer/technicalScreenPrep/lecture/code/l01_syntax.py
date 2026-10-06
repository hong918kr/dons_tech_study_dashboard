"""L01 문법 예제 — 각 구역은 실제로 실행되고 assert 가 '결과'를 보여 준다. (python3 l01_syntax.py → OK)"""

# --- queue_list ---
q = []
q.append(1)            # enqueue — 뒤에 넣기, O(1)
q.append(2)
q.append(3)
assert q.pop(0) == 1   # dequeue — 앞에서 빼기, 하지만 O(n): 나머지를 전부 한 칸씩 당긴다
assert q == [2, 3]
# --- end ---

# --- queue_deque ---
from collections import deque

q = deque()
q.append(1)            # enqueue (오른쪽에 넣기)   O(1)
q.append(2)
q.appendleft(0)        # 왼쪽에 넣기               O(1)
assert q.popleft() == 0  # dequeue (왼쪽에서 빼기) O(1)  ← 큐는 이것
assert q.pop() == 2      # 오른쪽에서 빼기        O(1)  ← 스택처럼도 쓸 수 있다
assert q[0] == 1 and len(q) == 1        # 맨 앞 보기(peek), 길이
assert not deque()                      # 빈 컨테이너는 False

last3 = deque(maxlen=3)                 # 고정 크기: 넘치면 반대쪽이 자동으로 밀려남 (= overwrite ring)
for x in range(5):
    last3.append(x)
assert list(last3) == [2, 3, 4]
# --- end ---

# --- stack ---
stack = []
stack.append("(")      # push
stack.append("[")
assert stack[-1] == "["  # top (peek) — 음수 인덱스 -1 = 마지막
assert stack.pop() == "["  # pop — 마지막을 빼기, O(1)
assert stack == ["("]
# --- end ---

# --- queue_threads ---
import queue

jobs = queue.Queue(maxsize=8)      # 스레드 사이 큐: 내부에 Lock + Condition
jobs.put("frame-1")                # enqueue (가득 차면 기다림)
assert jobs.get(timeout=1) == "frame-1"   # dequeue (비어 있으면 기다림, timeout 초과 → queue.Empty)
try:
    jobs.get(timeout=0.01)
except queue.Empty:
    pass
# --- end ---

# --- heap ---
import heapq

h = []
for x in (5, 1, 4):
    heapq.heappush(h, x)           # min-heap: 가장 작은 것이 h[0]
assert h[0] == 1
assert heapq.heappop(h) == 1       # O(log n)
assert heapq.nsmallest(2, [5, 1, 4, 2]) == [1, 2]

mx = []                            # max-heap 은 없다 → 음수로 넣는다
for x in (5, 1, 4):
    heapq.heappush(mx, -x)
assert -heapq.heappop(mx) == 5

tasks = []                         # (우선순위, 값) 튜플 — 앞 원소부터 비교
heapq.heappush(tasks, (2, "log"))
heapq.heappush(tasks, (0, "failsafe"))
assert heapq.heappop(tasks) == (0, "failsafe")
# --- end ---

# --- list_basics ---
xs = [10, 20, 30, 40, 50]
assert xs[0] == 10 and xs[-1] == 50          # 음수 인덱스는 뒤에서부터
assert xs[1:3] == [20, 30]                   # 슬라이스 [start, stop) — stop 은 포함 안 함
assert xs[:2] == [10, 20] and xs[3:] == [40, 50]
assert xs[::-1] == [50, 40, 30, 20, 10]      # 뒤집은 '새' 리스트
assert xs[::2] == [10, 30, 50]               # 두 칸씩
assert len(xs) == 5 and 30 in xs             # in 은 리스트에선 O(n)
assert xs.index(30) == 2
assert sum(xs) == 150 and max(xs) == 50 and min(xs) == 10

ys = xs                # 같은 객체를 가리키는 이름이 하나 더 (C 포인터 복사와 비슷)
zs = xs[:]             # 얕은 복사 (list(xs), xs.copy() 도 같음)
ys.append(60)
assert xs[-1] == 60 and len(zs) == 5

grid_bad = [[0] * 3] * 3      # 함정: 같은 행 객체 3개를 공유
grid_bad[0][0] = 1
assert grid_bad[1][0] == 1    # 모든 행이 바뀐다
grid = [[0] * 3 for _ in range(3)]   # 올바른 2차원 리스트
grid[0][0] = 1
assert grid[1][0] == 0
# --- end ---

# --- sorting ---
words = ["ccc", "a", "bb", "aa"]
assert sorted(words) == ["a", "aa", "bb", "ccc"]               # 새 리스트를 돌려준다
assert sorted(words, key=len) == ["a", "bb", "aa", "ccc"]      # 안정 정렬: 같은 길이는 원래 순서
assert sorted(words, key=lambda w: (len(w), w)) == ["a", "aa", "bb", "ccc"]   # 여러 기준 = 튜플
assert sorted(words, reverse=True)[0] == "ccc"
words.sort()                       # 제자리 정렬, None 을 돌려준다 (x = xs.sort() 는 버그)
assert words[0] == "a"
intervals = [[8, 10], [1, 3], [2, 6]]
intervals.sort(key=lambda iv: iv[0])
assert intervals == [[1, 3], [2, 6], [8, 10]]
# --- end ---

# --- dict ---
d = {"vbat": 16.4, "rssi": -70}
d["state"] = "ARMED"                       # 넣기 / 덮어쓰기, 평균 O(1)
assert d["vbat"] == 16.4
assert d.get("missing") is None            # 없으면 None (d["missing"] 은 KeyError)
assert d.get("missing", 0) == 0            # 기본값
assert "rssi" in d                         # 키 검사 O(1)
assert list(d) == ["vbat", "rssi", "state"]   # 넣은 순서를 기억한다 (3.7+)
for key, value in d.items():               # 키와 값 함께
    assert d[key] == value
del d["rssi"]
assert d.pop("state") == "ARMED" and d == {"vbat": 16.4}
squares = {n: n * n for n in range(4)}     # dict comprehension
assert squares[3] == 9

counts = {}
for ch in "abca":
    counts[ch] = counts.get(ch, 0) + 1     # 세기의 기본형
assert counts == {"a": 2, "b": 1, "c": 1}
# --- end ---

# --- collections ---
from collections import Counter, OrderedDict, defaultdict

c = Counter("abracadabra")                 # 세기 전용 dict
assert c["a"] == 5 and c["z"] == 0         # 없는 키는 0 (KeyError 아님)
assert c.most_common(2) == [("a", 5), ("b", 2)]

groups = defaultdict(list)                 # 없는 키에 접근하면 list() 를 만들어 넣는다
for w in ["arm", "abort", "boot"]:
    groups[w[0]].append(w)
assert dict(groups) == {"a": ["arm", "abort"], "b": ["boot"]}

lru = OrderedDict()
lru["a"] = 1
lru["b"] = 2
lru.move_to_end("a")                       # 'a' 를 가장 최근으로
assert list(lru) == ["b", "a"]
assert lru.popitem(last=False) == ("b", 2) # 가장 오래된 것 꺼내기
# --- end ---

# --- set_tuple ---
seen = set()
seen.add(3)
seen.add(3)                                # 중복은 무시
assert len(seen) == 1 and 3 in seen        # in 은 O(1) — 리스트(O(n))와 다르다
a, b = {1, 2, 3}, {2, 3, 4}
assert a & b == {2, 3} and a | b == {1, 2, 3, 4} and a - b == {1}

point = (3, 4)                             # 튜플: 바꿀 수 없다 → dict 키 / set 원소로 쓸 수 있다
x, y = point                               # 언패킹
x, y = y, x                                # swap — 임시 변수 없이
assert (x, y) == (4, 3)
visited = {(0, 0), (0, 1)}                 # 좌표 집합 (그리드 문제 단골)
assert (0, 1) in visited
first, *rest = [1, 2, 3]
assert first == 1 and rest == [2, 3]
# --- end ---

# --- strings ---
s = "  Arm,Disarm ,Land "
parts = [p.strip().lower() for p in s.split(",")]
assert parts == ["arm", "disarm", "land"]
assert "-".join(parts) == "arm-disarm-land"     # 문자열 합치기는 join (+= 반복은 O(n²) 가능)
assert "abc"[::-1] == "cba"
assert "a1!".isalnum() is False and "a1".isalnum() and "7".isdigit()
assert ord("a") == 97 and chr(98) == "b"
assert ord("c") - ord("a") == 2                 # 알파벳 인덱스 (26칸 배열 문제)
assert "".join(sorted("tea")) == "aet"          # sorted(문자열) 은 리스트 → join
assert "TEMP=23.5".split("=")[1] == "23.5"
assert "key = v=1".partition("=") == ("key ", "=", " v=1")
t = "abc"
try:
    t[0] = "z"                                  # 문자열은 바꿀 수 없다 (immutable)
except TypeError:
    t = "z" + t[1:]
assert t == "zbc"
name, ver = "fc", 3
assert f"{name}-v{ver:02d} {0.5:.1%} {255:#04x}" == "fc-v03 50.0% 0xff"
# --- end ---

# --- loops ---
xs = ["a", "b", "c"]
out = []
for i, x in enumerate(xs):                 # 인덱스와 값 함께 (range(len(xs)) 대신)
    out.append(f"{i}{x}")
assert out == ["0a", "1b", "2c"]
assert list(zip([1, 2, 3], "xy")) == [(1, "x"), (2, "y")]   # 짧은 쪽에서 멈춘다
assert list(range(5)) == [0, 1, 2, 3, 4]
assert list(range(2, 10, 3)) == [2, 5, 8]
assert list(range(4, 0, -1)) == [4, 3, 2, 1]                 # 거꾸로
assert list(reversed(xs)) == ["c", "b", "a"]

i, j = 0, len(xs) - 1                      # 투 포인터 while
while i < j:
    i, j = i + 1, j - 1
assert (i, j) == (1, 1)

evens = [x for x in range(10) if x % 2 == 0]     # list comprehension
assert evens == [0, 2, 4, 6, 8]
total = sum(x * x for x in range(4))             # generator — 리스트를 만들지 않는다
assert total == 14
assert any(x > 8 for x in evens) is False and all(x % 2 == 0 for x in evens)
# --- end ---

# --- numbers ---
assert 7 / 2 == 3.5            # / 는 항상 float
assert 7 // 2 == 3             # // 는 내림(floor) 나눗셈
assert -7 // 2 == -4           # ⚠ C 의 -7 / 2 == -3 (0 쪽으로 버림) 과 다르다
assert -7 % 2 == 1             # ⚠ C 의 -7 % 2 == -1. Python 의 % 는 나누는 수의 부호를 따른다
assert int(-7 / 2) == -3       # C 처럼 0 쪽으로 버리려면 int(a / b)
assert divmod(17, 5) == (3, 2)
assert 2 ** 100 > 10 ** 30     # int 는 크기 제한이 없다 → 오버플로 없음
assert (0xFFFFFFFF + 1) & 0xFFFFFFFF == 0       # 32비트처럼 쓰려면 직접 마스크
assert float("inf") > 10 ** 100 and -float("inf") < -1
assert round(2.675, 2) == 2.67                 # float 표현 오차 (2.675 는 정확히 표현 안 됨)
assert abs(-3) == 3 and max(3, 7, 5) == 7
# --- end ---

# --- bits ---
x = 0b1011_0000
assert x >> 4 == 0b1011 and (1 << 3) == 8
assert x & 0xF0 == 0xB0 and x | 1 == 0xB1 and x ^ 0xFF == 0x4F
assert bin(5) == "0b101" and hex(255) == "0xff" and int("ff", 16) == 255
assert ~5 == -6                # ⚠ 무한 정밀도 2의 보수: ~x == -x - 1 (C 의 ~5u == 0xFFFFFFFA 와 다름)
assert ~5 & 0xFF == 0xFA       # 8비트로 보려면 마스크
assert (12).bit_length() == 4 and bin(12).count("1") == 2
n = 12
n &= n - 1                     # 가장 낮은 1 비트 지우기
assert n == 8
# --- end ---

# --- functions ---
def add_item(item, bucket=None):     # ⚠ bucket=[] 로 쓰면 모든 호출이 같은 리스트를 공유한다
    if bucket is None:
        bucket = []
    bucket.append(item)
    return bucket


assert add_item(1) == [1] and add_item(2) == [2]


def stats(*values, scale=1):         # *args = 위치 인자 묶음, 키워드 전용 인자
    return min(values) * scale, max(values) * scale


assert stats(3, 1, 2) == (1, 3) and stats(3, 1, scale=10) == (10, 30)


def make_counter():
    count = 0

    def inc():
        nonlocal count               # 바깥 함수의 변수를 바꾸려면 nonlocal
        count += 1
        return count
    return inc


inc = make_counter()
assert inc() == 1 and inc() == 2
key_fn = lambda pair: pair[1]        # 한 줄 함수 (sort key 에 주로)
assert sorted([("a", 2), ("b", 1)], key=key_fn)[0] == ("b", 1)
# --- end ---

# --- classes ---
class Ring:
    def __init__(self, capacity):    # 생성자. self = C 의 'this 포인터'를 명시적으로 받는 것
        self.capacity = capacity     # 인스턴스 변수는 self.이름 으로 만든다
        self._items = []             # 앞에 _ = '내부용' 관례 (접근 제한은 없다)

    def push(self, x):
        if len(self._items) == self.capacity:
            raise OverflowError("full")
        self._items.append(x)

    def __len__(self):               # len(r)
        return len(self._items)

    def __iter__(self):              # for x in r
        return iter(self._items)

    def __repr__(self):              # print(r), 디버거 표시
        return f"Ring({self._items}, cap={self.capacity})"

    @property
    def full(self):                  # r.full  (괄호 없이 읽는 계산 속성)
        return len(self) == self.capacity


r = Ring(2)
r.push("a")
r.push("b")
assert len(r) == 2 and list(r) == ["a", "b"] and r.full and repr(r) == "Ring(['a', 'b'], cap=2)"
try:
    r.push("c")
except OverflowError as e:
    assert str(e) == "full"

from dataclasses import dataclass, field


@dataclass
class Frame:                         # __init__, __repr__, __eq__ 자동 생성
    msg_id: int
    payload: bytes = b""
    tags: list = field(default_factory=list)    # mutable 기본값은 default_factory 로


assert Frame(1) == Frame(1, b"") and Frame(2).tags == []
# --- end ---

# --- exceptions ---
class DeviceError(Exception):
    """모든 장치 오류의 부모 — 호출자는 이것 하나로 잡을 수 있다."""


class DeviceTimeout(DeviceError):
    pass


def read_or_default(fn, default):
    try:
        return fn()
    except DeviceTimeout:            # 좁은 것부터 잡는다
        return default
    finally:
        pass                         # 예외가 나도 반드시 실행 (자원 정리)


def boom():
    raise DeviceTimeout("no reply")


assert read_or_default(boom, -1) == -1
try:
    int("abc")
except ValueError as e:
    assert "invalid literal" in str(e)
# --- end ---

# --- bytes ---
import struct

raw = bytes([0xAA, 0x02, 0x10, 0x00])
assert raw[0] == 0xAA                         # 인덱스 하나 = int
assert raw[1:3] == b"\x02\x10"                # 슬라이스 = bytes
assert raw.hex() == "aa021000" and bytes.fromhex("aa02") == b"\xaa\x02"
assert int.from_bytes(raw[2:4], "little") == 16
sync, kind, length = struct.unpack("<BBH", raw)   # < little-endian, B u8, H u16
assert (sync, kind, length) == (0xAA, 2, 16)
buf = bytearray(4)                            # 바꿀 수 있는 bytes
buf[0:2] = b"\x01\x02"
assert buf == bytearray(b"\x01\x02\x00\x00")
assert "온도".encode() == b"\xec\x98\xa8\xeb\x8f\x84" and b"OK\r\n".decode().strip() == "OK"
# --- end ---

if __name__ == "__main__":
    print("OK")
