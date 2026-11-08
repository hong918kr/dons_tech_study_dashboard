#!/usr/bin/env python3
"""HWLAB-M10 데모: dual-Dirac 꼬리 피팅이 기록 길이·피팅 깊이에 따라 어떻게 변하는가 (Part A 의 DEMO 지터 모델).

  python3 -I demo_tailfit.py
같은 지터 모델(DDJ + PJ 6ps@50.97MHz + RJ 1.5ps)로 TIE 만 길게 만들고(gen_capture.tie_record),
피팅 창(2F 범위)을 바꿔 RJδδ·DJδδ·TJ(1e-12)를 정답(성분 컨볼루션)과 비교한다.
σ고정 열: RJ 를 스펙트럼 RJ(정답 1.5ps)로 고정하고 μ 만 맞춘 경우.
"""
import math
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.dont_write_bytecode = True
import gen_capture as G   # noqa: E402
import solution as S      # noqa: E402


def sigma_fixed_tj(tie, p_lo, p_hi, sigma, ber=1e-12):
    """σ 를 고정하고 각 꼬리의 μ 만: μ = mean(x - σ·Q)."""
    s = sorted(tie)
    N = len(s)
    dlo, dhi = math.log10(p_lo), math.log10(p_hi)
    steps = int(round((dhi - dlo) * 40))
    idx = sorted(set(int(round(10 ** (dlo + (dhi - dlo) * j / steps) / 2 * N - 0.5)) for j in range(steps + 1)))
    L, R = [], []
    for i in idx:
        F = (i + 0.5) / N
        if 0 <= i < N and p_lo <= 2 * F <= p_hi:
            q = S.inv_phi(2 * F)
            L.append(s[i] - sigma * q)
            R.append(s[N - 1 - i] + sigma * q)
    dj = sum(R) / len(R) - sum(L) / len(L)
    return dj, dj + 2 * (-S.inv_phi(ber)) * sigma


def main():
    t0 = time.time()
    cfg = G.PART_A
    tv = [x for x in G.true_ddj(cfg) if x is not None]
    tj = G.tj_convolution(tv, cfg["pj_ps"], cfg["rj_ps"])
    djpp = max(tv) - min(tv) + 2 * cfg["pj_ps"]
    print("truth: TJ(1e-12)=%.2fps  DJpp=%.2fps  RJ=%.2fps   (DJpp+2Q*RJ=%.2fps)"
          % (tj, djpp, cfg["rj_ps"], djpp + 2 * 7.0345 * cfg["rj_ps"]))
    print("%8s  %-15s %7s %7s %8s %7s | %8s %8s | %7s" % ("edges", "fit 2F window", "RJdd", "DJdd", "TJdd", "err",
                                                        "DJ(sfix)", "err", "pk-pk"))
    for reps in (130, 500, 2000, 8000):
        ns, tie = G.tie_record(cfg, G.PLEN * reps, seed=4242)
        N = len(tie)
        for lo, hi in ((5e-4, 2e-2), (1e-4, 1e-2), (1e-5, 1e-3)):
            if lo < 4.0 / N:
                continue
            dd = S.dual_dirac(S.tail_fit(tie, lo, hi))
            djf, tjf = sigma_fixed_tj(tie, lo, hi, cfg["rj_ps"])
            print("%8d  [%.0e,%.0e] %7.3f %7.2f %8.2f %+6.1f%% | %8.2f %+7.1f%% | %7.2f"
                  % (N, lo, hi, dd["rj"], dd["dj"], dd["tj"], (dd["tj"] / tj - 1) * 100, djf, (tjf / tj - 1) * 100,
                     max(tie) - min(tie)))
    print("(%.1fs)" % (time.time() - t0))


if __name__ == "__main__":
    main()
