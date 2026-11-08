#!/usr/bin/env python3
"""HWLAB-B10 — 합성 열 soak 로그 생성기 (결정적, random.Random(1010))

보드 SOC0 를 두 환경에서 1200 s 동안 돌린 로그를 흉내 낸다 (2 s 간격, 시나리오당 601 행).
  bench_fan  : 열린 벤치, 큰 8층 개발 보드 + 팬 바람(히트싱크 없음). 실제 유효 열저항(접합→국소 공기) 11 °C/W.
  enclosure  : 밀폐 ECU 박스 안, 히트싱크 없음. 박스 내부 공기가 28 → 54 °C 로 오른다. 유효 16 °C/W.

물리 모델 (교육용 단순화):
  P(t)      = 0.54 W (t < 60 s) → 3.58 W (VDD_CORE 0.796 V·3.75 A + VDD_IO 1.8 V·0.333 A)
  Tlocal(t) = 국소 공기 (bench: 24.8 일정, enclosure: 1차 지연 τ=250 s)
  Tj(t)     = Tlocal(t) + θeff · Pf(t),   Pf = P 를 1차 지연(τ_j) 시킨 값
  Tc(t)     = Tj(t) - ΨJT_true · P(t)     (ΨJT_true: bench 0.65 (팬 → 윗면 열 흐름 증가), enclosure 0.55;
                                         데이터시트 0.5 와 일부러 조금 다르게 — ΨJT 도 환경에 따라 변한다)
  tc_uV     = (E_K(Tc) - E_K(Tcj)) · 1000 + 잡음 σ=1.0 µV   ← 열전대 단자에서 DAQ 가 재는 전압
  tsens_C   = Tj + 0.6 (센서 오프셋) + 잡음 σ=0.15, 0.1 °C 반올림 ← 온다이 센서
  cj_C      = DAQ 단자대 서미스터 (bench 26.5 부근, enclosure 는 박스 안이라 Tlocal - 3)
  ta_C      = 실험실 공기 (박스 밖), tlocal_C = 보드 근처 공기

  python3 -I gen_soak_log.py     # → soak_log.csv (커밋된 파일과 바이트 단위로 같아야 한다)
"""
import math
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

# NIST ITS-90 type K 정방향 계수 (solution.py 와 같은 값, 0..1372 °C 구간만 필요)
K_POS = (-0.176004136860E-01, 0.389212049750E-01, 0.185587700320E-04, -0.994575928740E-07,
         0.318409457190E-09, -0.560728448890E-12, 0.560750590590E-15, -0.320207200030E-18,
         0.971511471520E-22, -0.121047212750E-25)
K_A0, K_A1, K_A2 = 0.118597600000E+00, -0.118343200000E-03, 0.126968600000E+03


def emf_k_pos(t):
    s = 0.0
    for c in reversed(K_POS):
        s = s * t + c
    return s + K_A0 * math.exp(K_A1 * (t - K_A2) ** 2)


SCENARIOS = (
    # name,       theta_eff, psi_jt_true, tau_j, Tlocal0, Tlocal_inf, tau_air, cj_mode
    ("bench_fan", 11.0, 0.65, 60.0, 24.8, 24.8, 1.0, "bench"),
    ("enclosure", 16.0, 0.55, 110.0, 28.0, 54.0, 250.0, "box"),
)
DT = 2.0
T_END = 1200.0
T_STEP = 60.0


def gen(rng):
    rows = []
    for name, theta, psi_true, tau_j, a0, ainf, tau_a, cj_mode in SCENARIOS:
        pf = 0.800 * 0.60 + 1.800 * 0.0333
        t = 0.0
        while t <= T_END + 1e-9:
            load = t >= T_STEP
            i_core = (3.75 if load else 0.60) + rng.gauss(0, 0.02)
            v_core = (0.796 if load else 0.800) + rng.gauss(0, 0.001)
            i_io = (0.333 if load else 0.0333) + rng.gauss(0, 0.002)
            v_io = 1.800 + rng.gauss(0, 0.001)
            # 실제(잡음 없는) 패키지 전력 = 모든 레일의 V·I 합: load 3.584 W, idle 0.540 W
            p = (0.796 * 3.75 + 1.800 * 0.333) if load else (0.800 * 0.60 + 1.800 * 0.0333)
            if t > 0:
                pf += (p - pf) * (1.0 - math.exp(-DT / tau_j))
            tl = ainf - (ainf - a0) * math.exp(-max(0.0, t - T_STEP) / tau_a)
            tj = tl + theta * pf
            tc = tj - psi_true * p
            if cj_mode == "bench":
                tcj = 26.5 + 0.3 * (1.0 - math.exp(-t / 400.0))
            else:
                tcj = tl - 3.0
            tcj_meas = tcj + rng.gauss(0, 0.05)
            tc_uv = (emf_k_pos(tc) - emf_k_pos(tcj)) * 1000.0 + rng.gauss(0, 1.0)
            ta = 24.0 + rng.gauss(0, 0.05)
            tsens = round(tj + 0.6 + rng.gauss(0, 0.15), 1)
            rows.append("%s,%.0f,%.1f,%.2f,%.2f,%.2f,%.4f,%.4f,%.4f,%.4f,%.1f" % (
                name, t, tc_uv, tcj_meas, ta, tl + rng.gauss(0, 0.1),
                v_core, i_core, v_io, i_io, tsens))
            t += DT
    return rows


def main():
    rows = gen(random.Random(1010))
    out = os.path.join(HERE, "soak_log.csv")
    with open(out, "w", encoding="ascii", newline="\n") as f:
        f.write("scenario,t_s,tc_uV,cj_C,ta_C,tlocal_C,vdd_core_V,idd_core_A,vdd_io_V,idd_io_A,tsens_C\n")
        for r in rows:
            f.write(r + "\n")
    print("wrote %s: %d rows" % (out, len(rows)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
