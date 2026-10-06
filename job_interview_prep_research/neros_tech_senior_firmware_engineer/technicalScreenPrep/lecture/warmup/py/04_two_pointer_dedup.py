"""04 · Two pointer (같은 방향) — 정렬된 배열의 중복 제거, 제자리 (LeetCode 26)
prompt: 정렬된 리스트 nums 를 제자리에서 중복 없이 앞쪽에 모으고, 고유 원소 개수 k 를 돌려준다. 추가 메모리 O(1).
note: 03 은 양 끝에서 좁혀 오는 투 포인터, 이건 slow(쓸 자리) · fast(읽는 자리)가 같은 방향으로 가는 투 포인터
note: `list(set(nums))` 는 쉽지만 O(n) 메모리 + 순서 보장 안 됨 → 면접에선 제자리 O(1) 이 요구된다
note: 같은 패턴: 0 을 뒤로 밀기, 특정 값 제거, C 에서 버퍼 압축
"""
# --- solution ---
def dedup(nums):
    if not nums:
        return 0
    slow = 0                           # 마지막으로 남긴 고유 값의 자리
    for fast in range(1, len(nums)):
        if nums[fast] != nums[slow]:
            slow += 1
            nums[slow] = nums[fast]
    return slow + 1


def test_dedup():
    a = [1, 1, 2, 3, 3, 3]
    assert dedup(a) == 3 and a[:3] == [1, 2, 3]
    assert dedup([]) == 0


if __name__ == "__main__":
    test_dedup()
    print("PASS")
# --- end ---
