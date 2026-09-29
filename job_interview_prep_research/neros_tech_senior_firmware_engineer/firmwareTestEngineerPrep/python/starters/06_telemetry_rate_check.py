"""06 · Telemetry rate check — starter

HIL 리그에서 캡처한 telemetry 샘플 [(timestamp_s, msg_type), ...] 을 받아서
메시지 타입별로 rate(Hz) · 기대 rate ± tolerance 통과 여부 · 큰 gap · jitter ·
sliding-window 최대 개수를 계산하고, 구조화된 리포트(dataclass)로 돌려준다.

  python3 python/run.py 06          # 내 풀이 채점
  python3 python/run.py 06 --sol    # 모범답안
"""
from collections import defaultdict
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Sequence, Tuple


@dataclass
class TypeReport:
    msg_type: str
    count: int
    rate_hz: float                      # (count-1) / (last-first)
    expected_hz: Optional[float]        # 기대값이 없으면 None
    rate_ok: Optional[bool]             # 기대값이 없으면 None (판정 안 함)
    max_gap_s: float                    # 연속 두 샘플 사이 최대 간격
    gaps: List[Tuple[float, float]] = field(default_factory=list)  # (gap 시작 시각, gap 길이)
    jitter_s: float = 0.0               # max |interval - mean interval|


@dataclass
class RateReport:
    per_type: Dict[str, TypeReport]
    missing: List[str]                  # 기대했는데 한 번도 안 온 타입 (정렬)
    passed: bool


def max_count_in_window(timestamps: Sequence[float], window_s: float) -> int:
    """정렬된 timestamps 에서 [t, t+window) 구간 안 최대 샘플 수."""
    # TODO: two-pointer. right 를 늘리면서, t[right] - t[left] >= window 인 동안 left 를 당긴다.
    raise NotImplementedError


def analyze(samples: Sequence[Tuple[float, str]], expected: Dict[str, float],
            tolerance: float = 0.10, gap_factor: float = 3.0) -> RateReport:
    """samples: [(timestamp_s, msg_type)], expected: {msg_type: Hz}, tolerance: 0.10 = ±10%.

    규칙
    - rate = (count-1) / (last-first). 샘플 1개 이하면 0.0
    - gap 기준 = gap_factor × (1/expected). 기대값이 없으면 실측 평균 주기 기준
    - gaps = [(앞 샘플 시각, 간격)] 중 기준보다 큰 것
    - rate_ok = |rate - expected| <= tolerance × expected  (expected 없으면 None)
    - passed = missing 없음 AND rate_ok 가 False 인 타입 없음 AND gap 있는 타입 없음
    """
    # TODO: 1) defaultdict(list) 로 타입별 timestamp 모으기  2) 정렬 (순서 뒤섞임 대비)
    # TODO: 3) 타입별 TypeReport 만들기  4) missing / passed 계산
    raise NotImplementedError


# ------------------------------------------------------------------ tests
def _stream(kind, hz, seconds, start=0.0, drop=range(0)):
    period = 1.0 / hz
    return [(start + i * period, kind) for i in range(int(hz * seconds)) if i not in drop]


def test_clean_streams_pass():
    samples = _stream("ATTITUDE", 50, 2) + _stream("BATTERY", 10, 2)
    rep = analyze(samples, {"ATTITUDE": 50, "BATTERY": 10})
    assert rep.passed, rep
    assert abs(rep.per_type["ATTITUDE"].rate_hz - 50) < 0.01
    assert rep.per_type["BATTERY"].gaps == []


def test_dropout_detected_as_gap_and_rate_fail():
    samples = _stream("ATTITUDE", 50, 2, drop=range(40, 50))   # 0.2 s 끊김
    rep = analyze(samples, {"ATTITUDE": 50})
    att = rep.per_type["ATTITUDE"]
    assert not rep.passed
    assert len(att.gaps) == 1
    start, length = att.gaps[0]
    assert abs(start - 0.78) < 1e-9 and abs(length - 0.22) < 1e-9
    assert att.rate_ok is False                     # 89 / 1.98 ≈ 44.9 Hz < 45


def test_missing_type_fails():
    rep = analyze(_stream("ATTITUDE", 50, 1), {"ATTITUDE": 50, "GPS": 5})
    assert rep.missing == ["GPS"] and not rep.passed


def test_unexpected_type_reported_but_not_judged():
    rep = analyze(_stream("DEBUG", 7, 1), {})
    assert rep.per_type["DEBUG"].rate_ok is None
    assert rep.passed


def test_out_of_order_and_tolerance_boundary():
    samples = _stream("RC", 100, 1)
    samples.reverse()                                # 순서 뒤집혀도 결과 같아야
    rep = analyze(samples, {"RC": 110}, tolerance=0.10)   # 100 vs 110±11 → 통과
    assert rep.per_type["RC"].rate_ok is True
    rep = analyze(samples, {"RC": 112}, tolerance=0.10)   # 100 vs 112±11.2 → 실패
    assert rep.per_type["RC"].rate_ok is False


def test_single_sample_and_jitter():
    rep = analyze([(1.0, "X")], {})
    assert rep.per_type["X"].rate_hz == 0.0 and rep.per_type["X"].max_gap_s == 0.0
    ts = [(0.0, "J"), (0.010, "J"), (0.020, "J"), (0.035, "J"), (0.040, "J")]
    j = analyze(ts, {}).per_type["J"]
    assert abs(j.jitter_s - 0.005) < 1e-9           # 평균 10ms, 최대 15ms


def test_max_count_in_window():
    ts = [0, 100, 200, 250, 300, 1000]              # ms 정수 — float 경계 오차를 피한다
    assert max_count_in_window(ts, 210) == 4        # 100, 200, 250, 300 (폭 200 < 210)
    assert max_count_in_window(ts, 200) == 3        # [t, t+200) — 끝점은 제외
    assert max_count_in_window(ts, 2000) == 6
    assert max_count_in_window([], 1.0) == 0

if __name__ == "__main__":
    for _n, _f in list(globals().items()):
        if _n.startswith("test_") and callable(_f):
            _f()
            print("PASS", _n)
