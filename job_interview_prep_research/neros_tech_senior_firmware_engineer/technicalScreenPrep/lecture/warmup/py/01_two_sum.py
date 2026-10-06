"""01 · Two Sum — dict 로 '값 → 인덱스' 기억하기 (LeetCode 1)
prompt: nums 와 target 이 주어질 때 합이 target 인 두 인덱스 [i, j] (없으면 []). O(n). 테스트 함수 하나 + main 까지 직접.
note: dict 기본 — `d[k] = v` 넣기 · `k in d` 있나(O(1)) · `d[k]` 꺼내기(없으면 KeyError) · `d.get(k, 기본값)` 안전하게 꺼내기
note: 순서가 중요: '확인 → 저장'. 저장부터 하면 같은 원소를 두 번 쓴다 ([3], 6 → [0, 0] 버그)
note: 복잡도: 시간 O(n), 메모리 O(n) (dict)
"""
# --- solution ---
def two_sum(nums, target):
    seen = {}                          # 값 → 인덱스
    for i, x in enumerate(nums):
        need = target - x
        if need in seen:               # 짝이 이미 지나갔나?
            return [seen[need], i]
        seen[x] = i                    # 확인한 다음에 저장
    return []


def test_two_sum():
    assert two_sum([2, 7, 11, 15], 9) == [0, 1]
    assert two_sum([3, 3], 6) == [0, 1]       # 같은 값 두 개
    assert two_sum([3], 6) == []              # 자기 자신과 짝 금지


if __name__ == "__main__":
    test_two_sum()
    print("PASS")
# --- end ---
