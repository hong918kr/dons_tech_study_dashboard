#!/usr/bin/env python3
"""HWLAB-B04 데모: 감쇠비 ζ 를 바꿔 가며 IEEE(히스토그램) vs naive(min/max) 측정 오차를 표로 본다.

naive 의 10 % 레벨 = base - OS·A + 0.1·(1+2·OS)·A  →  OS > 12.5 % 이면 base 아래로 내려가
rise time 이 '앞 하강 에지의 undershoot 링잉'부터 재진다 (절벽). 잡음 10 mV rms, fn = 150 MHz.

  python3 -I demo_sweep.py
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.dont_write_bytecode = True     # 레슨 디렉터리에 __pycache__ 를 남기지 않는다
import solution as S  # noqa: E402

wn = 2 * math.pi * 150e6
print("zeta  OS_true  OS_ieee  OS_naive  rise_true  rise_ieee(err)     rise_naive(err)    naive_10%lvl")
for zeta in (0.30, 0.40, 0.45, 0.50, 0.55, 0.60, 0.70, 0.80, 0.90):
    t, v = S.synth(0.1e-9, 5200, S.pulse_edges(10e-9, 100e-9, 50e-9, 5), 0.0, 3.3,
                   lambda x: S.step2(x, zeta, wn), noise_sd=0.010, seed=181)
    tr, os_true = S.analytic_rise2(zeta, wn), S.analytic_os2(zeta) * 100
    mi, mn = S.measure(t, v, "ieee"), S.measure(t, v, "naive")
    lvl10 = mn["base"] + 0.1 * mn["amp"]
    print("%.2f  %6.2f%%  %6.2f%%  %6.2f%%   %6.3fns  %6.3fns(%+5.1f%%)  %7.3fns(%+7.1f%%)  %+.3fV"
          % (zeta, os_true, mi["overshoot"], mn["overshoot"], tr * 1e9, mi["rise"] * 1e9,
             (mi["rise"] / tr - 1) * 100, mn["rise"] * 1e9, (mn["rise"] / tr - 1) * 100, lvl10))
