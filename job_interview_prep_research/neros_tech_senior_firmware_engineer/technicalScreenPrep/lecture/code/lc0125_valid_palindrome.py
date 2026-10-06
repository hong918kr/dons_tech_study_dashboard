# --- solution ---
def isPalindrome(s: str) -> bool:
    i, j = 0, len(s) - 1
    while i < j:
        if not s[i].isalnum():
            i += 1
        elif not s[j].isalnum():
            j -= 1
        elif s[i].lower() != s[j].lower():
            return False
        else:
            i, j = i + 1, j - 1
    return True
# --- end ---


def test_lc125():
    assert isPalindrome("A man, a plan, a canal: Panama")
    assert not isPalindrome("race a car") and isPalindrome(" ")
