"""모의 C · 내가 쓸 테스트 — 스펙만 보고 쓴다. 버그 버전에선 3개가 실패해야 하고, 고친 버전에선 전부 통과.

  python3 -m pytest -q lecture/mock/test_ring_mock.py                     # 고친 버전
  MOCK_RING=ring_buggy python3 -m pytest -q lecture/mock/test_ring_mock.py  # 버그 버전 → 3 failed
"""
# --- tests ---
import importlib
import os

import pytest

RingBuffer = importlib.import_module(os.environ.get("MOCK_RING", "ring_fixed")).RingBuffer


@pytest.fixture
def rb():
    return RingBuffer(3)


def test_can_hold_exactly_capacity(rb):          # → 버그 1
    assert rb.push(1) and rb.push(2) and rb.push(3)
    assert not rb.push(4) and len(rb) == 3


def test_order_survives_wraparound(rb):          # → 버그 2
    for x in (1, 2):
        rb.push(x)
    for _ in range(10):                          # 여러 바퀴: tail 이 끝을 넘어가게
        x = rb.pop()
        rb.push(x + 2)
    assert [rb.pop(), rb.pop()] == [11, 12]        # k 바퀴 뒤 큐는 [k+1, k+2]


def test_peek_returns_oldest_without_consuming(rb):   # → 버그 3
    rb.push("a")
    rb.push("b")
    assert rb.peek() == "a" and len(rb) == 2


@pytest.mark.parametrize("method", ["pop", "peek"])
def test_empty_raises(rb, method):
    with pytest.raises(IndexError):
        getattr(rb, method)()
# --- end ---
