#!/usr/bin/env python3
"""HWLAB-B04 추가 연습 — 본문 측정 라이브러리(solution.py)를 확장한다

  연습 1  settling_time      : 상승 에지 50 % 시각부터 top ±band·A 안에 '머무르기' 시작할 때까지
  연습 2  robust_levels      : 히스토그램이 쌍봉(bimodal)이 아니면 min/max 로 후퇴 (삼각파·톱니파)
  연습 3  clip_check         : 화면 밖으로 잘린(clipped) 캡처를 찾아 측정을 무효 처리
  연습 4  edge_statistics    : 스코프 'Statistics' 처럼 에지별 rise 의 n/mean/std/min/max

  python3 -I practice.py      # "N/M passed", 실패 있으면 exit 1
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)            # -I 는 스크립트 디렉터리를 sys.path 에서 빼므로 직접 넣는다
sys.dont_write_bytecode = True     # 레슨 디렉터리에 __pycache__ 를 남기지 않는다
import solution as S                # noqa: E402  (같은 레슨의 본문 라이브러리)


# ---------------------------------------------------------------- 연습 1
def settling_time(t, v, trs, top, base, band=0.02):
    """첫 상승 전이의 t_mid 부터, 다음 전이 시작 전까지 |v - top| <= band·A 에 계속 머무르기 시작하는 시각까지.
    마지막으로 band 밖에 있던 샘플 j 와 j+1 사이의 band 경계 교차를 보간한다."""
    A = top - base
    k = next(k for k, tr in enumerate(trs) if tr["dir"] > 0)
    tr = trs[k]
    end = trs[k + 1]["i0"] if k + 1 < len(trs) else len(v) - 1
    last_out = None
    for j in range(tr["i0"], end + 1):
        if abs(v[j] - top) > band * A:
            last_out = j
    j = last_out
    edge = top + band * A if v[j] > top else top - band * A
    return S.cross_time(t, v, j, edge) - tr["t_mid"]


def analytic_settling2(zeta, wn, band, step=1e-12):
    """해석 응답을 1 ps 간격으로 훑어 '마지막으로 band 밖' 시각 − 50 % 시각."""
    t50 = None
    last = 0.0
    tmax = 12.0 / (zeta * wn)                      # e^-12 ≈ 6e-6 — 그 뒤로는 band 밖으로 못 나감
    n = int(tmax / step)
    for i in range(1, n):
        x = i * step
        s = S.step2(x, zeta, wn)
        if t50 is None and s >= 0.5:
            t50 = x
        if abs(s - 1.0) > band:
            last = x
    return last - t50


# ---------------------------------------------------------------- 연습 2
def robust_levels(v, nbins=200, peak_ratio=4.0):
    """양쪽 절반의 최빈 칸 개수가 그 절반 평균 칸 개수의 peak_ratio 배 이상이면 히스토그램, 아니면 min/max.
    반환 (base, top, 'histogram' | 'minmax')."""
    vmin, vmax = min(v), max(v)
    w = (vmax - vmin) / nbins
    counts = [0] * nbins
    for x in v:
        counts[min(int((x - vmin) / w), nbins - 1)] += 1
    half = nbins // 2
    ok = True
    for rng in (range(0, half), range(half, nbins)):
        c = [counts[k] for k in rng]
        if max(c) < peak_ratio * (sum(c) / len(c)):
            ok = False
    if not ok:
        return vmin, vmax, "minmax"
    b, tp = S.state_levels(v, nbins)
    return b, tp, "histogram"


# ---------------------------------------------------------------- 연습 3
def requantize(v, vdiv, offset, bits=8, divs=8):
    """다른 수직 설정(V/div, offset)으로 다시 찍었다고 가정: 화면 밖은 ADC 끝 코드로 잘린다."""
    vbot = offset - divs / 2 * vdiv
    lsb = divs * vdiv / (1 << bits)
    out = []
    for x in v:
        code = min(max(int(round((x - vbot) / lsb)), 0), (1 << bits) - 1)
        out.append(vbot + code * lsb)
    return out


def clip_check(v, min_run=3):
    """최댓값(최솟값)과 같은 샘플이 연속 min_run 개 이상 있으면 그 쪽이 잘렸다고 본다.
    반환 (top_clipped, bottom_clipped). 잡음 있는 아날로그 신호의 극값이 여러 샘플 연속 같을 일은 드물다."""
    def has_run(target):
        run = 0
        for x in v:
            run = run + 1 if x == target else 0
            if run >= min_run:
                return True
        return False
    return has_run(max(v)), has_run(min(v))


# ---------------------------------------------------------------- 연습 4
def edge_statistics(trs, direction=+1):
    """에지별 10–90 % 시간의 n, mean, std(표본 표준편차, n-1), min, max."""
    d = S.edge_durations(trs, direction)
    n = len(d)
    mean = sum(d) / n
    std = math.sqrt(sum((x - mean) ** 2 for x in d) / (n - 1)) if n > 1 else 0.0
    return dict(n=n, mean=mean, std=std, min=min(d), max=max(d))


# ---------------------------------------------------------------- 테스트
def run():
    check, near, ns = S.check, S.near, S.ns
    wn = 2 * math.pi * 150e6

    print("-- 연습 1: settling time --")
    for zeta, band in ((0.6, 0.02), (0.45, 0.05)):
        t, v = S.synth(0.1e-9, 1500, [(10e-9, +1)], 0.0, 3.3, lambda x: S.step2(x, zeta, wn))
        trs = S.transitions(t, v, 0.0, 3.3)
        got = settling_time(t, v, trs, 3.3, 0.0, band)
        ref = analytic_settling2(zeta, wn, band)
        check("settling(zeta=%.2f, +-%d%%) matches analytic within 20ps" % (zeta, band * 100),
              lambda: (near(got, ref, 20e-12), "got=%s ref=%s" % (ns(got), ns(ref))))

    print("-- 연습 2: robust state levels --")
    dt = 0.1e-9
    tz, vz = S.synth(dt, 5200, S.pulse_edges(10e-9, 100e-9, 50e-9, 5), 0.0, 3.3, lambda x: S.ramp(x, 5e-9))
    tri = [3.3 * (1 - abs(((i % 1000) / 500.0) - 1)) for i in range(5000)]          # 0→3.3→0 삼각파
    sine = [1.65 + 1.65 * math.sin(2 * math.pi * i / 1000.0) for i in range(5000)]
    rz, rt, rs = robust_levels(vz), robust_levels(tri), robust_levels(sine)
    check("trapezoid -> histogram", lambda: (rz[2] == "histogram", "%s base=%.3f top=%.3f" % (rz[2], rz[0], rz[1])))
    check("triangle (flat histogram) -> minmax fallback",
          lambda: (rt[2] == "minmax", "%s (%.3f, %.3f)" % (rt[2], rt[0], rt[1])))
    check("sine: arcsine histogram peaks at the extremes -> histogram levels within 1% of min/max",
          lambda: (rs[2] == "histogram" and near(rs[1], 3.3, 0.033) and near(rs[0], 0.0, 0.033),
                   "%s base=%.4f top=%.4f" % (rs[2], rs[0], rs[1])))

    print("-- 연습 3: clipping --")
    t42, v42 = S.load_csv(os.path.join(HERE, "SN0042_tp31_mco8m.csv"))
    v_clip = requantize(v42, 0.5, 1.65)                           # 500 mV/div → -0.35 .. 3.65 V
    m_ok, m_clip = S.measure(t42, v42), S.measure(t42, v_clip)
    check("original 700mV/div capture: not clipped", lambda: clip_check(v42) == (False, False))
    check("500mV/div capture: clipped top and bottom", lambda: (clip_check(v_clip) == (True, True),
                                                                 "max=%.4f min=%.4f" % (max(v_clip), min(v_clip))))
    check("clipped capture under-reports overshoot (screen edge, not the signal)",
          lambda: (m_clip["overshoot"] < 12.0 < m_ok["overshoot"],
                   "clipped=%.1f%% true=%.1f%%" % (m_clip["overshoot"], m_ok["overshoot"])))

    print("-- 연습 4: per-edge statistics --")
    trs42 = S.transitions(t42, v42, m_ok["base"], m_ok["top"])
    st = edge_statistics(trs42, +1)
    check("SN0042: 8 rising edges, mean == measure()['rise'], std < 50ps",
          lambda: (st["n"] == 8 and near(st["mean"], m_ok["rise"], 1e-15) and st["std"] < 50e-12,
                   "n=%d mean=%s std=%.1fps min=%s max=%s"
                   % (st["n"], ns(st["mean"]), st["std"] * 1e12, ns(st["min"]), ns(st["max"]))))
    sf = edge_statistics(trs42, -1)
    check("SN0042: 8 falling edges, |mean fall - mean rise| < 50ps",
          lambda: (sf["n"] == 8 and abs(sf["mean"] - st["mean"]) < 50e-12,
                   "fall mean=%s" % ns(sf["mean"])))

    print("%d/%d passed" % (S.g_pass, S.g_total))
    return S.g_pass == S.g_total


if __name__ == "__main__":
    sys.exit(0 if run() else 1)
