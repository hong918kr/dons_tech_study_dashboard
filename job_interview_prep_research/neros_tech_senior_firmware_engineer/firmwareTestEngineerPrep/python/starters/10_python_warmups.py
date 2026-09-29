"""10 · Python warm-ups — starter

면접관이 "Python으로 간단히 하나 풀어 봐" 할 때 나오는 LeetCode easy/medium 8개.
하나당 5~8분. 말하면서 풀고, 끝나면 복잡도를 말한다.

  python3 python/run.py 10          # 내 풀이 채점
  python3 python/run.py 10 --sol    # 모범답안
"""
import heapq
from collections import Counter, defaultdict, deque
from typing import Dict, Hashable, Iterable, Iterator, List, Optional, Sequence, Tuple


def two_sum(nums: Sequence[int], target: int) -> Optional[Tuple[int, int]]:
    """합이 target 인 두 인덱스 (i<j), 없으면 None. 목표 O(n)."""
    # TODO: dict 에 '값 → 인덱스' 기억, target - x 가 이미 있으면 반환
    raise NotImplementedError


def valid_brackets(cmd: str) -> bool:
    """명령 문자열의 (), [], {} 짝이 맞는지. 다른 문자는 무시."""
    # TODO: stack
    raise NotImplementedError


def merge_intervals(windows: Iterable[Tuple[float, float]]) -> List[Tuple[float, float]]:
    """겹치거나 맞닿은(end == 다음 start) fault 구간 합치기. 결과는 시작 순 튜플 리스트."""
    # TODO: 정렬 후 마지막 구간과 비교
    raise NotImplementedError


def top_k_frequent_errors(codes: Iterable[str], k: int) -> List[str]:
    """가장 자주 나온 에러 코드 k 개 (빈도 내림차순, 동률이면 코드 사전순)."""
    # TODO: Counter + heapq.nsmallest(key=(-count, code)) 또는 sorted
    raise NotImplementedError


def reverse_bits_32(x: int) -> int:
    """32비트 비트 순서 뒤집기 (bit0 ↔ bit31)."""
    # TODO: 32번 루프. out = (out << 1) | (x & 1); x >>= 1
    raise NotImplementedError


def moving_average(samples: Iterable[float], window: int) -> Iterator[float]:
    """generator. window 개가 찬 시점부터 평균을 하나씩 yield. window <= 0 이면 ValueError.
    샘플당 O(1) 이어야 한다 (매번 sum(buf) 금지)."""
    # TODO: deque + 누적합. 주의: generator 안의 raise 는 첫 next() 때 발생한다
    raise NotImplementedError


def dedupe_keep_order(items: Iterable[Hashable]) -> List[Hashable]:
    """처음 나온 순서 유지하며 중복 제거."""
    # TODO
    raise NotImplementedError


def group_log_keys(keys: Iterable[str]) -> Dict[str, List[str]]:
    """숫자 덩어리를 '#' 하나로 바꾼 패턴으로 묶기. MOTOR12_FAULT → 'MOTOR#_FAULT'.
    그룹 안 순서는 입력 순서."""
    # TODO: defaultdict(list)
    raise NotImplementedError


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
