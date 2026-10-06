"""03 · Two pointer — 정렬된 배열의 두 수의 합 (LeetCode 167)
prompt: 오름차순 nums 에서 합이 target 인 두 인덱스 [i, j] (없으면 []). 추가 메모리 O(1).
note: 같은 문제의 세 가지 풀이 — dict: O(n) 시간 · O(n) 메모리 / 투 포인터: O(n) 시간 · **O(1) 메모리** / 원소마다 이진 탐색: O(n log n) · O(1)
note: 정렬돼 있다는 조건이 있으면 투 포인터가 가장 좋다 (메모리 0, 캐시 친화적)
note: 왜 맞나: 합이 작으면 왼쪽 값을 키울 수밖에 없고, 크면 오른쪽 값을 줄일 수밖에 없다 → 버리는 쌍 중에 답이 없다
"""
# --- solution ---
def pair_with_sum(nums, target):
    i, j = 0, len(nums) - 1
    while i < j:
        s = nums[i] + nums[j]
        if s == target:
            return [i, j]
        if s < target:
            i += 1
        else:
            j -= 1
    return []


def test_pair_with_sum():
    assert pair_with_sum([1, 3, 4, 6, 9], 10) == [0, 4]
    assert pair_with_sum([1, 2], 5) == []


if __name__ == "__main__":
    test_pair_with_sum()
    print("PASS")
# --- end ---
