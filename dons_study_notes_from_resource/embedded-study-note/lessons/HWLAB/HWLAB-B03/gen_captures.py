#!/usr/bin/env python3
"""HWLAB-B03 Part C 캡처 생성기 (결정적) — 가상 보드 SNS-EVT1 의 I2C 버스 4개를 스코프로 찍은 것처럼 만든다.

  python3 -I gen_captures.py        # captures/*.csv 를 다시 쓴다 (같은 seed → 바이트 단위로 같은 파일)

숨은 정답(HIDDEN)은 이 파일에만 있다. buses.json 은 회로도·BOM 이 "주장하는" 값(Rp, Cb 예산)만 담는다.
레슨 코드는 캡처에서 tr·VOL 을 재고, 회로도 값과 어긋나는 버스를 찾아내야 한다.

파형 모델 (단일 극점 RC, 오픈드레인):
  t < t_fall          : VDD
  t_fall ≤ t < t_rise : VOL + (VDD-VOL)·exp(-(t-t_fall)/τf),  τf = (Ron‖Rp)·C,  VOL = VDD·Ron/(Ron+Rp)
  t ≥ t_rise          : VDD - (VDD-v0)·exp(-(t-t_rise)/τr),   τr = Rp·C
  C = Cb(버스) + C_probe(10x 패시브 프로브 12 pF)
스코프: 8비트 ADC, 500 mV/div × 8 div = 4.0 V full scale (offset -0.5 V), 가우스 잡음 σ = 6 mV.
"""
import math
import os
import random

HERE = os.path.dirname(os.path.abspath(__file__))
PROBE_F = 12e-12            # buses.json 의 probe_pF 와 같아야 한다
ADC_BITS = 8
FS_MIN, FS_SPAN = -0.5, 4.0  # 500 mV/div × 8 div
NOISE_V = 0.006
N = 2500

# name: (vdd, rp_true, cb_bus_true, ron_driver, dt, t_fall, t_rise, seed, 설명)
HIDDEN = {
    "I2C0_SCL": (3.3, 2200.0, 78e-12, 40.0, 1e-9, 0.2e-6, 0.6e-6, 1001, "정상"),
    "I2C1_SCL": (1.8, 4700.0, 168e-12, 35.0, 4e-9, 0.5e-6, 1.5e-6, 1002, "FPC 때문에 Cb 큼, Rp 너무 큼"),
    "I2C2_SCL": (3.3, 40000.0, 50e-12, 40.0, 10e-9, 2.0e-6, 5.0e-6, 1003, "R230 DNP → MCU 내부 풀업 ~40k"),
    "I2C3_SDA": (3.3, 500.0, 80e-12, 110.0, 0.4e-9, 0.1e-6, 0.4e-6, 1004, "1k 풀업이 메인+캐리어 양쪽에 → 500Ω, 약한 슬레이브 ACK"),
}


def quantize(v):
    lsb = FS_SPAN / (1 << ADC_BITS)
    code = int(round((v - FS_MIN) / lsb))
    code = max(0, min((1 << ADC_BITS) - 1, code))
    return FS_MIN + code * lsb


def waveform(vdd, rp, cb, ron, dt, t_fall, t_rise, seed):
    rng = random.Random(seed)
    c = cb + PROBE_F
    vol = vdd * ron / (ron + rp)
    tau_f = (ron * rp / (ron + rp)) * c
    tau_r = rp * c
    v0 = vol + (vdd - vol) * math.exp(-(t_rise - t_fall) / tau_f)
    rows = []
    for i in range(N):
        t = i * dt
        if t < t_fall:
            v = vdd
        elif t < t_rise:
            v = vol + (vdd - vol) * math.exp(-(t - t_fall) / tau_f)
        else:
            v = vdd - (vdd - v0) * math.exp(-(t - t_rise) / tau_r)
        rows.append((t, quantize(v + rng.gauss(0.0, NOISE_V))))
    return rows


def main():
    os.makedirs(os.path.join(HERE, "captures"), exist_ok=True)
    for name, (vdd, rp, cb, ron, dt, tf, tr, seed, _) in sorted(HIDDEN.items()):
        rows = waveform(vdd, rp, cb, ron, dt, tf, tr, seed)
        path = os.path.join(HERE, "captures", name + ".csv")
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write("# SNS-EVT1 %s  probe=10x passive 12pF  500mV/div  8-bit  dt=%gns  n=%d\n"
                    % (name, dt * 1e9, N))
            f.write("t_ns,v\n")
            for t, v in rows:
                f.write("%.2f,%.5f\n" % (t * 1e9, v))
        print("wrote %s (%d rows)" % (os.path.relpath(path, HERE), len(rows)))


if __name__ == "__main__":
    main()
