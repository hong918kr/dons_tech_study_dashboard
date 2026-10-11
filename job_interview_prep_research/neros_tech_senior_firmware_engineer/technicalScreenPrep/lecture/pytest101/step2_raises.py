# 2단계: "에러가 나야 맞는" 경우 — pytest.raises
import pytest


def pop_first(items):
    if not items:
        raise IndexError("empty")
    return items.pop(0)


def test_pop_first_returns_oldest():
    assert pop_first([10, 20]) == 10


def test_pop_first_on_empty_raises():
    with pytest.raises(IndexError):      # 이 블록 안에서 IndexError 가 '나야' 통과
        pop_first([])
