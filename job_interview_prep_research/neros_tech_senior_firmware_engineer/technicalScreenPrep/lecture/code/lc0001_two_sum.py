# --- solution ---
def twoSum(nums: list[int], target: int) -> list[int]:
    seen = {}                              # 값 → 인덱스
    for i, x in enumerate(nums):
        if target - x in seen:
            return [seen[target - x], i]
        seen[x] = i
    return []
# --- end ---


def test_lc1():
    assert twoSum([2, 7, 11, 15], 9) == [0, 1]
    assert twoSum([3, 3], 6) == [0, 1]
    assert twoSum([1, 2], 7) == []
