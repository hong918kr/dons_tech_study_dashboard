#!/usr/bin/env python3
"""HWLAB-M01 데모: §5 의 표들을 실제 코드로 찍는다.

  1) bounce(lattice) 표 — 15 Ω 드라이버 + 50 Ω 선로(td = 1 ns) + 열린 끝, 3.3 V 계단
  2) 같은 선로에서 직렬 R 을 바꿨을 때 소스·부하 파형 요약 (Rs+Rdrv < / = / > Z0)
  3) TDR: 50|30|75|50 Ω 계단 선로에서 순진한 Z(t) vs layer peeling
  4) TDR 상승시간 vs 3 mm 35 Ω 패드의 겉보기 Z

  python3 -I demo_bounce.py
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.dont_write_bytecode = True     # 레슨 디렉터리에 __pycache__ 를 남기지 않는다
import solution as S  # noqa: E402

print("1) bounce diagram: Vs=3.3V, Rdrv=15ohm, Z0=50ohm, td=1ns, open end")
print("   rhoS=%.4f rhoL=%.0f launch=%.4fV" % (S.rho(15, 50), S.rho(math.inf, 50), 3.3 * 50 / 65))
print("   t(ns) end   arriving   reflected   node V")
for e in S.bounce_diagram(3.3, 15.0, 50.0, math.inf, 1e-9, 9e-9):
    print("   %4.0f  %-4s  %+8.4f   %+8.4f   %7.4f" % (e["t"] * 1e9, e["end"], e["wave"], e["refl"], e["v"]))

print("\n2) series R at the driver (Rdrv=15): load V at 1,3,5,7 ns and source V at 0+,2+ ns")
N = 20
for rs in (0.0, 35.0, 60.0):
    vs, vl = S.simulate([50.0] * N, 1e-9 / N, S.step_fn(3.3), 15.0 + rs, S.Load(), 7 * N + 1)
    print("   Rs=%2.0f (Rsum=%2.0f): load %s | src %s" % (
        rs, 15 + rs, " ".join("%.3f" % vl[k * N] for k in (1, 3, 5, 7)),
        " ".join("%.3f" % vs[k] for k in (0, 2 * N))))

print("\n3) TDR of 50|30|75|50 ohm (2/3/3/3 cm, eps_eff=4, matched end): naive Z(t) vs layer peeling")
dt1 = 1e-3 / S.v_prop(4.0)
zt = [50.0] * 20 + [30.0] * 30 + [75.0] * 30 + [50.0] * 30
_, v = S.tdr_capture(zt, dt1, 0.25, S.Load(shunts=[(50.0, 0.0)]), 2 * len(zt) + 2, pre=0)
vrt = v[0::2]
zn, zl = S.z_profile(vrt, 0.25), S.layer_peel(vrt, 0.25, 50.0)
print("   cell(mm)  truth   naive Z(t)   peeled")
for k in (10, 25, 45, 55, 75, 85, 105):
    print("   %5d    %5.1f    %7.2f    %7.2f" % (k, zt[k], zn[k], zl[k]))

print("\n4) TDR rise time vs a 3 mm 35-ohm pad (eps_eff=4, round trip 40 ps)")
dt5 = 0.5e-3 / S.v_prop(4.0)
zv = [50.0] * 40 + [35.0] * 6 + [50.0] * 60
tv, vv = S.tdr_capture(zv, dt5, 0.25, S.Load(shunts=[(50.0, 0.0)]), 260)
r = S.rho(35.0, 50.0)
print("   tr(ps)  min Z(ohm)  rho_app  rho*erf(Trt/(2*sqrt2*sigma))")
for tr in (10e-12, 20e-12, 35e-12, 60e-12, 100e-12, 150e-12):
    zz = S.z_profile(S.gauss_filter(vv, tr, dt5), 0.25)
    zm = min(z for t_, z in zip(tv, zz) if t_ > 40 * dt5)
    pred = r * math.erf(12 * dt5 / (2 * math.sqrt(2) * tr / 2.5631031311))
    print("   %5.0f   %8.2f    %+.4f   %+.4f" % (tr * 1e12, zm, (zm - 50) / (zm + 50), pred))
