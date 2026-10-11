# 3단계: 같은 테스트를 입력만 바꿔 여러 번 — @pytest.mark.parametrize
import pytest


def is_even(n):
    return n % 2 == 0


@pytest.mark.parametrize("n, expected", [   # (입력, 기대값) 표
    (0, True),
    (1, False),
    (2, True),
    (-3, False),
])
def test_is_even(n, expected):              # 표의 한 줄마다 한 번씩 실행된다 (4번)
    assert is_even(n) == expected
