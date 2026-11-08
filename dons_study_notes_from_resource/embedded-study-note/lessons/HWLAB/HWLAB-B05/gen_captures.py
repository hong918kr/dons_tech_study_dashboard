#!/usr/bin/env python3
"""HWLAB-B05 Part C 용 합성 스코프 캡처 생성기 (결정적, 표준 라이브러리만).

  python3 -I gen_captures.py          # captures/*.csv 6개를 다시 쓴다 (같은 내용, 바이트 단위 동일)

가상 보드 BRD-A rev0 의 세 넷을 10x 패시브 프로브(10MΩ‖12pF)로 두 번씩 잰 것처럼 만든다:
  lead15cm  = 15cm 악어 클립 그라운드 리드 (~150nH)
  spring    = 그라운드 스프링 (~10nH)
측정 체인 모델 (solution.py 의 simulate_probe 와 같은 회로):
  Vth(t) --Rs--+--L_gnd--+-- C_tip ‖ R_in --+ (스코프는 C 양단 전압을 본다)
                                          |
  스코프+프로브 시스템 응답 = 1GHz 단일 극점(single-pole, 가정)
  잡음 6mV rms (가우시안), 값은 0.1mV 로 반올림.
QSPI_CLK 는 '심어 둔 결함(planted defect)': 직렬 종단 저항 미실장(DNP)으로 보드 자체가 250MHz, ζ=0.25 로
링잉한다 (Thevenin 소스 자체가 2차 공진기를 거친다 — 전송선로를 집중 정수로 아주 단순화한 모델).
"""
import math
import os
import random

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "captures")
TR_K = 2.5631031311  # 10-90% of a Gaussian CDF = 2 * 1.2815515655 sigma


def gauss_edge(t, t0, tr, v0, v1):
    s = tr / TR_K
    return v0 + (v1 - v0) * 0.5 * (1.0 + math.erf((t - t0) / (s * math.sqrt(2.0))))


def ringing_source(edge, f, zeta, h, n):
    """edge(t) 를 2차 공진기(f, zeta, DC gain 1)에 통과시킨 값을 h 간격 n+1 점으로 (RK4)."""
    w = 2.0 * math.pi * f

    def d(t, x, xd):
        return xd, w * w * (edge(t) - x) - 2.0 * zeta * w * xd
    x = xd = 0.0
    out = [0.0]
    for k in range(n):
        t = k * h
        a1, b1 = d(t, x, xd)
        a2, b2 = d(t + h / 2, x + h / 2 * a1, xd + h / 2 * b1)
        a3, b3 = d(t + h / 2, x + h / 2 * a2, xd + h / 2 * b2)
        a4, b4 = d(t + h, x + h * a3, xd + h * b3)
        x += h / 6 * (a1 + 2 * a2 + 2 * a3 + a4)
        xd += h / 6 * (b1 + 2 * b2 + 2 * b3 + b4)
        out.append(x)
    return out


def simulate(src, Rs, L, C, Rp, bw, dt, n):
    """상태 (i_L, v_C, y_scope) RK4. src(t) 는 Thevenin 개방 전압. y 를 매 스텝 반환."""
    tau = 1.0 / (2.0 * math.pi * bw)

    def d(t, i, v, y):
        return (src(t) - Rs * i - v) / L, (i - v / Rp) / C, (v - y) / tau
    i = v = y = 0.0
    ys = [0.0]
    for k in range(n):
        t = k * dt
        a1 = d(t, i, v, y)
        a2 = d(t + dt / 2, i + dt / 2 * a1[0], v + dt / 2 * a1[1], y + dt / 2 * a1[2])
        a3 = d(t + dt / 2, i + dt / 2 * a2[0], v + dt / 2 * a2[1], y + dt / 2 * a2[2])
        a4 = d(t + dt, i + dt * a3[0], v + dt * a3[1], y + dt * a3[2])
        i += dt / 6 * (a1[0] + 2 * a2[0] + 2 * a3[0] + a4[0])
        v += dt / 6 * (a1[1] + 2 * a2[1] + 2 * a3[1] + a4[1])
        y += dt / 6 * (a1[2] + 2 * a2[2] + 2 * a3[2] + a4[2])
        ys.append(y)
    return ys


#        net          tr     t0     ts(sample)  board ring (f, zeta) or None
NETS = [("SPI0_SCLK", 1e-9, 10e-9, 50e-12, None),
        ("QSPI_CLK", 0.8e-9, 10e-9, 50e-12, (250e6, 0.25)),
        ("FAN_PWM", 20e-9, 50e-9, 100e-12, None)]
PROBES = [("lead15cm", 150e-9, "15cm alligator ground lead"),
          ("spring", 10e-9, "ground spring at tip")]
V = 3.3
RS = 33.0
C_TIP = 12e-12
R_IN = 10e6
BW = 1e9
NOISE = 0.006
NS = 1600          # samples after t=0 (file has NS+1 rows)
DT = 5e-12         # RK4 step


def make(idx, net, tr, t0, ts, ring, pidx, pname, L, pdesc):
    sub = int(round(ts / DT))
    n = NS * sub

    def edge(t):
        return gauss_edge(t, t0, tr, 0.0, V)
    if ring is None:
        src = edge
    else:
        h = DT / 2
        tab = ringing_source(edge, ring[0], ring[1], h, 2 * n + 2)

        def src(t):
            return tab[int(round(t / h))]
    ys = simulate(src, RS, L, C_TIP, R_IN, BW, DT, n)
    rng = random.Random(5050 + 10 * idx + pidx)
    lines = ["# HWLAB-B05 synthetic capture (gen_captures.py) board=BRD-A rev0",
             "# net=%s probe=10x passive 10MOhm||12pF ground=%s" % (net, pdesc),
             "# scope: 1GHz system BW, %.0f GS/s, noise ~6mV rms; columns: t_ns,volt" % (1e-9 / ts),
             "t_ns,volt"]
    for k in range(NS + 1):
        v = ys[k * sub] + rng.gauss(0.0, NOISE)
        lines.append("%.3f,%.4f" % (k * ts * 1e9, v))
    path = os.path.join(OUT, "%s_%s.csv" % (net, pname))
    with open(path, "w", encoding="ascii", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    return path


def main():
    os.makedirs(OUT, exist_ok=True)
    for idx, (net, tr, t0, ts, ring) in enumerate(NETS):
        for pidx, (pname, L, pdesc) in enumerate(PROBES):
            p = make(idx, net, tr, t0, ts, ring, pidx, pname, L, pdesc)
            print("wrote %s (%d bytes)" % (os.path.relpath(p, HERE), os.path.getsize(p)))


if __name__ == "__main__":
    main()
