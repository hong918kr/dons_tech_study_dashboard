"""08 · 괄호 검사 — 스택 (LeetCode 20)
prompt: '()[]{}' 로만 된 문자열이 올바르게 닫혔는지 True/False.
note: 여는 괄호는 push, 닫는 괄호는 pop 해서 짝 확인. 끝나고 스택이 비어 있어야 한다
note: dict 로 '닫는 → 여는' 짝을 표로 두면 if 문이 없어진다
"""
# --- solution ---
def is_valid(s):
    pairs = {")": "(", "]": "[", "}": "{"}
    stack = []
    for ch in s:
        if ch in pairs:
            if not stack or stack.pop() != pairs[ch]:
                return False
        else:
            stack.append(ch)
    return not stack


def test_is_valid():
    assert is_valid("{[()]}") and is_valid("")
    assert not is_valid("([)]") and not is_valid("(") and not is_valid("]")


if __name__ == "__main__":
    test_is_valid()
    print("PASS")
# --- end ---
