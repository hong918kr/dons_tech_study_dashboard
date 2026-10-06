# --- solution ---
from collections import Counter
import heapq


def topKFrequent(nums: list[int], k: int) -> list[int]:
    counts = Counter(nums)
    return [x for x, _ in counts.most_common(k)]          # 내부적으로 heap: O(n log k)


def topKFrequent_heap(nums: list[int], k: int) -> list[int]:
    counts = Counter(nums)
    return heapq.nlargest(k, counts, key=counts.get)
# --- end ---


def test_lc347():
    assert sorted(topKFrequent([1, 1, 1, 2, 2, 3], 2)) == [1, 2]
    assert sorted(topKFrequent_heap([1, 1, 1, 2, 2, 3], 2)) == [1, 2]
    assert topKFrequent([1], 1) == [1]
