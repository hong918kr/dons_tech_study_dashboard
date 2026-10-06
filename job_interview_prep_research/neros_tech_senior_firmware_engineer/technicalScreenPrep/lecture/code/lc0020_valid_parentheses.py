# --- solution ---
def isValid(s: str) -> bool:
    pairs = {")": "(", "]": "[", "}": "{"}
    stack = []
    for ch in s:
        if ch in pairs:
            if not stack or stack.pop() != pairs[ch]:
                return False
        else:
            stack.append(ch)
    return not stack
# --- end ---


def test_lc20():
    assert isValid("()[]{}") and isValid("{[]}")
    assert not isValid("(]") and not isValid("(") and not isValid(")")
