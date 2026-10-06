"""02 · Two Sum 변형 — dict 로 개수 세기 (합이 target 인 쌍의 수)
prompt: nums 에서 i < j 이고 nums[i] + nums[j] == target 인 쌍의 개수를 O(n) 으로. dict.get 으로 빈도를 센다.
note: 01 은 dict 에 '인덱스'를, 이 문제는 '지금까지 본 횟수'를 넣는다 — dict 의 두 번째 단골 용도
note: `count[x] = count.get(x, 0) + 1` 이 세기의 기본형 (collections.Counter 도 같은 일)
note: 전부 짝을 비교하면 O(n²), dict 로 O(n)
"""
# --- solution ---
def count_pairs(nums, target):
    count = {}                         # 값 → 지금까지 나온 횟수
    pairs = 0
    for x in nums:
        pairs += count.get(target - x, 0)    # 앞에서 나온 짝의 수만큼
        count[x] = count.get(x, 0) + 1
    return pairs


def test_count_pairs():
    assert count_pairs([1, 5, 7, -1, 5], 6) == 3     # (1,5) (1,5) (7,-1)
    assert count_pairs([2, 2, 2], 4) == 3            # 세 개 중 두 개 고르기
    assert count_pairs([], 4) == 0


if __name__ == "__main__":
    test_count_pairs()
    print("PASS")
# --- end ---
