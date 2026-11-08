#!/usr/bin/env python3
"""HWLAB-M07 데모 — §5 의 두 표를 실제로 만든다 (solution.py 의 함수를 그대로 사용)

  표 1  PJ 주파수 스윕: TIE 에 10 ps 사인 PJ 를 넣고 TIE / period / c2c 에서 잰 진폭 → 이득 vs 2·sin(πν), 4·sin²(πν)
  표 2  샘플링률 스윕: 깨끗한 100 MHz +25 ppm 클럭을 5/10/20/40 GS/s 로 찍었을 때 보간 오차가 만드는 가짜 TIE

  python3 -I demo_sweeps.py
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.dont_write_bytecode = True
import solution as S  # noqa: E402

F0 = S.F0

print("table 1: PJ 10ps injected, RJ 0.3ps, 2048 edges @ 20GS/s")
print("  f_pj      nu     TIE amp   period amp (gain / 2sin(pi nu))   c2c amp (gain / 4sin^2(pi nu))")
for f in (1e6, 2e6, 5e6, 10e6, 25e6, 40e6, 45e6):
    nu = f / F0
    e = S.capture(S.clock_edges(2048, F0, rj=0.3e-12, pj=[(10e-12, f)], seed=31))
    tie = S.tie_ls(e)[0]
    at, ap, ac = S.sine_amp(tie, nu), S.sine_amp(S.periods(e), nu), S.sine_amp(S.c2c_direct(e), nu)
    print("  %4.0fMHz  %.3f  %7.3fps  %7.3fps (%.4f / %.4f)          %7.3fps (%.4f / %.4f)" % (
        f / 1e6, nu, at * 1e12, ap * 1e12, ap / at, 2 * math.sin(math.pi * nu),
        ac * 1e12, ac / at, 4 * math.sin(math.pi * nu) ** 2))

print()
print("table 2: clean 100MHz +25ppm clock, 10-90% = 0.8ns, linear-interpolated 0V crossing, 2048 edges")
print("  fs        samples on 10-90   |e - truth| max   TIE rms (fake jitter)")
truth = S.clock_edges(2048, F0, ppm=25.0)
for fs in (5e9, 10e9, 20e9, 40e9):
    e = S.capture(truth, fs=fs)
    d = [a - b for a, b in zip(e, truth)]
    print("  %2.0fGS/s   %5.1f              %7.3fps         %7.3fps" % (
        fs / 1e9, S.TR * fs, max(abs(x) for x in d) * 1e12, S.jitter_metrics(e)["tie_rms"] * 1e12))
