#!/usr/bin/env python3
"""SIV-A03 Part C 데이터 생성기 — 결정적(시드 고정). 다시 돌리면 같은 바이트가 나온다.

    python3 -I gen_power_sweep.py   # power_campaign.csv, power_campaign_truth.json 재생성

가상 SoC 'SENSE-1' VDD_CPU 레일 특성화 캠페인: 칩 8개 x 워크로드 3 x 챔버온도 5 x 전압 3 x 주파수 5 = 1800행.
모델(대표값): P = L25 * (V/0.8) * exp(a*(Tj-25) + b*(V-0.8)) + C_w * V^2 * f_GHz  [W, C_w 단위 nF]
자기발열: Tj = Tchamber + Rth * P (고정점 반복).
심은 결함: U2 샘플 1개 글리치 / U3 누설 과다 / U5 idle 클럭게이팅 고장 / U6 shunt 실장 오류(전류 x0.5) / U7 Tj 센서 고착.
"""
import json
import math
import os
import random

HERE = os.path.dirname(os.path.abspath(__file__))
WORKLOADS = ("idle", "mem", "cpu")
TEMPS = (-40, 0, 25, 85, 105)
VOLTS = (0.72, 0.80, 0.88)
FREQS = (0.8, 1.1, 1.4, 1.7, 2.0)
NOM = {"L25": 0.150, "a": 0.025, "b": 4.0, "C": {"idle": 0.15, "mem": 0.9, "cpu": 1.6}}
RTH = 3.0  # C/W (대표값: 챔버 소켓 보드). 루프이득 Rth*a*Pleak < 1 이어야 수렴(열폭주 아님)
CHIPS = ["U%d" % i for i in range(1, 9)]


def main():
    rng = random.Random(20261108)
    truth = {}
    rows = ["chip,w,t,v,f,tj,i_a,vbus_v,p_w"]
    for chip in CHIPS:
        L25 = NOM["L25"] * math.exp(rng.gauss(0, 0.08))
        a = NOM["a"] + rng.gauss(0, 0.001)
        b = NOM["b"] + rng.gauss(0, 0.2)
        C = {w: NOM["C"][w] * math.exp(rng.gauss(0, 0.02)) for w in WORKLOADS}
        defect = None
        gain = 1.0
        if chip == "U3":
            L25 *= 2.2
            defect = "leakage_excess"
        if chip == "U5":
            C["idle"] *= 3.0
            defect = "idle_clock_gating"
        if chip == "U6":
            gain = 0.5
            defect = "shunt_misstuffed_gain_0.5"
        if chip == "U7":
            defect = "tj_sensor_stuck_47C"
        truth[chip] = {"L25": L25, "a": a, "b": b, "C": C, "gain": gain, "defect": defect}
        for w in WORKLOADS:
            for t in TEMPS:
                for v in VOLTS:
                    for f in FREQS:
                        tj = t
                        for _ in range(40):
                            leak = L25 * (v / 0.8) * math.exp(a * (tj - 25) + b * (v - 0.8))
                            dyn = C[w] * v * v * f
                            p = leak + dyn
                            tj = t + RTH * p
                            if tj > 175.0:
                                raise ValueError("thermal runaway in generator: %s %s %s %s" % (chip, w, t, v))
                        meas_p = p * (1 + rng.gauss(0, 0.002)) + rng.gauss(0, 0.003)
                        if chip == "U2" and w == "cpu" and t == 85 and v == 0.88 and f == 1.7:
                            meas_p += 0.40  # 단일 샘플 글리치
                        meas_p *= gain
                        tj_meas = 47.0 if chip == "U7" else round(tj + rng.gauss(0, 0.3), 2)
                        i_a = meas_p / v
                        rows.append("%s,%s,%d,%.2f,%.2f,%.2f,%.5f,%.4f,%.5f"
                                    % (chip, w, t, v, f, tj_meas, i_a, v, meas_p))
    with open(os.path.join(HERE, "power_campaign.csv"), "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(rows) + "\n")
    with open(os.path.join(HERE, "power_campaign_truth.json"), "w", encoding="utf-8", newline="\n") as fh:
        json.dump(truth, fh, indent=1, sort_keys=True)
        fh.write("\n")
    print("wrote %d rows" % (len(rows) - 1))


if __name__ == "__main__":
    main()
