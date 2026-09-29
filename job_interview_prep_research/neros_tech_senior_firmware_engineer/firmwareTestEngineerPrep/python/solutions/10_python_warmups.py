"""10 · Python warm-ups — 모범답안

면접관이 "Python으로 간단히 하나 풀어 봐" 할 때 나오는 LeetCode easy/medium 8개.
전부 테스트/로그 맥락으로 포장했다. 각 함수 docstring 에 복잡도.

  python3 python/solutions/10_python_warmups.py
"""
import heapq
from collections import Counter, defaultdict, deque
from typing import Dict, Hashable, Iterable, Iterator, List, Optional, Sequence, Tuple


def two_sum(nums: Sequence[int], target: int) -> Optional[Tuple[int, int]]:
    """두 인덱스 (i<j) 반환, 없으면 None. dict 로 '필요한 짝'을 기억 → O(n) 시간, O(n) 공간."""
    seen: Dict[int, int] = {}
    for j, x in enumerate(nums):
        if target - x in seen:
            return seen[target - x], j
        seen[x] = j
    return None


def valid_brackets(cmd: str) -> bool:
    """명령 문자열의 (), [], {} 짝이 맞는지. stack → O(n)."""
    pairs = {")": "(", "]": "[", "}": "{"}
    stack: List[str] = []
    for ch in cmd:
        if ch in "([{":
            stack.append(ch)
        elif ch in pairs:
            if not stack or stack.pop() != pairs[ch]:
                return False
    return not stack


def merge_intervals(windows: Iterable[Tuple[float, float]]) -> List[Tuple[float, float]]:
    """겹치거나 맞닿은 fault 구간 합치기. 정렬 O(n log n) + 한 번 순회."""
    merged: List[List[float]] = []
    for start, end in sorted(windows):
        if merged and start <= merged[-1][1]:
            merged[-1][1] = max(merged[-1][1], end)
        else:
            merged.append([start, end])
    return [(s, e) for s, e in merged]


def top_k_frequent_errors(codes: Iterable[str], k: int) -> List[str]:
    """가장 자주 나온 에러 코드 k 개. 동률이면 코드 사전순. Counter O(n) + heapq.nsmallest O(n log k)."""
    counts = Counter(codes)
    return [c for c, _ in heapq.nsmallest(k, counts.items(), key=lambda kv: (-kv[1], kv[0]))]


def reverse_bits_32(x: int) -> int:
    """32비트 비트 순서 뒤집기. 32번 루프 O(1). Python 은 음수/큰 수 방지를 위해 & 0xFFFFFFFF."""
    x &= 0xFFFFFFFF
    out = 0
    for _ in range(32):
        out = (out << 1) | (x & 1)
        x >>= 1
    return out


def moving_average(samples: Iterable[float], window: int) -> Iterator[float]:
    """generator. 창이 찰 때부터 평균을 yield. deque + 누적합 → 샘플당 O(1), 메모리 O(window)."""
    if window <= 0:
        raise ValueError("window must be positive")
    buf: deque = deque()
    total = 0.0
    for s in samples:
        buf.append(s)
        total += s
        if len(buf) > window:
            total -= buf.popleft()
        if len(buf) == window:
            yield total / window


def dedupe_keep_order(items: Iterable[Hashable]) -> List[Hashable]:
    """처음 나온 순서 유지하며 중복 제거. set 으로 O(n). (dict.fromkeys(items) 한 줄도 가능)"""
    seen = set()
    out = []
    for it in items:
        if it not in seen:
            seen.add(it)
            out.append(it)
    return out


def group_log_keys(keys: Iterable[str]) -> Dict[str, List[str]]:
    """'MOTOR3_OVERCURRENT' 같은 키를 숫자를 빼고 정규화한 패턴으로 묶기 (group anagrams 변형).
    예: MOTOR1_FAULT, MOTOR3_FAULT → 'MOTOR#_FAULT'. O(n · L)."""
    groups: Dict[str, List[str]] = defaultdict(list)
    for k in keys:
        pattern = "".join("#" if ch.isdigit() else ch for ch in k)
        while "##" in pattern:
            pattern = pattern.replace("##", "#")
        groups[pattern].append(k)
    return dict(groups)


# ------------------------------------------------------------------ tests
def test_two_sum():
    assert two_sum([2, 7, 11, 15], 9) == (0, 1)
    assert two_sum([3, 3], 6) == (0, 1)
    assert two_sum([1, 2], 7) is None


def test_valid_brackets():
    assert valid_brackets("set(motor[1], {pwm: 1500})")
    assert not valid_brackets("set(motor[1)]")
    assert not valid_brackets("((")
    assert not valid_brackets(")")
    assert valid_brackets("")


def test_merge_intervals():
    got = merge_intervals([(5, 7), (1, 3), (2, 4), (7, 8), (10, 11)])
    assert got == [(1, 4), (5, 8), (10, 11)]
    assert merge_intervals([]) == []
    assert merge_intervals([(1, 10), (2, 3)]) == [(1, 10)]


def test_top_k_frequent_errors():
    log = ["E_RX_LOSS", "E_GYRO", "E_RX_LOSS", "E_BATT_LOW", "E_GYRO", "E_RX_LOSS", "E_ARM"]
    assert top_k_frequent_errors(log, 2) == ["E_RX_LOSS", "E_GYRO"]
    assert top_k_frequent_errors(log, 3) == ["E_RX_LOSS", "E_GYRO", "E_ARM"]   # 동률 1회 → 사전순
    assert top_k_frequent_errors([], 3) == []


def test_reverse_bits_32():
    assert reverse_bits_32(1) == 0x80000000
    assert reverse_bits_32(0b1011) == 0xD0000000
    assert reverse_bits_32(0xFFFFFFFF) == 0xFFFFFFFF
    assert reverse_bits_32(reverse_bits_32(0x12345678)) == 0x12345678


def test_moving_average_is_lazy_generator():
    gen = moving_average(iter([1, 2, 3, 4, 5]), 3)
    assert next(gen) == 2.0                      # generator: 필요한 만큼만 계산
    assert list(gen) == [3.0, 4.0]
    assert list(moving_average([1, 2], 3)) == []
    try:
        list(moving_average([1], 0))
        assert False
    except ValueError:
        pass


def test_dedupe_keep_order():
    assert dedupe_keep_order(["b", "a", "b", "c", "a"]) == ["b", "a", "c"]
    assert dedupe_keep_order([]) == []


def test_group_log_keys():
    keys = ["MOTOR1_FAULT", "MOTOR3_FAULT", "MOTOR12_FAULT", "ESC2_TEMP", "BATT_LOW"]
    g = group_log_keys(keys)
    assert g["MOTOR#_FAULT"] == ["MOTOR1_FAULT", "MOTOR3_FAULT", "MOTOR12_FAULT"]
    assert g["ESC#_TEMP"] == ["ESC2_TEMP"]
    assert g["BATT_LOW"] == ["BATT_LOW"]


if __name__ == "__main__":
    for _n, _f in list(globals().items()):
        if _n.startswith("test_") and callable(_f):
            _f()
            print("PASS", _n)
