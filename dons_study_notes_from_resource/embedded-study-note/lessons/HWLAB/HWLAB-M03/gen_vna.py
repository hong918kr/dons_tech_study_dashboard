#!/usr/bin/env python3
"""HWLAB-M03 Part C 측정 데이터 생성기 (결정적: 같은 seed → 바이트 단위로 같은 .s2p)

가상의 보드 VALBRD-A rev C, 레일 VDD_CORE_0V9. BOM rev C = 100µF×2 + 10µF×5 + 1µF×3 (pdn_spec.json).
VNA 2-port shunt-thru 로 레일을 잰 Touchstone 파일 두 장 (VRM 켠 상태, DC 블록 사용 가정):
  SN0106  BOM 대로 전부 실장 (골든)
  SN0107  10µF 0603 다섯 개 중 세 개 미실장 (피더 빔 / 조립 오류) = 심어 둔 결함

참값 모델 = VRM(R + jωL) ∥ 부품 하나하나(직렬 RLC). 부품마다 공차를 독립으로 뽑는다:
  C ±10 % (균일), ESR ±20 %, 실장 ESL ±5 %.
측정 = 이상적인 shunt-thru S 파라미터 S21 = 2Z/(2Z+Z0), S11 = S22 = −Z0/(2Z+Z0), S12 = S21
     + 트레이스 잡음(크기 0.005 dB rms, 위상 0.03° rms) + 잡음 바닥(복소 가우스 σ = 2e-6).
스윕: 10 kHz – 1 GHz 로그, 201 점 (decade 당 40 점), Z0 = 50 Ω.

  python3 -I gen_vna.py        # SN0106_vdd_core_shunt_thru.s2p, SN0107_vdd_core_shunt_thru.s2p 를 다시 쓴다
"""
import cmath
import json
import math
import os
import random

HERE = os.path.dirname(os.path.abspath(__file__))
Z0 = 50.0
NPTS = 201
F1, F2 = 1e4, 1e9

BOARDS = [
    # (serial, seed, 실장 개수(부품별), 설명)
    ("SN0106", 106, {"C100u_1206": 2, "C10u_0603": 5, "C1u_0402": 3}, "golden: BOM rev C fully populated"),
    ("SN0107", 107, {"C100u_1206": 2, "C10u_0603": 2, "C1u_0402": 3}, "C10u_0603 3 of 5 not populated (feeder empty)"),
]


def board_parts(spec, population, rng):
    """실장된 부품 각각의 (C, ESR, ESL) — 공차 적용."""
    out = []
    for k in sorted(population):
        p = spec["parts"][k]
        for _ in range(population[k]):
            c = p["c_f"] * (1 + rng.uniform(-0.10, 0.10))
            esr = p["esr_ohm"] * (1 + rng.uniform(-0.20, 0.20))
            esl = (p["esl_part_h"] + p["l_mount_h"]) * (1 + rng.uniform(-0.05, 0.05))
            out.append((c, esr, esl))
    return out


def z_true(f, vrm, caps):
    w = 2 * math.pi * f
    y = 1 / complex(vrm["r_ohm"], w * vrm["l_h"])
    for c, esr, esl in caps:
        y += 1 / complex(esr, w * esl - 1 / (w * c))
    return 1 / y


def db_ang(s):
    return 20 * math.log10(abs(s)), math.degrees(cmath.phase(s))


def main():
    with open(os.path.join(HERE, "pdn_spec.json"), encoding="utf-8") as f:
        spec = json.load(f)
    for sn, seed, pop, desc in BOARDS:
        rng = random.Random(seed)
        caps = board_parts(spec, pop, rng)
        lines = ["! VALBRD-A rev C %s  rail VDD_CORE_0V9  2-port shunt-thru, VRM on, DC blocks on both ports" % sn,
                 "! SYNTHETIC data for HWLAB-M03 (gen_vna.py seed=%d) - %s" % (seed, desc),
                 "! IFBW 100 Hz, log sweep 10 kHz - 1 GHz, %d points" % NPTS,
                 "# Hz S DB R 50"]
        for k in range(NPTS):
            f = F1 * 10 ** (k * math.log10(F2 / F1) / (NPTS - 1))
            z = z_true(f, spec["vrm"], caps)
            s21 = 2 * z / (2 * z + Z0)
            s21 *= 10 ** (rng.gauss(0, 0.005) / 20) * cmath.exp(1j * math.radians(rng.gauss(0, 0.03)))
            s21 += complex(rng.gauss(0, 2e-6), rng.gauss(0, 2e-6))
            s11 = s21 - 1.0                      # shunt 소자: S11 = S21 − 1
            vals = []
            for s in (s11, s21, s21, s11):
                vals += db_ang(s)
            lines.append("%.6e " % f + " ".join("%.6f" % x for x in vals))
        with open(os.path.join(HERE, "%s_vdd_core_shunt_thru.s2p" % sn), "w", encoding="ascii", newline="\n") as f:
            f.write("\n".join(lines) + "\n")
        print("wrote %s_vdd_core_shunt_thru.s2p (%d points, %d caps) - %s" % (sn, NPTS, len(caps), desc))


if __name__ == "__main__":
    main()
