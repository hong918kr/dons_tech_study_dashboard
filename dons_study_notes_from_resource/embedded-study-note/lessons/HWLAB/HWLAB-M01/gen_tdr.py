#!/usr/bin/env python3
"""HWLAB-M01 Part C TDR 캡처 생성기 (결정적: 같은 seed → 바이트 단위로 같은 CSV)

가상의 보드 VALBRD-A rev B, 클럭 버퍼 U12 → FPGA U1 의 FPGA_CLK_50M 넷 (L3 스트립라인, 50 Ω 설계, 15.0 cm).
U12 미실장 패드에 50 Ω TDR 프로브를 대고 찍은 반사 파형 두 장.
  SN0041 (golden)  : 50 Ω 트레이스, 6.0 cm 에 레이어 전환 비아(1 mm 를 32 Ω 짧은 구간으로 모델),
                     끝은 U1 입력 핀(열림 + Cin 4 pF)
  SN0042 (의심 보드): 위와 같고, 11.8 cm 부터 1.5 cm 구간이 68 Ω  ← 심어 둔 결함
                     (기준면 void/split 위를 지나가거나 트레이스 neck-down 이면 이렇게 높아진다)

모델 = 손실 없는 선로를 0.5 mm 칸으로 나눈 격자(bounce/lattice) 시뮬레이션 — solution.py 의 simulate() 와 같은 물리지만
       이 파일은 정답 생성기라 혼자 돈다(import 없음).
  TDR 소스: 2·Vinc EMF + 50 Ω (정합) → 입사 스텝 Vinc = 250 mV (대표값), 측정점 = 소스 노드
  상승시간: 이상 스텝 응답을 가우시안으로 걸러 10–90 % = 35 ps (선형 시스템이라 입력 대신 출력을 걸러도 같다)
  잡음: 0.3 mV rms, 10 µV 로 반올림.  εeff = 4.0 (스트립라인 → εeff = εr, 대표값)
  시간축: t = 0 이 프로브 팁 기준면(3 cm 50 Ω 케이블 끝). 입사 에지는 t = -0.400 ns 에 보인다.

  python3 -I gen_tdr.py          # SN0041_fpga_clk_tdr.csv, SN0042_fpga_clk_tdr.csv 를 다시 쓴다
"""
import math
import os
import random

HERE = os.path.dirname(os.path.abspath(__file__))

C0 = 299792458.0
EPS_EFF = 4.0
CELL = 0.5e-3                                   # 0.5 mm 칸
DT = CELL * math.sqrt(EPS_EFF) / C0             # 칸 하나의 편도 지연 ≈ 3.336 ps
N_CAB = 60                                      # 3 cm 50 Ω 케이블(기준)
K0 = 40                                         # 프리트리거 칸 수
VINC = 0.25
TR = 35e-12
NOISE = 0.3e-3
CIN = 4e-12
T_END = 3.2e-9                                  # 기준면 이후 기록 길이

BOARDS = [
    ("SN0041", 41, "golden: 50 ohm trace, via @6.0cm, U1 pin (open + Cin 4pF) @15.0cm", None),
    ("SN0042", 42, "suspect: same as golden (planted defect: see gen_tdr.py)", (11.8e-2, 1.5e-2, 68.0)),
]


def board_cells(defect):
    n = int(round(15.0e-2 / CELL))
    z = [50.0] * n
    via = int(round(6.0e-2 / CELL))
    z[via] = z[via + 1] = 32.0                   # 1 mm 비아 = 저임피던스(용량성) 짧은 구간
    if defect:
        pos, ln, zd = defect
        a = int(round(pos / CELL))
        for i in range(a, a + int(round(ln / CELL))):
            z[i] = zd
    return [50.0] * N_CAB + z


def simulate_tdr(z, n_steps):
    """정합 소스(50 Ω, EMF 2·Vinc 스텝 @ k=K0) → 소스 노드 전압. 끝은 Cin(사다리꼴 companion)."""
    n = len(z)
    rho = [(z[i + 1] - z[i]) / (z[i + 1] + z[i]) for i in range(n - 1)]
    fwd, bwd = [0.0] * n, [0.0] * n
    gc = 2.0 * CIN / DT
    vl, il = 0.0, 0.0
    out = []
    for k in range(n_steps):
        vs = 2.0 * VINC if k >= K0 else 0.0
        g = bwd[0]
        v0 = (vs / 50.0 + 2.0 * g / z[0]) / (1.0 / 50.0 + 1.0 / z[0])
        nf, nb = [0.0] * n, [0.0] * n
        nf[0] = v0 - g
        for i in range(n - 1):
            f, b, r = fwd[i], bwd[i + 1], rho[i]
            nf[i + 1] = (1.0 + r) * f - r * b
            nb[i] = r * f + (1.0 - r) * b
        f = fwd[n - 1]
        ieq = gc * vl + il
        v = (2.0 * f / z[n - 1] + ieq) / (1.0 / z[n - 1] + gc)
        il = gc * v - ieq
        vl = v
        nb[n - 1] = v - f
        fwd, bwd = nf, nb
        out.append(v0)
    return out


def gauss_filter(x, tr, dt):
    s = tr / 2.5631031311 / dt                  # 10–90 % = 2·1.2816·σ
    m = int(math.ceil(4 * s))
    ker = [math.exp(-0.5 * (j / s) ** 2) for j in range(-m, m + 1)]
    tot = sum(ker)
    ker = [k / tot for k in ker]
    n = len(x)
    return [sum(ker[j + m] * x[min(n - 1, max(0, i + j))] for j in range(-m, m + 1)) for i in range(n)]


def make(sn, seed, defect):
    z = board_cells(defect)
    n_steps = K0 + 2 * N_CAB + int(T_END / DT)
    v = gauss_filter(simulate_tdr(z, n_steps), TR, DT)
    rng = random.Random(seed)
    t0 = (K0 + 2 * N_CAB - 0.5) * DT            # 계단이 샘플 K0−1 → K0 사이에서 뛰므로 50 % 는 K0−0.5 칸
    rows = []
    for k, x in enumerate(v):
        rows.append((k * DT - t0, round(x + rng.gauss(0.0, NOISE), 5)))
    return rows


def main():
    for sn, seed, desc, defect in BOARDS:
        rows = make(sn, seed, defect)
        path = os.path.join(HERE, "%s_fpga_clk_tdr.csv" % sn)
        with open(path, "w", encoding="ascii", newline="\n") as f:
            f.write("# SYNTHETIC TDR capture for HWLAB-M01 (gen_tdr.py seed=%d) - %s\n" % (seed, desc))
            f.write("# board=VALBRD-A rev B net=FPGA_CLK_50M probe=50ohm TDR at U12 pad (U12 not populated)\n")
            f.write("# vinc_nominal=0.250V tr_10_90=35ps t0=probe-tip reference plane eps_eff(stackup)=4.0\n")
            f.write("time_s,volt_v\n")
            for t, x in rows:
                f.write("%.6e,%.5f\n" % (t, x))
        print("wrote %s (%d samples, dt=%.4fps)" % (os.path.basename(path), len(rows), DT * 1e12))


if __name__ == "__main__":
    main()
