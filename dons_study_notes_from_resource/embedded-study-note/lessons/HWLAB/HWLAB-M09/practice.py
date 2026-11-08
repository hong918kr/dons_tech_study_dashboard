#!/usr/bin/env python3
"""HWLAB-M09 추가 연습 — 본문 라이브러리(solution.py)를 확장한다

  연습 1  jitter_transfer   : golden PLL 이 eye 에 남기는 지터 |1−H(f)| 를 SJ 주파수 스윕으로 측정 (고역 통과 확인)
  연습 2  static_offset     : 1차 PLL 을 공칭 비트레이트로 돌리면 +100 ppm 에서 생기는 정적 위상 오차
  연습 3  mask_margin       : 마스크를 중심 기준으로 몇 % 키우면 첫 히트가 나는가 (볼록 다각형 게이지 함수)
  연습 4  ew_at_level       : DCD 레인을 0 V(수신기 결정 임계)에서 재면 eye width 가 DCD 만큼 줄어든다

  python3 -I practice.py      # "N/M passed", 실패 있으면 exit 1
"""
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)            # -I 는 스크립트 디렉터리를 sys.path 에서 빼므로 직접 넣는다
sys.dont_write_bytecode = True     # 레슨 디렉터리에 __pycache__ 를 남기지 않는다
import solution as S                # noqa: E402  (같은 레슨의 본문 라이브러리)

DT = S.UI / S.SPU
RAMP = S.channel_step(DT, S.UI, kind="ramp", ramp_ui=0.4)


# ---------------------------------------------------------------- 연습 1
def jitter_transfer(f_hz, a_ui=0.05, n_bits=32768):
    """SJ(f) 만 넣은 송신 에지 → golden PLL → TIE 의 f 성분 진폭 / 주입 진폭 = |1 − H(f)| 측정값."""
    cap = S.synth_capture(n_bits, chan=RAMP, sj_ui=a_ui, sj_hz=f_hz, seed=31)
    e = [t for t, _ in cap["tx_edges"]]
    clk = S.golden_pll(e, S.UI, S.PLL_BW, int((e[-1] - e[0]) / S.UI) + 2)
    return S.sine_amp(S.tie(e, clk, S.SPEC["skip_ui"]), f_hz) / S.UI / a_ui


def one_minus_h(f_hz, bw=S.PLL_BW):
    return f_hz / math.hypot(f_hz, bw)


# ---------------------------------------------------------------- 연습 2
def static_offset(ppm=100.0):
    """PLL 중심 주파수 = 공칭(5.000 Gb/s), 송신 = +ppm. 락 후 TIE 평균 [UI] 과 이론값 −(ppm·1e-6)/(2π·BW·UI)."""
    cap = S.synth_capture(16384, chan=RAMP, ppm=ppm, seed=32)
    e = [t for t, _ in cap["tx_edges"]]
    clk = S.golden_pll(e, S.UI, S.PLL_BW, int((e[-1] - e[0]) / S.UI) + 2)
    tl = S.tie(e, clk, 4000)
    meas = sum(y for _, y in tl) / len(tl) / S.UI
    theory = -(ppm * 1e-6) / (2 * math.pi * S.PLL_BW * S.UI)
    return meas, theory


# ---------------------------------------------------------------- 연습 3
def mask_margin(xs, vs, poly):
    """마스크 여유 [%] = (min_i γ(p_i) − 1)·100.  γ = 볼록 다각형의 게이지(Minkowski) 함수:
    원점을 품은 볼록 다각형의 각 변을 직선 a·x + b·y = c (c > 0) 로 쓰면 γ(p) = max_i (a_i·x + b_i·y)/c_i.
    점 p 가 마스크를 s 배 한 다각형 안 ⇔ γ(p) < s. 그러므로 첫 히트가 나는 배율 = 모든 점의 γ 중 최솟값."""
    lines = []
    n = len(poly)
    for i in range(n):
        (x1, y1), (x2, y2) = poly[i], poly[(i + 1) % n]
        a, b = y2 - y1, x1 - x2                  # 변의 법선
        c = a * x1 + b * y1
        if c < 0:
            a, b, c = -a, -b, -c
        lines.append((a / c, b / c))
    g = min(max(a * x + b * y for a, b in lines) for x, y in zip(xs, vs))
    return (g - 1.0) * 100.0


# ---------------------------------------------------------------- 연습 4
def ew_at_level(v, dt, clk, c, level, skip_ui):
    """level 교차들을 접어 crossing 평균 기준 pp → EW_pp = 1 − pp [UI]. c = eye_metrics 의 crossing 위치."""
    xr = [((u - c + 0.5) % 1.0) - 0.5 for u in S.phases(S.edge_times(v, dt, level), clk, skip_ui) if u is not None]
    return 1.0 - (max(xr) - min(xr))


# ---------------------------------------------------------------- 하네스
def main():
    S.g_pass = S.g_total = 0
    print("-- 연습 1: jitter transfer of the golden PLL (bw = %.3f MHz) --" % (S.PLL_BW / 1e6))
    print("      f_SJ      measured |1-H|   analytic f/sqrt(f^2+bw^2)   err")
    for f in (0.3e6, 1e6, 3e6, 10e6, 30e6):
        m, a = jitter_transfer(f), one_minus_h(f)
        print("      %5.1fMHz   %.4f           %.4f                      %+5.1f%%" % (f / 1e6, m, a, (m / a - 1) * 100))
        S.check("|1-H(%.1fMHz)| within 5%% of analytic" % (f / 1e6), lambda m=m, a=a: (S.near(m, a, 0.05 * a), "%.4f vs %.4f" % (m, a)))
    S.check("at f = bw the eye sees 1/sqrt2 of the SJ (-3dB point of the jitter high-pass)",
            lambda: S.near(jitter_transfer(S.PLL_BW), 1 / math.sqrt(2), 0.03))

    print("-- 연습 2: 1st-order PLL static phase error --")
    meas, theory = static_offset()
    S.check("+100ppm with nominal center freq -> constant TIE offset = -1e-4/(2pi*bw*UI)",
            lambda: (S.near(meas, theory, 0.03 * abs(theory)), "mean TIE=%+.4fUI theory=%+.4fUI" % (meas, theory)))

    print("-- 연습 3/4: Part C lanes --")
    poly = S.SPEC["mask_ui_v"]
    res = {}
    for n in ("TX0", "TX1", "TX2"):
        cap = S.lane_capture(n)
        clk = S.recover(cap, "pll")
        m = S.eye_metrics(cap["v"], cap["dt"], clk, S.SPEC["skip_ui"], S.SPEC["eye_window_ui"])
        res[n] = (cap, clk, m, mask_margin(m["xs"], m["vs"], poly), S.mask_hits(m["xs"], m["vs"], poly))
        print("      %s mask margin = %+.1f%%  (hits at 100%% = %d)" % (n, res[n][3], res[n][4]))
    S.check("TX0 mask margin > +20% (hits 0)", lambda: (res["TX0"][3] > 20 and res["TX0"][4] == 0, "%+.1f%%" % res["TX0"][3]))
    S.check("TX1 mask margin < 0 (consistent with hits > 0)", lambda: (res["TX1"][3] < 0 < res["TX1"][4], "%+.1f%%" % res["TX1"][3]))

    def scaled_hits():                          # 게이지 함수 결과를 직접 확인: margin 바로 아래/위 배율로 히트 수
        cap, clk, m, mg, _ = res["TX0"]
        s_lo, s_hi = 1 + mg / 100 - 0.002, 1 + mg / 100 + 0.002
        lo = S.mask_hits(m["xs"], m["vs"], [(x * s_lo, y * s_lo) for x, y in poly])
        hi = S.mask_hits(m["xs"], m["vs"], [(x * s_hi, y * s_hi) for x, y in poly])
        return lo == 0 and hi > 0, "hits at x%.3f=%d, at x%.3f=%d" % (s_lo, lo, s_hi, hi)
    S.check("TX0: mask scaled just below margin -> 0 hits, just above -> >0 hits", scaled_hits)

    cap, clk, m, _, _ = res["TX2"]
    ew0 = ew_at_level(cap["v"], cap["dt"], clk, m["c"], 0.0, S.SPEC["skip_ui"])
    ewx = ew_at_level(cap["v"], cap["dt"], clk, m["c"], m["vcross"], S.SPEC["skip_ui"])
    S.check("TX2 (DCD 0.10UI): EW_pp at 0V is ~0.10UI narrower than at crossing level",
            lambda: (S.near(ewx - ew0, 0.10, 0.02), "EW_pp@Vx(%.0fmV)=%.3f  EW_pp@0V=%.3f  diff=%.3fUI" % (
                m["vcross"] * 1e3, ewx, ew0, ewx - ew0)))
    print("%d/%d passed" % (S.g_pass, S.g_total))
    return 0 if S.g_pass == S.g_total else 1


if __name__ == "__main__":
    sys.exit(main())
