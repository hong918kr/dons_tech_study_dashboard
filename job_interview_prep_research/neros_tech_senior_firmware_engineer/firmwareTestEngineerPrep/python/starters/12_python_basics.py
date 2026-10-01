"""12 · Python 기초 몸풀기 12문제 — 문제당 3~5분.

문자열 · 리스트 · dict · bytes/hex · 비트 연산 · 작은 클래스. 인터뷰 직전 손풀기용.
채점: python3 python/run.py 12   (모범답안: --sol)
"""
from collections import Counter, deque

def count_words(text):
    """단어 빈도 dict. 대소문자 무시, 공백 기준.  "Hi hi there" -> {"hi": 2, "there": 1}"""
    raise NotImplementedError  # TODO

def is_palindrome(s):
    """영숫자만 보고 대소문자 무시.  "A man, a plan, a canal: Panama" -> True"""
    raise NotImplementedError  # TODO

def find_duplicates(items):
    """두 번 이상 나온 값들을 정렬해서.  [3, 1, 3, 2, 1] -> [1, 3]"""
    raise NotImplementedError  # TODO

def flatten(nested):
    """한 단계만 펼치기.  [[1, 2], [3], []] -> [1, 2, 3]"""
    raise NotImplementedError  # TODO

def second_largest(nums):
    """서로 다른 값 중 두 번째로 큰 값, 없으면 None.  [5, 1, 5, 3] -> 3"""
    raise NotImplementedError  # TODO

def hex_dump(data):
    """bytes -> 대문자 hex, 공백 구분.  b"\\x01\\xa2\\xff" -> "01 A2 FF" """
    raise NotImplementedError  # TODO

def parse_hex(text):
    """hex_dump의 역. 대소문자·여분 공백 허용.  "01 a2  ff" -> b"\\x01\\xa2\\xff" """
    raise NotImplementedError  # TODO

def checksums(data):
    """(xor8, sum8) 튜플. sum8은 합의 하위 8비트.  b"\\x01\\x02\\xff" -> (0xFC, 0x02)"""
    raise NotImplementedError  # TODO

def bit_tools(n, k):
    """dict: popcount, is_pow2, bit_k(k번 비트), set_k, clear_k, toggle_k.  n >= 0"""
    raise NotImplementedError  # TODO

def parse_kv(text):
    """"a=1, b = 2,name=imu" -> {"a": 1, "b": 2, "name": "imu"}. 숫자면 int로. 빈 항목은 무시"""
    raise NotImplementedError  # TODO

def voltage_stats(lines):
    """"time,voltage" CSV 줄들 -> (min, max, avg). 헤더·깨진 줄은 건너뜀. 유효값 없으면 None
    avg는 소수 둘째 자리 반올림."""
    raise NotImplementedError  # TODO


class RingBuffer:
    """고정 용량 FIFO. 가득 차면 push가 가장 오래된 값을 덮어쓰고 덮어쓴 횟수를 센다.
    push(x), pop() -> 값 (비었으면 IndexError), len(), is_full(), dropped 속성"""

    def __init__(self, capacity):
        # TODO: capacity <= 0 이면 ValueError. 리스트 + head + count 로 구현 (deque 금지 연습)
        raise NotImplementedError

    def push(self, x):
        raise NotImplementedError

    def pop(self):
        raise NotImplementedError

    def __len__(self):
        raise NotImplementedError

    def is_full(self):
        raise NotImplementedError


# ------------------------------------------------------------------ tests
def test_01_count_words():
    assert count_words("Hi hi there") == {"hi": 2, "there": 1}
    assert count_words("") == {}


def test_02_is_palindrome():
    assert is_palindrome("A man, a plan, a canal: Panama")
    assert not is_palindrome("drone")
    assert is_palindrome("")


def test_03_find_duplicates():
    assert find_duplicates([3, 1, 3, 2, 1]) == [1, 3]
    assert find_duplicates([1, 2, 3]) == []


def test_04_flatten():
    assert flatten([[1, 2], [3], []]) == [1, 2, 3]
    assert flatten([]) == []


def test_05_second_largest():
    assert second_largest([5, 1, 5, 3]) == 3
    assert second_largest([7, 7]) is None
    assert second_largest([]) is None
    assert second_largest([-1, -5]) == -5


def test_06_hex_roundtrip():
    assert hex_dump(b"\x01\xa2\xff") == "01 A2 FF"
    assert hex_dump(b"") == ""
    assert parse_hex("01 a2  ff") == b"\x01\xa2\xff"
    assert parse_hex(hex_dump(bytes(range(256)))) == bytes(range(256))


def test_07_checksums():
    assert checksums(b"\x01\x02\xff") == (0xFC, 0x02)
    assert checksums(b"") == (0, 0)


def test_08_bit_tools():
    r = bit_tools(0b1010, 1)
    assert r["popcount"] == 2 and r["is_pow2"] is False and r["bit_k"] == 1
    assert r["set_k"] == 0b1010 and r["clear_k"] == 0b1000 and r["toggle_k"] == 0b1000
    assert bit_tools(64, 0)["is_pow2"] is True
    assert bit_tools(0, 3)["is_pow2"] is False and bit_tools(0, 3)["set_k"] == 8


def test_09_parse_kv():
    assert parse_kv("a=1, b = 2,name=imu") == {"a": 1, "b": 2, "name": "imu"}
    assert parse_kv("x=-3,,junk, y=") == {"x": -3, "y": ""}


def test_10_voltage_stats():
    lines = ["time,voltage", "0,16.8", "1,16.2", "bad line", "2,abc", "3,15.9"]
    assert voltage_stats(lines) == (15.9, 16.8, 16.3)
    assert voltage_stats(["time,voltage"]) is None


def test_11_ring_buffer():
    rb = RingBuffer(3)
    for x in (1, 2, 3):
        rb.push(x)
    assert rb.is_full() and len(rb) == 3
    rb.push(4)                         # 1 이 덮어써짐
    assert rb.dropped == 1
    assert [rb.pop(), rb.pop(), rb.pop()] == [2, 3, 4]
    assert len(rb) == 0
    try:
        rb.pop()
        assert False, "should raise"
    except IndexError:
        pass
    try:
        RingBuffer(0)
        assert False, "should raise"
    except ValueError:
        pass


def test_12_ring_buffer_wraparound():
    rb = RingBuffer(2)
    out = []
    for i in range(10):                # push/pop 교차로 인덱스가 여러 번 한 바퀴 돈다
        rb.push(i)
        if i % 2:
            out.append(rb.pop())
    assert out == [0, 2, 4, 6, 8]
    assert len(rb) == 1 and rb.dropped == 4


if __name__ == "__main__":
    import sys
    fails = 0
    for name, fn in list(globals().items()):
        if name.startswith("test_"):
            try:
                fn()
                print("PASS", name)
            except NotImplementedError:
                print("TODO", name)
                fails += 1
            except AssertionError as e:
                print("FAIL", name, e)
                fails += 1
    sys.exit(1 if fails else 0)
