# 1단계-b: 일부러 틀린 테스트 — 실패하면 pytest 가 무엇을 보여 주나

def add(a, b):
    return a + b


def test_add_wrong_expectation():
    assert add(2, 3) == 6   # 틀린 기대값 → 실패
