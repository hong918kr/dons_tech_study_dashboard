# --- solution ---
def lengthOfLongestSubstring(s: str) -> int:
    last = {}                              # 문자 → 마지막 위치
    left = best = 0
    for right, ch in enumerate(s):
        if ch in last and last[ch] >= left:
            left = last[ch] + 1            # 중복 바로 뒤로 창을 민다
        last[ch] = right
        best = max(best, right - left + 1)
    return best
# --- end ---


def test_lc3():
    assert lengthOfLongestSubstring("abcabcbb") == 3
    assert lengthOfLongestSubstring("bbbbb") == 1
    assert lengthOfLongestSubstring("pwwkew") == 3
    assert lengthOfLongestSubstring("") == 0
    assert lengthOfLongestSubstring("abba") == 2
