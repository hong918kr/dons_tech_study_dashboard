"""02 · 남이 만든 RingBuffer 를 pytest 로 테스트하기 — starter
# mutants: ringbuf

피검 코드: lib/ringbuf.py (스펙은 그 파일 docstring). 구현은 열어 보지 말고 스펙만 보고 테스트를 쓴다.
채점: python3 python/run.py 02
  1) 정상 구현에서 전부 통과해야 하고
  2) 버그를 하나씩 심은 mutant 9개(lib/mutants/ringbuf_m*.py)에서 하나 이상 실패해야 '잡았다(killed)'
목표: 25분 안에 9/9 killed. 막히면 survived 로 나온 mutant 번호만 보고 어떤 경계를 빠뜨렸는지 추리한다.
"""
import pytest

from lib import load

RingBuffer = load("ringbuf").RingBuffer


# ------------------------------------------------------------------ fixtures
@pytest.fixture
def rb():
    return RingBuffer(4)


@pytest.fixture
def full_rb(rb):
    # TODO: rb 를 가득 채워서 돌려준다 (fixture 가 fixture 를 받는 예)
    pytest.skip("TODO")


@pytest.fixture
def wrapped_rb():
    # TODO: 저장 배열 기준으로 한 바퀴 돈 상태를 만든다 (push 4 → pop 2 → push 2)
    pytest.skip("TODO")


# ------------------------------------------------------------------ 예시 (완성)
def test_new_buffer_is_empty(rb):
    assert len(rb) == 0
    assert rb.empty() and not rb.full()


# ------------------------------------------------------------------ TODO
def test_fifo(rb):
    pytest.skip("TODO: 넣은 순서대로 나오나")


def test_every_slot_is_usable(full_rb):
    pytest.skip("TODO: capacity 개를 다 넣을 수 있나 (한 칸 비우는 구현과 구분)")


@pytest.mark.parametrize("method", ["pop", "peek"])
def test_empty_access_raises(rb, method):
    pytest.skip("TODO: pytest.raises(IndexError, match=...)")


def test_peek_does_not_consume(rb):
    pytest.skip("TODO")


def test_drop_new_returns_false_and_keeps_old(full_rb):
    pytest.skip("TODO: 반환값 · 내용 · dropped")


@pytest.mark.parametrize("extra", [1, 3, 10])
def test_overwrite_drops_oldest_and_counts(extra):
    pytest.skip("TODO: overwrite=True 로 capacity+extra 개 → 남은 내용 · dropped == extra")


def test_iter_order_after_wrap(wrapped_rb):
    pytest.skip("TODO")


def test_clear_resets_everything(wrapped_rb):
    pytest.skip("TODO: clear 뒤 len · empty · 다시 push")


@pytest.mark.parametrize("bad", [0, -3, 1.5, None])
def test_invalid_capacity_rejected(bad):
    pytest.skip("TODO")


def test_matches_reference_model():
    pytest.skip("TODO (보너스): deque 를 정답 모델로 무작위 연산 1000번 비교, random.Random(seed)")
