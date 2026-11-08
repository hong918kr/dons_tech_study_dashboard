#!/usr/bin/env python3
"""HWLAB-M04 데모 — 본문(solution.py) 모델로 §5 의 스윕 표를 만든다

  표 1  샘플링률 × 방식: 점 샘플의 1회 캡처 최악 오차 · 적분형 피크
  표 2  자동 레인지 전환 지연 t_switch: Vmin, 선형 근사 sag, 과전류 시간, 재충전 스파이크, 전하 오차
  표 3  DMM µA 레인지(100 Ω)에서 디캡 C_dut 에 따른 Vmin — 디캡이 burden 을 얼마나 가려 주나
  표 4  AR_100K 가 1 s 캡처 한 번에서 +83 % 까지 틀리는 이유: 위상별 평균과 '스파이크에 맞은 샘플'

  python3 -I demo_sweep.py
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)            # -I 는 스크립트 디렉터리를 sys.path 에서 빼므로 직접 넣는다
sys.dont_write_bytecode = True
import solution as S                # noqa: E402

prof = S.load_json("profile_sensnode.json")
meters = {m["name"]: m for m in S.load_json("instruments.json")["meters"]}
truth = S.build_profile(prof)
i_true = S.average_current(truth)
dut = prof["dut"]
Is, Ib = prof["sleep_a"], 0.015

print("표 1  ideal ADC, truth waveform (sleep 2uA / 15mA bursts 4x63us + 817us per 1 s)")
print("   fs[S/s]  point: mean err  worst 1-capture  worst peak | avg: worst peak")
for fs in (1000, 2000, 5000, 10000, 31750, 100000, 416000, 1000000):
    sp = S.phase_sweep(truth, fs, "point", 100)
    sa = S.phase_sweep(truth, fs, "avg", 10)
    e = [(a / i_true - 1) * 100 for a, _ in sp]
    print("   %7d   %+8.2f%%      %+9.2f%%     %8.3fmA | %8.3fmA"
          % (fs, sum(e) / len(e), max(e, key=abs), min(p for _, p in sp) * 1e3, min(p for _, p in sa) * 1e3))

print()
print("표 2  AR_100K ranges, 0.817ms burst, C_dut=2.2uF, V_src=3.0V, BOR 1.8V")
print("   t_sw[us]  Vmin[V]  sag[mV]  linear[mV]  over[us]  spike[mA]  charge err")
ar = dict(meters["AR_100K"])
C = dut["c_dut_f"]
for tsw in (1, 5, 20, 50, 100, 139, 150, 200, 300):
    ar["t_switch_us"] = tsw
    ev = S.simulate_event(500411, 817, Ib, Is, ar, dut)
    spike = max(I for _, _, I in ev["segs"])
    print("   %6d    %.4f  %7.1f   %7.1f    %6d    %8.1f   %+8.3f%%"
          % (tsw, ev["v_min"], (dut["v_src"] - ev["v_min"]) * 1e3, (0.05 + Ib * (tsw + 1) * 1e-6 / C) * 1e3,
             ev["over_us"], spike * 1e3, (ev["q_meas"] / ev["q_true"] - 1) * 100))

print()
print("표 3  DMM_uA (single 100 ohm shunt) on the 0.817ms burst vs DUT decoupling")
print("   C_dut[uF]  Vmin[V]   verdict (BOR 1.8V + 0.2V margin)")
for c in (0.1, 1.0, 2.2, 4.7, 7.44, 10.0, 22.0):
    d = dict(dut)
    d["c_dut_f"] = c * 1e-6
    ev = S.simulate_event(500411, 817, Ib, Is, meters["DMM_uA"], d)
    v = ev["v_min"]
    print("   %7.2f    %.4f   %s" % (c, v, "BROWNOUT" if v < 1.8 else ("margin" if v < 2.0 else "ok")))

print()
print("표 4  AR_100K (point 100 kS/s) - 1 s capture average vs sample phase")
msegs, evs = S.meter_trace(prof, meters["AR_100K"])
for ev in evs:
    sp = max(ev["segs"], key=lambda s: s[2])
    print("   burst @%7dus: recharge spike %6.1fmA for %dus at t=%dus (switch to top at %dus)"
          % (ev["t0"], sp[2] * 1e3, sp[1] - sp[0], sp[0], ev["switches"][0][0]))
for j in range(10):
    ph = j + 0.5
    s = S.sample_point(msegs, 100000, ph)
    a = sum(s) / len(s)
    print("   phase %4.1fus: avg %8.3fuA  err %+7.2f%%  samples > 50mA: %d"
          % (ph, a * 1e6, (a / i_true - 1) * 100, sum(1 for x in s if x > 0.05)))
