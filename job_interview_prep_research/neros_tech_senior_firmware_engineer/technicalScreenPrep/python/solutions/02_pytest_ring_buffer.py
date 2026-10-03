"""02 · 남이 만든 RingBuffer 를 pytest 로 테스트하기 — 모범답안
# mutants: ringbuf

피검 코드: lib/ringbuf.py (load("ringbuf") 로 가져온다 → run.py 가 버그 심은 mutant 로 바꿔 끼워 채점)

보여 주려는 pytest 기능
- fixture: 빈 버퍼 / 가득 찬 버퍼 / wrap 된 버퍼를 준비해 주는 fixture, fixture 가 fixture 를 쓰기
- parametrize: 용량 · 정책 조합, ids 로 읽기 좋은 이름
- pytest.raises(match=...)
- model-based test: collections.deque(maxlen) 를 '정답 모델'로 두고 무작위 연산 1000번 비교 (seed 고정)
"""
import random
from collections import deque

import pytest

from lib import load

RingBuffer = load("ringbuf").RingBuffer


# ------------------------------------------------------------------ fixtures
@pytest.fixture
def rb():
    return RingBuffer(4)


@pytest.fixture
def full_rb(rb):                     # fixture 가 다른 fixture 를 받는다
    for i in range(rb.capacity):
        rb.push(i)
    return rb


@pytest.fixture
def wrapped_rb():
    """저장소 배열 기준으로 한 바퀴 돈 상태: 내용은 [2, 3, 4, 5] 지만 배열은 [4, 5, 2, 3]."""
    r = RingBuffer(4)
    for i in range(4):
        r.push(i)
    r.pop(), r.pop()
    r.push(4), r.push(5)
    return r


# ------------------------------------------------------------------ 기본 동작
def test_new_buffer_is_empty(rb):
    assert len(rb) == 0
    assert rb.empty() and not rb.full()


def test_fifo(rb):
    for x in "abc":
        rb.push(x)
    assert [rb.pop() for _ in range(3)] == ["a", "b", "c"]


def test_every_slot_is_usable(full_rb):
    assert full_rb.full() and len(full_rb) == full_rb.capacity


def test_not_full_one_below_capacity(rb):
    for i in range(rb.capacity - 1):
        rb.push(i)
    assert not rb.full()


@pytest.mark.parametrize("method", ["pop", "peek"])
def test_empty_access_raises(rb, method):
    with pytest.raises(IndexError, match="empty"):
        getattr(rb, method)()


def test_peek_does_not_consume(rb):
    rb.push("x")
    assert rb.peek() == "x"
    assert len(rb) == 1
    assert rb.pop() == "x"


# ------------------------------------------------------------------ 가득 찼을 때 정책
def test_drop_new_returns_false_and_keeps_old(full_rb):
    assert full_rb.push(99) is False
    assert list(full_rb) == [0, 1, 2, 3]
    assert full_rb.dropped == 0


@pytest.mark.parametrize("extra", [1, 3, 10], ids=lambda n: f"{n}_extra")
def test_overwrite_drops_oldest_and_counts(extra):
    r = RingBuffer(4, overwrite=True)
    total = 4 + extra
    for i in range(total):
        assert r.push(i) is True
    assert list(r) == list(range(total - 4, total))
    assert r.dropped == extra
    assert r.pop() == total - 4                  # 가장 오래 남은 것부터 나온다


# ------------------------------------------------------------------ wraparound
def test_iter_order_after_wrap(wrapped_rb):
    assert list(wrapped_rb) == [2, 3, 4, 5]


def test_pop_order_after_wrap(wrapped_rb):
    assert [wrapped_rb.pop() for _ in range(4)] == [2, 3, 4, 5]
    assert wrapped_rb.empty()


def test_iter_does_not_consume(wrapped_rb):
    list(wrapped_rb)
    assert len(wrapped_rb) == 4


# ------------------------------------------------------------------ clear / 생성자
def test_clear_resets_everything(wrapped_rb):
    wrapped_rb.clear()
    assert len(wrapped_rb) == 0 and wrapped_rb.empty()
    assert wrapped_rb.push(7) and list(wrapped_rb) == [7]


@pytest.mark.parametrize("bad", [0, -3, 1.5, None], ids=repr)
def test_invalid_capacity_rejected(bad):
    with pytest.raises(ValueError):
        RingBuffer(bad)


# ------------------------------------------------------------------ model-based
@pytest.mark.parametrize("capacity", [1, 2, 5, 8])
@pytest.mark.parametrize("overwrite", [False, True], ids=["drop_new", "overwrite"])
def test_matches_reference_model(capacity, overwrite):
    """무작위 push/pop 을 정답 모델(deque)과 나란히 실행. 실패하면 seed 로 재현 가능."""
    rnd = random.Random(1234 + capacity)
    r, model, dropped = RingBuffer(capacity, overwrite=overwrite), deque(), 0
    for step in range(1000):
        if rnd.random() < 0.6:
            x = step
            if len(model) == capacity:
                if overwrite:
                    model.popleft()
                    dropped += 1
                    model.append(x)
                    assert r.push(x) is True
                else:
                    assert r.push(x) is False
            else:
                model.append(x)
                assert r.push(x) is True
        elif model:
            assert r.pop() == model.popleft()
        else:
            with pytest.raises(IndexError):
                r.pop()
        assert len(r) == len(model), f"step {step}"
        assert list(r) == list(model), f"step {step}"
    assert r.dropped == dropped
