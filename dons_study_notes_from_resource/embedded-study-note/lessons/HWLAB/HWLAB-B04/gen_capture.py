#!/usr/bin/env python3
"""HWLAB-B04 Part C 캡처 생성기 (결정적: 같은 seed → 바이트 단위로 같은 CSV)

가상의 보드 VALBRD-A rev B, SoC 클럭 출력 MCO_8M 을 TP31 에서 스프링 팁(ground spring)으로 측정한 스코프 CSV 두 장.
  SN0041  R47 = 33 Ω 직렬 종단 실장       → 감쇠 큰 2차 응답 (zeta 0.75, fn 110 MHz)
  SN0042  R47 자리에 0 Ω (BOM/조립 오류)  → 감쇠 작음       (zeta 0.42, fn 160 MHz) = 심어 둔 결함

파형 = 에지마다 2차(RLC) 계단 응답을 겹친 것(선형 시스템이라 중첩이 정확) + 에지 지터 + 백색 잡음
     + 12-bit ADC 양자화 (700 mV/div x 8 div, 화면 중앙 1.65 V → -1.15 V .. 4.45 V, LSB ≈ 1.37 mV).
스코프 설정: 5 GS/s (0.2 ns), 1 µs 레코드 = 5000 샘플.

  python3 -I gen_capture.py          # SN0041_tp31_mco8m.csv, SN0042_tp31_mco8m.csv 를 다시 쓴다
"""
import math
import os
import random

HERE = os.path.dirname(os.path.abspath(__file__))

FS = 5e9                 # 샘플링률 5 GS/s
N = 5000                 # 1 µs
F_CLK = 8e6              # 8 MHz
DUTY = 0.49              # 49 % (50 % 지점 기준)
T0 = 20e-9               # 첫 상승 에지(50 %) 시각
VBASE, VTOP = 0.0, 3.3   # LVCMOS33
NOISE_SD = 6e-3          # 6 mV rms (스코프 + 보드 잡음)
JITTER_SD = 15e-12       # 에지 지터 15 ps rms
VDIV, OFFSET, BITS = 0.70, 1.65, 12

BOARDS = [
    # (serial, zeta, fn_hz, seed, 설명)
    ("SN0041", 0.75, 110e6, 41, "R47=33R populated"),
    ("SN0042", 0.42, 160e6, 42, "R47=0R (BOM error)"),
]


def step2(t, zeta, wn):
    """2차 저역 시스템의 단위 계단 응답 (0 <= zeta < 1). t < 0 이면 0."""
    if t <= 0.0:
        return 0.0
    wd = wn * math.sqrt(1.0 - zeta * zeta)
    k = zeta / math.sqrt(1.0 - zeta * zeta)
    return 1.0 - math.exp(-zeta * wn * t) * (math.cos(wd * t) + k * math.sin(wd * t))


def edge_times(rng):
    """(시각, +1 상승 / -1 하강) 목록. 50 % 기준 시각에 지터를 더한다."""
    period = 1.0 / F_CLK
    out = []
    k = 0
    while True:
        tr = T0 + k * period
        if tr > N / FS:
            break
        out.append((tr + rng.gauss(0.0, JITTER_SD), +1))
        out.append((tr + DUTY * period + rng.gauss(0.0, JITTER_SD), -1))
        k += 1
    return out


def synth(zeta, fn, seed):
    rng = random.Random(seed)
    wn = 2.0 * math.pi * fn
    edges = edge_times(rng)
    # 2차 응답의 50 % 지점까지 걸리는 시간만큼 앞당겨, edges 의 시각이 50 % 교차가 되게 한다
    lo, hi = 0.0, math.pi / (wn * math.sqrt(1 - zeta * zeta))
    for _ in range(80):
        mid = 0.5 * (lo + hi)
        if step2(mid, zeta, wn) < 0.5:
            lo = mid
        else:
            hi = mid
    t50 = 0.5 * (lo + hi)
    vbot = OFFSET - 4 * VDIV
    lsb = 8 * VDIV / (1 << BITS)
    rows = []
    for i in range(N):
        t = i / FS
        x = 0.0
        for te, sgn in edges:
            x += sgn * step2(t - (te - t50), zeta, wn)
        v = VBASE + (VTOP - VBASE) * x + rng.gauss(0.0, NOISE_SD)
        code = int(round((v - vbot) / lsb))
        code = min(max(code, 0), (1 << BITS) - 1)          # 화면 밖은 클립 (이 설정에선 안 일어남)
        rows.append((t, vbot + code * lsb))
    return rows


def write_csv(path, header, rows):
    with open(path, "w", encoding="ascii", newline="\n") as f:
        f.write(header + "\n")
        f.write("time_s,ch1_v\n")
        for t, v in rows:
            f.write("%.6e,%.4f\n" % (t, v))


def main():
    for serial, zeta, fn, seed, note in BOARDS:
        rows = synth(zeta, fn, seed)
        name = "%s_tp31_mco8m.csv" % serial
        hdr = "# VALBRD-A revB %s net=MCO_8M TP31 %s fs=5GS/s %dmV/div %d-bit" % (
            serial, note, int(VDIV * 1000), BITS)
        write_csv(os.path.join(HERE, name), hdr, rows)
        print("wrote %s (%d samples)" % (name, len(rows)))


if __name__ == "__main__":
    main()
