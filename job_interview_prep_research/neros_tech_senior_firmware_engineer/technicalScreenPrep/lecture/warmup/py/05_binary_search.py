"""05 · 이진 탐색 — left · right · mid (LeetCode 704)
prompt: 오름차순 nums 에서 target 의 인덱스, 없으면 -1. left/right/mid 로 O(log n).
note: 불변식: 답이 있다면 항상 nums[left..right] 안에 있다 → `while left <= right`
note: mid 는 `(left + right) // 2` (Python 은 오버플로 없음, C 에선 `left + (right - left) / 2`)
note: 흔한 버그: `left = mid` / `right = mid` 로 두면 무한 루프 → 반드시 mid ± 1
note: 실무에선 `bisect.bisect_left(nums, target)` — 없을 때 '들어갈 자리'까지 준다
"""
# --- solution ---
def search(nums, target):
    left, right = 0, len(nums) - 1
    while left <= right:
        mid = (left + right) // 2
        if nums[mid] == target:
            return mid
        if nums[mid] < target:
            left = mid + 1             # 답은 오른쪽 절반에
        else:
            right = mid - 1            # 답은 왼쪽 절반에
    return -1


def test_search():
    assert search([-1, 0, 3, 5, 9, 12], 9) == 4
    assert search([-1, 0, 3, 5, 9, 12], 2) == -1
    assert search([], 1) == -1


if __name__ == "__main__":
    test_search()
    print("PASS")
# --- end ---
